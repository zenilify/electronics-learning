#include "svc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "papp_http.h"
#include "audio.h"
#include "sdcard.h"
#include <sys/stat.h>
#include <unistd.h>
#include "esp_timer.h"

static const char *TAG = "svc";

static void mkdirs(const char *file)          // mkdir -p for the parent of `file`
{
    char d[200]; strlcpy(d, file, sizeof d);
    for (char *p = d + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(d, 0775); *p = '/'; }
}

static void urlenc(const char *in, char *out, size_t n)
{
    static const char *hex = "0123456789ABCDEF"; size_t o = 0;
    for (const unsigned char *s = (const unsigned char *)in; *s && o + 4 < n; s++) {
        if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || strchr("/._-", *s)) out[o++] = *s;
        else { out[o++] = '%'; out[o++] = hex[*s >> 4]; out[o++] = hex[*s & 15]; }
    }
    out[o] = 0;
}

static void sd_sync(svc_progress_t prog, void *ud)
{
    uint8_t *idx; size_t len;
    int st = papp_get("/sd/index", &idx, &len);
    if (st != 200 || !idx) { ESP_LOGW(TAG, "sd sync: index HTTP %d", st); free(idx); if (prog) prog(0, 0, 0, ud); return; }
    int total = 0;
    for (const char *p = (char *)idx; (p = strstr(p, "\"path\"")); p++) total++;
    int i = 0, got = 0, kept = 0, bad = 0; int64_t t0 = esp_timer_get_time(); size_t bytes = 0;
    for (const char *p = (char *)idx; (p = strstr(p, "\"path\"")); ) {
        p = strchr(p + 6, '"'); if (!p) break; p++;
        const char *e = strchr(p, '"'); if (!e) break;
        char rel[160]; snprintf(rel, sizeof rel, "%.*s", (int)(e - p), p);
        const char *sz = strstr(e, "\"size\""); long size = sz ? atol(strchr(sz + 6, ':') + 1) : -1;
        p = e + 1; i++;
        if (strstr(rel, "..")) { bad++; continue; }
        char dst[200], tmp[208], q[400], enc[300];
        snprintf(dst, sizeof dst, SD_ROOT "/%s", rel);
        struct stat stt;
        int ok;
        if (stat(dst, &stt) == 0 && stt.st_size == size) { kept++; ok = 2; }
        else {
            mkdirs(dst);
            snprintf(tmp, sizeof tmp, "%s.part", dst);
            FILE *fp = fopen(tmp, "wb");
            urlenc(rel, enc, sizeof enc);
            snprintf(q, sizeof q, "/sd/file?path=%s", enc);
            int r = fp ? papp_get_file(q, fp) : -2;
            if (fp) fclose(fp);
            if (r == 200 && stat(tmp, &stt) == 0 && stt.st_size == size) {
                unlink(dst); rename(tmp, dst); got++; bytes += size; ok = 1;
            } else { unlink(tmp); bad++; ok = 0; ESP_LOGW(TAG, "sd sync: %s HTTP %d", rel, r); }
        }
        if (prog && (i % 10 == 0 || i == total)) prog(i, total, ok, ud);
    }
    free(idx);
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    char ln[160]; snprintf(ln, sizeof ln, "sd sync: %d files, %d new (%u kB), %d kept, %d failed, %d ms", total, got, (unsigned)(bytes / 1024), kept, bad, ms);
    ESP_LOGI(TAG, "%s", ln); sd_log("sync.log", ln);
    if (prog) prog(total, total, bad == 0, ud);
}

typedef enum { R_TEXT, R_SAY, R_JSON, R_PREFETCH, R_SDSYNC, R_POST } rtype_t;
typedef struct {
    rtype_t t; lang_t a, b; char *text; bool speak;
    svc_cb_t cb; void *ud;
    const char *const *list; int n; svc_progress_t prog;
    uint8_t *buf; size_t len; const char *ctype;
} req_t;
static QueueHandle_t s_q;
static volatile bool s_busy;

bool svc_busy(void) { return s_busy || (s_q && uxQueueMessagesWaiting(s_q)); }

static bool push(req_t *r)
{
    if (!s_q || svc_busy()) { free(r->text); return false; }
    return xQueueSend(s_q, r, 0) == pdTRUE;
}

// cache key: "<lang>|<text>"
static bool say_cached(lang_t lang, const char *text, papp_result_t *r, bool fetch_only)
{
    char *key = malloc(strlen(text) + 8);
    sprintf(key, "%s|%s", lang_code(lang), text);
    bool hit = sd_cache_get(key, &r->pcm, &r->samples, &r->rate);
    if (hit) { r->http_status = 200; r->endpoint = -2; }      // -2 = SD cache
    else {
        papp_tts(lang, text, r);
        if (r->http_status == 200 && r->samples) sd_cache_put(key, r->pcm, r->samples, r->rate);
    }
    ESP_LOGI(TAG, "say %s %s (%u samples)%s", lang_code(lang), hit ? "SD-cache" : "server",
             (unsigned)r->samples, fetch_only ? " [prefetch]" : "");
    free(key);
    return r->http_status == 200 && r->samples;
}

static void task(void *a)
{
    req_t q;
    for (;;) {
        if (!xQueueReceive(s_q, &q, portMAX_DELAY)) continue;
        s_busy = true;
        papp_result_t r; memset(&r, 0, sizeof r);
        switch (q.t) {
        case R_TEXT:
            papp_text_translate(q.a, q.b, q.text, &r);
            if (r.http_status == 200 && r.tgt_text && r.samples) {
                char *key = malloc(strlen(r.tgt_text) + 8);
                sprintf(key, "%s|%s", lang_code(q.b), r.tgt_text);
                sd_cache_put(key, r.pcm, r.samples, r.rate);     // replay offline later
                free(key);
                char *ln = malloc(strlen(q.text) + strlen(r.tgt_text) + 16);
                sprintf(ln, "%s>%s\t%s\t%s", lang_code(q.a), lang_code(q.b), q.text, r.tgt_text);
                sd_log("typed.tsv", ln); free(ln);
            }
            if (q.cb) q.cb(&r, q.ud);
            if (q.speak && r.samples) audio_play(r.pcm, r.samples, r.rate);
            break;
        case R_SAY:
            say_cached(q.a, q.text, &r, false);
            if (q.cb) q.cb(&r, q.ud);
            if (r.samples) audio_play(r.pcm, r.samples, r.rate);
            break;
        case R_JSON:
            papp_get_json(q.text, &r);
            if (q.cb) q.cb(&r, q.ud);
            break;
        case R_POST:
            papp_post_bytes(q.text, q.ctype, q.buf, q.len, &r);
            free(q.buf);
            if (q.cb) q.cb(&r, q.ud);
            break;
        case R_SDSYNC:
            sd_sync(q.prog, q.ud);
            break;
        case R_PREFETCH:
            for (int i = 0; i < q.n; i++) {
                papp_result_t x; memset(&x, 0, sizeof x);
                int ok = say_cached(q.a, q.list[i], &x, true);
                papp_result_free(&x);
                if (q.prog) q.prog(i + 1, q.n, ok, q.ud);
            }
            break;
        }
        papp_result_free(&r);
        free(q.text);
        s_busy = false;
    }
}

void svc_start(void)
{
    s_q = xQueueCreate(2, sizeof(req_t));
    // PSRAM stack: internal RAM is the scarce resource on this build (landmine: 18 kB -> sockets failed)
    xTaskCreatePinnedToCoreWithCaps(task, "svc", 16384, NULL, 4, NULL, 1, MALLOC_CAP_SPIRAM);
}

bool svc_text(lang_t src, lang_t tgt, const char *text, bool speak, svc_cb_t cb, void *ud)
{ req_t r = { .t = R_TEXT, .a = src, .b = tgt, .text = strdup(text), .speak = speak, .cb = cb, .ud = ud }; return push(&r); }
bool svc_say(lang_t lang, const char *text, svc_cb_t cb, void *ud)
{ req_t r = { .t = R_SAY, .a = lang, .text = strdup(text), .cb = cb, .ud = ud }; return push(&r); }
bool svc_json(const char *path, svc_cb_t cb, void *ud)
{ req_t r = { .t = R_JSON, .text = strdup(path), .cb = cb, .ud = ud }; return push(&r); }
bool svc_prefetch(lang_t lang, const char *const *texts, int n, svc_progress_t prog, void *ud)
{ req_t r = { .t = R_PREFETCH, .a = lang, .text = NULL, .list = texts, .n = n, .prog = prog, .ud = ud }; return push(&r); }
bool svc_sd_sync(svc_progress_t prog, void *ud)
{ req_t r = { .t = R_SDSYNC, .prog = prog, .ud = ud }; return push(&r); }
bool svc_post(const char *path, const char *ctype, uint8_t *buf, size_t len, svc_cb_t cb, void *ud)
{
    req_t r = { .t = R_POST, .text = strdup(path), .ctype = ctype, .buf = buf, .len = len, .cb = cb, .ud = ud };
    if (!push(&r)) { free(buf); return false; }
    return true;
}
