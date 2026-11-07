// PAPP — Tab5 push-to-talk translator (es/en ↔ zh). Lane T5, 2026-11-07.
// Flow per turn: hold a side's button -> ES7210 mic -> release -> one POST to the PAPP backend
// (cachy via Tailscale, cachy LAN, or Cloud Run) -> both texts on screen -> translated WAV on the speaker.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "bsp/m5stack_tab5.h"
#include "papp.h"
#include "ui.h"
#include "audio.h"
#include "net.h"
#include "papp_http.h"
#include "battery.h"
#include "settings.h"
#include "demo_data.h"

static const char *TAG = "papp";
volatile papp_status_t g_status = { .endpoint = -1, .batt_v = -1 };

// ---------- languages ----------
const char *lang_code(lang_t l) { static const char *c[] = { "es", "en", "zh" }; return l < LANG_COUNT ? c[l] : "es"; }
const char *lang_label(lang_t l) { static const char *c[] = { "ES", "EN", "中文" }; return l < LANG_COUNT ? c[l] : "?"; }
lang_t lang_from_code(const char *c) { return !strcmp(c, "zh") ? LANG_ZH : !strcmp(c, "en") ? LANG_EN : LANG_ES; }
void pair_langs(pair_t p, lang_t *l, lang_t *r)
{
    switch (p) {
    case PAIR_EN_ZH: *l = LANG_EN; *r = LANG_ZH; break;
    case PAIR_ES_EN: *l = LANG_ES; *r = LANG_EN; break;
    default:         *l = LANG_ES; *r = LANG_ZH; break;
    }
}
static volatile pair_t s_pair = PAIR_ES_ZH;
void papp_set_pair(pair_t p) { s_pair = p; settings_set_u8("pair", (uint8_t)p); }
pair_t papp_get_pair(void) { return s_pair; }

// ---------- embedded demo clips ----------
#define CLIP(n) extern const uint8_t d##n##_src_s[] asm("_binary_d" #n "_src_wav_start"); \
                extern const uint8_t d##n##_src_e[] asm("_binary_d" #n "_src_wav_end");   \
                extern const uint8_t d##n##_tgt_s[] asm("_binary_d" #n "_tgt_wav_start"); \
                extern const uint8_t d##n##_tgt_e[] asm("_binary_d" #n "_tgt_wav_end");
CLIP(1) CLIP(2) CLIP(3) CLIP(4) CLIP(5)
static const struct { const uint8_t *ss, *se, *ts, *te; } CLIPS[DEMO_COUNT] = {
    { d1_src_s, d1_src_e, d1_tgt_s, d1_tgt_e }, { d2_src_s, d2_src_e, d2_tgt_s, d2_tgt_e },
    { d3_src_s, d3_src_e, d3_tgt_s, d3_tgt_e }, { d4_src_s, d4_src_e, d4_tgt_s, d4_tgt_e },
    { d5_src_s, d5_src_e, d5_tgt_s, d5_tgt_e },
};

// ---------- worker ----------
typedef enum { MSG_JOB, MSG_PLAY } msg_type_t;
typedef struct { msg_type_t type; papp_job_t *job; int16_t *pcm; size_t n; int rate; } msg_t;
static QueueHandle_t s_q;
static volatile bool s_working, s_demo, s_demo_offline;

bool papp_submit(papp_job_t *job)
{
    if (s_working || uxQueueMessagesWaiting(s_q)) return false;
    msg_t m = { .type = MSG_JOB, .job = job };
    return xQueueSend(s_q, &m, 0) == pdTRUE;
}

void papp_replay(int16_t *pcm, size_t n, int rate)
{
    if (s_working) return;
    msg_t m = { .type = MSG_PLAY, .pcm = pcm, .n = n, .rate = rate };
    xQueueSend(s_q, &m, 0);
}

static int side_for(lang_t src)
{
    lang_t l, r; pair_langs(s_pair, &l, &r);
    return src == r ? 1 : 0;
}

static void offline_reply(const papp_job_t *job, papp_result_t *r)
{
    const int i = job->demo_idx;
    const int16_t *pcm; size_t ns; int rate;
    memset(r, 0, sizeof *r);
    r->src_text = strdup(DEMO_TEXT[i].src_text);
    r->tgt_text = strdup(DEMO_TEXT[i].tgt_text);
    if (wav_parse(CLIPS[i].ts, CLIPS[i].te - CLIPS[i].ts, &pcm, &ns, &rate)) {
        r->pcm = heap_caps_malloc(ns * 2, MALLOC_CAP_SPIRAM);
        if (r->pcm) { memcpy(r->pcm, pcm, ns * 2); r->samples = ns; r->rate = rate; }
    }
    r->http_status = 200; r->endpoint = -1;
}

static void run_job(papp_job_t *job)
{
    papp_result_t r;
    char msg[96];
    snprintf(msg, sizeof msg, "Traduciendo %s → %s · 翻译中", lang_label(job->src), lang_label(job->tgt));
    ui_busy(job, msg);
    bool offline = false;
    if (job->demo_idx >= 0 && s_demo_offline) { offline_reply(job, &r); offline = true; }
    else {
        papp_translate(job, &r);
        g_status.endpoint = r.endpoint;
        if (r.http_status != 200 && job->demo_idx >= 0) {   // demo never fails on stage: canned reply
            papp_result_free(&r); offline_reply(job, &r); offline = true;
        }
    }
    ESP_LOGI(TAG, "turn %s->%s status %d rtt %d ms (asr %d nmt %d tts %d) via %s%s",
             lang_code(job->src), lang_code(job->tgt), r.http_status, r.rtt_ms, r.t_asr, r.t_nmt, r.t_tts,
             papp_endpoint_name(r.endpoint), offline ? " [offline demo]" : "");
    if (r.http_status == 200) {
        int16_t *pcm = r.pcm; size_t n = r.samples; int rate = r.rate;
        ui_add_turn(job, &r, offline);           // bubble now owns pcm
        ui_busy(NULL, NULL);
        ui_set_hint("Reproduciendo · 播放中  (toca la burbuja para repetir · 点击重播)");
        audio_play(pcm, n, rate);
        ui_busy(NULL, NULL);
        if (job->wav_owned) free(job->wav);
        free(job);
    } else {
        ui_busy(NULL, NULL);
        ui_add_error(job, &r);                    // error bubble owns job (retry)
    }
    papp_result_free(&r);
}

static void worker(void *a)
{
    msg_t m;
    for (;;) {
        if (!xQueueReceive(s_q, &m, portMAX_DELAY)) continue;
        s_working = true;
        if (m.type == MSG_PLAY) audio_play(m.pcm, m.n, m.rate);
        else run_job(m.job);
        s_working = false;
    }
}

// ---------- demo mode ----------
static void demo_task(void *a)
{
    for (int i = 0; i < DEMO_COUNT && s_demo; i++) {
        ui_demo_state(true, i, DEMO_COUNT);
        papp_job_t *j = calloc(1, sizeof *j);
        j->src = lang_from_code(DEMO_TEXT[i].src); j->tgt = lang_from_code(DEMO_TEXT[i].tgt);
        // pick the pair that contains both languages so the bubble lands on the right side
        if ((j->src == LANG_EN || j->tgt == LANG_EN) && (j->src == LANG_ZH || j->tgt == LANG_ZH)) s_pair = PAIR_EN_ZH;
        else if (j->src == LANG_ZH || j->tgt == LANG_ZH) s_pair = PAIR_ES_ZH;
        j->side = side_for(j->src);
        j->wav = (uint8_t *)CLIPS[i].ss; j->wav_len = CLIPS[i].se - CLIPS[i].ss; j->wav_owned = false;
        j->demo_idx = i;
        while (s_demo && !papp_submit(j)) vTaskDelay(pdMS_TO_TICKS(200));
        if (!s_demo) { free(j); break; }
        vTaskDelay(pdMS_TO_TICKS(300));
        while (s_working || uxQueueMessagesWaiting(s_q)) vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    s_demo = false;
    s_pair = (pair_t)settings_get_u8("pair", PAIR_ES_ZH);
    ui_demo_state(false, 0, DEMO_COUNT);
    vTaskDelete(NULL);
}

void papp_demo_toggle(bool offline_only)
{
    if (s_demo) { s_demo = false; audio_play_stop(); return; }
    s_demo = true; s_demo_offline = offline_only;
    xTaskCreate(demo_task, "demo", 4096, NULL, 3, NULL);
}
bool papp_demo_running(void) { return s_demo; }

// ---------- boot ----------
void app_main(void)
{
    ESP_LOGI(TAG, "PAPP Tab5 v%s", PAPP_VERSION);
    settings_init();
    s_pair = (pair_t)settings_get_u8("pair", PAIR_ES_ZH);
    if (s_pair >= PAIR_COUNT) s_pair = PAIR_ES_ZH;
    papp_http_init();
    g_status.have_token = papp_have_token();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = true,
        .flags = { .buff_dma = true, .buff_spiram = false, .sw_rotate = true },
    };
    cfg.lvgl_port_cfg.task_stack = 12288;
    cfg.lvgl_port_cfg.task_affinity = 1;
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp) { ESP_LOGE(TAG, "display init failed"); return; }
    uint8_t rot = settings_get_u8("rot", LV_DISPLAY_ROTATION_90);
    bsp_display_rotate(disp, rot == LV_DISPLAY_ROTATION_270 ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
    bsp_display_backlight_on();
    bsp_display_brightness_set(85);

    s_q = xQueueCreate(2, sizeof(msg_t));
    ui_init(disp);
    if (!audio_init()) ui_set_hint(LV_SYMBOL_WARNING " audio no disponible");
    battery_start();
    xTaskCreatePinnedToCore(worker, "worker", 8192, NULL, 5, NULL, 0);
    net_start();
    ESP_LOGI(TAG, "up: free internal %u kB, PSRAM %u kB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}
