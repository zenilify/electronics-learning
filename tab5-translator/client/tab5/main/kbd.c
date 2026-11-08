#include "kbd.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ui_priv.h"

static const char *TAG = "kbd";
#define KB_ADDR 0x6D
#define KB_SDA  GPIO_NUM_0
#define KB_SCL  GPIO_NUM_1
#define KB_INT  GPIO_NUM_50
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_present;

static bool wr2(uint8_t r, uint8_t v) { uint8_t b[2] = { r, v }; return i2c_master_transmit(s_dev, b, 2, 50) == ESP_OK; }

static bool arm(void)
{
    return wr2(0x10, 0x01) && wr2(0x00, 0x02) && wr2(0x02, 0x00);   // HID mode, INT on HID events, clear queue
}

// HID usage 0x04..0x38 -> US ANSI (same tables as the ESPHome config)
static const char *B = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;'`,./";
static const char *S = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|~:\"~<>?";

// Alt (+Shift) layer for Spanish: Alt+a/e/i/o/u = á é í ó ú, Alt+n = ñ, Alt+? = ¿, Alt+1 = ¡, Alt+y = ü
static const char *alt_map(char c, bool shift)
{
    switch (c) {
    case 'a': return shift ? "Á" : "á"; case 'e': return shift ? "É" : "é"; case 'i': return shift ? "Í" : "í";
    case 'o': return shift ? "Ó" : "ó"; case 'u': return shift ? "Ú" : "ú"; case 'n': return shift ? "Ñ" : "ñ";
    case 'y': return "ü"; case '/': return "¿"; case '1': return "¡";
    default: return NULL;
    }
}

static void deliver(uint8_t mod, uint8_t kc)
{
    int sp = 0; char t[4] = {0}; const char *txt = NULL;
    bool shift = mod & 0x22, alt = mod & 0x44;
    switch (kc) {
    case 0x28: case 0x58: sp = KBD_ENTER; break;
    case 0x2A: sp = KBD_BKSP; break;
    case 0x29: sp = KBD_ESC; break;
    case 0x2B: sp = KBD_TAB; break;
    case 0x52: sp = KBD_UP; break;
    case 0x51: sp = KBD_DOWN; break;
    case 0x50: sp = KBD_LEFT; break;
    case 0x4F: sp = KBD_RIGHT; break;
    default:
        if (kc < 0x04 || kc > 0x38) return;
        char c = (shift ? S : B)[kc - 0x04];
        if (c == '\n' || c == '\x1b' || c == '\b' || c == '\t') return;
        if (alt) { txt = alt_map(B[kc - 0x04], shift); if (!txt) return; }
        else { t[0] = c; txt = t; }
    }
    if (mod & 0x11) return;            // Ctrl combos reserved
    lock();
    home_kbd(txt, sp);
    unlock();
}

static void task(void *a)
{
    int tick = 0;
    for (;;) {
        if (++tick % 250 == 0 || !s_present) {          // every ~5 s: hot-plug / re-arm
            uint8_t reg = 0x10, mode = 0xFF;
            bool p = i2c_master_transmit_receive(s_dev, &reg, 1, &mode, 1, 50) == ESP_OK;
            if (p && mode != 1) p = arm();
            if (p != s_present) { s_present = p; ESP_LOGI(TAG, "Keyboard for Tab5 %s", p ? "present (HID mode)" : "absent"); }
            if (!p) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
        }
        if (gpio_get_level(KB_INT) == 0) {
            for (int i = 0; i < 32; i++) {
                uint8_t reg = 0x30, ev[2] = { 0xFF, 0xFF };
                if (i2c_master_transmit_receive(s_dev, &reg, 1, ev, 2, 50) != ESP_OK) { s_present = false; break; }
                if (ev[0] == 0xFF && ev[1] == 0xFF) break;
                if (ev[1]) deliver(ev[0], ev[1]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void kbd_start(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = -1, .sda_io_num = KB_SDA, .scl_io_num = KB_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    esp_log_level_set("i2c.master", ESP_LOG_NONE);   // absent keyboard = NACK every probe; not an error
    if (i2c_new_master_bus(&bc, &s_bus) != ESP_OK) { ESP_LOGW(TAG, "Ext.Port1 I2C bus init failed"); return; }
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = KB_ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(s_bus, &dc, &s_dev) != ESP_OK) return;
    gpio_config_t g = { .pin_bit_mask = 1ULL << KB_INT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&g);
    xTaskCreate(task, "kbd", 3072, NULL, 3, NULL);
}
