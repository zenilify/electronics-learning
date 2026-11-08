#include "net.h"
#include "papp.h"
#include "papp_http.h"
#include "settings.h"
#include "c6ota.h"
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
#define EV_SCAN  BIT3
#define EV_JOIN  BIT4
static net_known_t s_known[NET_SLOTS];
static int s_nknown;
static volatile unsigned s_gen;
static volatile int s_join = -1;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
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
    if (g_status.wifi_up) { esp_wifi_disconnect(); vTaskDelay(pdMS_TO_TICKS(300)); }   // let our own reason-8 event land
    xEventGroupClearBits(s_ev, EV_IP | EV_FAIL);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    memset(&wc, 0, sizeof wc);
    ESP_LOGI(TAG, "joining wifi%d \"%s\"", idx, ssid);
    esp_wifi_connect();
    EventBits_t b = xEventGroupWaitBits(s_ev, EV_IP | EV_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (b & EV_IP) { strlcpy((char *)g_status.ssid, ssid, sizeof g_status.ssid); return true; }
    return false;
}

// Provisioned slots whose SSID shows up in a scan, strongest first. Scan failure / nothing seen ->
// every provisioned slot in NVS order (old behaviour), so a hidden or slow AP still gets tried.
static int visible_slots(int *order)
{
    int n = 0, rssi[NET_SLOTS];
    uint16_t na = 0; wifi_ap_record_t *r = NULL;
    wifi_scan_config_t sc = { .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    bool scanned = esp_wifi_scan_start(&sc, true) == ESP_OK && esp_wifi_scan_get_ap_num(&na) == ESP_OK;
    if (scanned && na) {
        if (na > 30) na = 30;
        r = calloc(na, sizeof *r);
        scanned = r && esp_wifi_scan_get_ap_records(&na, r) == ESP_OK;
        if (!scanned) na = 0;
    } else if (scanned) esp_wifi_clear_ap_list();
    for (int i = 0; i < NET_SLOTS; i++) {
        char ssid[33], pw[65];
        if (!settings_get_wifi(i, ssid, sizeof ssid, pw, sizeof pw)) continue;
        memset(pw, 0, sizeof pw);
        int best = -999;
        if (scanned) for (int a = 0; a < na; a++) if (!strcmp((char *)r[a].ssid, ssid) && r[a].rssi > best) best = r[a].rssi;
        if (scanned && best == -999) continue;
        int pos = n++;
        while (pos > 0 && rssi[pos - 1] < best) { order[pos] = order[pos - 1]; rssi[pos] = rssi[pos - 1]; pos--; }
        order[pos] = i; rssi[pos] = best;
        ESP_LOGI(TAG, "candidate wifi%d \"%s\" %d dBm", i, ssid, best);
    }
    // remember the scan for the picker (NVS reads outside the critical section)
    net_known_t tmp[NET_SLOTS]; int nt = 0;
    for (int i = 0; i < NET_SLOTS; i++) {
        char ssid[33], pw[65];
        if (!settings_get_wifi(i, ssid, sizeof ssid, pw, sizeof pw)) continue;
        memset(pw, 0, sizeof pw);
        net_known_t *k = &tmp[nt++];
        k->slot = i; strlcpy(k->ssid, ssid, sizeof k->ssid); k->rssi = -999;
        k->current = g_status.wifi_up && !strcmp((char *)g_status.ssid, ssid);
        if (scanned) for (int a = 0; a < na; a++) if (!strcmp((char *)r[a].ssid, ssid) && r[a].rssi > k->rssi) k->rssi = r[a].rssi;
    }
    portENTER_CRITICAL(&s_mux);
    memcpy(s_known, tmp, sizeof tmp); s_nknown = nt; s_gen++;
    portEXIT_CRITICAL(&s_mux);
    // a network picked in Ajustes goes first whenever it is on the air
    int pref = net_pref();
    for (int j = 1; j < n; j++) if (order[j] == pref) { int t = order[j]; memmove(order + 1, order, j * sizeof *order); order[0] = t; break; }
    free(r);
    if (scanned && n == 0) {                       // nothing known on the air: fall back to all slots
        for (int i = 0; i < NET_SLOTS; i++) { char a[33], b[65]; if (settings_get_wifi(i, a, 33, b, 65)) order[n++] = i; }
        ESP_LOGW(TAG, "no provisioned SSID visible in scan (%u APs) — trying all %d", na, n);
    }
    return n;
}

static void net_task(void *arg)
{
    int64_t last_probe = 0;
    for (;;) {
        if (!(xEventGroupGetBits(s_ev) & EV_IP)) {
            // Scan first, then try only the provisioned networks that are on the air, strongest first
            // (travel: kaizen-tr UTR, S26U / iPhone hotspots, home — up to NET_SLOTS in NVS).
            int order[NET_SLOTS], no = visible_slots(order);
            bool ok = false;
            for (int j = 0; j < no && !ok; j++)
                for (int k = 0; k < 3 && !ok; k++) {
                    // the driver may finish an earlier association late: never tear down a link that came up
                    if (xEventGroupGetBits(s_ev) & EV_IP) { ok = true; break; }
                    ok = try_connect(order[j]);
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
        EventBits_t ev = xEventGroupGetBits(s_ev);
        if (ev & EV_JOIN) {                       // picked in Ajustes: switch now (pref is already saved)
            xEventGroupClearBits(s_ev, EV_JOIN);
            int slot = s_join;
            if (slot >= 0) {
                ESP_LOGI(TAG, "picker: switching to wifi%d", slot);
                bool ok = false;
                for (int k = 0; k < 3 && !ok; k++) { ok = try_connect(slot); if (!ok) vTaskDelay(pdMS_TO_TICKS(1500)); }
                if (!ok) { ESP_LOGW(TAG, "picker: wifi%d failed — back to automatic", slot); esp_wifi_disconnect(); }
                int o[NET_SLOTS]; visible_slots(o);          // refresh the picker list
                continue;                                    // EV_IP clear on failure -> the scan-first loop rejoins
            }
        }
        if (ev & EV_SCAN) { xEventGroupClearBits(s_ev, EV_SCAN); int o[NET_SLOTS]; visible_slots(o); }
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
        xEventGroupWaitBits(s_ev, EV_PROBE | EV_FAIL | EV_SCAN | EV_JOIN, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
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
void net_request_scan(void) { if (s_ev) xEventGroupSetBits(s_ev, EV_SCAN); }
int net_pref(void) { uint8_t v = settings_get_u8("wpref", 0); return v ? v - 1 : -1; }
void net_join(int slot)
{
    settings_set_u8("wpref", slot >= 0 ? (uint8_t)(slot + 1) : 0);
    s_join = slot;
    if (s_ev) xEventGroupSetBits(s_ev, EV_JOIN);
}
int net_known(net_known_t *out, int max, unsigned *gen)
{
    portENTER_CRITICAL(&s_mux);
    int n = s_nknown < max ? s_nknown : max;
    memcpy(out, s_known, n * sizeof *out);
    for (int i = 0; i < n; i++) out[i].current = g_status.wifi_up && !strcmp((char *)g_status.ssid, out[i].ssid);
    if (gen) *gen = s_gen;
    portEXIT_CRITICAL(&s_mux);
    return n;
}

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
    // All esp_hosted RPCs from ONE task: an Ajustes refresh calling c6_version() on the LVGL task while the net task
    // scanned crashed in rpc_wifi_scan_get_ap_records (memcpy from NULL, 11-08). The UI reads this cached copy.
    uint32_t v[3];
    if (c6_version(&v[0], &v[1], &v[2])) { memcpy((void *)g_status.c6v, v, sizeof v); g_status.c6v_ok = true; }
    xTaskCreatePinnedToCore(net_task, "net", 8192, NULL, 4, NULL, 0);
}
