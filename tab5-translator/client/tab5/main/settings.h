#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
// NVS namespace "papp": token, wifi0..2 (ssid/pass), rot, pair. Provisioned by tools/provision.py.
void settings_init(void);
bool settings_get_token(char *buf, size_t len);
bool settings_get_wifi(int idx, char *ssid, size_t sl, char *pass, size_t pl);
uint8_t settings_get_u8(const char *key, uint8_t dflt);
void settings_set_u8(const char *key, uint8_t v);
