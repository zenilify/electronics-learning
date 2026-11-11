// PAPP ESP-NOW bridge (lane T5, 2026-11-11) — "transport B" of ../PROTOCOL.md.
//
// Why a bridge: the Tab5's own radio is an ESP32-C6 behind esp-hosted, and esp-hosted has no ESP-NOW (Espressif's
// coverage manifest: "ESP-NOW is not implemented in hosted", issue #19 still open). ESP-NOW between ESP32-S3 boards
// is the fully supported case, so one small S3 sits next to the Tab5, speaks ESP-NOW to the remotes and plain text
// lines to the Tab5 (UART on the Grove port, or USB). The remotes then need no access point at all.
//
// The bridge is deliberately dumb: it forwards bytes. The HMAC, the allow-list, the nonce and the acks all stay
// end-to-end between the remote and the Tab5 (same datagrams as over UDP).
//
// Lines to the host (USB CDC, and the UART when enabled) — one per line, '\n' terminated:
//   R <src mac, 12 hex> <rssi> <payload>     a frame received over ESP-NOW (payload untouched)
//   T5B ...                                  answers to the commands below / boot banner
// Lines from the host:
//   T <dst mac, 12 hex | *> <payload>        send payload over ESP-NOW (unicast, or broadcast for *)
//   T5B info | T5B ch <1-13> | T5B uart <rx> <tx> | T5B uart off | T5B reboot
// Handled by the bridge itself, never forwarded (channel finder for the remotes):
//   {"t5b":"ping"}  ->  {"t5b":"pong","ch":<n>}  (unicast back to the sender)
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_mac.h>
#include <Preferences.h>

static const char *FW = "0.1.0";
#ifdef ESP_NOW_MAX_DATA_LEN_V2
static const int MAXLEN = ESP_NOW_MAX_DATA_LEN_V2;      // 1470 B with ESP-NOW v2 (IDF >= 5.4 on both ends)
#else
static const int MAXLEN = ESP_NOW_MAX_DATA_LEN;         // 250 B
#endif

struct Frame { uint8_t mac[6]; int8_t rssi; uint16_t len; uint8_t data[512]; };
static QueueHandle_t rxq;
static Preferences prefs;
static uint8_t chan = 1;
static int urx = -1, utx = -1;                          // UART to the Tab5 (Grove), -1 = off
static uint32_t nRx = 0, nTx = 0, nTxFail = 0, nDrop = 0, nPing = 0, nBig = 0;
static uint8_t myMac[6];

// peers we added, oldest first (ESP-NOW holds 20; keep 12 and recycle)
static uint8_t peers[12][6];
static int npeers = 0;

static void macHex(const uint8_t *m, char out[13]) { for (int i = 0; i < 6; i++) sprintf(out + 2 * i, "%02x", m[i]); }
static bool hexMac(const char *s, uint8_t *m) {
  for (int i = 0; i < 6; i++) { char t[3] = {s[2 * i], s[2 * i + 1], 0}; char *e; m[i] = (uint8_t)strtoul(t, &e, 16); if (*e || !t[0] || !t[1]) return false; }
  return true;
}

// Never block on a console nobody reads (remotes froze on that, 2026-11-10): drop the line instead.
static void out(const char *s, size_t n) {
  if ((size_t)Serial.availableForWrite() >= n + 2) { Serial.write((const uint8_t *)s, n); Serial.write('\n'); } else nDrop++;
  if (utx >= 0) { Serial1.write((const uint8_t *)s, n); Serial1.write('\n'); }
}
static void say(const char *fmt, ...) {
  char b[320]; va_list a; va_start(a, fmt); int n = vsnprintf(b, sizeof b, fmt, a); va_end(a);
  if (n > 0) out(b, n < (int)sizeof b ? n : (int)sizeof b - 1);
}

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {   // Wi-Fi task: just queue it
  Frame f;
  if (len <= 0 || len > (int)sizeof f.data) { nBig++; return; }
  memcpy(f.mac, info->src_addr, 6);
  f.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  f.len = len; memcpy(f.data, data, len);
  if (xQueueSend(rxq, &f, 0) != pdTRUE) nDrop++;
}

static bool ensurePeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  if (npeers == 12) {                                   // recycle the oldest
    esp_now_del_peer(peers[0]); memmove(peers[0], peers[1], 11 * 6); npeers = 11;
  }
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6); p.channel = 0; p.ifidx = WIFI_IF_STA; p.encrypt = false;
  if (esp_now_add_peer(&p) != ESP_OK) return false;
  memcpy(peers[npeers++], mac, 6);
  return true;
}

static bool sendTo(const uint8_t *mac, const uint8_t *d, size_t n) {
  if (n > (size_t)MAXLEN) { nBig++; return false; }
  if (!ensurePeer(mac)) { nTxFail++; return false; }
  esp_err_t e = esp_now_send(mac, d, n);
  if (e == ESP_OK) nTx++; else nTxFail++;
  return e == ESP_OK;
}

static void setChannel(uint8_t c) {
  if (c < 1 || c > 13) c = 1;
  esp_wifi_set_channel(c, WIFI_SECOND_CHAN_NONE);
  uint8_t p; wifi_second_chan_t s; esp_wifi_get_channel(&p, &s);
  chan = p;
}

static void info() {
  char m[13]; macHex(myMac, m);
  say("T5B info {\"fw\":\"%s\",\"mac\":\"%s\",\"ch\":%u,\"maxlen\":%d,\"rx\":%u,\"tx\":%u,\"tx_fail\":%u,\"ping\":%u,\"drop\":%u,\"too_big\":%u,\"peers\":%d,\"uart\":[%d,%d],\"up_s\":%u}",
      FW, m, chan, MAXLEN, (unsigned)nRx, (unsigned)nTx, (unsigned)nTxFail, (unsigned)nPing, (unsigned)nDrop, (unsigned)nBig, npeers, urx, utx, (unsigned)(millis() / 1000));
}

static void hostLine(char *l) {
  if (l[0] == 'T' && l[1] == ' ') {                     // T <mac|*> <payload>
    char *sp = strchr(l + 2, ' ');
    if (!sp) return;
    *sp = 0;
    uint8_t mac[6];
    if (!strcmp(l + 2, "*")) memset(mac, 0xFF, 6);
    else if (strlen(l + 2) != 12 || !hexMac(l + 2, mac)) { say("T5B err bad mac"); return; }
    sendTo(mac, (const uint8_t *)(sp + 1), strlen(sp + 1));
  } else if (!strncmp(l, "T5B ", 4)) {
    l += 4;
    if (!strcmp(l, "info")) info();
    else if (!strncmp(l, "ch ", 3)) { int c = atoi(l + 3); if (c < 1 || c > 13) { say("T5B err ch 1-13"); return; } prefs.putUChar("ch", c); setChannel(c); say("T5B ok ch %u", chan); }
    else if (!strcmp(l, "uart off")) { prefs.putInt("urx", -1); prefs.putInt("utx", -1); say("T5B ok uart off (reboot to apply)"); }
    else if (!strncmp(l, "uart ", 5)) { int r = -1, t = -1; if (sscanf(l + 5, "%d %d", &r, &t) == 2 && r >= 0 && t >= 0) { prefs.putInt("urx", r); prefs.putInt("utx", t); say("T5B ok uart rx=%d tx=%d (reboot to apply)", r, t); } else say("T5B err uart <rx> <tx>"); }
    else if (!strcmp(l, "reboot")) { say("T5B ok reboot"); delay(100); ESP.restart(); }
    else say("T5B err unknown command");
  }
}

static void pollStream(Stream &s, char *buf, size_t cap, size_t &n) {
  while (s.available()) {
    int c = s.read();
    if (c == '\n' || c == '\r') { if (n) { buf[n] = 0; hostLine(buf); n = 0; } }
    else if (n + 1 < cap) buf[n++] = (char)c;
  }
}

void setup() {
  Serial.begin(115200);
  prefs.begin("t5b", false);
  chan = prefs.getUChar("ch", 1);
  urx = prefs.getInt("urx", -1); utx = prefs.getInt("utx", -1);
  if (urx >= 0 && utx >= 0) Serial1.begin(115200, SERIAL_8N1, urx, utx); else urx = utx = -1;
  rxq = xQueueCreate(8, sizeof(Frame));
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  for (int i = 0; i < 50 && !WiFi.STA.started(); i++) delay(20);
  esp_wifi_set_ps(WIFI_PS_NONE);                        // a sleeping receiver misses ESP-NOW frames
  setChannel(chan);
  esp_read_mac(myMac, ESP_MAC_WIFI_STA);
  if (esp_now_init() != ESP_OK) { say("T5B err esp_now_init"); return; }
  esp_now_register_recv_cb(onRecv);
  uint8_t bc[6]; memset(bc, 0xFF, 6); ensurePeer(bc);
  char m[13]; macHex(myMac, m);
  say("T5B ready fw=%s mac=%s ch=%u maxlen=%d", FW, m, chan, MAXLEN);
}

void loop() {
  static char hb[700], ub[700]; static size_t hn = 0, un = 0;
  pollStream(Serial, hb, sizeof hb, hn);
  if (urx >= 0) pollStream(Serial1, ub, sizeof ub, un);
  Frame f;
  while (xQueueReceive(rxq, &f, 0) == pdTRUE) {
    if (f.len >= 14 && !memcmp(f.data, "{\"t5b\":\"ping\"", 13)) {            // channel finder: answer here, do not forward
      char p[40]; int n = snprintf(p, sizeof p, "{\"t5b\":\"pong\",\"ch\":%u}", chan);
      sendTo(f.mac, (const uint8_t *)p, n); nPing++;
      continue;
    }
    nRx++;
    static char line[600];
    char m[13]; macHex(f.mac, m);
    int n = snprintf(line, sizeof line, "R %s %d ", m, f.rssi);
    size_t k = f.len < sizeof line - n - 1 ? f.len : sizeof line - n - 1;
    for (size_t i = 0; i < k; i++) { char c = (char)f.data[i]; line[n++] = (c == '\n' || c == '\r') ? ' ' : c; }
    out(line, n);
  }
  delay(1);
}
