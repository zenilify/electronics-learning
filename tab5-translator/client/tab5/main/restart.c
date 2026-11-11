// See restart.h. The LP ("RTC") watchdog is the one reset source that software can aim at the whole chip.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "soc/reset_reasons.h"
#include "soc/rtc.h"
#include "hal/wdt_hal.h"
#include "esp_private/brownout.h"
#include "settings.h"
#include "restart.h"

static const char *TAG = "restart";
static int s_reason;
static esp_reset_reason_t s_idf;
static char s_text[56] = "?";
static bool s_bod_on = true;

static const char *idf_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "POWERON";   case ESP_RST_EXT: return "EXT";            case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";       case ESP_RST_INT_WDT: return "INT_WDT";    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";           case ESP_RST_DEEPSLEEP: return "DEEPSLEEP"; case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";         case ESP_RST_USB: return "USB";            case ESP_RST_JTAG: return "JTAG";
    case ESP_RST_EFUSE: return "EFUSE";       case ESP_RST_PWR_GLITCH: return "PWR_GLITCH"; case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default: return "UNKNOWN";
    }
}
const char *papp_reset_name(void) { return idf_name(s_idf); }
const char *papp_reset_text(void) { return s_text; }
int papp_brownouts(void) { return settings_get_u8("rbod", 0); }
void papp_restart_note(uint8_t why) { settings_set_u8("rwhy", why); }
bool papp_bod_is_on(void) { return s_bod_on; }
void papp_bod_off(void)
{
    if (!s_bod_on) return;
    esp_brownout_disable();          // not saved anywhere: the next boot has the protection again
    s_bod_on = false;
    ESP_LOGW(TAG, "brownout detector OFF until the next boot (test)");
}

void papp_full_reset(void)
{
    wdt_hal_context_t w;
    wdt_hal_init(&w, WDT_RWDT, 0, false);                                   // LP watchdog, starts disabled
    uint32_t ticks = (uint32_t)(100ULL * rtc_clk_slow_freq_get_hz() / 1000ULL);   // 100 ms in slow-clock ticks
    wdt_hal_write_protect_disable(&w);
    wdt_hal_config_stage(&w, WDT_STAGE0, ticks, WDT_STAGE_ACTION_RESET_SYSTEM);   // HP system + peripherals (like esptool's reset)
    wdt_hal_config_stage(&w, WDT_STAGE1, ticks * 10, WDT_STAGE_ACTION_RESET_RTC); // if that did not happen: everything
    wdt_hal_enable(&w);
    wdt_hal_write_protect_enable(&w);
    for (;;) { }                                                            // the watchdog fires in 100 ms
}

int papp_cpu_resets_fixed(void) { return settings_get_u8("cpurst", 0); }
int papp_boot_reason(void) { return s_reason; }

void papp_restart_guard(void)
{
    soc_reset_reason_t rr = esp_rom_get_reset_reason(0);
    s_reason = (int)rr;
    s_idf = esp_reset_reason();                          // ROM reason + the hint ESP-IDF left before it reset (brownout, panic ...)
    uint8_t why = settings_get_u8("rwhy", 0);            // what our own code said right before it restarted
    if (why) settings_set_u8("rwhy", 0);
    bool cpu_only = rr == RESET_REASON_CPU0_SW || rr == RESET_REASON_CPU_MWDT || rr == RESET_REASON_CPU_RWDT || rr == RESET_REASON_CPU_LOCKUP;
    bool chained = settings_get_u8("rchain", 0);          // 1 = the previous boot already escalated (loop breaker)
    if (chained) settings_set_u8("rchain", 0);
    if (cpu_only && !chained) {
        uint8_t n = settings_get_u8("cpurst", 0);
        settings_set_u8("cpurst", n < 255 ? n + 1 : 255);
        settings_set_u8("rchain", 1);
        settings_set_u8("rwhy", s_idf == ESP_RST_PANIC ? 3 : 4);   // shown on the boot that follows the full reset
        ESP_LOGW(TAG, "this boot follows a CPU-only reset (reason 0x%x): SDIO and USB would stay dead -> full chip reset (number %d)", (int)rr, n + 1);
        vTaskDelay(pdMS_TO_TICKS(60));                    // let the line out
        papp_full_reset();
    }
    if (cpu_only) ESP_LOGE(TAG, "still a CPU-only reset (0x%x) after a full reset was asked for - carrying on", (int)rr);
    int bod = settings_get_u8("rbod", 0);
    if (s_idf == ESP_RST_BROWNOUT) { if (bod < 255) bod++; settings_set_u8("rbod", (uint8_t)bod); }
    // our own note only counts when this boot really is our whole-chip restart (0x9); otherwise what happened wins
    if (why && rr == RESET_REASON_CORE_RWDT)
        snprintf(s_text, sizeof s_text, "%s", why == 1 ? "vigilante de pantalla" : why == 2 ? "pedido por consola"
                                              : why == 3 ? "tras un fallo del programa" : "tras un reinicio de CPU");
    else if (s_idf == ESP_RST_BROWNOUT) snprintf(s_text, sizeof s_text, "baja tensión ×%d", bod);
    else snprintf(s_text, sizeof s_text, "%s", s_idf == ESP_RST_PANIC ? "fallo del programa" : s_idf == ESP_RST_POWERON ? "encendido"
                                              : s_idf == ESP_RST_USB ? "por USB" : s_idf == ESP_RST_WDT ? "reinicio completo"
                                              : s_idf == ESP_RST_SW ? "software" : idf_name(s_idf));
    ESP_LOGI(TAG, "reset reason 0x%x · ESP-IDF says %s · shown as \"%s\" · brownout resets so far %d", (int)rr, idf_name(s_idf), s_text, bod);
    esp_register_shutdown_handler(papp_full_reset);       // esp_restart() calls this first, so it never gets to its CPU reset
}
