#pragma once
#include <stdbool.h>
#include <stdint.h>
bool c6_version(uint32_t *maj, uint32_t *min, uint32_t *pat);
void c6_ota_start(const char *url);   // http URL of network_adapter.bin (esp32c6, esp-hosted 2.12.x)
