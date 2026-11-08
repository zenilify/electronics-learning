// PAPP back camera — Seeed XIAO ESP32S3 Sense + OV3660 stuck on the back of the Tab5 (lane T5, 2026-11-08).
// Joins the same Wi-Fi networks as the Tab5 (NVS wifi0..5, strongest visible), announces _pappcam._tcp via mDNS
// and serves, behind the PAPP bearer token (NVS "token"; a hotel network must not see the camera):
//   GET /frame    -> small JPEG (VGA) for the Tab5 viewfinder
//   GET /capture  -> full 3 MP JPEG (QXGA 2048x1536) for OCR
//   GET /health   -> {"ok":true,"sensor":"OV3660",...} (no auth)
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "mdns.h"

static const char *TAG = "xcam";
#define LED GPIO_NUM_21                   // XIAO user LED, active low
static char s_token[96];
static SemaphoreHandle_t s_cam;
static EventGroupHandle_t s_ev;
#define EV_IP BIT0
static int s_frames, s_caps;

// ---------- camera (pins: Seeed XIAO ESP32S3 Sense) ----------
static esp_err_t cam_init(void)
{
    camera_config_t c = {
        .pin_pwdn = -1, .pin_reset = -1, .pin_xclk = 10, .pin_sccb_sda = 40, .pin_sccb_scl = 39,
        .pin_d7 = 48, .pin_d6 = 11, .pin_d5 = 12, .pin_d4 = 14, .pin_d3 = 16, .pin_d2 = 18, .pin_d1 = 17, .pin_d0 = 15,
        .pin_vsync = 38, .pin_href = 47, .pin_pclk = 13,
        .xclk_freq_hz = 20000000, .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG, .frame_size = FRAMESIZE_QXGA,    // allocate for the biggest frame
        .jpeg_quality = 12, .fb_count = 2, .fb_location = CAMERA_FB_IN_PSRAM, .grab_mode = CAMERA_GRAB_LATEST,
    };
    esp_err_t e = esp_camera_init(&c);
    if (e != ESP_OK) return e;
    sensor_t *s = esp_camera_sensor_get();
    ESP_LOGI(TAG, "sensor PID 0x%x", s->id.PID);
    s->set_framesize(s, FRAMESIZE_VGA);                                 // viewfinder size by default
    s->set_quality(s, 14);
    return ESP_OK;
}

static bool authed(httpd_req_t *r)
{
    char h[128] = "";
    if (!s_token[0]) return true;
    if (httpd_req_get_hdr_value_str(r, "Authorization", h, sizeof h) != ESP_OK) return false;
    return !strncmp(h, "Bearer ", 7) && !strcmp(h + 7, s_token);
}

static esp_err_t send_jpeg(httpd_req_t *r, bool full)
{
    if (!authed(r)) { httpd_resp_set_status(r, "401 Unauthorized"); return httpd_resp_send(r, "bad token", -1); }
    xSemaphoreTake(s_cam, portMAX_DELAY);
    sensor_t *s = esp_camera_sensor_get();
    int64_t t0 = esp_timer_get_time();
    if (full) {
        gpio_set_level(LED, 0);
        s->set_framesize(s, FRAMESIZE_QXGA); s->set_quality(s, 10);
        for (int i = 0; i < 2; i++) { camera_fb_t *d = esp_camera_fb_get(); if (d) esp_camera_fb_return(d); }   // settle
    }
    camera_fb_t *fb = esp_camera_fb_get();
    esp_err_t e;
    if (!fb) { httpd_resp_send_500(r); e = ESP_FAIL; }
    else {
        char hdr[48];
        httpd_resp_set_type(r, "image/jpeg");
        snprintf(hdr, sizeof hdr, "%ux%u", fb->width, fb->height);
        httpd_resp_set_hdr(r, "X-Size", hdr);
        e = httpd_resp_send(r, (const char *)fb->buf, fb->len);
        if (full) ESP_LOGI(TAG, "capture %s %u B in %d ms", hdr, (unsigned)fb->len, (int)((esp_timer_get_time() - t0) / 1000));
        esp_camera_fb_return(fb);
    }
    if (full) {
        s->set_framesize(s, FRAMESIZE_VGA); s->set_quality(s, 14);
        gpio_set_level(LED, 1);
        s_caps++;
    } else s_frames++;
    xSemaphoreGive(s_cam);
    return e;
}
static esp_err_t h_frame(httpd_req_t *r) { return send_jpeg(r, false); }
static esp_err_t h_capture(httpd_req_t *r) { return send_jpeg(r, true); }
static esp_err_t h_health(httpd_req_t *r)
{
    char b[160]; sensor_t *s = esp_camera_sensor_get();
    wifi_ap_record_t ap = {0}; esp_wifi_sta_get_ap_info(&ap);
    snprintf(b, sizeof b, "{\"ok\":true,\"sensor\":\"0x%x\",\"rssi\":%d,\"frames\":%d,\"captures\":%d,\"uptime_s\":%lld}",
             s ? s->id.PID : 0, ap.rssi, s_frames, s_caps, (long long)(esp_timer_get_time() / 1000000));
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, b, -1);
}

// ---------- Wi-Fi: same NVS layout as the Tab5 (wifi0..5_ssid/pass), strongest visible first ----------
static bool nvs_str(nvs_handle_t h, const char *k, char *v, size_t n) { size_t l = n; return nvs_get_str(h, k, v, &l) == ESP_OK; }

static void on_evt(void *a, esp_event_base_t b, int32_t id, void *d)
{
    if (b == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) { xEventGroupClearBits(s_ev, EV_IP); esp_wifi_connect(); }
    else if (b == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = d; ESP_LOGI(TAG, "ip " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_ev, EV_IP);
    }
}

static void wifi_join(void)
{
    nvs_handle_t h;
    if (nvs_open("papp", NVS_READONLY, &h) != ESP_OK) { ESP_LOGE(TAG, "no NVS papp namespace — provision first"); return; }
    nvs_str(h, "token", s_token, sizeof s_token);
    uint16_t na = 30; wifi_ap_record_t *r = calloc(na, sizeof *r);
    wifi_scan_config_t sc = { .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    esp_wifi_scan_start(&sc, true); esp_wifi_scan_get_ap_records(&na, r);
    int best = -1, best_rssi = -999; char ssid[33], pass[65];
    for (int i = 0; i < 6; i++) {
        char k[24]; snprintf(k, sizeof k, "wifi%d_ssid", i);
        if (!nvs_str(h, k, ssid, sizeof ssid)) continue;
        for (int a = 0; a < na; a++) if (!strcmp((char *)r[a].ssid, ssid) && r[a].rssi > best_rssi) { best = i; best_rssi = r[a].rssi; }
    }
    free(r);
    if (best < 0) best = 0;
    char k[24]; snprintf(k, sizeof k, "wifi%d_ssid", best); nvs_str(h, k, ssid, sizeof ssid);
    snprintf(k, sizeof k, "wifi%d_pass", best); if (!nvs_str(h, k, pass, sizeof pass)) pass[0] = 0;
    nvs_close(h);
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, pass, sizeof wc.sta.password);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    memset(pass, 0, sizeof pass);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    ESP_LOGI(TAG, "joining wifi%d \"%s\" (%d dBm)", best, ssid, best_rssi);
    esp_wifi_connect();
}

void app_main(void)
{
    gpio_reset_pin(LED); gpio_set_direction(LED, GPIO_MODE_OUTPUT); gpio_set_level(LED, 1);
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { ESP_LOGE(TAG, "NVS needs provisioning"); }
    s_cam = xSemaphoreCreateMutex(); s_ev = xEventGroupCreate();
    e = cam_init();
    if (e != ESP_OK) ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(e));
    esp_netif_init(); esp_event_loop_create_default(); esp_netif_create_default_wifi_sta();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&ic);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_evt, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_evt, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA); esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    wifi_join();
    mdns_init(); mdns_hostname_set("papp-cam"); mdns_instance_name_set("PAPP back camera (XIAO OV3660)");
    mdns_service_add(NULL, "_pappcam", "_tcp", 80, NULL, 0);
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG(); hc.stack_size = 8192;
    httpd_handle_t srv;
    if (httpd_start(&srv, &hc) == ESP_OK) {
        httpd_uri_t u[] = { { .uri = "/frame", .method = HTTP_GET, .handler = h_frame },
                            { .uri = "/capture", .method = HTTP_GET, .handler = h_capture },
                            { .uri = "/health", .method = HTTP_GET, .handler = h_health } };
        for (int i = 0; i < 3; i++) httpd_register_uri_handler(srv, &u[i]);
    }
    ESP_LOGI(TAG, "PAPP back camera up (token %s)", s_token[0] ? "set" : "MISSING");
    for (;;) {                                                   // heartbeat blink while not on Wi-Fi
        bool up = xEventGroupGetBits(s_ev) & EV_IP;
        if (!up) { gpio_set_level(LED, 0); vTaskDelay(pdMS_TO_TICKS(80)); gpio_set_level(LED, 1); }
        vTaskDelay(pdMS_TO_TICKS(up ? 5000 : 1000));
    }
}
