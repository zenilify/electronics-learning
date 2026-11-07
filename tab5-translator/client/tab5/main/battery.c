#include "battery.h"
#include "papp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "bsp/m5stack_tab5.h"

static const char *TAG = "battery";
static i2c_master_dev_handle_t s_dev;

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
            g_status.batt_a = (int16_t)sh * 2.5e-6f / 0.005f;    // shunt LSB 2.5 uV over 5 mOhm
        } else g_status.batt_v = -1;
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
            xTaskCreate(task, "batt", 3072, NULL, 2, NULL);
            return;
        }
        i2c_master_bus_rm_device(s_dev); s_dev = NULL;
    }
    ESP_LOGW(TAG, "no INA226 found at 0x40/0x41");
}
