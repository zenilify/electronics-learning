#include "net.h"
#include "papp.h"
#include "papp_http.h"
#include "settings.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

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
                    ok = try_connect(i);
                    if (!ok) vTaskDelay(pdMS_TO_TICKS(1500));
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

void net_reprobe(void) { if (s_ev) xEventGroupSetBits(s_ev, EV_PROBE); }

void net_start(void)
{
    s_ev = xEventGroupCreate();
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
