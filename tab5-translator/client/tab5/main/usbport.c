// Tab5 USB-A (host) port: the 5 V switch, and (optional) the USB host stack that tells us what is plugged in.
// See usbport.h for the why. Measured 2026-11-11 on Erick's unit — docs/PDCA-2026-11-11.md cycle 1.
#include "usbport.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "usb/usb_host.h"
#include "bsp/m5stack_tab5.h"
#include "settings.h"

static const char *TAG = "usbport";
static bool s_pwr;

// ---------- 5 V switch ----------
bool usbport_power(bool on, bool save)
{
    // BSP_FEATURE_USB = expander 0x44 pin 3: direction out, level, push-pull (the driver leaves pins high-Z after reset)
    esp_err_t e = bsp_feature_enable(BSP_FEATURE_USB, on);
    if (e == ESP_OK) { s_pwr = on; if (save) settings_set_u8("usb5v", on); }
    ESP_LOGI(TAG, "USB-A 5 V %s: %s", on ? "ON" : "off", esp_err_to_name(e));
    return e == ESP_OK;
}
bool usbport_power_is_on(void) { return s_pwr; }

// ---------- I/O expander read-back (PI4IOE5V6408 x2 on the system I2C bus) ----------
// 0x43: P0 antenna (0 int / 1 ext) · P1 SPK_EN · P2 EXT5V_EN · P4 LCD_RST · P5 TP_RST · P6 CAM_RST · P7 HP_DET (in)
// 0x44: P0 WLAN_PWR_EN · P3 USB5V_EN · P4 PWROFF_PLUSE · P5 nCHG_QC_EN · P6 CHG_STAT (in) · P7 CHG_EN
void usbport_io_dump(void)
{
    static const uint8_t REG[] = { 0x01, 0x03, 0x05, 0x07, 0x0B, 0x0D, 0x0F };   // id, dir, out, hi-Z, pull en, pull sel, input
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) { ESP_LOGW(TAG, "io: no I2C bus"); return; }
    for (uint8_t addr = 0x43; addr <= 0x44; addr++) {
        i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000 };
        i2c_master_dev_handle_t d;
        if (i2c_master_bus_add_device(bus, &dc, &d) != ESP_OK) { ESP_LOGW(TAG, "io 0x%02x: add failed", addr); continue; }
        uint8_t v[sizeof REG] = {0}; bool ok = true;
        for (size_t i = 0; i < sizeof REG; i++) ok &= i2c_master_transmit_receive(d, &REG[i], 1, &v[i], 1, 50) == ESP_OK;
        i2c_master_bus_rm_device(d);
        ESP_LOGI(TAG, "io 0x%02x %s id=%02x dir=%02x out=%02x hiz=%02x pull_en=%02x pull_sel=%02x in=%02x",
                 addr, ok ? "ok" : "READ-FAILED", v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
    }
}

// ---------- USB host: who is on the port? ----------
static volatile bool s_host, s_gone;
static volatile uint8_t s_new_addr;
static usb_host_client_handle_t s_cl;
static usb_device_handle_t s_devh;
static struct { bool present; uint16_t vid, pid; uint8_t cls, speed; char prod[40]; } s_info;

static void lib_task(void *a)
{
    for (;;) {
        uint32_t f = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &f);
        if (f & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

static void client_cb(const usb_host_client_event_msg_t *m, void *arg)
{
    if (m->event == USB_HOST_CLIENT_EVENT_NEW_DEV) s_new_addr = m->new_dev.address;
    else if (m->event == USB_HOST_CLIENT_EVENT_DEV_GONE) s_gone = true;
}

static void str_ascii(const usb_str_desc_t *s, char *out, size_t n)      // UTF-16LE string descriptor -> ASCII
{
    out[0] = 0;
    if (!s) return;
    size_t o = 0;
    for (int i = 0; i < (s->bLength - 2) / 2 && o + 1 < n; i++) { uint16_t c = s->wData[i]; out[o++] = c >= 0x20 && c < 0x7f ? (char)c : '?'; }
    out[o] = 0;
}

static void client_task(void *a)
{
    usb_host_client_config_t cc = { .is_synchronous = false, .max_num_event_msg = 5,
                                    .async = { .client_event_callback = client_cb, .callback_arg = NULL } };
    if (usb_host_client_register(&cc, &s_cl) != ESP_OK) { ESP_LOGE(TAG, "host: client register failed"); s_host = false; vTaskDelete(NULL); }
    ESP_LOGI(TAG, "host: up, waiting for a device on USB-A");
    for (;;) {
        usb_host_client_handle_events(s_cl, pdMS_TO_TICKS(500));
        if (s_gone) {
            s_gone = false;
            if (s_devh) { usb_host_device_close(s_cl, s_devh); s_devh = NULL; }
            ESP_LOGI(TAG, "host: device %04x:%04x unplugged", s_info.vid, s_info.pid);
            memset(&s_info, 0, sizeof s_info);
        }
        uint8_t addr = s_new_addr;
        if (!addr) continue;
        s_new_addr = 0;
        if (s_devh) { usb_host_device_close(s_cl, s_devh); s_devh = NULL; }
        if (usb_host_device_open(s_cl, addr, &s_devh) != ESP_OK) { ESP_LOGW(TAG, "host: open addr %u failed", addr); continue; }
        const usb_device_desc_t *dd = NULL; usb_device_info_t di = {0};
        if (usb_host_get_device_descriptor(s_devh, &dd) == ESP_OK && dd && usb_host_device_info(s_devh, &di) == ESP_OK) {
            s_info.vid = dd->idVendor; s_info.pid = dd->idProduct; s_info.cls = dd->bDeviceClass; s_info.speed = di.speed;
            str_ascii(di.str_desc_product, s_info.prod, sizeof s_info.prod);
            s_info.present = true;
            ESP_LOGI(TAG, "host: device %04x:%04x \"%s\" class 0x%02x speed %s addr %u", s_info.vid, s_info.pid, s_info.prod,
                     s_info.cls, di.speed == USB_SPEED_HIGH ? "high" : di.speed == USB_SPEED_FULL ? "full" : "low", addr);
        }
    }
}

bool usbport_host_start(bool save)
{
    if (s_host) return true;
    if (!s_pwr) usbport_power(true, false);
    const usb_host_config_t hc = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LOWMED };
    esp_err_t e = usb_host_install(&hc);
    if (e != ESP_OK) { ESP_LOGE(TAG, "host: install failed: %s", esp_err_to_name(e)); return false; }
    s_host = true;
    xTaskCreate(lib_task, "usb_lib", 4096, NULL, 10, NULL);
    xTaskCreate(client_task, "usb_cl", 4096, NULL, 5, NULL);
    if (save) settings_set_u8("usbh", 1);
    return true;
}
bool usbport_host_running(void) { return s_host; }

int usbport_describe(char *out, size_t n)
{
    int o = snprintf(out, n, "5 V %s", s_pwr ? "on" : "off");
    if (s_host && s_info.present) o += snprintf(out + o, n - o, " · %04x:%04x %s", s_info.vid, s_info.pid, s_info.prod);
    else if (s_host) o += snprintf(out + o, n - o, " · nothing plugged in");
    return o;
}

void usbport_start(void)
{
    usbport_power(settings_get_u8("usb5v", 1), false);
    if (settings_get_u8("usbh", 0)) usbport_host_start(false);
}
