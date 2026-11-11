/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* Switch driver: on-board LED indicator + boot button, via the app_led/app_button
 * common components (so the example is testable on a bare devkit, and simulated on
 * POSIX where those components are no-op stubs).
 *
 * RM-1 addendum (2026-11-11, Seeed XIAO ESP32C6): the board has a single plain user
 * LED instead of an RGB LED, so the power state is mirrored on SWITCH_USER_LED_GPIO
 * as well. app_led stays in place for an optional Grove WS2812 stick. */

#include "osal_err.h"
#include "osal_log.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>

#include "app_led.h"
#include "app_button.h"
#include "app_priv.h"

/* Configuration includes. */
#include "sdkconfig.h"

#if defined(ESP_PLATFORM) && defined(CONFIG_SWITCH_USER_LED_GPIO) && (CONFIG_SWITCH_USER_LED_GPIO >= 0)
#include "driver/gpio.h"
#define USER_LED_GPIO CONFIG_SWITCH_USER_LED_GPIO
#ifdef CONFIG_SWITCH_USER_LED_ACTIVE_LOW
#define USER_LED_LEVEL(on) ((on) ? 0 : 1)
#else
#define USER_LED_LEVEL(on) ((on) ? 1 : 0)
#endif
#endif

static const char *TAG = "app_driver";

/* Current switch state */
static bool g_power_state = DEFAULT_POWER;

/* LED indicator default: red at full brightness (HSV), starts off. */
static const app_led_state_t s_default_led_state = {
    .power = DEFAULT_POWER,
    .brightness = 100,
    .color_hs = {.hue = 0, .saturation = 100},
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};

static void user_led_init(void)
{
#ifdef USER_LED_GPIO
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << USER_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(USER_LED_GPIO, USER_LED_LEVEL(DEFAULT_POWER));
    OSAL_LOGI(TAG, "User LED on GPIO %d (%s)", USER_LED_GPIO,
#ifdef CONFIG_SWITCH_USER_LED_ACTIVE_LOW
              "active-low");
#else
              "active-high");
#endif
#endif
}

static void user_led_set(bool on)
{
#ifdef USER_LED_GPIO
    gpio_set_level(USER_LED_GPIO, USER_LED_LEVEL(on));
#else
    (void)on;
#endif
}

/* Button press toggles power and reports the new state to the cloud. */
static void push_btn_cb(void *handle, void *arg)
{
    (void)handle;
    (void)arg;
    bool new_state = !g_power_state;
    if (app_driver_set_state(new_state) != OSAL_ERR_OK) {
        return;
    }
    /* Before the node is up the device does not exist yet: the hardware still
     * changes, only the report is skipped. */
    if (switch_device) {
#if CONFIG_SWITCH_NOTIFY_ON_PUSH_BTN_CB
        OSAL_LOGI(TAG, "Sending notification for power state change");
        esp_rmaker_param_update_and_notify(
            esp_rmaker_device_get_param_by_type(switch_device, ESP_RMAKER_PARAM_POWER),
            esp_rmaker_bool(new_state));
#else
        esp_rmaker_param_update(
            esp_rmaker_device_get_param_by_type(switch_device, ESP_RMAKER_PARAM_POWER),
            esp_rmaker_bool(new_state));
#endif /* CONFIG_SWITCH_NOTIFY_ON_PUSH_BTN_CB */
    }
}

osal_err_t app_driver_set_state(bool state)
{
    if (g_power_state != state) {
        osal_err_t err = app_led_set_power(state);
        if (err != OSAL_ERR_OK) {
            return err;
        }
        user_led_set(state);
        g_power_state = state;
    }
    return OSAL_ERR_OK;
}

bool app_driver_get_state(void)
{
    return g_power_state;
}

void app_driver_init(void)
{
    user_led_init();
    if (app_led_init(&s_default_led_state) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the LED; check the configuration");
    }
    /* Boot button: single/long press toggles power. */
    app_button_config_t btn_cfg = {
        .callbacks = {
            .on_short_press = push_btn_cb,
            .on_long_press = push_btn_cb,
        },
    };
    app_button_init(&btn_cfg);
}
