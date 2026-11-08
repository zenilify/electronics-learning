#pragma once
// Wi-Fi (ESP32-C6 over esp_hosted) + backend health. Networks wifi0..5 from NVS (tools/provision.py);
// scan-first, strongest visible wins, a user-picked network ("wpref") wins when it's on the air.
#include <stdbool.h>
void net_start(void);
void net_reprobe(void);   // ask the net task to re-run the endpoint probe now
void net_scan_log(void);  // console `scan`: SSID / channel / RSSI of nearby APs

// --- network picker (Ajustes) — all non-blocking; the net task does the work
#define NET_SLOTS 6
typedef struct { int slot; char ssid[33]; int rssi; bool current; } net_known_t;   // rssi -999 = not seen
void net_request_scan(void);
int  net_known(net_known_t *out, int max, unsigned *gen);   // provisioned networks + last scan; gen bumps per scan
void net_join(int slot);                                   // slot >= 0: switch now + remember; -1 = automatic
int  net_pref(void);                                       // remembered slot or -1
