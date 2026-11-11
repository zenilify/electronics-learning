#pragma once
#include <stdbool.h>
#include <stddef.h>
// Remotes receiver (StickS3 / M5Dial, protocol v1 — satellites/PROTOCOL.md). UDP 47800, HMAC-tagged JSON.
void remotes_start(void);                       // after ui_init (creates an LVGL timer with the display lock)
bool remotes_set_key_hex(const char *hex32);    // console `rkey <32 hex>`; stored in NVS papp/rkey, never printed
void remotes_accept(const char *dev);           // add to the allow-list (NVS papp/rdevs)
void remotes_forget(void);                      // clear the allow-list
void remotes_bridge_line(char *line, int via);  // "R <mac> <rssi> <datagram>" from the ESP-NOW bridge (1 = console relay)
int  remotes_list(char *out, size_t n);         // human-readable status lines; returns the number of devices

// implemented in ui.c / audio.c, called by remotes.c with the display lock held (ui_*) or without (audio_*)
void ui_remote_ptt(int side, bool down);
void ui_remote_pair(int dir);
void ui_remote_replay(void);
const char *ui_remote_state(void);              // idle | listen | busy | speak
int  ui_remote_side(void);
bool papp_sha256_file(const char *path, char hex[65]);   // SHA-256 of a file, 64 hex chars + NUL
bool remotes_bridge_uart(int rx, int tx, bool save);     // ESP-NOW bridge on a UART (Grove port); untested path, off by default
