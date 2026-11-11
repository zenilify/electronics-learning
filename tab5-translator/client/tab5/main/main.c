// PAPP — Tab5 push-to-talk translator (es/en ↔ zh). Lane T5, 2026-11-07.
// Flow per turn: hold a side's button -> ES7210 mic -> release -> one POST to the PAPP backend
// (cachy via Tailscale, cachy LAN, or Cloud Run) -> both texts on screen -> translated WAV on the speaker.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "bsp/m5stack_tab5.h"
#include "papp.h"
#include "ui.h"
#include "audio.h"
#include "net.h"
#include "papp_http.h"
#include "battery.h"
#include "settings.h"
#include "demo_data.h"
#include "c6ota.h"
#include "sdcard.h"
#include "svc.h"
#include "kbd.h"
#include "ui_priv.h"
#include "notes.h"
#include "remotes.h"
#include "restart.h"
#include "camera.h"
#include "usbport.h"

static const char *TAG = "papp";
void shot_console(int quality);              // shot.c
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
    if (r.http_status == 200 && (!r.tgt_text || !r.tgt_text[0] || !r.samples)) {
        r.http_status = 422;                      // server answered but heard nothing usable
    }
    if (r.http_status == 200) {
        char *ln = malloc(strlen(r.src_text ? r.src_text : "") + strlen(r.tgt_text) + 48);
        if (ln) {
            sprintf(ln, "%s>%s\t%d ms\t%s\t%s", lang_code(job->src), lang_code(job->tgt), r.rtt_ms,
                    r.src_text ? r.src_text : "", r.tgt_text);
            sd_log("history.tsv", ln); free(ln);
        }
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

// ---------- serial console (bench tests over the USB-C port) ----------
static void console_task(void *a)
{
    static char line[720]; size_t n = 0;       // long enough for a relayed remote datagram (`t5b R ...`, <= 512 B)
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }   // 5 ms: a relayed ESP-NOW frame should not wait 50 ms here
        if (c != '\n' && c != '\r') { if (n < sizeof line - 1) line[n++] = (char)c; continue; }
        line[n] = 0; n = 0;
        if (!strcmp(line, "demo")) papp_demo_toggle(false);
        else if (!strcmp(line, "demo offline")) papp_demo_toggle(true);
        else if (!strncmp(line, "c6ota ", 6)) c6_ota_start(line + 6);
        else if (!strcmp(line, "ant ext") || !strcmp(line, "ant int")) {
            settings_set_u8("ant", line[4] == 'e');
            ESP_LOGW(TAG, "antenna set to %s — rebooting", line + 4);
            vTaskDelay(pdMS_TO_TICKS(300)); esp_restart();
        }
        else if (!strcmp(line, "scan")) net_scan_log();
        else if (!strncmp(line, "wifi ", 5)) {          // wifi <slot> | wifi auto — same as the Ajustes picker
            int slot = !strcmp(line + 5, "auto") ? -1 : atoi(line + 5);
            ESP_LOGI(TAG, "picker (console): %s", slot < 0 ? "auto" : line + 5);
            net_join(slot);
        }
        else if (!strncmp(line, "key ", 4)) {           // inject a keyboard key: key a | key esc | key enter
            const char *k = line + 4; int sp = !strcmp(k, "esc") ? KBD_ESC : !strcmp(k, "enter") ? KBD_ENTER : 0;
            ESP_LOGI(TAG, "key '%s'", k);
            lock(); home_kbd(sp ? NULL : k, sp); unlock();
        }
        else if (!strncmp(line, "convtest ", 9)) {     // convtest <1..5>: demo clip through the conversation path
            int i = atoi(line + 9) - 1;
            if (i >= 0 && i < DEMO_COUNT) conv_inject(CLIPS[i].ss, CLIPS[i].se - CLIPS[i].ss);
        }
        else if (!strncmp(line, "rkey ", 5)) {          // rkey <32 hex>: remotes HMAC key (from cachy t5-remote-key)
            if (!remotes_set_key_hex(line + 5)) ESP_LOGW(TAG, "rkey: need 32 hex chars");
            memset(line, 0, sizeof line);
        }
        else if (!strncmp(line, "t5b ", 4)) remotes_bridge_line(line + 4, 1);   // ESP-NOW bridge frame relayed over this console
        else if (!strcmp(line, "t5buart off")) { remotes_bridge_uart(-1, -1, true); ESP_LOGI(TAG, "bridge UART off after the next boot"); }
        else if (!strncmp(line, "t5buart ", 8)) {       // t5buart <rx gpio> <tx gpio>: ESP-NOW bridge on the Grove port (53 54)
            int rx = -1, tx = -1;
            if (sscanf(line + 8, "%d %d", &rx, &tx) == 2 && rx >= 0 && tx >= 0 && rx < 55 && tx < 55) remotes_bridge_uart(rx, tx, true);
            else ESP_LOGW(TAG, "t5buart <rx gpio> <tx gpio> | t5buart off");
        }
        else if (!strncmp(line, "remote accept ", 14)) remotes_accept(line + 14);
        else if (!strcmp(line, "remote forget")) remotes_forget();
        else if (!strcmp(line, "remote list")) { char b[400]; remotes_list(b, sizeof b); ESP_LOGI(TAG, "remotes:\n%s", b); }
        else if (!strncmp(line, "substest ", 9)) {     // substest <1..5>: demo clip through live subtitles
            int i = atoi(line + 9) - 1;
            if (i >= 0 && i < DEMO_COUNT) subs_inject(CLIPS[i].ss, CLIPS[i].se - CLIPS[i].ss);
        }
        else if (!strcmp(line, "camsrc")) { lock(); cam_toggle_source(); unlock(); }
        else if (!strcmp(line, "sdsync") || !strcmp(line, "sdsync full")) {   // full: hash every file, ignore the manifest
            extern volatile bool g_sd_sync_full; g_sd_sync_full = line[6] != 0;
            if (!svc_sd_sync(NULL, NULL)) ESP_LOGW(TAG, "svc busy");
        }
        else if (!strcmp(line, "nets")) {
            net_known_t k[NET_SLOTS]; int n = net_known(k, NET_SLOTS, NULL);
            for (int i = 0; i < n; i++) ESP_LOGI(TAG, "wifi%d %-20s %4d dBm%s%s", k[i].slot, k[i].ssid, k[i].rssi,
                                                 k[i].current ? " [current]" : "", k[i].slot == net_pref() ? " [picked]" : "");
        }
        else if (!strcmp(line, "sd")) {
            uint32_t t, f; bool ok = sd_mount(); sd_info(&t, &f);
            ESP_LOGI(TAG, "sd %s card=%s total=%lu MB free=%lu MB", ok ? "OK" : "FAIL", sd_card_name(),
                     (unsigned long)t, (unsigned long)f);
        }
        else if (!strcmp(line, "c6ver")) {
            uint32_t a, b, c;
            if (c6_version(&a, &b, &c)) ESP_LOGI(TAG, "C6 esp-hosted fw %lu.%lu.%lu", (unsigned long)a, (unsigned long)b, (unsigned long)c);
            else ESP_LOGW(TAG, "C6 fw version not available");
        }
        else if (!strcmp(line, "voice erick") || !strcmp(line, "voice papp")) {   // Ajustes "Voz"
            settings_set_u8("voice", line[6] == 'e'); ESP_LOGI(TAG, "voice %s", line + 6);
        }
        else if (!strncmp(line, "say ", 4) && strlen(line) > 7) {   // say es|en|zh <text>: SD voice/cache -> /tts
            line[6] = 0; svc_say(lang_from_code(line + 4), line + 7, NULL, NULL);
        }
        else if (!strcmp(line, "tz cn") || !strcmp(line, "tz mx")) {   // same as the Ajustes "Hora" button
            settings_set_u8("tz", line[3] == 'c'); papp_tz_apply();
            time_t t = time(NULL); struct tm lt; localtime_r(&t, &lt); char b[32]; strftime(b, sizeof b, "%Y-%m-%d %H:%M", &lt);
            ESP_LOGI(TAG, "local time %s", b);
        }
        else if (!strcmp(line, "usb on") || !strcmp(line, "usb off")) usbport_power(line[5] == 'n', true);   // USB-A 5 V (saved)
        else if (!strcmp(line, "usb host")) usbport_host_start(true);      // start the USB host stack (saved: on at boot)
        else if (!strcmp(line, "usb host once")) usbport_host_start(false); // same, this boot only (first test of a build)
        else if (!strcmp(line, "usb")) { char b[200]; usbport_describe(b, sizeof b, false); ESP_LOGI(TAG, "usb-a: %s", b); }
        else if (!strcmp(line, "io")) usbport_io_dump();                   // I/O expander registers, read from the chips
        else if (!strcmp(line, "ext5v on") || !strcmp(line, "ext5v off")) usbport_ext5v(line[7] == 'n', true);   // Grove / M5-Bus 5 V (saved)
        else if (!strcmp(line, "chg on") || !strcmp(line, "chg off")) usbport_charge(line[5] == 'n', true);       // battery charger (saved)
        else if (!strcmp(line, "chg once")) usbport_charge(true, false);                                           // this boot only
        else if (!strcmp(line, "uiwd reset")) { settings_set_u8("uistall", 0); ESP_LOGI(TAG, "ui restart counter cleared"); }
        else if (!strncmp(line, "bcam ", 5)) cam_set_back_usb(!strcmp(line + 5, "auto") ? "" : line + 5);          // bcam <ip> | bcam auto
#ifdef PAPP_CAMTEST
        else if (!strncmp(line, "camtest ", 8)) { void cam_test_bad_jpeg(int); cam_test_bad_jpeg(atoi(line + 8)); }
        else if (!strcmp(line, "uistall")) { ESP_LOGW(TAG, "uistall: holding the display lock for 25 s"); lock(); vTaskDelay(pdMS_TO_TICKS(25000)); unlock(); }
#endif
        else if (!strncmp(line, "shot", 4) && (line[4] == 0 || line[4] == ' ')) {   // shot [quality]: screen -> JPEG -> base64 (tools/screenshot.py)
            int q = line[4] ? atoi(line + 5) : 90;
            shot_console(q >= 10 && q <= 100 ? q : 90);
        }
        else if (!strcmp(line, "reboot")) {                      // a clean restart from the no-reset console (tools/console_nr.py)
            ESP_LOGW(TAG, "reboot: asked on the console");
            vTaskDelay(pdMS_TO_TICKS(300)); esp_restart();          // -> papp_full_reset() through the shutdown handler
        }
        else if (!strcmp(line, "reboot cpu")) {                  // test: restart the way a crash does (CPU-only reset);
            extern void esp_restart_noos(void);                  // the next boot must notice and do the full reset itself
            ESP_LOGW(TAG, "reboot cpu: CPU-only reset, as after a panic");
            vTaskDelay(pdMS_TO_TICKS(300)); esp_restart_noos();
        }
        else if (!strcmp(line, "status")) {
            char ub[200]; usbport_describe(ub, sizeof ub, false);
            // the leading fields keep their order (bench scripts and old logs grep "status wifi="); new ones go at the end
            ESP_LOGI(TAG, "status wifi=%d rssi=%d ep=%d zfix=%d health=%d ms batt=%.2f V %.2f A free_int=%u kB psram=%u kB v=%s up=%lld s usb-a=[%s]",
                     g_status.wifi_up, g_status.rssi, g_status.endpoint, g_status.zombie_fixes, g_status.health_ms, g_status.batt_v, g_status.batt_a,
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                     PAPP_VERSION, (long long)(esp_timer_get_time() / 1000000), ub);
            ESP_LOGI(TAG, "status2 cam=%s cam_bad_frames=%d ui_restarts=%d", cam_back_url()[0] ? cam_back_url() : "-", cam_bad_frames(), settings_get_u8("uistall", 0));
            ESP_LOGI(TAG, "status3 chg_kicks=%d", battery_chg_kicks());
            // internal RAM: int_min = sum of per-region low-water marks (a worst-case bound, not a moment that happened); int_largest = biggest piece left
            ESP_LOGI(TAG, "status4 int_min=%u kB int_largest=%u kB boot_reason=0x%x cpu_resets_fixed=%d",
                     (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                     papp_boot_reason(), papp_cpu_resets_fixed());
        }
    }
}

// ---------- UI heartbeat watchdog ----------
// The LVGL task has frozen in lv_refr.c wait_for_flushing() (display flush never reported done) on 11-08 and again on
// 11-11, both times with the camera app open; cause not found (30 deliberately broken JPEG frames did not reproduce it).
// A frozen screen in front of people is the worst outcome, so: if the 100 ms UI timer has been silent for 15 s the
// app restarts by itself (~14 s to the home screen) and counts it in NVS "uistall" (console `status`).
extern volatile int64_t g_ui_beat_us;      // ui.c
static void uiwd_task(void *a)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        int64_t now = esp_timer_get_time(), beat = g_ui_beat_us;
        if (!beat || now - beat < 15000000) continue;
        uint8_t n = settings_get_u8("uistall", 0);
        settings_set_u8("uistall", n < 255 ? n + 1 : 255);
        ESP_LOGE(TAG, "UI silent for %d s (display flush stuck?) - restarting. UI restarts so far: %d",
                 (int)((now - beat) / 1000000), n + 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }
}

// ---------- time zone (Ajustes) ----------
static const char *TZ_POSIX[] = { "CST6", "CST-8" };   // America/Mexico_City (no DST) · Asia/Shanghai
const char *TZ_NAME[] = { "México (UTC-6)", "China (UTC+8)" };
void papp_tz_apply(void)
{
    int i = settings_get_u8("tz", 0) ? 1 : 0;
    setenv("TZ", TZ_POSIX[i], 1); tzset();
    ESP_LOGI(TAG, "time zone %s (%s)", TZ_NAME[i], TZ_POSIX[i]);
}

// ---------- boot ----------
void app_main(void)
{
    ESP_LOGI(TAG, "PAPP Tab5 v%s", PAPP_VERSION);
    settings_init();
    papp_restart_guard();                      // a CPU-only reset leaves Wi-Fi dead on this board: see restart.h
    papp_tz_apply();                           // Ajustes: México (CST6) / China (CST-8), NVS "tz"
    s_pair = (pair_t)settings_get_u8("pair", PAIR_ES_ZH);
    if (s_pair >= PAIR_COUNT) s_pair = PAIR_ES_ZH;
    papp_http_init();
    g_status.have_token = papp_have_token();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = true,
        .flags = { .buff_dma = false, .buff_spiram = true, .sw_rotate = true },
    };
    cfg.lvgl_port_cfg.task_stack = 12288;
    cfg.lvgl_port_cfg.task_affinity = 1;
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp) { ESP_LOGE(TAG, "display init failed"); return; }
    uint8_t rot = settings_get_u8("rot", LV_DISPLAY_ROTATION_90);
    bsp_display_rotate(disp, rot == LV_DISPLAY_ROTATION_270 ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
    bsp_display_backlight_on();
    bsp_display_brightness_set(settings_get_u8("bri", 85));

    s_q = xQueueCreate(2, sizeof(msg_t));
    ui_init(disp);
    if (!audio_init()) ui_set_hint(LV_SYMBOL_WARNING " audio no disponible");
    battery_start();
    usbport_start();                           // USB-A 5 V on (NVS usb5v): powers the XIAO back camera. Before net_start():
                                               // the first use of expander 0x44 resets the chip, which also drops the C6's power
    xTaskCreatePinnedToCore(worker, "worker", 8192, NULL, 5, NULL, 0);
    net_start();
    svc_start();
    remotes_start();                           // StickS3 / Dial over UDP 47800 (protocol v1, T5-R)
    kbd_start();
    sd_mount();                       // after the C6 (slot 1) so both slots share one initialised host
    xTaskCreate(console_task, "console", 6144, NULL, 2, NULL);
    xTaskCreate(uiwd_task, "uiwd", 3072, NULL, 6, NULL);
    ESP_LOGI(TAG, "up: free internal %u kB, PSRAM %u kB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}
