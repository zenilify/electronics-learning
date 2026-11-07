#include "net.h"
#include "papp.h"
#include "papp_http.h"
#include "settings.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "bsp/m5stack_tab5.h"
#include "settings.h"

static const char *TAG = "net";
static EventGroupHandle_t s_ev;
#define EV_IP    BIT0
#define EV_FAIL  BIT1
#define EV_PROBE BIT2
static esp_netif_t *s_netif;

static void on_wifi(void *a, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "disconnected, reason %d rssi %d", d ? d->reason : -1, d ? d->rssi : 0);
        g_status.wifi_up = false; g_status.endpoint = -1;
        xEventGroupClearBits(s_ev, EV_IP);
        xEventGroupSetBits(s_ev, EV_FAIL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf((char *)g_status.ip, sizeof g_status.ip, IPSTR, IP2STR(&e->ip_info.ip));
        g_status.wifi_up = true;
        xEventGroupSetBits(s_ev, EV_IP | EV_PROBE);
    }
}

static bool try_connect(int idx)
{
    char ssid[33], pass[65];
    if (!settings_get_wifi(idx, ssid, sizeof ssid, pass, sizeof pass)) return false;
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, pass, sizeof wc.sta.password);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;   // accept WPA/WPA2 mixed routers
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;       // fast scan missed the AP (reason 201, rssi -128) on the hosted C6
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wc.sta.failure_retry_cnt = 3;
    wc.sta.pmf_cfg.capable = true;
    memset(pass, 0, sizeof pass);
    if (g_status.wifi_up) esp_wifi_disconnect();
    xEventGroupClearBits(s_ev, EV_IP | EV_FAIL);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    memset(&wc, 0, sizeof wc);
    ESP_LOGI(TAG, "joining wifi%d \"%s\"", idx, ssid);
    esp_wifi_connect();
    EventBits_t b = xEventGroupWaitBits(s_ev, EV_IP | EV_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (b & EV_IP) { strlcpy((char *)g_status.ssid, ssid, sizeof g_status.ssid); return true; }
    return false;
}

static void net_task(void *arg)
{
    int64_t last_probe = 0;
    for (;;) {
        if (!(xEventGroupGetBits(s_ev) & EV_IP)) {
            bool ok = false;
            for (int i = 0; i < 3 && !ok; i++)
                for (int k = 0; k < 3 && !ok; k++) {
                    char tmp[33], tp[65];
                    if (!settings_get_wifi(i, tmp, sizeof tmp, tp, sizeof tp)) break;
                    // the driver may finish an earlier association late: never tear down a link that came up
                    if (xEventGroupGetBits(s_ev) & EV_IP) { ok = true; break; }
                    ok = try_connect(i);
                    if (!ok) vTaskDelay(pdMS_TO_TICKS(1500));
                    if (xEventGroupGetBits(s_ev) & EV_IP) ok = true;
                }
            if (!ok) {
                if (!settings_get_wifi(0, (char[33]){0}, 33, (char[65]){0}, 65)) {
                    ESP_LOGW(TAG, "no Wi-Fi provisioned (tools/provision.py)");
                    vTaskDelay(pdMS_TO_TICKS(30000));
                } else vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
        }
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) g_status.rssi = ap.rssi;
        int64_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool want = (xEventGroupGetBits(s_ev) & EV_PROBE) ||
                    (g_status.endpoint < 0 && now - last_probe > 20000) || now - last_probe > 120000;
        if (want) {
            xEventGroupClearBits(s_ev, EV_PROBE);
            int ms = 0, ep = papp_probe(&ms);
            g_status.endpoint = ep; g_status.health_ms = ms; last_probe = now;
        }
        xEventGroupWaitBits(s_ev, EV_PROBE | EV_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
    }
}

void net_scan_log(void)
{
    wifi_scan_config_t sc = { .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) { ESP_LOGW(TAG, "scan failed"); return; }
    uint16_t n = 20; wifi_ap_record_t *r = calloc(n, sizeof *r);
    if (!r) return;
    esp_wifi_scan_get_ap_records(&n, r);
    wifi_country_t cc = {0}; esp_wifi_get_country(&cc);
    ESP_LOGI(TAG, "scan: %u APs, country %.2s ch %u-%u", n, cc.cc, cc.schan, cc.schan + cc.nchan - 1);
    for (int i = 0; i < n; i++)
        ESP_LOGI(TAG, "  ch%2u %4d dBm auth %d  %s", r[i].primary, r[i].rssi, r[i].authmode, (char *)r[i].ssid);
    free(r);
}

void net_reprobe(void) { if (s_ev) xEventGroupSetBits(s_ev, EV_PROBE); }

void net_start(void)
{
    s_ev = xEventGroupCreate();
    // Power-cycle the ESP32-C6 (WLAN_PWR_EN = IO expander 0x44 P0) before esp_hosted starts. A C6 left wedged by a
    // previous transport failure / host restart / co-proc OTA does not recover from the GPIO15 CHIP_PU reset alone,
    // only from losing power (esp-hosted-mcu #240, M5Tab5-UserDemo #20). Costs ~0.7 s of boot.
    // Antenna switch (schematic p.4: TPG619C6 SPDT, RF1 = internal 3D antenna, RF2 = MMCX J15 "External ANT").
    // Driven from IO expander 0x43 P0 (net RF_PTH_L_INT_H_EXT: low = internal, high = external). The other MMCX
    // (J16 "EXT. module bypass") is a pass-through for an add-on module's IPEX — not Wi-Fi.
    // NVS "ant": 0 internal (default), 1 external. Console: `ant ext` / `ant int` (saved, applied after reboot).
    uint8_t ant = settings_get_u8("ant", 0);
    esp_io_expander_handle_t ex = bsp_io_expander_init();
    if (ex) {
        esp_io_expander_set_dir(ex, IO_EXPANDER_PIN_NUM_0, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(ex, IO_EXPANDER_PIN_NUM_0, ant ? 1 : 0);
        ESP_LOGI(TAG, "antenna: %s", ant ? "EXTERNAL (MMCX J15)" : "internal");
    }
    bsp_feature_enable(BSP_FEATURE_WIFI, false);
    vTaskDelay(pdMS_TO_TICKS(500));
    bsp_feature_enable(BSP_FEATURE_WIFI, true);
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "C6 power-cycled");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif, "papp-tab5");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    // Modem sleep off: with power save on, the hosted C6 dropped the AP with reason 200 (beacon timeout)
    // mid-turn and then 201 (no AP found) for ~25 s (bench 2026-11-07). A demo unit on USB/battery can afford it.
    esp_err_t ps = esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "wifi power save off: %s", esp_err_to_name(ps));
    xTaskCreatePinnedToCore(net_task, "net", 8192, NULL, 4, NULL, 0);
}
