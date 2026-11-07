#include "settings.h"
#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "settings";

void settings_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Never erase silently: the token lives here. Log and continue without NVS.
        ESP_LOGE(TAG, "nvs init: %s — re-provision with tools/provision.py", esp_err_to_name(e));
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(e);
}

static bool get_str(const char *key, char *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open("papp", NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = len;
    esp_err_t e = nvs_get_str(h, key, buf, &l);
    nvs_close(h);
    return e == ESP_OK && buf[0];
}

bool settings_get_token(char *buf, size_t len) { return get_str("token", buf, len); }

bool settings_get_wifi(int idx, char *ssid, size_t sl, char *pass, size_t pl)
{
    char k[16];
    snprintf(k, sizeof k, "wifi%d_ssid", idx);
    if (!get_str(k, ssid, sl)) return false;
    snprintf(k, sizeof k, "wifi%d_pass", idx);
    if (!get_str(k, pass, pl)) pass[0] = 0;   // open network
    return true;
}

uint8_t settings_get_u8(const char *key, uint8_t dflt)
{
    nvs_handle_t h; uint8_t v = dflt;
    if (nvs_open("papp", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, key, &v); nvs_close(h); }
    return v;
}

void settings_set_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open("papp", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, key, v); nvs_commit(h); nvs_close(h); }
}
