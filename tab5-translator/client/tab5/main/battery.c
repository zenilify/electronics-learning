#include "battery.h"
#include "papp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "bsp/m5stack_tab5.h"
#include "esp_timer.h"
#include "usbport.h"
#include "settings.h"

static const char *TAG = "battery";
static i2c_master_dev_handle_t s_dev;
static int s_kicks;
int battery_chg_kicks(void) { return s_kicks; }
// Power source, seen from the battery current: clearly negative = the pack is feeding the Tab5 (measured 11-11: -0.44 A
// with the camera on USB-A), about zero or positive = external power. Counted so that a test can tell afterwards
// whether the USB-C power really went away and came back (the console is gone while it is away).
static int s_src_changes; static int8_t s_on_batt = -1;
int battery_src_changes(void) { return s_src_changes; }
bool battery_on_batt(void) { return s_on_batt == 1; }

// Charger supervisor (11-11). Measured on Erick's unit, USB power from cachy:
//   - charger pin low (what PAPP did until 0.4.1): 0.00 A, the battery is never charged;
//   - pin raised: +0.32 A and the voltage climbs (8.14 -> 8.23 V);
//   - but it also STOPPED by itself with the pin still high (0.00 A at 8.15 V, same boot, minutes later; twice it
//     did not start at all when the pin went high at second 3 of the boot), and a low->high edge started it again.
// Cause not found. So: raise the pin 12 s after boot (C6 and USB-A power-up are over by then), and when the pack
// sits idle on external power below 8.25 V (~93 %) for a minute, give the pin a 3 s low pulse, at most every 3 min.
// NOT proven to be enough: on cachy's USB port two short kicks did not restart it. Likely the port cannot feed the
// system + camera + charger together (see docs/PDCA-2026-11-11.md §2.5) — check with the wall charger.
// On battery power the current is clearly negative, so nothing happens there.
static void charger_tick(void)
{
    static bool started; static int idle_s; static int64_t last_kick;
    int64_t now = esp_timer_get_time();
    if (!started) {
        if (now < 12 * 1000000LL) return;
        started = true;
        usbport_charge(settings_get_u8("chg", 1), false);
        return;
    }
    if (!usbport_charge_is_on() || g_status.batt_v < 6.0f) return;
    bool idle = g_status.batt_a > -0.03f && g_status.batt_a < 0.03f;
    idle_s = idle ? idle_s + 5 : 0;
    if (idle_s >= 60 && g_status.batt_v < 8.25f && (!last_kick || now - last_kick > 180 * 1000000LL)) {
        last_kick = now; idle_s = 0; s_kicks++;
        ESP_LOGW(TAG, "charger idle at %.2f V on external power - re-triggering it (kick %d)", g_status.batt_v, s_kicks);
        // 3 s low: the 8 s low of the manual test restarted it, two 0.3 s kicks did not (11-11, cachy USB port).
        usbport_charge(false, false); vTaskDelay(pdMS_TO_TICKS(3000)); usbport_charge(true, false);
    }
}

static bool rd(uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, b, 2, 100) != ESP_OK) return false;
    *v = (uint16_t)(b[0] << 8 | b[1]); return true;
}

int battery_percent(float v)
{
    if (v < 0) return -1;
    float p = (v - 6.4f) / (8.3f - 6.4f) * 100.f;
    return p < 0 ? 0 : p > 100 ? 100 : (int)(p + 0.5f);
}

static void task(void *a)
{
    for (;;) {
        uint16_t bus, sh;
        if (rd(0x02, &bus) && rd(0x01, &sh)) {
            g_status.batt_v = bus * 1.25e-3f;                    // bus voltage LSB 1.25 mV
            // shunt LSB 2.5 uV over 5 mOhm. Sign: on this board the raw value is NEGATIVE while the charger feeds the
            // battery (measured 11-11: charger off 0.00 A at 8.14 V; charger on raw -0.32 A and the voltage climbs to
            // 8.21 V), so it is flipped here: + = charging, - = running from the battery.
            g_status.batt_a = -(int16_t)sh * 2.5e-6f / 0.005f;
            int8_t ob = g_status.batt_a < -0.10f;
            if (s_on_batt >= 0 && ob != s_on_batt) {
                s_src_changes++;
                ESP_LOGW(TAG, "%s (%.2f V %.2f A, change %d)", ob ? "running from the battery now" : "external power is back",
                         g_status.batt_v, g_status.batt_a, s_src_changes);
            }
            s_on_batt = ob;
        } else g_status.batt_v = -1;
        charger_tick();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void battery_start(void)
{
    g_status.batt_v = -1;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) return;
    for (uint8_t addr = 0x41; addr >= 0x40; addr--) {
        if (i2c_master_probe(bus, addr, 100) != ESP_OK) continue;
        i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 100000 };
        if (i2c_master_bus_add_device(bus, &dc, &s_dev) != ESP_OK) continue;
        uint16_t id = 0;
        if (rd(0xFE, &id) && id == 0x5449) {
            // 16-sample averaging, 1.1 ms conversions, continuous shunt+bus (INA226 datasheet reg 0x00)
            uint8_t cfg[3] = { 0x00, 0x45, 0x27 };
            i2c_master_transmit(s_dev, cfg, 3, 100);
            ESP_LOGI(TAG, "INA226 at 0x%02x", addr);
            xTaskCreate(task, "batt", 4096, NULL, 2, NULL);
            return;
        }
        i2c_master_bus_rm_device(s_dev); s_dev = NULL;
    }
    ESP_LOGW(TAG, "no INA226 found at 0x40/0x41");
}
