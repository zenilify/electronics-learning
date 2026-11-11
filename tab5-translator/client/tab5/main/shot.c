// Screenshot over the USB console (lane T5, 2026-11-11): console `shot` renders the active LVGL screen, encodes it
// with the P4's hardware JPEG encoder and prints it as base64 between "@@SHOT" marker lines.
// Host side: tools/screenshot.py PORT out.jpg (no reset, FNV-1a check, retries).
// Why: nobody can see the 5" screen from a build machine. With this a session can CHECK a UI change (PDCA) and
// Erick gets clean screenshots of every app for the M5Stack deck without photographing the panel.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/jpeg_encode.h"
#include "ui_priv.h"

static const char *TAG = "shot";
static SemaphoreHandle_t s_done;
static lv_draw_buf_t *s_snap;

// runs on the LVGL task (12 kB stack): rendering a 1280x720 screen needs more stack than the console task has
static void snap_cb(void *a)
{
    s_snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    xSemaphoreGive(s_done);
}

static void b64_line(const uint8_t *p, size_t n)        // one "@@<base64>" line, n <= 72 bytes
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char o[2 + 96 + 2]; size_t k = 0;
    o[k++] = '@'; o[k++] = '@';
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        o[k++] = T[v >> 18]; o[k++] = T[v >> 12 & 63];
        o[k++] = i + 1 < n ? T[v >> 6 & 63] : '=';
        o[k++] = i + 2 < n ? T[v & 63] : '=';
    }
    o[k++] = '\n'; o[k] = 0;
    fputs(o, stdout);
}

void shot_console(int quality)
{
    if (!s_done) s_done = xSemaphoreCreateBinary();
    if (s_snap) { lock(); lv_draw_buf_destroy(s_snap); unlock(); s_snap = NULL; }   // left over from a timed-out call
    xSemaphoreTake(s_done, 0);
    int64_t t0 = esp_timer_get_time();
    lock(); lv_async_call(snap_cb, NULL); unlock();
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(8000)) != pdTRUE || !s_snap) { printf("@@SHOT FAIL snapshot\n"); return; }
    int w = s_snap->header.w, h = s_snap->header.h;
    size_t in_sz = (size_t)w * h * 2, in_cap = 0, out_cap = 0;
    uint32_t olen = 0; esp_err_t e = ESP_FAIL;
    jpeg_encoder_handle_t enc = NULL;
    jpeg_encode_engine_cfg_t ec = { .timeout_ms = 1000 };
    jpeg_encode_memory_alloc_cfg_t mi = { .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER };
    jpeg_encode_memory_alloc_cfg_t mo = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    uint8_t *in = jpeg_alloc_encoder_mem(in_sz, &mi, &in_cap);
    uint8_t *out = jpeg_alloc_encoder_mem(in_sz / 3, &mo, &out_cap);
    if (in && out && jpeg_new_encoder_engine(&ec, &enc) == ESP_OK) {
        memcpy(in, s_snap->data, in_sz);
        jpeg_encode_cfg_t cfg = { .width = w, .height = h, .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
                                  .sub_sample = JPEG_DOWN_SAMPLING_YUV420, .image_quality = quality };
        e = jpeg_encoder_process(enc, &cfg, in, in_sz, out, out_cap, &olen);
    }
    if (enc) jpeg_del_encoder_engine(enc);
    free(in);
    lock(); lv_draw_buf_destroy(s_snap); unlock(); s_snap = NULL;
    if (e != ESP_OK || !olen) { free(out); printf("@@SHOT FAIL encode %s\n", esp_err_to_name(e)); return; }
    uint32_t fnv = 2166136261u;
    for (uint32_t i = 0; i < olen; i++) { fnv ^= out[i]; fnv *= 16777619u; }
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    printf("@@SHOT BEGIN %d %d %lu %08lx %d\n", w, h, (unsigned long)olen, (unsigned long)fnv, ms);
    for (uint32_t i = 0, ln = 0; i < olen; i += 72, ln++) {
        b64_line(out + i, olen - i > 72 ? 72 : olen - i);
        if ((ln & 15) == 15) { fflush(stdout); vTaskDelay(1); }       // let lower-priority tasks breathe
    }
    printf("@@SHOT END\n");
    free(out);
    ESP_LOGI(TAG, "%dx%d -> %lu B JPEG q%d in %d ms", w, h, (unsigned long)olen, quality, ms);
}
