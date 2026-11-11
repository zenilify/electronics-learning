// Tab5 USB-A (host) port: the 5 V switch, and (optional) the USB host stack that tells us what is plugged in.
// See usbport.h for the why. Measured 2026-11-11 on Erick's unit — docs/PDCA-2026-11-11.md cycle 1.
#include "usbport.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "usb/usb_host.h"
#include "bsp/m5stack_tab5.h"
#include "settings.h"
#include "esp_timer.h"
#include "usb/usb_helpers.h"
#include "papp.h"
#include "papp_http.h"
#include "net.h"
#include "camera.h"
#include <stdlib.h>

static const char *TAG = "usbport";
static bool s_pwr;
// The expander driver does read-modify-write on shadow registers with no lock of its own; the switches below are
// used from several tasks (boot, console, battery supervisor), so they take turns here.
static SemaphoreHandle_t s_xmx;
static void xlock(void) { if (!s_xmx) s_xmx = xSemaphoreCreateMutex(); xSemaphoreTake(s_xmx, portMAX_DELAY); }
static void xunlock(void) { xSemaphoreGive(s_xmx); }

// ---------- 5 V switch ----------
bool usbport_power(bool on, bool save)
{
    // BSP_FEATURE_USB = expander 0x44 pin 3: direction out, level, push-pull (the driver leaves pins high-Z after reset)
    xlock();
    esp_err_t e = bsp_feature_enable(BSP_FEATURE_USB, on);
    xunlock();
    if (e == ESP_OK) { s_pwr = on; if (save) settings_set_u8("usb5v", on); }
    ESP_LOGI(TAG, "USB-A 5 V %s: %s", on ? "ON" : "off", esp_err_to_name(e));
    return e == ESP_OK;
}
bool usbport_power_is_on(void) { return s_pwr; }

// EXT5V_EN = expander 0x43 pin 2: the 5 V of the Grove port (Port A) and the M5-Bus. Not driven by the BSP either;
// a Grove unit or an ESP-NOW bridge on Port A needs it. Default stays off (unchanged behaviour) until Erick decides.
// CHG_EN = expander 0x44 pin 7: the IP2326 battery charger only runs while this is high. M5Stack's own firmware
// raises it at boot (UserDemo, M5Unified, M5GFX); the Espressif BSP never does, so under PAPP the battery was not
// charged at all (INA226: 0.00 A on USB power, 11-11).
static bool s_chg;
bool usbport_charge(bool on, bool save)
{
    esp_io_expander_handle_t ex = bsp_io_expander1_init();
    if (!ex) return false;
    xlock();
    esp_err_t e = esp_io_expander_set_dir(ex, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT);
    e |= esp_io_expander_set_level(ex, IO_EXPANDER_PIN_NUM_7, on);
    e |= esp_io_expander_set_output_mode(ex, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
    xunlock();
    if (e == ESP_OK) { s_chg = on; if (save) settings_set_u8("chg", on); }
    ESP_LOGI(TAG, "battery charger %s: %s", on ? "ENABLED" : "disabled", esp_err_to_name(e));
    return e == ESP_OK;
}
bool usbport_charge_is_on(void) { return s_chg; }

static bool s_ext;
bool usbport_ext5v(bool on, bool save)
{
    esp_io_expander_handle_t ex = bsp_io_expander_init();
    if (!ex) return false;
    xlock();
    esp_err_t e = esp_io_expander_set_dir(ex, IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT);
    e |= esp_io_expander_set_level(ex, IO_EXPANDER_PIN_NUM_2, on);
    e |= esp_io_expander_set_output_mode(ex, IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
    xunlock();
    if (e == ESP_OK) { s_ext = on; if (save) settings_set_u8("ext5v", on); }
    ESP_LOGI(TAG, "EXT 5 V (Grove / M5-Bus) %s: %s", on ? "ON" : "off", esp_err_to_name(e));
    return e == ESP_OK;
}

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

// ---------- USB host: who is on the port, and what does it say? ----------
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
    for (int i = 0; i < (s->bLength - 2) / 2 && o + 1 < n; i++) {
        uint16_t c = s->wData[i];
        if (!c) break;                                   // some devices pad the descriptor with a NUL
        out[o++] = c >= 0x20 && c < 0x7f ? (char)c : '?';
    }
    out[o] = 0;
}

// ---------- listening to the device's serial console (CDC-ACM, read only) ----------
// The XIAO back camera is an ESP32-S3 whose console is its USB-Serial-JTAG port, so its log arrives on this cable:
//   "xcam: joining wifi2 "kaizen-tr" (-40 dBm)"   and   "xcam: ip 192.168.29.246"
// That is how the Tab5 learns the camera's network and address without mDNS. mDNS does not cross a router, and the
// camera picks its own SSID (strongest known), which on the bench was often not the Tab5's (11-11: camera on the home
// LAN, Tab5 on the travel router -> "camera not found" although it was powered and healthy).
// We never write to the device and never touch DTR/RTS (on an ESP32 those lines mean reset).
static usb_transfer_t *s_xin;
static volatile bool s_xin_busy;
static int s_cdc_if = -1;
static char s_ln[200]; static size_t s_ln_n;
static char s_cam_ip[16], s_cam_ssid[33];
static volatile bool s_cam_new;                  // a fresh "ip" line: check it from the task loop (no HTTP in a callback)
static uint32_t s_cdc_lines;

static void line_done(char *l)
{
    s_cdc_lines++;
    char *p;
    if ((p = strstr(l, "xcam: ip "))) {
        char ip[16] = "";
        if (sscanf(p + 9, "%15[0-9.]", ip) == 1 && strchr(ip, '.')) { strlcpy(s_cam_ip, ip, sizeof s_cam_ip); s_cam_new = true; }
    } else if ((p = strstr(l, "xcam: joining wifi"))) {
        char *q = strchr(p, '"'), *e = q ? strchr(q + 1, '"') : NULL;
        if (q && e && e - q - 1 < (int)sizeof s_cam_ssid) { memcpy(s_cam_ssid, q + 1, e - q - 1); s_cam_ssid[e - q - 1] = 0; }
    }
}

static void in_cb(usb_transfer_t *t)             // runs inside usb_host_client_handle_events() (client task)
{
    if (t->status != USB_TRANSFER_STATUS_COMPLETED) { s_xin_busy = false; return; }   // unplugged / cancelled / error
    for (int i = 0; i < t->actual_num_bytes; i++) {
        char c = (char)t->data_buffer[i];
        if (c == '\n' || c == '\r') { if (s_ln_n) { s_ln[s_ln_n] = 0; line_done(s_ln); s_ln_n = 0; } }
        else if (s_ln_n + 1 < sizeof s_ln) s_ln[s_ln_n++] = c;
    }
    if (usb_host_transfer_submit(t) != ESP_OK) s_xin_busy = false;
}

static void cdc_listen(void)
{
    const usb_config_desc_t *cfg = NULL;
    if (usb_host_get_active_config_descriptor(s_devh, &cfg) != ESP_OK || !cfg) return;
    int off = 0, data_if = -1; uint8_t cur_if = 0xFF, cur_cls = 0, ep_in = 0; uint16_t mps = 64;
    const usb_standard_desc_t *d = (const usb_standard_desc_t *)cfg;
    while ((d = usb_parse_next_descriptor(d, cfg->wTotalLength, &off))) {
        if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t *in = (const usb_intf_desc_t *)d; cur_if = in->bInterfaceNumber; cur_cls = in->bInterfaceClass;
        } else if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && cur_cls == 0x0A && !ep_in) {   // CDC data interface
            const usb_ep_desc_t *e = (const usb_ep_desc_t *)d;
            if ((e->bEndpointAddress & 0x80) && (e->bmAttributes & 3) == 2) { ep_in = e->bEndpointAddress; data_if = cur_if; mps = e->wMaxPacketSize & 0x7FF; }
        }
    }
    if (data_if < 0) return;                                             // not a serial device: nothing to listen to
    if (usb_host_interface_claim(s_cl, s_devh, (uint8_t)data_if, 0) != ESP_OK) { ESP_LOGW(TAG, "host: claim CDC data interface %d failed", data_if); return; }
    s_cdc_if = data_if;
    if (mps > 512) mps = 512;
    if (!s_xin && usb_host_transfer_alloc(512, 0, &s_xin) != ESP_OK) { ESP_LOGW(TAG, "host: transfer alloc failed"); return; }
    s_xin->device_handle = s_devh; s_xin->bEndpointAddress = ep_in; s_xin->callback = in_cb; s_xin->context = NULL;
    s_xin->num_bytes = mps; s_xin->timeout_ms = 0;
    s_ln_n = 0;
    s_xin_busy = usb_host_transfer_submit(s_xin) == ESP_OK;
    ESP_LOGI(TAG, "host: listening to the device console (interface %d, endpoint 0x%02x)%s", data_if, ep_in, s_xin_busy ? "" : " — submit FAILED");
}

static void dev_close(void)
{
    if (!s_devh) return;
    for (int i = 0; i < 20 && s_xin_busy; i++) usb_host_client_handle_events(s_cl, pdMS_TO_TICKS(50));   // let the IN transfer come back
    if (s_cdc_if >= 0) { usb_host_interface_release(s_cl, s_devh, (uint8_t)s_cdc_if); s_cdc_if = -1; }
    usb_host_device_close(s_cl, s_devh); s_devh = NULL;
}

// Is the camera reachable from the network the Tab5 is on? If not, and the Tab5 is free to choose (Ajustes: Auto),
// join the camera's network once. Called from the client task only.
static int64_t s_follow_t;
static int s_unreach;
static void cam_check(void)
{
    if (!s_cam_ip[0] || !g_status.wifi_up) return;
    char url[48]; snprintf(url, sizeof url, "http://%s/health", s_cam_ip);
    uint8_t *b = NULL; size_t n = 0;
    int st = papp_get_url(url, &b, &n, 2000);
    free(b);
    if (st == 200) { s_unreach = 0; cam_set_back_usb(s_cam_ip); return; }
    if (++s_unreach < 2) return;                 // one miss can simply be the camera busy with a 3 MP capture
    cam_set_back_usb("");                        // two in a row: stop pointing the viewfinder at an address that is silent
    bool other = s_cam_ssid[0] && strcmp(s_cam_ssid, (const char *)g_status.ssid);
    int64_t now = esp_timer_get_time();
    if (other && net_pref() < 0 && (!s_follow_t || now - s_follow_t > 300 * 1000000LL)) {
        ESP_LOGW(TAG, "camera is on \"%s\" (%s), not reachable from \"%s\" -> joining the camera's network", s_cam_ssid, s_cam_ip, (const char *)g_status.ssid);
        s_follow_t = now;
        net_follow(s_cam_ssid);
    } else ESP_LOGW(TAG, "camera at %s (\"%s\") not reachable from here (HTTP %d)", s_cam_ip, s_cam_ssid, st);
}

static void client_task(void *a)
{
    usb_host_client_config_t cc = { .is_synchronous = false, .max_num_event_msg = 5,
                                    .async = { .client_event_callback = client_cb, .callback_arg = NULL } };
    if (usb_host_client_register(&cc, &s_cl) != ESP_OK) { ESP_LOGE(TAG, "host: client register failed"); s_host = false; vTaskDelete(NULL); }
    ESP_LOGI(TAG, "host: up, waiting for a device on USB-A");
    int64_t next_check = 0; bool force = false; char last_ip[16] = "";
    for (;;) {
        usb_host_client_handle_events(s_cl, pdMS_TO_TICKS(500));
        int64_t now = esp_timer_get_time();
        if (s_gone) {
            s_gone = false;
            dev_close();
            ESP_LOGI(TAG, "host: device %04x:%04x unplugged", s_info.vid, s_info.pid);
            memset(&s_info, 0, sizeof s_info);
            s_cam_ip[0] = 0; s_cam_ssid[0] = 0; cam_set_back_usb("");
        }
        if (s_cam_new) {                         // the camera just told us its address: look once the Tab5's own link is up
            s_cam_new = false;
            ESP_LOGI(TAG, "camera says (USB): ssid \"%s\" ip %s", s_cam_ssid, s_cam_ip);
            next_check = now + 1500000; force = true; s_unreach = 0;
        }
        if (s_cam_ip[0] && strcmp(last_ip, (const char *)g_status.ip)) {       // the Tab5 changed network: is the camera
            strlcpy(last_ip, (const char *)g_status.ip, sizeof last_ip);       // still reachable from the new one?
            force = true; next_check = now + 1000000;
        }
        if (next_check && now >= next_check && g_status.wifi_up) {
            if (force || s_unreach || !cam_back_url()[0]) cam_check();         // in use and answering: leave it alone
            if (!s_unreach) force = false;
            next_check = now + (s_unreach ? 4 : 30) * 1000000LL;
        }
        uint8_t addr = s_new_addr;
        if (!addr) continue;
        s_new_addr = 0;
        dev_close();
        if (usb_host_device_open(s_cl, addr, &s_devh) != ESP_OK) { ESP_LOGW(TAG, "host: open addr %u failed", addr); continue; }
        const usb_device_desc_t *dd = NULL; usb_device_info_t di = {0};
        if (usb_host_get_device_descriptor(s_devh, &dd) == ESP_OK && dd && usb_host_device_info(s_devh, &di) == ESP_OK) {
            s_info.vid = dd->idVendor; s_info.pid = dd->idProduct; s_info.cls = dd->bDeviceClass; s_info.speed = di.speed;
            str_ascii(di.str_desc_product, s_info.prod, sizeof s_info.prod);
            s_info.present = true;
            ESP_LOGI(TAG, "host: device %04x:%04x \"%s\" class 0x%02x speed %s addr %u", s_info.vid, s_info.pid, s_info.prod,
                     s_info.cls, di.speed == USB_SPEED_HIGH ? "high" : di.speed == USB_SPEED_FULL ? "full" : "low", addr);
            cdc_listen();
        }
    }
}

bool usbport_host_start(bool save)
{
    if (s_host) return true;
    const usb_host_config_t hc = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LOWMED };
    esp_err_t e = usb_host_install(&hc);
    if (e != ESP_OK) { ESP_LOGE(TAG, "host: install failed: %s", esp_err_to_name(e)); return false; }
    s_host = true;
    xTaskCreate(lib_task, "usb_lib", 4096, NULL, 10, NULL);
    xTaskCreate(client_task, "usb_cl", 6144, NULL, 5, NULL);
    if (save) settings_set_u8("usbh", 1);
    return true;
}
bool usbport_host_running(void) { return s_host; }

int usbport_describe(char *out, size_t n, bool brief)       // brief: one screen line for Ajustes
{
    int o = brief ? snprintf(out, n, "5 V %s", s_pwr ? "on" : "off")
                  : snprintf(out, n, "5 V %s%s%s", s_pwr ? "on" : "off", s_ext ? " · ext5v on" : "", s_chg ? " · chg on" : " · chg off");
    if (s_host && s_info.present) o += brief ? snprintf(out + o, n - o, " · %.28s", s_info.prod)
                                             : snprintf(out + o, n - o, " · %04x:%04x %s", s_info.vid, s_info.pid, s_info.prod);
    else if (s_host) o += snprintf(out + o, n - o, brief ? " · nada conectado" : " · nothing plugged in");
    if (s_cam_ip[0]) o += snprintf(out + o, n - o, brief ? " · cam %.20s %s" : " · cam \"%s\" %s", s_cam_ssid, s_cam_ip);
    return o;
}

void usbport_start(void)
{
    // The battery charger (default ON, as in every M5Stack firmware for this board) is raised 12 s after boot by the
    // supervisor in battery.c: enabled at second 3 it twice did not start charging (11-11).
    if (settings_get_u8("ext5v", 0)) usbport_ext5v(true, false);
    bool want5v = settings_get_u8("usb5v", 1);
    if (want5v && settings_get_u8("usbh", 1)) {
        // Port off -> host stack up -> port on: the device boots while we are already listening, so its first log
        // lines (the camera's SSID and IP) are not missed. Also makes a soft restart of the Tab5 behave like a cold one.
        usbport_power(false, false);
        usbport_host_start(false);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    usbport_power(want5v, false);
}
