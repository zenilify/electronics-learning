// PAPP Tab5 — shared types. One HTTP call per turn, mirroring client/desktop/papp_client.py.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAPP_VERSION   "0.2.0"
#define PAPP_RATE      16000          // mic + speaker + requested reply rate
#define PAPP_MAX_SEC   30             // server rejects > 30 s (413)
#define PAPP_MIN_MS    450            // shorter presses are treated as accidental taps

typedef enum { LANG_ES = 0, LANG_EN, LANG_ZH, LANG_COUNT } lang_t;
const char *lang_code(lang_t l);      // "es" | "en" | "zh"
const char *lang_label(lang_t l);     // "ES" | "EN" | "中文"
lang_t lang_from_code(const char *c);

typedef enum { PAIR_ES_ZH = 0, PAIR_EN_ZH, PAIR_ES_EN, PAIR_COUNT } pair_t;
void pair_langs(pair_t p, lang_t *left, lang_t *right);

// A finished recording (or a demo clip) waiting for the worker.
typedef struct {
    int side;                 // 0 = left button, 1 = right button
    lang_t src, tgt;
    uint8_t *wav;             // full RIFF/WAVE (44-byte header + PCM16 mono), heap (PSRAM)
    size_t wav_len;
    bool wav_owned;           // free() after use (false for embedded demo clips)
    int demo_idx;             // -1 = live speech, else DEMO_TEXT index
} papp_job_t;

typedef struct {
    int http_status;          // 200 ok; <0 = transport error (no endpoint reachable)
    char *src_text, *tgt_text;  // URL-decoded UTF-8, heap (may be NULL)
    char asr_model[64];
    char src_lang[4], tgt_lang[4];   // X-Papp-Src / X-Papp-Tgt (conversation mode: who spoke, what we answered in)
    int t_asr, t_nmt, t_tts, t_total;   // server timings (ms)
    int rtt_ms;               // release -> last byte of the reply, measured on the Tab5
    int endpoint;             // index into papp_endpoint_name()
    int16_t *pcm;             // reply audio, heap (PSRAM)
    size_t samples;
    int rate;
} papp_result_t;

void papp_result_free(papp_result_t *r);

// --- worker (main.c)
bool papp_submit(papp_job_t *job);     // takes ownership; false if busy
void papp_demo_toggle(bool offline_only);
bool papp_demo_running(void);
void papp_set_pair(pair_t p);
pair_t papp_get_pair(void);

// --- global status (written by net/battery, read by the UI timer)
typedef struct {
    bool wifi_up;
    int rssi;
    char ssid[33];
    char ip[16];
    int endpoint;            // -1 none
    int health_ms;
    bool have_token;
    float batt_v;            // <0 unknown
    float batt_a;
    uint32_t c6v[3];         // C6 esp-hosted fw, read once by the net task (RPCs only from that task: landmine, see net.c)
    bool c6v_ok;            // + = into the battery (charging) by INA226 sign convention, see battery.c
} papp_status_t;
extern volatile papp_status_t g_status;
