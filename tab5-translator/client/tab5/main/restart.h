#pragma once
// Restarting the Tab5 so that it really comes back — lane T5, 2026-11-11.
// A plain esp_restart() is a CPU-only reset on the ESP32-P4 (boot log: rst:0xc SW_CPU_RESET). Measured on this board:
// after it the SDIO link to the ESP32-C6 never comes up again ("Card init success", then no INIT event), the USB host
// sees no device, and esp-hosted answers by restarting the host — a boot loop, every 18 s, until someone presses reset.
// A reset of the whole chip (what esptool's reset does) boots normally. So:
void papp_full_reset(void);      // reset the whole chip through the LP watchdog; does not return
void papp_restart_guard(void);   // call early in app_main: (1) a boot that follows a CPU-only reset (a crash restarts
                                 // that way) goes straight into a full reset, (2) every later esp_restart() - ours,
                                 // esp-hosted's, the C6 updater's - is turned into a full reset
int  papp_cpu_resets_fixed(void);  // how many CPU-only resets were escalated so far (NVS "cpurst")
int  papp_boot_reason(void);       // ROM reset reason of this boot (0xc = CPU-only software reset)
