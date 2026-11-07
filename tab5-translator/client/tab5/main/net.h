#pragma once
// Wi-Fi (ESP32-C6 over esp_hosted) + backend health. Networks wifi0..2 from NVS, tried in order.
void net_start(void);
void net_reprobe(void);   // ask the net task to re-run the endpoint probe now
