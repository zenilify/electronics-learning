#pragma once
#include "papp.h"
// Endpoint order (client/tab5 spec): 0 Tailscale -> 1 Toluca LAN -> 2 Cloud Run.
#define PAPP_EP_COUNT 3
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
