// Remotes receiver (ROADMAP row 11): StickS3 push-to-talk clicker + M5Dial knob.
// Transport A: UDP 47800 on the same Wi-Fi. Transport B (2026-11-11): ESP-NOW through a small ESP32-S3 bridge
// (satellites/espnow-bridge) that hands us the very same datagrams as text lines — no access point needed.
// Protocol v1 = lane T5-R, tab5-translator/satellites/PROTOCOL.md (bench 11-10: 40/40 presses per remote, ack median 5 ms).
// Every datagram carries an 8-byte HMAC-SHA256 tag with a 16-byte key shared with the remotes (NVS papp/rkey, provisioned
// over the USB console with `rkey <32 hex>` from cachy ~/.config/zenilify/t5-remote-key — never printed).
// Devices must be accepted once on the Tab5 (pop-up, or console `remote accept <dev>`); the allow-list is NVS papp/rdevs.
#include "remotes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "driver/uart.h"
#include "settings.h"
#include "ui_priv.h"
#include "papp.h"
#include "audio.h"

static const char *TAG = "remotes";
#define PORT 47800
#define MAXDEV 4

// ---------- SHA-256 / HMAC (small, self-contained; avoids the mbedtls 3/4 API split) ----------
typedef struct { uint32_t h[8]; uint8_t b[64]; uint64_t len; size_t bl; } sha_t;
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_blk(sha_t *s, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | p[4*i+1] << 16 | p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3), s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_init(sha_t *s)
{
    static const uint32_t H0[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(s->h, H0, sizeof H0); s->len = 0; s->bl = 0;
}
static void sha_upd(sha_t *s, const uint8_t *p, size_t n)
{
    s->len += n;
    while (n) { size_t k = 64 - s->bl < n ? 64 - s->bl : n; memcpy(s->b + s->bl, p, k); s->bl += k; p += k; n -= k;
                if (s->bl == 64) { sha_blk(s, s->b); s->bl = 0; } }
}
static void sha_fin(sha_t *s, uint8_t out[32])
{
    uint64_t bits = s->len * 8; uint8_t pad = 0x80, z = 0;
    sha_upd(s, &pad, 1);
    while (s->bl != 56) sha_upd(s, &z, 1);
    uint8_t L[8]; for (int i = 0; i < 8; i++) L[i] = bits >> (56 - 8 * i);
    sha_upd(s, L, 8);
    for (int i = 0; i < 8; i++) { out[4*i] = s->h[i] >> 24; out[4*i+1] = s->h[i] >> 16; out[4*i+2] = s->h[i] >> 8; out[4*i+3] = s->h[i]; }
}
// SHA-256 of a file as 64 hex chars — svc.c uses it to tell a changed file from an unchanged one of the same size.
bool papp_sha256_file(const char *path, char hex[65])
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    static uint8_t buf[8192];                  // only the svc task calls this
    sha_t s; sha_init(&s); size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha_upd(&s, buf, n);
    fclose(f);
    uint8_t d[32]; sha_fin(&s, d);
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", d[i]);
    return true;
}

static uint8_t s_key[16];
static bool s_have_key;
static void tag16(const uint8_t *msg, size_t n, char out[17])        // first 8 bytes of HMAC-SHA256(key, msg), hex
{
    uint8_t k[64] = {0}, ip[64], op[64], inner[32], full[32]; memcpy(k, s_key, 16);
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    sha_t s; sha_init(&s); sha_upd(&s, ip, 64); sha_upd(&s, msg, n); sha_fin(&s, inner);
    sha_init(&s); sha_upd(&s, op, 64); sha_upd(&s, inner, 32); sha_fin(&s, full);
    for (int i = 0; i < 8; i++) sprintf(out + 2 * i, "%02x", full[i]);
}

// ---------- tiny JSON field readers (the remotes send flat objects + one nested arg) ----------
static bool jstr(const char *j, const char *k, char *out, size_t n)
{
    char pat[24]; snprintf(pat, sizeof pat, "\"%s\":\"", k);
    const char *p = strstr(j, pat); if (!p) return false;
    p += strlen(pat); size_t o = 0;
    while (*p && *p != '"' && o + 1 < n) out[o++] = *p++;
    out[o] = 0; return true;
}
static bool jnum(const char *j, const char *k, long long *v)
{
    char pat[24]; snprintf(pat, sizeof pat, "\"%s\":", k);
    const char *p = strstr(j, pat); if (!p) return false;
    p += strlen(pat); if (*p == '"') return false;
    *v = strtoll(p, NULL, 10); return true;
}

// ---------- device table ----------
// Where a datagram came from = where its ack goes. via 0: UDP socket · 1: ESP-NOW bridge relayed over the USB
// console (bench, tools/bridge_relay.py) · 2: ESP-NOW bridge on a UART (Port A; not wired yet on 11-11).
typedef struct { uint8_t via; struct sockaddr_in addr; char mac[13]; } rsrc_t;
typedef struct { char dev[20], name[24], kind[8], n[9]; long long boot; uint32_t last; int64_t seen, ptt_t; bool ptt; int side, batt;
                 rsrc_t src; } rdev_t;
static SemaphoreHandle_t s_mx;                 // one datagram at a time: two tasks can feed handle() now
static rdev_t s_dev[MAXDEV];
static char s_allow[160];                      // "stick-353c,dial-944c"
static char s_pending[20];                     // first-contact request waiting for the pop-up
static int s_sock = -1;

static bool allowed(const char *d)
{
    char pat[24]; snprintf(pat, sizeof pat, ",%.19s,", d);
    char hay[170]; snprintf(hay, sizeof hay, ",%.160s,", s_allow);
    return strstr(hay, pat) != NULL;
}
static void allow_save(void)
{
    nvs_handle_t h;
    if (nvs_open("papp", NVS_READWRITE, &h) == ESP_OK) { nvs_set_str(h, "rdevs", s_allow); nvs_commit(h); nvs_close(h); }
}
void remotes_accept(const char *dev)
{
    if (!dev || !dev[0] || allowed(dev)) return;
    if (strlen(s_allow) + strlen(dev) + 2 >= sizeof s_allow) return;
    if (s_allow[0]) strcat(s_allow, ",");
    strcat(s_allow, dev); allow_save();
    ESP_LOGI(TAG, "accepted %s", dev);
    if (!strcmp(s_pending, dev)) s_pending[0] = 0;
}
void remotes_forget(void) { s_allow[0] = 0; allow_save(); memset(s_dev, 0, sizeof s_dev); ESP_LOGI(TAG, "allow-list cleared"); }

bool remotes_set_key_hex(const char *hex)
{
    uint8_t k[16];
    if (!hex || strlen(hex) < 32) return false;
    for (int i = 0; i < 16; i++) { unsigned v; if (sscanf(hex + 2 * i, "%2x", &v) != 1) return false; k[i] = v; }
    nvs_handle_t h;
    if (nvs_open("papp", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, "rkey", k, 16) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) { memcpy(s_key, k, 16); s_have_key = true; }
    uint8_t d[32]; sha_t s; sha_init(&s); sha_upd(&s, k, 16); sha_fin(&s, d);
    ESP_LOGI(TAG, "key set: len 16, sha256 %02x%02x%02x%02x…", d[0], d[1], d[2], d[3]);   // never the key itself
    memset(k, 0, sizeof k);
    return ok;
}

static rdev_t *dev_get(const char *dev)
{
    rdev_t *free_ = NULL;
    for (int i = 0; i < MAXDEV; i++) { if (!strcmp(s_dev[i].dev, dev)) return &s_dev[i]; if (!s_dev[i].dev[0] && !free_) free_ = &s_dev[i]; }
    if (!free_) free_ = &s_dev[0];             // table full: recycle the first slot
    memset(free_, 0, sizeof *free_); strlcpy(free_->dev, dev, sizeof free_->dev); free_->batt = -1;
    return free_;
}

static bool s_uart_on;
static void reply(const rsrc_t *to, const char *b, int n)
{
    if (to->via == 0) { sendto(s_sock, b, n, 0, (const struct sockaddr *)&to->addr, sizeof to->addr); return; }
    if (to->via == 1) { printf("@@T5B T %s %.*s\n", to->mac, n, b); fflush(stdout); }
    if (to->via == 2 && s_uart_on) {
        uart_write_bytes(UART_NUM_1, "T ", 2); uart_write_bytes(UART_NUM_1, to->mac, 12);
        uart_write_bytes(UART_NUM_1, " ", 1); uart_write_bytes(UART_NUM_1, b, n); uart_write_bytes(UART_NUM_1, "\n", 1);
    }
}

static void send_ack(const rsrc_t *to, const char *dev, uint32_t seq, bool ok, const char *err, const char *extra)
{
    char b[400];
    static const char *PAIRQ[PAIR_COUNT] = { "es-zh", "en-zh", "es-en" };
    int n = snprintf(b, sizeof b - 32, "{\"v\":1,\"dev\":\"%s\",\"seq\":%u,\"ev\":\"ack\",\"ok\":%s", dev, (unsigned)seq, ok ? "true" : "false");
    if (err) n += snprintf(b + n, sizeof b - 32 - n, ",\"err\":\"%s\"", err);
    if (ok) n += snprintf(b + n, sizeof b - 32 - n, ",\"state\":{\"st\":\"%s\",\"pair\":\"%s\",\"vol\":%d,\"side\":%d}",
                          ui_remote_state(), PAIRQ[papp_get_pair() % PAIR_COUNT], audio_get_volume(), ui_remote_side());
    if (extra) n += snprintf(b + n, sizeof b - 32 - n, "%s", extra);
    char t[17]; tag16((uint8_t *)b, n, t);
    n += snprintf(b + n, sizeof b - n, ",\"m\":\"%s\"}", t);
    reply(to, b, n);
}

// ---------- event execution (display lock taken here) ----------
static void exec_ev(rdev_t *d, const char *ev, const char *j)
{
    long long v;
    if (!strcmp(ev, "ptt_down")) {
        int side = jnum(j, "side", &v) ? (int)v : 0;
        bool rep = strstr(j, "\"rep\":1") != NULL;
        d->ptt_t = esp_timer_get_time();
        if (rep && d->ptt) return;                                  // keep-alive while held
        d->ptt = true; d->side = side;
        lock(); ui_remote_ptt(side, true); unlock();
    } else if (!strcmp(ev, "ptt_up")) {
        if (!d->ptt) return;
        d->ptt = false;
        lock(); ui_remote_ptt(d->side, false); unlock();           // a < 450 ms capture is dropped by audio.c (cancel)
    } else if (!strcmp(ev, "replay")) {
        lock(); ui_remote_replay(); unlock();
    } else if (!strcmp(ev, "pair_next") || !strcmp(ev, "pair_prev")) {
        lock(); ui_remote_pair(ev[5] == 'n' ? 1 : -1); unlock();
    } else if (!strcmp(ev, "vol_up") || !strcmp(ev, "vol_down")) {
        int step = jnum(j, "step", &v) ? (int)v : 5;
        audio_set_volume(audio_get_volume() + (ev[4] == 'u' ? step : -step));
    } else if (!strcmp(ev, "battery")) {
        d->batt = jnum(j, "pct", &v) ? (int)v : -1;
    }
}

// ---------- first-contact pop-up ----------
static lv_obj_t *s_mb;
static void on_mb(lv_event_t *e)
{
    bool yes = (bool)(intptr_t)lv_event_get_user_data(e);
    if (yes) remotes_accept(s_pending); else s_pending[0] = 0;
    if (s_mb) { lv_msgbox_close(s_mb); s_mb = NULL; }
}
static void popup_tick(lv_timer_t *t)
{
    if (!s_pending[0] || s_mb) return;
    char b[96]; snprintf(b, sizeof b, "%s  (%s)", s_pending, s_dev[0].dev[0] ? "" : "");
    for (int i = 0; i < MAXDEV; i++) if (!strcmp(s_dev[i].dev, s_pending)) snprintf(b, sizeof b, "%s  ·  %s", s_dev[i].name, s_pending);
    s_mb = lv_msgbox_create(NULL);
    lv_msgbox_add_title(s_mb, LV_SYMBOL_BLUETOOTH "  Nuevo control · 新遥控器");
    lv_obj_t *tx = lv_msgbox_add_text(s_mb, b); lv_obj_set_style_text_font(tx, f24, 0);
    lv_obj_t *y = lv_msgbox_add_footer_button(s_mb, "Aceptar · 接受");
    lv_obj_t *n = lv_msgbox_add_footer_button(s_mb, "No");
    lv_obj_add_event_cb(y, on_mb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(n, on_mb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
}

// ---------- one datagram (any transport) ----------
static void handle(char *b, int n, const rsrc_t *from)
{
    int64_t now = esp_timer_get_time();
    if (n <= 0 || !s_have_key) return;
    b[n] = 0;
    char *mp = NULL; for (char *p = b; (p = strstr(p, ",\"m\":\"")); p++) mp = p;
    if (n < 30 || !mp || (b + n) - mp != 6 + 16 + 2 || strcmp(b + n - 2, "\"}")) return;
    char t[17]; tag16((uint8_t *)b, mp - b, t);
    uint8_t diff = 0; for (int i = 0; i < 16; i++) diff |= t[i] ^ mp[6 + i];
    if (diff) return;                                        // bad HMAC: drop silently
    *mp = 0;
    char dev[20], ev[16]; long long seq;
    if (!jstr(b, "dev", dev, sizeof dev) || !jstr(b, "ev", ev, sizeof ev) || !jnum(b, "seq", &seq)) return;
    rdev_t *d = dev_get(dev);
    d->seen = now; d->src = *from;
    if (!strcmp(ev, "hello")) {
        long long boot = 0; jnum(b, "boot", &boot);
        jstr(b, "name", d->name, sizeof d->name); jstr(b, "kind", d->kind, sizeof d->kind);
        if (!allowed(dev)) {
            if (strcmp(s_pending, dev)) { strlcpy(s_pending, dev, sizeof s_pending); ESP_LOGI(TAG, "pairing request from %s (%s)", dev, d->name); }
            send_ack(from, dev, seq, false, "pair", NULL); return;
        }
        if (!d->n[0] || d->boot != boot) {                    // new boot of the remote -> new session nonce
            d->boot = boot; d->last = 0; snprintf(d->n, sizeof d->n, "%08lx", (unsigned long)esp_random());
            char ip[16] = ""; if (!from->via) inet_ntoa_r(from->addr.sin_addr, ip, sizeof ip);
            ESP_LOGI(TAG, "%s (%s) linked %s%s", dev, d->name, from->via ? "over ESP-NOW, bridge sees it as " : "from ", from->via ? from->mac : ip);
        }
        if ((uint32_t)seq > d->last) d->last = seq;
        char ex[48]; snprintf(ex, sizeof ex, ",\"n\":\"%s\",\"boot\":%lld", d->n, d->boot);
        send_ack(from, dev, seq, true, NULL, ex); return;
    }
    if (!allowed(dev)) { send_ack(from, dev, seq, false, "pair", NULL); return; }
    char nn[12] = ""; jstr(b, "n", nn, sizeof nn);
    if (!d->n[0] || strcmp(nn, d->n)) { send_ack(from, dev, seq, false, "nonce", NULL); return; }
    if ((uint32_t)seq <= d->last) { send_ack(from, dev, seq, true, NULL, NULL); return; }   // duplicate: re-ack only
    d->last = seq;
    static const char *KNOWN[] = { "ptt_down", "ptt_up", "replay", "pair_next", "pair_prev", "vol_up", "vol_down", "battery" };
    bool known = false; for (size_t i = 0; i < sizeof KNOWN / sizeof *KNOWN; i++) known |= !strcmp(ev, KNOWN[i]);
    if (!known) { send_ack(from, dev, seq, false, "unknown", NULL); return; }
    send_ack(from, dev, seq, true, NULL, NULL);            // ack first: the remote's RTT is the radio, not our UI
    if (strcmp(ev, "battery")) ESP_LOGI(TAG, "%s %s", dev, ev);
    exec_ev(d, ev, b);
}

// A line from the ESP-NOW bridge: "R <src mac, 12 hex> <rssi> <datagram>". via 1 = relayed over the console.
void remotes_bridge_line(char *l, int via)
{
    if (!s_mx || l[0] != 'R' || l[1] != ' ' || strlen(l) < 2 + 12 + 1 + 2 + 30) return;
    rsrc_t src = { .via = (uint8_t)via };
    memcpy(src.mac, l + 2, 12); src.mac[12] = 0;
    char *p = strchr(l + 15, ' ');                           // after the rssi field
    if (!p) return;
    p++;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    handle(p, (int)strlen(p), &src);
    xSemaphoreGive(s_mx);
}

// ---------- transport B on a UART: the bridge wired to the Grove port (Port A = GPIO 53 / 54) ----------
// WRITTEN, NOT TESTED ON HARDWARE (2026-11-11: no bridge was wired to the Tab5). Off unless `t5buart <rx> <tx>` was
// given on the console (NVS papp/t5brx, t5btx). The Grove 5 V needs `ext5v on`. Same lines as the console relay.
static void bridge_uart_task(void *a)
{
    static char ln[720]; size_t n = 0; uint8_t c[128];
    for (;;) {
        int k = uart_read_bytes(UART_NUM_1, c, sizeof c, pdMS_TO_TICKS(20));
        for (int i = 0; i < k; i++) {
            if (c[i] == '\n' || c[i] == '\r') { if (n) { ln[n] = 0; remotes_bridge_line(ln, 2); n = 0; } }
            else if (n + 1 < sizeof ln) ln[n++] = (char)c[i];
        }
    }
}
bool remotes_bridge_uart(int rx, int tx, bool save)          // rx/tx < 0: switch it off (takes effect after a reboot)
{
    nvs_handle_t h;
    if (save && nvs_open("papp", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "t5brx", rx < 0 ? 255 : (uint8_t)rx); nvs_set_u8(h, "t5btx", tx < 0 ? 255 : (uint8_t)tx);
        nvs_commit(h); nvs_close(h);
    }
    if (rx < 0 || tx < 0 || s_uart_on) return rx >= 0;
    uart_config_t uc = { .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
                         .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    if (uart_driver_install(UART_NUM_1, 2048, 2048, 0, NULL, 0) != ESP_OK || uart_param_config(UART_NUM_1, &uc) != ESP_OK ||
        uart_set_pin(UART_NUM_1, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) { ESP_LOGE(TAG, "bridge UART init failed"); return false; }
    s_uart_on = true;
    xTaskCreate(bridge_uart_task, "t5buart", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "ESP-NOW bridge UART on: rx GPIO%d tx GPIO%d 115200 (untested path)", rx, tx);
    return true;
}

// ---------- receiver task (UDP) ----------
static void rx_task(void *a)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int one = 1; setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    struct sockaddr_in me = { .sin_family = AF_INET, .sin_port = htons(PORT), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(s_sock, (struct sockaddr *)&me, sizeof me) < 0) { ESP_LOGE(TAG, "bind %d failed", PORT); vTaskDelete(NULL); }
    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 }; setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ESP_LOGI(TAG, "listening on UDP %d (key %s, allow-list \"%s\")", PORT, s_have_key ? "present" : "MISSING", s_allow);
    char b[520];
    for (;;) {
        rsrc_t from = { .via = 0 }; socklen_t fl = sizeof from.addr;
        int n = recvfrom(s_sock, b, sizeof b - 1, 0, (struct sockaddr *)&from.addr, &fl);
        int64_t now = esp_timer_get_time();
        xSemaphoreTake(s_mx, portMAX_DELAY);
        for (int i = 0; i < MAXDEV; i++)                        // stuck-talk guard: 2 s without a PTT datagram -> release
            if (s_dev[i].ptt && now - s_dev[i].ptt_t > 2000000) {
                s_dev[i].ptt = false; ESP_LOGW(TAG, "%s: PTT released by the 2 s guard", s_dev[i].dev);
                lock(); ui_remote_ptt(s_dev[i].side, false); unlock();
            }
        if (n > 0) handle(b, n, &from);
        xSemaphoreGive(s_mx);
    }
}

int remotes_list(char *out, size_t n)                          // "stick-353c StickS3 · 82% · 3 s" lines, for Ajustes / Controles
{
    int64_t now = esp_timer_get_time(); size_t o = 0; int c = 0;
    out[0] = 0;
    for (int i = 0; i < MAXDEV; i++) {
        if (!s_dev[i].dev[0]) continue;
        int age = (int)((now - s_dev[i].seen) / 1000000);
        char bt[16] = "—";                                     // battery not reported (yet): a dash, not "0%" / "-1%"
        if (s_dev[i].batt >= 0) snprintf(bt, sizeof bt, "%d%%", s_dev[i].batt);
        o += snprintf(out + o, n - o, "%s %s  %s  ·  %s%s  ·  %d s  ·  %s\n", age < 12 ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE, s_dev[i].dev,
                      s_dev[i].name, allowed(s_dev[i].dev) ? "" : "(sin aceptar) ", bt, age,
                      s_dev[i].src.via ? "ESP-NOW" : "Wi-Fi");
        if (o >= n) break;
        c++;
    }
    return c;
}

void remotes_start(void)
{
    nvs_handle_t h;
    if (nvs_open("papp", NVS_READONLY, &h) == ESP_OK) {
        size_t l = 16; s_have_key = nvs_get_blob(h, "rkey", s_key, &l) == ESP_OK && l == 16;
        l = sizeof s_allow; if (nvs_get_str(h, "rdevs", s_allow, &l) != ESP_OK) s_allow[0] = 0;
        nvs_close(h);
    }
    s_mx = xSemaphoreCreateMutex();
    {   uint8_t rx = settings_get_u8("t5brx", 255), tx = settings_get_u8("t5btx", 255);
        if (rx != 255 && tx != 255) remotes_bridge_uart(rx, tx, false); }
    lock(); lv_timer_create(popup_tick, 500, NULL); unlock();
    xTaskCreatePinnedToCore(rx_task, "remotes", 6144, NULL, 5, NULL, 0);
}
