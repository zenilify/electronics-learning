#pragma once
#include "papp.h"
#include <stdio.h>
// Endpoint order: 0 MacBook offline brain (mDNS, when seen) -> 1 Tailscale -> 2 Toluca LAN -> 3 Cloud Run.
#define PAPP_EP_COUNT 4
const char *papp_endpoint_url(int i);
const char *papp_endpoint_name(int i);   // "Tailscale" | "LAN" | "Cloud Run"
void papp_http_init(void);               // loads the token from NVS
bool papp_have_token(void);
int papp_probe(int *health_ms);          // GET /health (2 s each, in order) -> first 200 index or -1
// One turn: POST {BASE}/translate?src&tgt&format=wav&rate=16000 with the raw WAV body.
// Tries the active endpoint, then re-probes once and retries on a transport error.
void papp_translate(const papp_job_t *job, papp_result_t *res);
// Text in -> translated text (r->tgt_text, r->src_text) + spoken WAV. POST /translate-text, JSON {text}.
void papp_text_translate(lang_t src, lang_t tgt, const char *text, papp_result_t *r);
// Text -> speech only. POST /tts?lang=..&rate=16000, JSON {text}.
void papp_tts(lang_t lang, const char *text, papp_result_t *r);
// POST {path} with "{}" -> a JSON reply lands in r->tgt_text (e.g. /fx).
void papp_get_json(const char *path, papp_result_t *r);
// GET {BASE}{path}: body -> heap (PSRAM, NUL-terminated, caller frees). Returns HTTP status (<0 transport/short read).
int papp_get(const char *path, uint8_t **body, size_t *len);
// GET {BASE}{path} streamed into an open file. Returns HTTP status (-2 write error, -3 short read).
int papp_get_file(const char *path, FILE *fp);
void papp_set_brain(const char *ipv4, int port);   // NULL/"" = forget it
void papp_post_bytes(const char *path, const char *ctype, const uint8_t *b, size_t n, papp_result_t *r);  // JSON reply -> r->tgt_text
char *json_str(const char *json, const char *key);   // malloc'd, JSON escapes decoded; NULL if absent
