// See restart.h. The LP ("RTC") watchdog is the one reset source that software can aim at the whole chip.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "soc/reset_reasons.h"
#include "soc/rtc.h"
#include "hal/wdt_hal.h"
#include "settings.h"
#include "restart.h"

static const char *TAG = "restart";
static int s_reason;

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
    bool cpu_only = rr == RESET_REASON_CPU0_SW || rr == RESET_REASON_CPU_MWDT || rr == RESET_REASON_CPU_RWDT || rr == RESET_REASON_CPU_LOCKUP;
    bool chained = settings_get_u8("rchain", 0);          // 1 = the previous boot already escalated (loop breaker)
    if (chained) settings_set_u8("rchain", 0);
    if (cpu_only && !chained) {
        uint8_t n = settings_get_u8("cpurst", 0);
        settings_set_u8("cpurst", n < 255 ? n + 1 : 255);
        settings_set_u8("rchain", 1);
        ESP_LOGW(TAG, "this boot follows a CPU-only reset (reason 0x%x): SDIO and USB would stay dead -> full chip reset (number %d)", (int)rr, n + 1);
        vTaskDelay(pdMS_TO_TICKS(60));                    // let the line out
        papp_full_reset();
    }
    if (cpu_only) ESP_LOGE(TAG, "still a CPU-only reset (0x%x) after a full reset was asked for - carrying on", (int)rr);
    else ESP_LOGI(TAG, "reset reason 0x%x", (int)rr);
    esp_register_shutdown_handler(papp_full_reset);       // esp_restart() calls this first, so it never gets to its CPU reset
}
