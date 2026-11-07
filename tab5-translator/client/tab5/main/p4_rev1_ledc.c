// ESP32-P4 rev < 3 + ESP-IDF 6.0.3: LEDC is unusable on this unit (Tab5, P4 v1.3), measured 2026-11-07 (lane T5).
// Every LEDC duty write goes through the gamma-fade RAM (LEDC_GAMMA_RAM @0x500D3400), and on this chip those
// stores fault ("Store access fault", MTVAL 0x500D3440 / 0x500D3450) — sometimes in ledc_channel_config(),
// sometimes in the first ledc_update_duty(). Forcing the hw_ver1 per-channel gamma-RAM clock gates open did not
// make it reliable. The only LEDC user in this firmware is the BSP's LCD backlight (GPIO22), so on rev<3 we
// replace the four LEDC calls the BSP makes with a plain GPIO: duty > 0 -> backlight on, 0 -> off (no dimming).
// Linked with -Wl,--wrap=<fn> (main/CMakeLists.txt) — the BSP's calls into the LEDC driver are cross-object, so
// they are redirected; nothing else changes. Remove when ESP-IDF fixes LEDC for P4 rev<3.
#include "sdkconfig.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

esp_err_t __real_ledc_timer_config(const ledc_timer_config_t *c);
esp_err_t __real_ledc_channel_config(const ledc_channel_config_t *c);
esp_err_t __real_ledc_set_duty(ledc_mode_t m, ledc_channel_t ch, uint32_t duty);
esp_err_t __real_ledc_update_duty(ledc_mode_t m, ledc_channel_t ch);

#if CONFIG_ESP32P4_SELECTS_REV_LESS_V3
static int s_gpio[LEDC_CHANNEL_MAX] = { [0 ... LEDC_CHANNEL_MAX - 1] = -1 };
static uint32_t s_duty[LEDC_CHANNEL_MAX];

esp_err_t __wrap_ledc_timer_config(const ledc_timer_config_t *c) { (void)c; return ESP_OK; }

esp_err_t __wrap_ledc_channel_config(const ledc_channel_config_t *c)
{
    if (!c || c->channel >= LEDC_CHANNEL_MAX) return ESP_ERR_INVALID_ARG;
    gpio_config_t g = { .pin_bit_mask = 1ULL << c->gpio_num, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&g);
    s_gpio[c->channel] = c->gpio_num; s_duty[c->channel] = c->duty;
    gpio_set_level(c->gpio_num, c->duty > 0);
    ESP_LOGW("p4rev1", "LEDC ch%d -> plain GPIO%d (P4 rev<3 gamma-RAM fault workaround)", c->channel, c->gpio_num);
    return ESP_OK;
}

esp_err_t __wrap_ledc_set_duty(ledc_mode_t m, ledc_channel_t ch, uint32_t duty)
{
    (void)m; if (ch >= LEDC_CHANNEL_MAX) return ESP_ERR_INVALID_ARG;
    s_duty[ch] = duty; return ESP_OK;
}

esp_err_t __wrap_ledc_update_duty(ledc_mode_t m, ledc_channel_t ch)
{
    (void)m; if (ch >= LEDC_CHANNEL_MAX || s_gpio[ch] < 0) return ESP_ERR_INVALID_STATE;
    return gpio_set_level(s_gpio[ch], s_duty[ch] > 0);
}
#else
esp_err_t __wrap_ledc_timer_config(const ledc_timer_config_t *c) { return __real_ledc_timer_config(c); }
esp_err_t __wrap_ledc_channel_config(const ledc_channel_config_t *c) { return __real_ledc_channel_config(c); }
esp_err_t __wrap_ledc_set_duty(ledc_mode_t m, ledc_channel_t ch, uint32_t d) { return __real_ledc_set_duty(m, ch, d); }
esp_err_t __wrap_ledc_update_duty(ledc_mode_t m, ledc_channel_t ch) { return __real_ledc_update_duty(m, ch); }
#endif
