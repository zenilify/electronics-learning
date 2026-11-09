// Live subtitles (ROADMAP row 10): the mic streams 1 s PCM16 chunks to the server's chunked ASR
// (T5-S API.md: POST /stream/start?lang=auto&tgt=X -> {sid}; /stream/chunk?sid= -> {partial, final:[{text,lang,tgt_text}]};
// /stream/stop?sid=). Big captions for talks and meetings: the grey line is what is being heard, every final
// segment lands as source (small) + translation (big). Nothing is spoken — the speaker stays silent.
#include "notes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "ui_priv.h"
#include "papp.h"
#include "papp_http.h"
#include "audio.h"
#include "sdcard.h"

static const char *TAG = "subs";
#define CHUNK_MS 1000
#define MAX_MERGE 4                         // a slow round trip: up to 4 s of queued audio goes in one POST
typedef struct { uint8_t *pcm; size_t n; } chunk_t;
static QueueHandle_t s_q;
static TaskHandle_t s_task;
static lv_obj_t *s_scr, *u_go, *u_go_l, *u_bar, *u_st, *u_list, *u_part, *u_tgt[3];
static int s_tgt;                           // 0 es · 1 en · 2 zh
static const char *TGT_Q[3] = { "es", "en", "zh" };
static const char *TGT_T[3] = { "→ Español", "→ English", "→ 中文" };
static volatile bool s_on, s_stop_req;      // s_on: a session is wanted (mic or injector feeding the queue)
static char s_sid[48];
static volatile int s_lat_ms, s_finals, s_err;
static char s_partial[400];
static volatile bool s_part_dirty;

static uint32_t lang_col(const char *l) { return !strcmp(l, "zh") ? C_AMBER : !strcmp(l, "en") ? C_BLUE : C_TEAL; }

static void add_final(const char *lang, const char *src, const char *tgt)       // display lock held
{
    if (lv_obj_get_child_count(u_list) >= 40) lv_obj_delete(lv_obj_get_child(u_list, 0));
    lv_obj_t *b = lv_obj_create(u_list);
    plain(b, C_BG);
    lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *a = label(b, f24, C_DIM, src);
    lv_obj_t *t = label(b, f40, lang_col(TGT_Q[s_tgt]), tgt && tgt[0] ? tgt : src);
    lv_obj_t *ls[] = { a, t };
    for (int i = 0; i < 2; i++) { lv_obj_set_width(ls[i], LV_PCT(100)); lv_label_set_long_mode(ls[i], LV_LABEL_LONG_WRAP); }
    if (!strcmp(lang, TGT_Q[s_tgt])) lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);    // already in the target language
    lv_obj_scroll_to_view(b, LV_ANIM_ON);
}

// {"seq":3,"partial":"…","final":[{"text":"…","lang":"zh","tgt_text":"…","dur_s":2.1,"ms":640}],"open_s":0.8,…}
static void handle_reply(const char *j)
{
    char *part = json_str(j, "partial");
    if (part) { strlcpy(s_partial, part, sizeof s_partial); s_part_dirty = true; free(part); }
    const char *f = strstr(j, "\"final\"");
    const char *end = f ? strchr(f, ']') : NULL;
    for (const char *p = f; p && (p = strstr(p, "\"text\"")) && (!end || p < end);) {
        char *txt = json_str(p, "text"), *lang = json_str(p, "lang"), *tgt = json_str(p, "tgt_text");
        if (txt && txt[0]) {
            ESP_LOGI(TAG, "final [%s] %s -> %s", lang ? lang : "?", txt, tgt ? tgt : "");
            lock(); add_final(lang ? lang : "", txt, tgt); unlock();
            s_finals++;
            char *ln = malloc(strlen(txt) + (tgt ? strlen(tgt) : 0) + 24);
            if (ln) { sprintf(ln, "%s>%s\t%s\t%s", lang ? lang : "?", TGT_Q[s_tgt], txt, tgt ? tgt : ""); sd_log("subtitles.tsv", ln); free(ln); }
        }
        free(txt); free(lang); free(tgt);
        const char *nx = strstr(p, "\"tgt_text\"");               // continue after this element
        p = nx ? nx + 10 : p + 6;
        if (end && p > end) break;
        const char *close = strchr(p, '}'); if (close) p = close;
    }
}

static bool stream_start(void)
{
    char path[64]; snprintf(path, sizeof path, "/stream/start?lang=auto&tgt=%s", TGT_Q[s_tgt]);
    papp_result_t r; papp_post_bytes(path, "application/json", (const uint8_t *)"{}", 2, &r);
    char *sid = r.http_status == 200 ? json_str(r.tgt_text, "sid") : NULL;
    ESP_LOGI(TAG, "start HTTP %d via %s sid %s", r.http_status, papp_endpoint_name(r.endpoint), sid ? sid : "-");
    s_err = sid ? 0 : r.http_status ? r.http_status : -1;
    if (sid) strlcpy(s_sid, sid, sizeof s_sid);
    free(sid); papp_result_free(&r);
    return s_sid[0];
}

static void stream_post(const char *verb, uint8_t *pcm, size_t n)
{
    char path[96]; snprintf(path, sizeof path, "/stream/%s?sid=%s", verb, s_sid);
    papp_result_t r; int64_t t0 = esp_timer_get_time();
    papp_post_bytes(path, "application/octet-stream", pcm ? pcm : (const uint8_t *)"", n, &r);
    s_lat_ms = (int)((esp_timer_get_time() - t0) / 1000);
    if (r.http_status == 200 && r.tgt_text) { handle_reply(r.tgt_text); s_err = 0; }
    else {
        ESP_LOGW(TAG, "%s HTTP %d (%u B)", verb, r.http_status, (unsigned)n);
        s_err = r.http_status ? r.http_status : -1;
        if (r.http_status == 404) s_sid[0] = 0;                 // sid expired (idle 60 s) or server restarted
    }
    papp_result_free(&r);
}

static void subs_task(void *a)
{
    chunk_t c;
    for (;;) {
        if (xQueueReceive(s_q, &c, pdMS_TO_TICKS(200)) != pdTRUE) {
            if (s_stop_req && s_sid[0]) { stream_post("stop", NULL, 0); s_sid[0] = 0; s_partial[0] = 0; s_part_dirty = true; }
            s_stop_req = false;
            continue;
        }
        // merge whatever else is already waiting (slow network: fewer, bigger POSTs instead of a growing lag)
        size_t tot = c.n; chunk_t more[MAX_MERGE - 1]; int m = 0;
        while (m < MAX_MERGE - 1 && xQueueReceive(s_q, &more[m], 0) == pdTRUE) tot += more[m++].n;
        if (m) {
            uint8_t *big = heap_caps_realloc(c.pcm, tot, MALLOC_CAP_SPIRAM);
            if (big) { size_t o = c.n; for (int i = 0; i < m; i++) { memcpy(big + o, more[i].pcm, more[i].n); o += more[i].n; } c.pcm = big; c.n = tot; }
            for (int i = 0; i < m; i++) free(more[i].pcm);
            ESP_LOGI(TAG, "merged %d chunks (%u B)", m + 1, (unsigned)c.n);
        }
        if (!s_sid[0] && !stream_start()) { free(c.pcm); vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        stream_post("chunk", c.pcm, c.n);
        free(c.pcm);
    }
}

static void on_chunk(uint8_t *pcm, size_t n, void *ud)            // recorder task
{
    chunk_t c = { pcm, n };
    if (!s_q || xQueueSend(s_q, &c, 0) != pdTRUE) { free(pcm); ESP_LOGW(TAG, "queue full — chunk dropped"); }
}

static void ensure_task(void)
{
    if (s_task) return;
    s_q = xQueueCreate(8, sizeof(chunk_t));
    xTaskCreatePinnedToCoreWithCaps(subs_task, "subs", 8192, NULL, 4, &s_task, 0, MALLOC_CAP_SPIRAM);
}

static void set_tgt(int i)
{
    if (i != s_tgt && s_sid[0]) s_stop_req = true;               // new target = new server session
    s_tgt = i;
    for (int k = 0; k < 3; k++) lv_obj_set_style_bg_color(u_tgt[k], lv_color_hex(k == i ? C_TEAL : C_PANEL), 0);
}
static void on_tgt(lv_event_t *e) { set_tgt((int)(intptr_t)lv_event_get_user_data(e)); }

static void sub_stop(void) { audio_stream_stop(); s_on = false; s_stop_req = true; }
static void toggle(void)
{
    if (s_on) { sub_stop(); return; }
    ensure_task();
    if (audio_stream_start(CHUNK_MS, on_chunk, NULL)) { s_on = true; s_finals = 0; }
    else lv_label_set_text(u_st, LV_SYMBOL_WARNING " micrófono ocupado");
}
static void on_go(lv_event_t *e) { toggle(); }

static void tick(lv_timer_t *t)
{
    if (lv_screen_active() != s_scr) return;
    char b[96];
    if (!s_on) snprintf(b, sizeof b, "toca para empezar · 点击开始");
    else if (s_err) snprintf(b, sizeof b, LV_SYMBOL_WARNING " servidor: error %d — reintento", s_err);
    else snprintf(b, sizeof b, LV_SYMBOL_AUDIO " escuchando · 正在听  ·  %.1f s", s_lat_ms / 1000.0);
    lv_label_set_text(u_st, b);
    lv_obj_set_style_bg_color(u_go, lv_color_hex(s_on ? C_GREEN : C_PANEL), 0);
    lv_label_set_text(u_go_l, s_on ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    lv_bar_set_value(u_bar, s_on ? audio_level() : 0, LV_ANIM_OFF);
    if (s_part_dirty) { s_part_dirty = false; lv_label_set_text(u_part, s_partial); }
}

void subs_leave(void) { if (s_on) sub_stop(); }

void subs_app_open(void)
{
    if (!s_scr) {
        lv_obj_t *body = app_frame(&s_scr, "Subtítulos en vivo  ·  实时字幕", C_TEAL);
        lv_obj_t *left = lv_obj_create(body); plain(left, C_BG);
        lv_obj_set_size(left, 300, LV_PCT(100));
        lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(left, 14, 0);
        label(left, f18, C_DIM, "cualquier idioma · 任何语言 →");
        for (int i = 0; i < 3; i++) {
            u_tgt[i] = pill(left, TGT_T[i], C_PANEL, f24);
            lv_obj_set_width(u_tgt[i], 260);
            lv_obj_add_event_cb(u_tgt[i], on_tgt, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
        u_go = lv_obj_create(left); plain(u_go, C_PANEL);
        lv_obj_set_size(u_go, 150, 150); lv_obj_set_style_radius(u_go, 75, 0);
        lv_obj_add_flag(u_go, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(u_go, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(u_go, on_go, LV_EVENT_CLICKED, NULL);
        u_go_l = label(u_go, &lv_font_montserrat_40, C_TEXT, LV_SYMBOL_PLAY); lv_obj_center(u_go_l);
        u_bar = lv_bar_create(left); lv_obj_set_size(u_bar, 240, 14); lv_bar_set_range(u_bar, 0, 100);
        lv_obj_set_style_bg_color(u_bar, lv_color_hex(C_TEAL), LV_PART_INDICATOR);
        u_st = label(left, f18, C_DIM, "");
        lv_obj_set_width(u_st, 280); lv_label_set_long_mode(u_st, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(u_st, LV_TEXT_ALIGN_CENTER, 0);
        // right: captions, newest at the bottom, and the grey "being heard" line under them
        lv_obj_t *right = lv_obj_create(body); plain(right, C_BG);
        lv_obj_set_size(right, 1280 - 32 - 320, LV_PCT(100));
        lv_obj_align(right, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(right, 8, 0);
        lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
        u_list = lv_obj_create(right); plain(u_list, C_BG);
        lv_obj_set_width(u_list, LV_PCT(100)); lv_obj_set_flex_grow(u_list, 1);
        lv_obj_set_flex_flow(u_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(u_list, 14, 0);
        lv_obj_set_scroll_dir(u_list, LV_DIR_VER);
        u_part = label(right, f30, C_DIM, "");
        lv_obj_set_width(u_part, LV_PCT(100)); lv_label_set_long_mode(u_part, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_max_height(u_part, 130, 0);
        set_tgt(0);
        lv_timer_create(tick, 150, NULL);
    }
    open_scr(s_scr);
}

bool subs_key(int enter, const char *text)
{
    if (!s_scr || lv_screen_active() != s_scr) return false;
    if (enter) toggle();
    else if (text && text[0] >= '1' && text[0] <= '3') set_tgt(text[0] - '1');
    return true;
}

// console test: feed a WAV as if the microphone heard it, one CHUNK_MS piece per second
typedef struct { uint8_t *wav; size_t len; } inj_t;
static void inj_task(void *a)
{
    inj_t *in = a; const int16_t *pcm; size_t ns; int rate;
    if (wav_parse(in->wav, in->len, &pcm, &ns, &rate) && rate == PAPP_RATE) {
        size_t step = PAPP_RATE * CHUNK_MS / 1000, i = 0;
        s_on = true;
        while (i < ns) {
            size_t k = ns - i < step ? ns - i : step;
            uint8_t *c = heap_caps_malloc(k * 2, MALLOC_CAP_SPIRAM);
            if (c) { memcpy(c, pcm + i, k * 2); on_chunk(c, k * 2, NULL); }
            i += k; vTaskDelay(pdMS_TO_TICKS(CHUNK_MS));
        }
        // 2 s of silence so the server closes the segment, then stop
        for (int s = 0; s < 2; s++) { uint8_t *z = heap_caps_calloc(1, step * 2, MALLOC_CAP_SPIRAM); if (z) on_chunk(z, step * 2, NULL); vTaskDelay(pdMS_TO_TICKS(CHUNK_MS)); }
        vTaskDelay(pdMS_TO_TICKS(3000));
        s_on = false; s_stop_req = true;
    } else ESP_LOGW(TAG, "inject: not a 16 kHz mono WAV");
    free(in->wav); free(in);
    vTaskDelete(NULL);
}
void subs_inject(const uint8_t *wav, size_t len)
{
    if (!s_scr) return;
    ensure_task();
    inj_t *in = malloc(sizeof *in); if (!in) return;
    in->wav = heap_caps_malloc(len, MALLOC_CAP_SPIRAM); in->len = len;
    if (!in->wav) { free(in); return; }
    memcpy(in->wav, wav, len);
    xTaskCreatePinnedToCoreWithCaps(inj_task, "subinj", 4096, in, 3, NULL, 1, MALLOC_CAP_SPIRAM);
}
