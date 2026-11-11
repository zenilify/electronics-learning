#pragma once
#include <stdbool.h>
#include <stddef.h>
// Tab5 USB-A (host) port — lane T5, 2026-11-11.
// The 5 V on that connector is a switched rail: USB5V_EN = I/O expander 0x44 pin 3 (BSP_FEATURE_USB). The BSP only
// drives it inside bsp_usb_host_start(); PAPP never called that, so anything plugged into USB-A stayed dark
// (the XIAO back camera: "connected but not working", Erick 11-11).
void usbport_start(void);                    // boot: 5 V per NVS "usb5v" (default ON); host stack per NVS "usbh" (default off)
bool usbport_power(bool on, bool save);      // switch the 5 V rail; save = remember the choice in NVS
bool usbport_power_is_on(void);
bool usbport_host_start(bool save);          // USB host library + a client that reports what is plugged in
bool usbport_host_running(void);
int  usbport_describe(char *out, size_t n);  // "5 V on · 303a:1001 USB JTAG/serial debug unit" (Ajustes, console)
void usbport_io_dump(void);                  // console `io`: registers of both I/O expanders, read back from the chips
