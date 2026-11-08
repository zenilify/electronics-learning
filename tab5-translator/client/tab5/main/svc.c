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

static const char *TAG = "svc";
typedef enum { R_TEXT, R_SAY, R_JSON, R_PREFETCH } rtype_t;
typedef struct {
    rtype_t t; lang_t a, b; char *text; bool speak;
    svc_cb_t cb; void *ud;
    const char *const *list; int n; svc_progress_t prog;
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
