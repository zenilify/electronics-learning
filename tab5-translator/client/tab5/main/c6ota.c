// ESP32-C6 co-processor firmware update over esp_hosted (D-T5-1, Erick 2026-11-07).
// The C6 shipped with esp-hosted slave firmware that reports 0.0.0 while the host is 2.12 ("upgrade co-proc to
// avoid RPC timeouts"); the Wi-Fi link was flaky. Console: `c6ver` prints the version, `c6ota <http-url>` streams
// network_adapter.bin (built from the same esp_hosted 2.12.13 component, target esp32c6, SDIO) into the C6's
// spare OTA slot, then activates it (slave >= 2.6) or restarts the host, which resets the C6 via GPIO15.
// Same flow as esp_hosted examples/host_performs_slave_ota.
#include "c6ota.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui.h"

static const char *TAG = "c6ota";

bool c6_version(uint32_t *maj, uint32_t *min, uint32_t *pat)
{
    esp_hosted_coprocessor_fwver_t v = {0};
    if (esp_hosted_get_coprocessor_fwversion(&v) != ESP_OK) return false;
    *maj = v.major1; *min = v.minor1; *pat = v.patch1;
    return true;
}

static void ota_task(void *arg)
{
    char *url = arg;
    uint32_t a = 0, b = 0, c = 0;
    bool had = c6_version(&a, &b, &c);
    ESP_LOGW(TAG, "C6 OTA from %s (current %s %lu.%lu.%lu)", url, had ? "fw" : "fw unknown",
             (unsigned long)a, (unsigned long)b, (unsigned long)c);
    ui_set_hint("Actualizando Wi-Fi (C6) — no desconectar · 正在更新");
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 20000, .buffer_size = 4096 };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    esp_err_t e = esp_http_client_open(h, 0);
    int64_t len = e == ESP_OK ? esp_http_client_fetch_headers(h) : -1;
    int st = esp_http_client_get_status_code(h);
    if (e != ESP_OK || st != 200 || len <= 0) {
        ESP_LOGE(TAG, "download failed: %s status %d len %lld", esp_err_to_name(e), st, (long long)len);
        goto out;
    }
    if ((e = esp_hosted_slave_ota_begin()) != ESP_OK) { ESP_LOGE(TAG, "ota_begin: %s", esp_err_to_name(e)); goto out; }
    static uint8_t buf[1400];
    int64_t done = 0; int last = -1;
    while (done < len) {
        int n = esp_http_client_read(h, (char *)buf, sizeof buf);
        if (n <= 0) { ESP_LOGE(TAG, "read stopped at %lld/%lld", (long long)done, (long long)len); e = ESP_FAIL; break; }
        if ((e = esp_hosted_slave_ota_write(buf, n)) != ESP_OK) { ESP_LOGE(TAG, "ota_write: %s", esp_err_to_name(e)); break; }
        done += n;
        int pct = (int)(done * 100 / len);
        if (pct / 10 != last) { last = pct / 10; ESP_LOGI(TAG, "%d %% (%lld B)", pct, (long long)done); }
    }
    esp_err_t end = esp_hosted_slave_ota_end();
    ESP_LOGW(TAG, "ota_end: %s after %lld/%lld B", esp_err_to_name(end), (long long)done, (long long)len);
    if (e != ESP_OK || end != ESP_OK) goto out;
    if (had && (a > 2 || (a == 2 && b > 5))) {
        e = esp_hosted_slave_ota_activate();
        ESP_LOGW(TAG, "ota_activate: %s", esp_err_to_name(e));
    } else {
        ESP_LOGW(TAG, "old slave: no activate API, restarting host (resets the C6)");
    }
    esp_http_client_cleanup(h); free(url);
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
out:
    esp_http_client_cleanup(h); free(url);
    ui_set_hint("Actualización del C6 falló — ver log · 更新失败");
    vTaskDelete(NULL);
}

void c6_ota_start(const char *url)
{
    xTaskCreate(ota_task, "c6ota", 8192, strdup(url), 5, NULL);
}
