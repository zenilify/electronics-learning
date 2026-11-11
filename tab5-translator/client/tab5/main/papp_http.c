// Byte-for-byte the request client/desktop/papp_client.py makes:
//   POST {BASE}/translate?src=..&tgt=..&format=wav&rate=16000
//   Content-Type: audio/wav · Authorization: Bearer <token> · body = RIFF/WAVE PCM16 mono 16 kHz
// The token comes from NVS (tools/provision.py) and is never logged.
#include "papp_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "settings.h"
#include "audio.h"

static const char *TAG = "papp_http";
static char EP_URL[PAPP_EP_COUNT][96] = {
    "",                                                  // 0 MacBook offline brain — filled by mDNS (_papp._tcp), see net.c
    "",   // was cachy over Tailscale (100.69.244.21) — the Tab5 has no tailnet client, every probe cost 2 s (11-08)
    "http://192.168.1.5:8787",                           // cachy on the Toluca LAN (also via UTR Teleport)
    "https://papp-138753081869.us-central1.run.app",     // Cloud Run (outside mainland China)
};
static const char *EP_NAME[PAPP_EP_COUNT] = { "MacBook", "Tailscale", "LAN", "Cloud Run" };

static int s_active = -1;
void papp_set_brain(const char *ipv4, int port)
{
    char u[96] = "";
    if (ipv4 && ipv4[0]) snprintf(u, sizeof u, "http://%s:%d", ipv4, port);
    if (strcmp(u, EP_URL[0])) { ESP_LOGI(TAG, "offline brain: %s", u[0] ? u : "gone"); strlcpy(EP_URL[0], u, sizeof EP_URL[0]);
                                  // brain gone while it was the active endpoint: re-probe before the next request
                                  // (11-09: an empty URL reached esp_http_client_open -> store fault, landmine 329)
                                  if (!u[0] && s_active == 0) s_active = -1; }
}
static char s_token[96];

const char *papp_endpoint_url(int i) { return i >= 0 && i < PAPP_EP_COUNT ? EP_URL[i] : ""; }
const char *papp_endpoint_name(int i) { return i >= 0 && i < PAPP_EP_COUNT ? EP_NAME[i] : "offline"; }
bool papp_have_token(void) { return s_token[0] != 0; }

void papp_http_init(void)
{
    if (!settings_get_token(s_token, sizeof s_token)) s_token[0] = 0;
    ESP_LOGI(TAG, "token present: %s (len %u)", s_token[0] ? "yes" : "NO", (unsigned)strlen(s_token));
}

void papp_result_free(papp_result_t *r)
{
    free(r->src_text); free(r->tgt_text); free(r->pcm);
    memset(r, 0, sizeof *r);
}

static char *url_decode(const char *s)   // urllib.parse.unquote semantics (no '+' -> space)
{
    size_t n = strlen(s); char *o = malloc(n + 1), *p = o;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            char hx[3] = { s[i + 1], s[i + 2], 0 }; *p++ = (char)strtol(hx, NULL, 16); i += 2;
        } else *p++ = s[i];
    }
    *p = 0; return o;
}

typedef struct { char *src, *tgt, *tim, *asr; char sl[4], tl[4]; } hdrs_t;

static void app_hdr(char **dst, const char *v)
{
    size_t a = *dst ? strlen(*dst) : 0, b = strlen(v);
    char *n = realloc(*dst, a + b + 1);
    if (!n) return;
    memcpy(n + a, v, b + 1); *dst = n;
}

static esp_err_t on_evt(esp_http_client_event_t *e)
{
    hdrs_t *h = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_HEADER && h) {
        if (!strcasecmp(e->header_key, "X-Papp-Src-Text")) app_hdr(&h->src, e->header_value);
        else if (!strcasecmp(e->header_key, "X-Papp-Tgt-Text")) app_hdr(&h->tgt, e->header_value);
        else if (!strcasecmp(e->header_key, "X-Papp-Timings")) app_hdr(&h->tim, e->header_value);
        else if (!strcasecmp(e->header_key, "X-Papp-Asr-Model")) app_hdr(&h->asr, e->header_value);
        else if (!strcasecmp(e->header_key, "X-Papp-Src")) snprintf(h->sl, sizeof h->sl, "%s", e->header_value);
        else if (!strcasecmp(e->header_key, "X-Papp-Tgt")) snprintf(h->tl, sizeof h->tl, "%s", e->header_value);
    }
    return ESP_OK;
}

static esp_http_client_handle_t mk(const char *url, int timeout_ms, esp_http_client_method_t m, void *ud)
{
    esp_http_client_config_t c = {
        .url = url, .method = m, .timeout_ms = timeout_ms,
        .buffer_size = 8192, .buffer_size_tx = 2048,
        .event_handler = on_evt, .user_data = ud,
        .user_agent = "papp-tab5/" PAPP_VERSION,
        .keep_alive_enable = false,
    };
    if (!strncmp(url, "https", 5)) c.crt_bundle_attach = esp_crt_bundle_attach;
    return esp_http_client_init(&c);
}

int papp_probe(int *health_ms)
{
    for (int i = 0; i < PAPP_EP_COUNT; i++) {
        if (!EP_URL[i][0]) continue;
        char url[128]; snprintf(url, sizeof url, "%.100s/health", EP_URL[i]);
        esp_http_client_handle_t c = mk(url, 2000, HTTP_METHOD_GET, NULL);
        if (!c) continue;
        int64_t t0 = esp_timer_get_time();
        esp_err_t e = esp_http_client_perform(c);
        int st = e == ESP_OK ? esp_http_client_get_status_code(c) : -1;
        esp_http_client_cleanup(c);
        int ms = (int)((esp_timer_get_time() - t0) / 1000);
        ESP_LOGI(TAG, "probe %s -> %d (%d ms)", EP_NAME[i], st, ms);
        if (st == 200) { s_active = i; if (health_ms) *health_ms = ms; return i; }
    }
    s_active = -1;
    return -1;
}

static int jint(const char *j, const char *k)
{
    if (!j) return -1;
    char pat[16]; snprintf(pat, sizeof pat, "\"%s\"", k);
    const char *p = strstr(j, pat); if (!p) return -1;
    p = strchr(p + strlen(pat), ':'); return p ? atoi(p + 1) : -1;
}

// One POST to {BASE}{path}; reply = WAV (+ X-Papp-* headers) on 200.
static int one_req(int ep, const char *path, const char *ctype, const uint8_t *body_out, size_t out_len,
                   papp_result_t *r, int timeout_ms)
{
    char url[512], auth[128];
    snprintf(url, sizeof url, "%s%s", EP_URL[ep], path);
    snprintf(auth, sizeof auth, "Bearer %s", s_token);
    hdrs_t h = {0};
    esp_http_client_handle_t c = mk(url, timeout_ms, HTTP_METHOD_POST, &h);
    if (!c) { ESP_LOGW(TAG, "no URL for endpoint %d", ep); memset(auth, 0, sizeof auth); r->http_status = -1; r->endpoint = ep; return -1; }
    esp_http_client_set_header(c, "Content-Type", ctype);
    esp_http_client_set_header(c, "Authorization", auth);
    memset(auth, 0, sizeof auth);
    int64_t t0 = esp_timer_get_time();
    int status = -1;
    esp_err_t e = esp_http_client_open(c, out_len);
    if (e == ESP_OK) {
        size_t off = 0;
        while (off < out_len) {
            int n = esp_http_client_write(c, (const char *)body_out + off,
                                          out_len - off > 4096 ? 4096 : out_len - off);
            if (n <= 0) { e = ESP_FAIL; break; }
            off += n;
        }
    }
    if (e == ESP_OK) {
        int64_t cl = esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        size_t cap = cl > 0 ? (size_t)cl : 256 * 1024, len = 0;
        uint8_t *body = heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM);
        while (body) {
            if (len == cap) { uint8_t *nb = heap_caps_realloc(body, cap * 2 + 1, MALLOC_CAP_SPIRAM); if (!nb) break; body = nb; cap *= 2; }
            int n = esp_http_client_read(c, (char *)body + len, cap - len);
            if (n <= 0) break;
            len += n;
        }
        r->rtt_ms = (int)((esp_timer_get_time() - t0) / 1000);
        if (status == 200 && body) {
            const int16_t *pcm; size_t ns; int rate;
            if (wav_parse(body, len, &pcm, &ns, &rate)) {
                r->pcm = heap_caps_malloc(ns * 2, MALLOC_CAP_SPIRAM);
                if (r->pcm) { memcpy(r->pcm, pcm, ns * 2); r->samples = ns; r->rate = rate; }
            } else {                       // non-audio 200 (JSON): hand the text back in tgt_text
                body[len] = 0;
                r->tgt_text = strdup((char *)body);
            }
        } else if (body && len) {
            ESP_LOGW(TAG, "HTTP %d: %.*s", status, (int)(len > 160 ? 160 : len), (char *)body);
        }
        free(body);
    } else {
        ESP_LOGW(TAG, "%s: %s", EP_NAME[ep], esp_err_to_name(e));
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (!r->rtt_ms) r->rtt_ms = (int)((esp_timer_get_time() - t0) / 1000);   // also on a timeout: "after 0 ms" in the log was a lie
    r->http_status = status; r->endpoint = ep;
    if (h.src) r->src_text = url_decode(h.src);
    if (h.tgt) { free(r->tgt_text); r->tgt_text = url_decode(h.tgt); }
    if (h.asr) snprintf(r->asr_model, sizeof r->asr_model, "%s", h.asr);
    memcpy(r->src_lang, h.sl, sizeof h.sl); memcpy(r->tgt_lang, h.tl, sizeof h.tl);
    r->t_asr = jint(h.tim, "asr"); r->t_nmt = jint(h.tim, "nmt"); r->t_tts = jint(h.tim, "tts"); r->t_total = jint(h.tim, "total");
    free(h.src); free(h.tgt); free(h.tim); free(h.asr);
    return status;
}


// Active endpoint, then re-probe once and retry on a transport error.
static bool health_ok(int ep)
{
    char url[128]; snprintf(url, sizeof url, "%.100s/health", EP_URL[ep]);
    esp_http_client_handle_t c = mk(url, 2000, HTTP_METHOD_GET, NULL);
    if (!c) return false;
    bool ok = esp_http_client_perform(c) == ESP_OK && esp_http_client_get_status_code(c) == 200;
    esp_http_client_cleanup(c);
    return ok;
}

// Routes the offline brain answered 400/404/422 to (v2 extras it doesn't have yet): key = path up to the first '='.
static char s_miss[8][32];
static int s_alt = -1; static int64_t s_alt_t;        // last endpoint that served a brain-miss route (2 min)
static void route_key(const char *path, char *k, size_t n) { size_t l = strcspn(path, "="); snprintf(k, n, "%.*s", (int)l, path); }
static bool brain_misses(const char *k) { for (int i = 0; i < 8; i++) if (!strcmp(s_miss[i], k)) return true; return false; }
static void brain_miss_add(const char *k) { static int w; if (!brain_misses(k)) { strlcpy(s_miss[w++ % 8], k, sizeof s_miss[0]); ESP_LOGI(TAG, "brain lacks %s -> cachy/cloud", k); } }

static int alt_req(const char *path, const char *ctype, const uint8_t *b, size_t n, papp_result_t *r, int tmo)
{
    int64_t now = esp_timer_get_time();
    if (s_alt > 0 && now - s_alt_t < 120000000LL) {
        int st = one_req(s_alt, path, ctype, b, n, r, tmo);
        if (st >= 0 && st != 404) { s_alt_t = now; return st; }
        papp_result_free(r);
    }
    int st = -1;
    for (int ep = 1; ep < PAPP_EP_COUNT; ep++) {
        if (!EP_URL[ep][0] || !health_ok(ep)) continue;
        st = one_req(ep, path, ctype, b, n, r, tmo);
        if (st >= 0 && st != 404) { s_alt = ep; s_alt_t = now; return st; }
        papp_result_free(r);
    }
    r->http_status = st; return st;
}

static void with_failover_t(const char *path, const char *ctype, const uint8_t *b, size_t n, papp_result_t *r, int tmo)
{
    memset(r, 0, sizeof *r);
    r->http_status = -1; r->endpoint = -1;
    if (!s_token[0]) { r->http_status = 401; return; }
    if (s_active < 0) papp_probe(NULL);
    if (s_active < 0) return;
    char key[32]; route_key(path, key, sizeof key);
    if (s_active == 0 && brain_misses(key)) { alt_req(path, ctype, b, n, r, tmo); return; }
    int st = one_req(s_active, path, ctype, b, n, r, tmo);
    if (st < 0) {
        int old = s_active;
        papp_result_free(r); r->http_status = -1; r->endpoint = -1;
        if (papp_probe(NULL) >= 0 && s_active != old) st = one_req(s_active, path, ctype, b, n, r, tmo);
    }
    // the offline brain answers 400/404/422 to v2 routes it doesn't have yet (T5-B contract): remember the route
    // and serve it from the next healthy endpoint (cachy via LAN/Teleport, then Cloud Run)
    if ((st == 404 || st == 422 || st == 400) && r->endpoint == 0) {
        if (st != 400 || strstr(path, "pair=")) brain_miss_add(key);   // a plain 400 may just be bad input
        papp_result_free(r);
        alt_req(path, ctype, b, n, r, tmo);
    }
}

static void with_failover(const char *path, const char *ctype, const uint8_t *b, size_t n, papp_result_t *r)
{
    with_failover_t(path, ctype, b, n, r, 30000);
}

static char *json_text(const char *text)      // {"text": "<escaped>"}
{
    size_t n = strlen(text);
    char *o = malloc(n * 6 + 16), *p = o;
    p += sprintf(p, "{\"text\": \"");
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        if (*s == '"' || *s == '\\') { *p++ = '\\'; *p++ = *s; }
        else if (*s == '\n') { *p++ = '\\'; *p++ = 'n'; }
        else if (*s < 0x20) p += sprintf(p, "\\u%04x", *s);
        else *p++ = *s;
    }
    strcpy(p, "\"}");
    return o;
}

void papp_text_translate(lang_t src, lang_t tgt, const char *text, papp_result_t *r)
{
    char path[96]; snprintf(path, sizeof path, "/translate-text?src=%s&tgt=%s&format=wav&rate=%d",
                            lang_code(src), lang_code(tgt), PAPP_RATE);
    char *j = json_text(text);
    with_failover(path, "application/json", (uint8_t *)j, strlen(j), r);
    free(j);
}

void papp_tts(lang_t lang, const char *text, papp_result_t *r)
{
    char path[64]; snprintf(path, sizeof path, "/tts?lang=%s&rate=%d", lang_code(lang), PAPP_RATE);
    char *j = json_text(text);
    with_failover(path, "application/json", (uint8_t *)j, strlen(j), r);
    free(j);
}

void papp_get_json(const char *path, papp_result_t *r)   // e.g. /fx -> r->tgt_text = JSON body
{
    with_failover(path, "application/json", (const uint8_t *)"{}", 2, r);
}

void papp_translate(const papp_job_t *job, papp_result_t *r)
{
    char path[96]; snprintf(path, sizeof path, "/translate?src=%s&tgt=%s&format=wav&rate=%d",
                            lang_code(job->src), lang_code(job->tgt), PAPP_RATE);
    with_failover(path, "audio/wav", job->wav, job->wav_len, r);
}

// ---- GET helpers (SD sync) -------------------------------------------------------------------
// GET {BASE}{path} -> body in PSRAM (NUL-terminated) or streamed into `fp`. Returns HTTP status (<0 transport).
static int one_get(int ep, const char *path, uint8_t **body, size_t *len, FILE *fp)
{
    char url[512], auth[128];
    snprintf(url, sizeof url, "%s%s", EP_URL[ep], path);
    snprintf(auth, sizeof auth, "Bearer %s", s_token);
    esp_http_client_handle_t c = mk(url, 30000, HTTP_METHOD_GET, NULL);
    if (!c) { memset(auth, 0, sizeof auth); return -1; }
    esp_http_client_set_header(c, "Authorization", auth);
    memset(auth, 0, sizeof auth);
    int status = -1; size_t n = 0;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        int64_t cl = esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        size_t cap = cl > 0 ? (size_t)cl : 64 * 1024;
        uint8_t *b = heap_caps_malloc(fp ? 16384 : cap + 1, MALLOC_CAP_SPIRAM);
        while (b) {
            if (fp) {
                int r = esp_http_client_read(c, (char *)b, 16384);
                if (r <= 0) break;
                if (status == 200 && fwrite(b, 1, r, fp) != (size_t)r) { status = -2; break; }
                n += r;
            } else {
                if (n == cap) { uint8_t *nb = heap_caps_realloc(b, cap * 2 + 1, MALLOC_CAP_SPIRAM); if (!nb) break; b = nb; cap *= 2; }
                int r = esp_http_client_read(c, (char *)b + n, cap - n);
                if (r <= 0) break;
                n += r;
            }
        }
        if (cl > 0 && (int64_t)n != cl && status == 200) status = -3;       // short read
        if (!fp && b) { b[n] = 0; *body = b; *len = n; } else free(b);
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return status;
}

// SD sync source: the full pack lives on cachy (LAN/Teleport); the MacBook brain only mirrors the phrasebook part,
// so a sync pins cachy whenever it answers /health (11-10: the Erick-voice pack exists on cachy only, D-N4-2).
static int s_get_pin = -1;
int papp_sd_source_begin(void)
{
    s_get_pin = (EP_URL[2][0] && health_ok(2)) ? 2 : -1;
    ESP_LOGI(TAG, "sd sync source: %s", s_get_pin >= 0 ? EP_NAME[s_get_pin] : "active endpoint");
    return s_get_pin;
}
void papp_sd_source_end(void) { s_get_pin = -1; }

int papp_get(const char *path, uint8_t **body, size_t *len)
{
    *body = NULL; *len = 0;
    if (!s_token[0]) return 401;
    if (s_get_pin >= 0) return one_get(s_get_pin, path, body, len, NULL);
    if (s_active < 0) papp_probe(NULL);
    if (s_active < 0) return -1;
    int st = one_get(s_active, path, body, len, NULL);
    if (st < 0 && papp_probe(NULL) >= 0) { free(*body); *body = NULL; st = one_get(s_active, path, body, len, NULL); }
    // the offline brain lacks most v2 GET routes (/sd/*, /line/*): ask cachy / Cloud Run instead
    for (int ep = 1; st == 404 && s_active == 0 && ep < PAPP_EP_COUNT; ep++) {
        if (!EP_URL[ep][0]) continue;
        free(*body); *body = NULL; *len = 0;
        int s2 = one_get(ep, path, body, len, NULL);
        if (s2 >= 0) st = s2;
    }
    return st;
}

int papp_get_file(const char *path, FILE *fp)
{
    if (!s_token[0]) return 401;
    if (s_get_pin >= 0) return one_get(s_get_pin, path, NULL, NULL, fp);
    if (s_active < 0) papp_probe(NULL);
    if (s_active < 0) return -1;
    return one_get(s_active, path, NULL, NULL, fp);
}

void papp_post_bytes_t(const char *path, const char *ctype, const uint8_t *b, size_t n, papp_result_t *r, int timeout_ms)
{
    with_failover_t(path, ctype, b, n, r, timeout_ms);
}

void papp_post_bytes(const char *path, const char *ctype, const uint8_t *b, size_t n, papp_result_t *r)
{
    // long uploads (voice notes, up to minutes of audio) and long server work (/summarize) get 3 min
    with_failover_t(path, ctype, b, n, r, n > 2 * 1024 * 1024 || strstr(path, "/summarize") ? 180000 : 30000);
}

// "key": "value" -> malloc'd UTF-8 with JSON escapes decoded (\n \t \" \\ \/ \uXXXX incl. surrogate pairs).
char *json_str(const char *j, const char *key)
{
    if (!j) return NULL;
    char pat[40]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *k = strstr(j, pat); if (!k) return NULL;
    const char *v = k + strlen(pat);
    while (*v == ' ' || *v == ':') v++;
    if (*v != '"') return NULL;
    v++;
    size_t cap = strlen(v) + 1; char *o = malloc(cap), *w = o;
    if (!o) return NULL;
    while (*v && *v != '"') {
        if (*v != '\\') { *w++ = *v++; continue; }
        v++;
        switch (*v) {
        case 'n': *w++ = '\n'; v++; break;
        case 't': *w++ = '\t'; v++; break;
        case 'r': v++; break;
        case 'u': {
            unsigned cp = (unsigned)strtoul((char[5]){ v[1], v[2], v[3], v[4], 0 }, NULL, 16); v += 5;
            if (cp >= 0xD800 && cp < 0xDC00 && v[0] == '\\' && v[1] == 'u') {
                unsigned lo = (unsigned)strtoul((char[5]){ v[2], v[3], v[4], v[5], 0 }, NULL, 16); v += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            if (cp < 0x80) *w++ = cp;
            else if (cp < 0x800) { *w++ = 0xC0 | cp >> 6; *w++ = 0x80 | (cp & 63); }
            else if (cp < 0x10000) { *w++ = 0xE0 | cp >> 12; *w++ = 0x80 | (cp >> 6 & 63); *w++ = 0x80 | (cp & 63); }
            else { *w++ = 0xF0 | cp >> 18; *w++ = 0x80 | (cp >> 12 & 63); *w++ = 0x80 | (cp >> 6 & 63); *w++ = 0x80 | (cp & 63); }
            break;
        }
        default: if (*v) *w++ = *v++;
        }
    }
    *w = 0;
    return o;
}

int papp_get_url(const char *url, uint8_t **buf, size_t *len, int tmo)   // any URL, PAPP bearer, body -> PSRAM
{
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = tmo, .buffer_size = 4096, .keep_alive_enable = false };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    *buf = NULL; *len = 0;
    if (!c) return -1;
    char auth[128]; snprintf(auth, sizeof auth, "Bearer %s", s_token);
    esp_http_client_set_header(c, "Authorization", auth); memset(auth, 0, sizeof auth);
    int st = -1; *buf = NULL; *len = 0;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        int64_t cl = esp_http_client_fetch_headers(c);
        st = esp_http_client_get_status_code(c);
        size_t cap = cl > 0 ? (size_t)cl : 256 * 1024, n = 0;
        uint8_t *b = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
        while (b) {
            if (n == cap) { uint8_t *nb = heap_caps_realloc(b, cap * 2, MALLOC_CAP_SPIRAM); if (!nb) break; b = nb; cap *= 2; }
            int r = esp_http_client_read(c, (char *)b + n, cap - n);
            if (r <= 0) break;
            n += r;
        }
        if (st == 200 && cl > 0 && (int64_t)n != cl) st = -3;      // short read: never hand a cut-off body to the caller
        if (st == 200 && b && n) { *buf = b; *len = n; } else free(b);
    }
    esp_http_client_close(c); esp_http_client_cleanup(c);
    return st;
}
