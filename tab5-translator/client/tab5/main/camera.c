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
#include "driver/ppa.h"
#include "ui_priv.h"
#include "papp.h"
#include "svc.h"
#include "sdcard.h"

static const char *TAG = "cam";
#define PW 960            // PPA scales 1280x720 by 12/16 (exact step) -> shown 1:1, no LVGL transform
#define PH 540
#define NBUF 2
static lv_obj_t *s_scr, *c_img, *c_list, *c_st, *c_eng[2];   // c_list/c_st live on the RESULT screen
static lv_obj_t *s_res, *r_img, *r_sum, *l_st;                   // result screen + live-screen status
static uint16_t *s_shot;                                         // frozen capture, PW x PH, not mirrored
static lv_image_dsc_t s_shot_dsc;
static volatile bool s_paused;                                    // results showing: no preview work
static uint16_t *s_prev;                 // PW x PH RGB565 preview (PSRAM)
static lv_image_dsc_t s_dsc;
static volatile bool s_run, s_snap, s_task_alive, s_mirror = true;
static int s_engine;                     // 0 = ocr (fast), 1 = vl (better)
static bool s_cam_inited;
static jpeg_encoder_handle_t s_jpeg;
static ppa_client_handle_t s_ppa;
static size_t s_prev_sz;

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
    char sum[400]; jstr(j, "summary_es", sum, sizeof sum);
    char b[160];
    snprintf(b, sizeof b, "%s %d %s · %s · %.1f s", n ? LV_SYMBOL_OK : LV_SYMBOL_WARNING, n, n == 1 ? "texto" : "textos",
             eng, r->rtt_ms / 1000.0);
    lv_label_set_text(c_st, b);
    lv_label_set_text(r_sum, sum[0] ? sum : (n ? "" : "No encontré texto. Acerca el letrero y vuelve a intentar."));
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

static void scale_into(const uint16_t *src, int w, int h, uint16_t *dst, bool mirror)
{
    ppa_srm_oper_config_t op = {
        .in = { .buffer = src, .pic_w = w, .pic_h = h, .block_w = w, .block_h = h, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .out = { .buffer = dst, .buffer_size = s_prev_sz, .pic_w = PW, .pic_h = PH, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0, .scale_x = (float)PW / w, .scale_y = (float)PH / h,
        .mirror_x = mirror, .mode = PPA_TRANS_MODE_BLOCKING,
    };
    if (s_ppa && ppa_do_scale_rotate_mirror(s_ppa, &op) == ESP_OK) return;
    for (int y = 0; y < PH; y++) {                         // CPU fallback
        const uint16_t *row = src + (size_t)(y * h / PH) * w;
        for (int x = 0; x < PW; x++) dst[y * PW + x] = row[(mirror ? (PW - 1 - x) : x) * w / PW];
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
            if (copy) {
                scale_into(copy, w, h, s_shot, false);     // the photo as the sign really reads (not mirrored)
                lock();
                lv_obj_invalidate(r_img);
                lv_obj_clean(c_list);
                lv_label_set_text(c_st, LV_SYMBOL_REFRESH " leyendo el texto · 识别中 …");
                lv_label_set_text(r_sum, "");
                unlock();
                encode_and_send(copy, w, h); free(copy);
            }
            continue;
        }
        if (s_paused) {                                       // result screen up: keep the queue moving, no preview
            ioctl(fd, VIDIOC_QBUF, &b); vTaskDelay(pdMS_TO_TICKS(60));
            continue;
        }
        // hardware scale + mirror (PPA SRM): 1280x720 -> 960x540 in one DMA pass; CPU only invalidates
        ppa_srm_oper_config_t op = {
            .in = { .buffer = src, .pic_w = w, .pic_h = h, .block_w = w, .block_h = h, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
            .out = { .buffer = s_prev, .buffer_size = s_prev_sz, .pic_w = PW, .pic_h = PH, .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0, .scale_x = (float)PW / w, .scale_y = (float)PH / h,
            .mirror_x = s_mirror, .mode = PPA_TRANS_MODE_BLOCKING,
        };
        esp_err_t pe = s_ppa ? ppa_do_scale_rotate_mirror(s_ppa, &op) : ESP_FAIL;
        if (pe != ESP_OK) {                      // fallback: CPU nearest-neighbour
            for (int y = 0; y < PH; y++) {
                const uint16_t *row = src + (size_t)(y * h / PH) * w;
                uint16_t *dst = s_prev + y * PW;
                for (int x = 0; x < PW; x++) dst[x] = row[(s_mirror ? (PW - 1 - x) : x) * w / PW];
            }
        }
        lock(); lv_obj_invalidate(c_img); unlock();
        ioctl(fd, VIDIOC_QBUF, &b);
        if (++frames == 60) { ESP_LOGI(TAG, "preview %.1f fps (%s)", 60e6 / (esp_timer_get_time() - t0), s_ppa ? "PPA" : "CPU"); }
        vTaskDelay(pdMS_TO_TICKS(5));
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
    if (!s_run || s_paused) return;
    s_paused = true;                                            // freeze: the next frame becomes the photo
    s_snap = true;
    open_scr(s_res);
}
static void on_new(lv_event_t *e)                               // "Nueva captura": back to the live view
{
    s_paused = false;
    open_scr(s_scr);
}
static void eng_set(int i)
{
    s_engine = i;
    for (int k = 0; k < 2; k++) lv_obj_set_style_bg_color(c_eng[k], lv_color_hex(k == i ? C_VIOLET : C_PANEL), 0);
}
static void on_eng(lv_event_t *e) { eng_set((int)(intptr_t)lv_event_get_user_data(e)); }
static void on_mirror(lv_event_t *e) { s_mirror = !s_mirror; }

static void img_dsc(lv_image_dsc_t *d, uint16_t *buf)
{
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC; d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = PW; d->header.h = PH; d->header.stride = PW * 2;
    d->data = (const uint8_t *)buf; d->data_size = PW * PH * 2;
}

static void build_result(void)
{
    lv_obj_t *body = app_frame(&s_res, "Resultado  ·  翻译结果", C_VIOLET);
    // left: the photo (960x540 shown at 2/3 = 640x360) + summary + big "new capture"
    lv_obj_t *left = lv_obj_create(body); plain(left, C_BG);
    lv_obj_set_size(left, 644, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(left, 12, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    r_img = lv_image_create(left);
    lv_image_set_src(r_img, &s_shot_dsc);
    lv_image_set_scale(r_img, 171);                            // 960 * 171/256 = 641
    lv_obj_set_size(r_img, 640, 360);
    lv_image_set_inner_align(r_img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_style_radius(r_img, 14, 0); lv_obj_set_style_clip_corner(r_img, true, 0);
    c_st = label(left, f18, C_DIM, "");
    r_sum = label(left, f24, C_TEXT, "");
    lv_obj_set_width(r_sum, 640); lv_label_set_long_mode(r_sum, LV_LABEL_LONG_WRAP);
    lv_obj_t *nb = pill(left, LV_SYMBOL_IMAGE "  Nueva captura · 再拍一张", C_VIOLET, f30);
    lv_obj_set_size(nb, 640, 72);
    lv_obj_add_event_cb(nb, on_new, LV_EVENT_CLICKED, NULL);
    // right: the translations, big
    c_list = lv_obj_create(body); plain(c_list, C_BG);
    lv_obj_set_size(c_list, 1280 - 32 - 664, LV_PCT(100));
    lv_obj_align(c_list, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_flex_flow(c_list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(c_list, 10, 0);
    lv_obj_set_scroll_dir(c_list, LV_DIR_VER);
}

void cam_app_open(void)
{
    if (!s_scr) {
        lv_obj_t *body = app_frame(&s_scr, "Señala y traduce  ·  拍照翻译", C_VIOLET);
        s_prev_sz = (PW * PH * 2 + 127) & ~127;                 // PPA output: cache-line aligned + sized
        s_prev = heap_caps_aligned_calloc(128, 1, s_prev_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!s_prev) s_prev = heap_caps_aligned_calloc(128, 1, s_prev_sz, MALLOC_CAP_SPIRAM);
        s_shot = heap_caps_aligned_calloc(128, 1, s_prev_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!s_shot) s_shot = heap_caps_aligned_calloc(128, 1, s_prev_sz, MALLOC_CAP_SPIRAM);
        ppa_client_config_t pc = { .oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1 };
        if (ppa_register_client(&pc, &s_ppa) != ESP_OK) { s_ppa = NULL; ESP_LOGW(TAG, "PPA client failed, CPU scaling"); }
        img_dsc(&s_dsc, s_prev); img_dsc(&s_shot_dsc, s_shot);
        c_img = lv_image_create(body);
        lv_image_set_src(c_img, &s_dsc);
        lv_obj_set_size(c_img, 960, 540);
        lv_obj_align(c_img, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_radius(c_img, 14, 0); lv_obj_set_style_clip_corner(c_img, true, 0);
        lv_obj_add_flag(c_img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c_img, on_snap, LV_EVENT_CLICKED, NULL);
        // right column: one big shutter, then the model choice
        lv_obj_t *col = lv_obj_create(body); plain(col, C_BG);
        lv_obj_set_size(col, 1280 - 32 - 976, LV_PCT(100));
        lv_obj_align(col, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 14, 0);
        lv_obj_t *sh = lv_obj_create(col); plain(sh, C_VIOLET);
        lv_obj_set_size(sh, 200, 200); lv_obj_set_style_radius(sh, 100, 0);
        lv_obj_set_style_border_width(sh, 8, 0); lv_obj_set_style_border_color(sh, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_bg_color(sh, lv_color_hex(C_LINE), LV_STATE_PRESSED);
        lv_obj_add_flag(sh, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(sh, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(sh, on_snap, LV_EVENT_CLICKED, NULL);
        lv_obj_t *si = label(sh, &lv_font_montserrat_40, C_TEXT, LV_SYMBOL_IMAGE); lv_obj_center(si);
        label(col, f24, C_TEXT, "Capturar · 拍照");
        label(col, f18, C_DIM, "modelo · model");
        c_eng[0] = pill(col, LV_SYMBOL_CHARGE " rápido  ~1 s", C_PANEL, f24);
        c_eng[1] = pill(col, LV_SYMBOL_EYE_OPEN " mejor  ~3 s", C_PANEL, f24);
        for (int i = 0; i < 2; i++) { lv_obj_set_width(c_eng[i], 240); lv_obj_add_event_cb(c_eng[i], on_eng, LV_EVENT_CLICKED, (void *)(intptr_t)i); }
        lv_obj_t *mi = pill(col, LV_SYMBOL_SHUFFLE " espejo", C_PANEL, f18);
        lv_obj_add_event_cb(mi, on_mirror, LV_EVENT_CLICKED, NULL);
        l_st = label(col, f18, C_DIM, "pon el letrero frente a la cámara");
        lv_obj_set_width(l_st, 240); lv_label_set_long_mode(l_st, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l_st, LV_TEXT_ALIGN_CENTER, 0);
        build_result();
        eng_set(1);                                              // Erick 11-08: VL ("mejor") gives clearly better answers
    }
    s_paused = false;
    open_scr(s_scr);
    if (!s_task_alive) {
        s_run = true;
        xTaskCreatePinnedToCoreWithCaps(cam_task, "cam", 12288, NULL, 3, NULL, 1, MALLOC_CAP_SPIRAM);
    }
}

bool cam_key(int enter)          // physical keyboard: Enter / space = capture, or "new capture" on the result screen
{
    if (!s_scr) return false;
    lv_obj_t *a = lv_screen_active();
    if (a == s_res) { if (enter) on_new(NULL); return true; }
    if (a != s_scr) return false;
    if (enter) on_snap(NULL);
    return true;
}
