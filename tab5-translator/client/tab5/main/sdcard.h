#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// 1 TB TF card (FAT32 "PAPP", 32 KB clusters) on SDMMC slot 0. The C6 lives on slot 1 of the same host.
#define SD_ROOT "/sdcard/papp"
bool sd_mount(void);                       // idempotent; false if no card / mount failed (logged)
bool sd_ok(void);
void sd_info(uint32_t *total_mb, uint32_t *free_mb);
const char *sd_card_name(void);
// Speech cache: key = "<lang>|<text>" -> SD_ROOT/tts/<fnv32>.wav (PCM16 mono)
bool sd_cache_get(const char *key, int16_t **pcm, size_t *samples, int *rate);   // pcm heap (PSRAM)
void sd_cache_put(const char *key, const int16_t *pcm, size_t samples, int rate);
// Append one line (no newline needed) to SD_ROOT/<file>, prefixed with uptime seconds.
void sd_log(const char *file, const char *line);
