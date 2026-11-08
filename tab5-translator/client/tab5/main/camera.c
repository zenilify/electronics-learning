// Point & Translate (D-T5-5 row 9). The Tab5 camera faces the user, so the preview is mirrored like a selfie;
// the frame sent for OCR is not mirrored. Server contract: T5-S server/API.md (/ocr-translate engine=ocr|vl|best).
#include "camera.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "linux/videodev2.h"
#include "esp_video_device.h"
#include "driver/jpeg_encode.h"
#include "ui_priv.h"
#include "papp.h"
#include "svc.h"
#include "sdcard.h"

static const char *TAG = "cam";
#define PW 640
#define PH 360
#define NBUF 2
static lv_obj_t *s_scr, *c_img, *c_list, *c_st, *c_eng[2];
static uint16_t *s_prev;                 // PW x PH RGB565 preview (PSRAM)
static lv_image_dsc_t s_dsc;
static volatile bool s_run, s_snap, s_task_alive, s_mirror = true;
static int s_engine;                     // 0 = ocr (fast), 1 = vl (better)
static bool s_cam_inited;
static jpeg_encoder_handle_t s_jpeg;

// ---------- results ----------
static char *jstr(const char *p, const char *key, char *out, size_t n)   // "key": "value" (minimal unescape)
{
    char pat[24]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *k = strstr(p, pat); out[0] = 0;
    if (!k) return NULL;
    const char *v = strchr(k + strlen(pat), '"'); if (!v) return NULL;
    size_t o = 0; v++;
    while (*v && *v != '"' && o + 1 < n) {
        if (*v == '\\' && v[1]) { v++; out[o++] = *v == 'n' ? ' ' : *v; v++; continue; }
        out[o++] = *v++;
    }
    out[o] = 0;
    return (char *)v;
}

static void on_item(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_current_target(e);
    const char *zh = lv_label_get_text(lv_obj_get_child(b, 0));
    const char *py = lv_label_get_text(lv_obj_get_child(b, 1));
    const char *es = lv_label_get_text(lv_obj_get_child(b, 2));
    show_card(zh, py, es, NULL);
    svc_say(LANG_ES, es, NULL, NULL);
}

static void ocr_done(const papp_result_t *r, void *ud)
{
    lock();
    lv_obj_clean(c_list);
    if (r->http_status != 200 || !r->tgt_text) {
        char b[80]; snprintf(b, sizeof b, LV_SYMBOL_WARNING " error %d · ¿hay red?", r->http_status);
        lv_label_set_text(c_st, b); unlock(); return;
    }
    const char *j = r->tgt_text, *items = strstr(j, "\"items\"");
    char eng[24]; jstr(j, "engine", eng, sizeof eng);
    int n = 0;
    for (const char *p = items; p && (p = strstr(p, "\"zh\"")); n++) {
        char zh[160], py[200], es[240];
        const char *end = strchr(p, '}');
        jstr(p, "zh", zh, sizeof zh); jstr(p, "pinyin", py, sizeof py); jstr(p, "es", es, sizeof es);
        lv_obj_t *b = lv_obj_create(c_list);
        plain(b, C_PANEL);
        lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(b, 10, 0); lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_LINE), LV_STATE_PRESSED);
        lv_obj_t *l0 = label(b, f30, C_AMBER, zh);
        lv_obj_t *l1 = label(b, f18, C_DIM, py);
        lv_obj_t *l2 = label(b, f24, C_TEXT, es);
        lv_obj_t *ls[] = { l0, l1, l2 };
        for (int i = 0; i < 3; i++) { lv_obj_set_width(ls[i], LV_PCT(100)); lv_label_set_long_mode(ls[i], LV_LABEL_LONG_WRAP); }
        lv_obj_add_event_cb(b, on_item, LV_EVENT_CLICKED, NULL);
        p = end ? end : p + 4;
    }
    char sum[300]; jstr(j, "summary_es", sum, sizeof sum);
    char b[400];
    snprintf(b, sizeof b, "%s %d %s · %s · %d ms%s%s", n ? LV_SYMBOL_OK : LV_SYMBOL_WARNING, n, n == 1 ? "texto" : "textos",
             eng, r->rtt_ms, sum[0] ? "\n" : "", sum);
    lv_label_set_text(c_st, b);
    sd_log("ocr.jsonl", j);
    unlock();
}

// ---------- capture ----------
static void encode_and_send(const uint16_t *frame, int w, int h)
{
    if (!s_jpeg) {
        jpeg_encode_engine_cfg_t ec = { .timeout_ms = 200 };
        if (jpeg_new_encoder_engine(&ec, &s_jpeg) != ESP_OK) { ESP_LOGE(TAG, "jpeg engine"); return; }
    }
    size_t in_sz = (size_t)w * h * 2, in_cap = 0, out_cap = 0;
    jpeg_encode_memory_alloc_cfg_t mi = { .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER };
    jpeg_encode_memory_alloc_cfg_t mo = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    uint8_t *in = jpeg_alloc_encoder_mem(in_sz, &mi, &in_cap);
    uint8_t *out = jpeg_alloc_encoder_mem(in_sz / 4, &mo, &out_cap);
    uint32_t olen = 0;
    if (in && out) {
        memcpy(in, frame, in_sz);
        jpeg_encode_cfg_t cfg = { .width = w, .height = h, .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
                                  .sub_sample = JPEG_DOWN_SAMPLING_YUV420, .image_quality = 85 };
        int64_t t0 = esp_timer_get_time();
        esp_err_t e = jpeg_encoder_process(s_jpeg, &cfg, in, in_sz, out, out_cap, &olen);
        ESP_LOGI(TAG, "jpeg %dx%d -> %lu B in %d ms (%s)", w, h, (unsigned long)olen,
                 (int)((esp_timer_get_time() - t0) / 1000), esp_err_to_name(e));
        if (e != ESP_OK) olen = 0;
    }
    free(in);
    if (!olen) { free(out); lock(); lv_label_set_text(c_st, LV_SYMBOL_WARNING " no pude codificar la foto"); unlock(); return; }
    if (sd_ok()) {                                // keep the photo: SD_ROOT/photos/<uptime>.jpg
        char p[64]; snprintf(p, sizeof p, SD_ROOT "/photos/%lld.jpg", (long long)(esp_timer_get_time() / 1000000));
        mkdir(SD_ROOT "/photos", 0775);
        FILE *f = fopen(p, "wb"); if (f) { fwrite(out, 1, olen, f); fclose(f); }
    }
    uint8_t *body = heap_caps_malloc(olen, MALLOC_CAP_SPIRAM);
    if (body) memcpy(body, out, olen);
    free(out);
    const char *path = s_engine ? "/ocr-translate?engine=vl" : "/ocr-translate?engine=ocr";
    if (!body || !svc_post(path, "image/jpeg", body, olen, ocr_done, NULL)) {
        lock(); lv_label_set_text(c_st, "ocupado, intenta de nuevo"); unlock();
    }
}

static void cam_task(void *a)
{
    s_task_alive = true;
    int fd = -1; void *mem[NBUF] = {0}; size_t mlen[NBUF] = {0}; int w = 0, h = 0;
    if (!s_cam_inited) {
        esp_err_t e = bsp_camera_start(NULL);
        if (e != ESP_OK) { ESP_LOGE(TAG, "bsp_camera_start: %s", esp_err_to_name(e)); goto out; }
        s_cam_inited = true;
    }
    fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (fd < 0) { ESP_LOGE(TAG, "open %s failed", ESP_VIDEO_MIPI_CSI_DEVICE_NAME); goto out; }
    struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    int g = ioctl(fd, VIDIOC_G_FMT, &fmt);
    ESP_LOGI(TAG, "G_FMT %d: %lux%lu fourcc %.4s", g, (unsigned long)fmt.fmt.pix.width, (unsigned long)fmt.fmt.pix.height,
             (char *)&fmt.fmt.pix.pixelformat);
    for (int i = 0; i < 12; i++) {
        struct v4l2_fmtdesc d = { .index = i, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
        if (ioctl(fd, VIDIOC_ENUM_FMT, &d) != 0) break;
        ESP_LOGI(TAG, "  fmt %d: %.4s %s", i, (char *)&d.pixelformat, d.description);
    }
    struct v4l2_format want = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    want.fmt.pix.width = fmt.fmt.pix.width; want.fmt.pix.height = fmt.fmt.pix.height;
    want.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(fd, VIDIOC_S_FMT, &want) != 0) { ESP_LOGE(TAG, "S_FMT RGB565 failed"); goto out; }
    fmt = want;
    w = fmt.fmt.pix.width; h = fmt.fmt.pix.height;
    ESP_LOGI(TAG, "camera %dx%d RGB565", w, h);
    struct v4l2_requestbuffers rb = { .count = NBUF, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
    if (ioctl(fd, VIDIOC_REQBUFS, &rb) != 0) { ESP_LOGE(TAG, "REQBUFS"); goto out; }
    for (int i = 0; i < NBUF; i++) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i };
        ioctl(fd, VIDIOC_QUERYBUF, &b);
        mem[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset); mlen[i] = b.length;
        ioctl(fd, VIDIOC_QBUF, &b);
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);
    int frames = 0; int64_t t0 = esp_timer_get_time();
    while (s_run) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
        if (ioctl(fd, VIDIOC_DQBUF, &b) != 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        const uint16_t *src = mem[b.index];
        if (s_snap) {
            s_snap = false;
            uint16_t *copy = heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM);
            if (copy) memcpy(copy, src, (size_t)w * h * 2);
            ioctl(fd, VIDIOC_QBUF, &b);
            if (copy) { encode_and_send(copy, w, h); free(copy); }
            continue;
        }
        // nearest-neighbour downscale to PW x PH (+ mirror for the front camera)
        lock();
        for (int y = 0; y < PH; y++) {
            const uint16_t *row = src + (size_t)(y * h / PH) * w;
            uint16_t *dst = s_prev + y * PW;
            for (int x = 0; x < PW; x++) dst[x] = row[(s_mirror ? (PW - 1 - x) : x) * w / PW];
        }
        lv_obj_invalidate(c_img);
        unlock();
        ioctl(fd, VIDIOC_QBUF, &b);
        if (++frames == 60) { ESP_LOGI(TAG, "preview %.1f fps", 60e6 / (esp_timer_get_time() - t0)); }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    ioctl(fd, VIDIOC_STREAMOFF, &type);
out:
    for (int i = 0; i < NBUF; i++) if (mem[i]) munmap(mem[i], mlen[i]);
    if (fd >= 0) close(fd);
    s_task_alive = false;
    vTaskDelete(NULL);
}

void cam_stop(void) { s_run = false; }

static void on_snap(lv_event_t *e)
{
    if (!s_run) return;
    s_snap = true;
    lv_label_set_text(c_st, LV_SYMBOL_REFRESH " leyendo el texto · 识别中 …");
}
static void eng_set(int i)
{
    s_engine = i;
    for (int k = 0; k < 2; k++) lv_obj_set_style_bg_color(c_eng[k], lv_color_hex(k == i ? C_VIOLET : C_PANEL), 0);
}
static void on_eng(lv_event_t *e) { eng_set((int)(intptr_t)lv_event_get_user_data(e)); }
static void on_mirror(lv_event_t *e) { s_mirror = !s_mirror; }

void cam_app_open(void)
{
    if (!s_scr) {
        lv_obj_t *body = app_frame(&s_scr, "Señala y traduce  ·  拍照翻译", C_VIOLET);
        s_prev = heap_caps_calloc(PW * PH, 2, MALLOC_CAP_SPIRAM);
        s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_dsc.header.w = PW; s_dsc.header.h = PH; s_dsc.header.stride = PW * 2;
        s_dsc.data = (const uint8_t *)s_prev; s_dsc.data_size = PW * PH * 2;
        c_img = lv_image_create(body);
        lv_image_set_src(c_img, &s_dsc);
        lv_image_set_scale(c_img, 384);                          // 640x360 shown at 960x540
        lv_obj_set_size(c_img, 960, 540);
        lv_image_set_inner_align(c_img, LV_IMAGE_ALIGN_CENTER);
        lv_obj_align(c_img, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_add_flag(c_img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c_img, on_snap, LV_EVENT_CLICKED, NULL);
        lv_obj_t *col = lv_obj_create(body);
        plain(col, C_BG);
        lv_obj_set_size(col, 1280 - 32 - 976, LV_PCT(100));
        lv_obj_align(col, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(col, 10, 0);
        lv_obj_t *sn = pill(col, LV_SYMBOL_IMAGE " Leer texto", C_VIOLET, f30);
        lv_obj_set_width(sn, LV_PCT(100));
        lv_obj_add_event_cb(sn, on_snap, LV_EVENT_CLICKED, NULL);
        lv_obj_t *row = lv_obj_create(col); plain(row, C_BG);
        lv_obj_set_size(row, LV_PCT(100), 56); lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 8, 0);
        c_eng[0] = pill(row, "rápido", C_PANEL, f18); c_eng[1] = pill(row, "mejor", C_PANEL, f18);
        for (int i = 0; i < 2; i++) lv_obj_add_event_cb(c_eng[i], on_eng, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *mi = pill(row, LV_SYMBOL_SHUFFLE, C_PANEL, f18);
        lv_obj_add_event_cb(mi, on_mirror, LV_EVENT_CLICKED, NULL);
        c_st = label(col, f18, C_DIM, "pon el letrero frente a la cámara y toca");
        lv_obj_set_width(c_st, LV_PCT(100)); lv_label_set_long_mode(c_st, LV_LABEL_LONG_WRAP);
        c_list = lv_obj_create(col); plain(c_list, C_BG);
        lv_obj_set_size(c_list, LV_PCT(100), LV_PCT(100)); lv_obj_set_flex_grow(c_list, 1);
        lv_obj_set_flex_flow(c_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(c_list, 8, 0);
        lv_obj_set_scroll_dir(c_list, LV_DIR_VER);
        eng_set(0);
    }
    open_scr(s_scr);
    if (!s_task_alive) {
        s_run = true;
        xTaskCreatePinnedToCoreWithCaps(cam_task, "cam", 12288, NULL, 3, NULL, 1, MALLOC_CAP_SPIRAM);
    }
}

bool cam_key(int enter)          // physical keyboard on the camera screen: Enter / space = read text
{
    if (!s_scr || lv_screen_active() != s_scr) return false;
    if (enter) on_snap(NULL);
    return true;
}
