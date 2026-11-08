#include "sdcard.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "ff.h"
#include "bsp/m5stack_tab5.h"
#include "audio.h"

static const char *TAG = "sd";
static bool s_ok;

bool sd_ok(void) { return s_ok; }

bool sd_mount(void)
{
    if (s_ok) return true;
    int64_t t0 = esp_timer_get_time();
    esp_err_t e = bsp_sdcard_mount();
    if (e != ESP_OK) { ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(e)); return false; }
    s_ok = true;
    mkdir(SD_ROOT, 0775);
    mkdir(SD_ROOT "/tts", 0775);
    mkdir(SD_ROOT "/notes", 0775);
    uint32_t t, f; sd_info(&t, &f);
    ESP_LOGI(TAG, "mounted %s in %d ms: %lu MB total, %lu MB free", sd_card_name(),
             (int)((esp_timer_get_time() - t0) / 1000), (unsigned long)t, (unsigned long)f);
    sd_log("boot.log", "boot");
    return true;
}

const char *sd_card_name(void)
{
    sdmmc_card_t *c = bsp_sdcard_get_handle();
    return c ? c->cid.name : "-";
}

void sd_info(uint32_t *total_mb, uint32_t *free_mb)
{
    *total_mb = *free_mb = 0;
    if (!s_ok) return;
    FATFS *fs; DWORD fre;
    if (f_getfree("0:", &fre, &fs) != FR_OK) return;
    uint64_t csz = (uint64_t)fs->csize * 512;
    *total_mb = (uint32_t)(((uint64_t)(fs->n_fatent - 2) * csz) >> 20);
    *free_mb = (uint32_t)(((uint64_t)fre * csz) >> 20);
}

static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

static void cache_path(const char *key, char *p, size_t n) { snprintf(p, n, SD_ROOT "/tts/%08lx.wav", (unsigned long)fnv(key)); }

bool sd_cache_get(const char *key, int16_t **pcm, size_t *samples, int *rate)
{
    if (!s_ok) return false;
    char p[64]; cache_path(key, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 44 ? heap_caps_malloc(n, MALLOC_CAP_SPIRAM) : NULL;
    bool ok = b && fread(b, 1, n, f) == (size_t)n;
    fclose(f);
    const int16_t *pp; size_t ns; int r;
    if (ok && wav_parse(b, n, &pp, &ns, &r)) {
        *pcm = heap_caps_malloc(ns * 2, MALLOC_CAP_SPIRAM);
        if (*pcm) { memcpy(*pcm, pp, ns * 2); *samples = ns; *rate = r; free(b); return true; }
    }
    free(b);
    return false;
}

void sd_cache_put(const char *key, const int16_t *pcm, size_t samples, int rate)
{
    if (!s_ok || !pcm || !samples) return;
    char p[64]; cache_path(key, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) { ESP_LOGW(TAG, "cache write %s failed", p); return; }
    uint8_t h[44]; wav_header(h, samples * 2, rate);
    fwrite(h, 1, 44, f); fwrite(pcm, 2, samples, f);
    fclose(f);
}

void sd_log(const char *file, const char *line)
{
    if (!s_ok) return;
    char p[96]; snprintf(p, sizeof p, SD_ROOT "/%s", file);
    FILE *f = fopen(p, "a");
    if (!f) return;
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    if (tm.tm_year + 1900 >= 2026) { char ts[24]; strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm); fprintf(f, "%s\t%s\n", ts, line); }
    else fprintf(f, "up%lld\t%s\n", (long long)(esp_timer_get_time() / 1000000), line);
    fclose(f);
}
