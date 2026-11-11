// t5r — PAPP remote core. See t5r.h and ../../PROTOCOL.md.
#include "t5r.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_mac.h>
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#ifdef T5R_ESPNOW
#include <esp_now.h>
#include <esp_wifi.h>
#endif

namespace t5r {

static const uint16_t PORT = 47800;
#ifdef T5R_ESPNOW
static const char *FW = "0.2.0-espnow";
#else
static const char *FW = "0.1.2";
#endif
static const int SLOTS = 4;
static const uint32_t RETRY_MS = 60;

static Preferences prefs;
static WiFiUDP udp;
static uint8_t key[16];
static bool haveKey = false;
static char dev[16], kind[8], devName[24];
static char nonce[12] = "";
static uint32_t bootId, seqCtr = 0;
static Link lnk = L_NOWIFI;
static State stt = {"?", "--", -1, 0};
static Stats st = {0, 0, 0, 0, 0, 0, 0xFFFFFFFF, 0, 0};
static IPAddress peer, hint;
static bool havePeer = false, haveHint = false;
static char curSsid[33] = "";
static int batPct = -1, batMv = 0;
static bool batChg = false;
static char extra[160] = "";

// ---------- Wi-Fi ----------
enum { W_SCAN_START, W_SCANNING, W_JOINING, W_UP, W_WAIT, W_NOW };

#ifdef T5R_ESPNOW
// ---------- transport B: ESP-NOW through a bridge next to the Tab5 (PROTOCOL.md §6) ----------
// No access point needed. The bridge (satellites/espnow-bridge) forwards our datagrams to the Tab5 untouched, so the
// HMAC, nonce, seq and acks are exactly the ones used over UDP. Finding the bridge: ping on every channel (~0.4 s).
static bool nowUp = false;            // esp_now_init done
static bool viaNow = false;           // talking through the bridge instead of Wi-Fi/UDP
static uint8_t nowPeer[6];            // the bridge's MAC
static uint8_t nowCh = 0;
static int nowRssi = 0;
static volatile bool pongSeen = false;
static uint8_t pongMac[6];
static volatile uint8_t pongCh = 0;
struct NowRx { uint16_t len; int8_t rssi; char d[512]; };
static QueueHandle_t nowQ = nullptr;
static uint8_t nowMode = 1;           // prefs "now": 0 = never, 1 = try the bridge first, then Wi-Fi
static uint32_t nowSweeps = 0, nowT = 0, nowSkipUntil = 0;
#endif
static uint8_t wst = W_SCAN_START;
static uint32_t wT = 0, wDownSince = 0;
static int lastScanN = -1, lastScanKnown = 0;

// ---------- event queue ----------
struct Ev {
  uint32_t seq;
  char ev[12];
  char arg[96];
  uint8_t tries, maxTries;
  uint32_t lastTx;
  uint32_t firstTxUs;
};
static const int QN = 8;
static Ev q[QN];
static int qh = 0, qc = 0;
static uint8_t consecFail = 0;

// ---------- hello ----------
static uint32_t helloSeq = 0, helloTx = 0;
static bool helloPending = false;
static uint8_t helloMiss = 0;

// ---------- ptt ----------
static bool pHeld = false, pSyn = false;
static int pSide = 0;
static uint32_t pT0 = 0, pRep = 0;
static uint32_t batT = 0;

// ---------- bench ----------
static int benchLeft = 0;
static uint32_t benchT = 0;
static bool benchDown = false;

// ---------- helpers ----------
static void hmacTag(const uint8_t *d, size_t n, char out17[17]) {
  uint8_t h[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, sizeof key, d, n, h);
  for (int i = 0; i < 8; i++) sprintf(out17 + 2 * i, "%02x", h[i]);
  out17[16] = 0;
}

static bool jstr(const char *s, const char *k, char *out, size_t n) {
  char pat[20];
  snprintf(pat, sizeof pat, "\"%s\":\"", k);
  const char *p = strstr(s, pat);
  if (!p) return false;
  p += strlen(pat);
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
  out[i] = 0;
  return true;
}

static bool jnum(const char *s, const char *k, long long *v) {
  char pat[20];
  snprintf(pat, sizeof pat, "\"%s\":", k);
  const char *p = strstr(s, pat);
  if (!p) return false;
  p += strlen(pat);
  if (!(*p == '-' || (*p >= '0' && *p <= '9'))) return false;
  *v = atoll(p);
  return true;
}

static uint32_t logDropped = 0;
static void say(const char *fmt, ...) {            // console line; never carries secrets
  char b[400];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  // Never block on the USB console: with no host draining it the HWCDC TX buffer fills after a few lines and a plain
  // print() then stalls the whole loop (seen 2026-11-10: both remotes froze ~4 min after linking, display stuck on "Tab5 OK").
  size_t need = strlen(b) + 6;
  if ((size_t)Serial.availableForWrite() < need) { logDropped++; return; }
  Serial.print("T5R ");
  Serial.println(b);
}

static void loadKey() {
  haveKey = prefs.getBytesLength("key") == 16 && prefs.getBytes("key", key, 16) == 16;
}

static bool slot(int i, char *ss, size_t sn, char *pw, size_t pn) {
  char k[4];
  snprintf(k, sizeof k, "s%d", i);
  String s = prefs.getString(k, "");
  if (!s.length()) return false;
  strlcpy(ss, s.c_str(), sn);
  snprintf(k, sizeof k, "p%d", i);
  strlcpy(pw, prefs.getString(k, "").c_str(), pn);
  return true;
}

static int slotCount() {
  int n = 0;
  char a[33], b[65];
  for (int i = 0; i < SLOTS; i++) n += slot(i, a, sizeof a, b, sizeof b);
  memset(b, 0, sizeof b);
  return n;
}

static void unlink(const char *why) {
  if (lnk == L_LINKED || lnk == L_PAIR) say("log unlink %s", why);
  havePeer = false;
  nonce[0] = 0;
  qc = 0;
  qh = 0;
  consecFail = 0;
  helloPending = false;
  helloMiss = 0;
  pHeld = false;
  if (wst == W_UP || wst == W_NOW) lnk = haveKey ? L_DISCOVER : L_NOKEY;
#ifdef T5R_ESPNOW
  nowT = millis();
#endif
}

static void tx(const IPAddress &to, const char *ev, uint32_t seq, const char *arg, uint8_t tryN) {
  char b[512];
  int n = snprintf(b, sizeof b - 32, "{\"v\":1,\"dev\":\"%s\",\"seq\":%u,\"ev\":\"%s\",\"arg\":%s", dev, (unsigned)seq, ev, arg);
  if (nonce[0] && strcmp(ev, "hello")) n += snprintf(b + n, sizeof b - 32 - n, ",\"n\":\"%s\"", nonce);
  if (st.rttLastUs) n += snprintf(b + n, sizeof b - 32 - n, ",\"lr\":%u", (unsigned)st.rttLastUs);
  if (tryN) n += snprintf(b + n, sizeof b - 32 - n, ",\"try\":%u", tryN);
  char tag[17];
  hmacTag((const uint8_t *)b, n, tag);
  n += snprintf(b + n, sizeof b - n, ",\"m\":\"%s\"}", tag);
#ifdef T5R_ESPNOW
  if (viaNow) { esp_now_send(nowPeer, (const uint8_t *)b, n); return; }   // unicast to the bridge: MAC-level ack + retries
#endif
  udp.beginPacket(to, PORT);
  udp.write((const uint8_t *)b, n);
  udp.endPacket();
}

static void sendHello() {
  char arg[120];
  snprintf(arg, sizeof arg, "{\"kind\":\"%s\",\"name\":\"%s\",\"fw\":\"%s\",\"boot\":%u}", kind, devName, FW, (unsigned)bootId);
  helloSeq = ++seqCtr;
#ifdef T5R_ESPNOW
  if (viaNow) { tx(peer, "hello", helloSeq, arg, 0); helloTx = millis(); helloPending = true; return; }
#endif
  if (lnk == L_LINKED && havePeer) {
    tx(peer, "hello", helloSeq, arg, 0);
  } else {
    tx(IPAddress(255, 255, 255, 255), "hello", helloSeq, arg, 0);
    IPAddress bc = WiFi.broadcastIP();
    if (bc != IPAddress(255, 255, 255, 255)) tx(bc, "hello", helloSeq, arg, 0);
    if (haveHint) tx(hint, "hello", helloSeq, arg, 0);
  }
  helloTx = millis();
  helloPending = true;
}

static void parseState(const char *s) {
  char v[12];
  long long x;
  if (jstr(s, "st", v, sizeof v)) strlcpy(stt.st, v, sizeof stt.st);
  if (jstr(s, "pair", v, sizeof v)) strlcpy(stt.pair, v, sizeof stt.pair);
  if (jnum(s, "vol", &x)) stt.vol = (int)x;
  if (jnum(s, "side", &x)) stt.side = (int)x;
}

static void handle(char *b, int n, const IPAddress &from);

static void rx() {
  int len = udp.parsePacket();
  if (len <= 0) return;
  char b[512];
  IPAddress from = udp.remoteIP();
  int n = udp.read((uint8_t *)b, sizeof b - 1);
  if (n <= 0) return;
  b[n] = 0;
  handle(b, n, from);
}

#ifdef T5R_ESPNOW
static void nowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {   // Wi-Fi task: queue only
  if (len >= 14 && !memcmp(data, "{\"t5b\":\"pong\"", 13)) {
    char t[48];
    int k = len < (int)sizeof t - 1 ? len : (int)sizeof t - 1;
    memcpy(t, data, k);
    t[k] = 0;
    const char *c = strstr(t, "\"ch\":");
    memcpy(pongMac, info->src_addr, 6);
    pongCh = c ? (uint8_t)atoi(c + 5) : 0;
    pongSeen = true;
    return;
  }
  if (!nowQ || len <= 0 || len >= 512) return;
  NowRx r;
  r.len = len;
  r.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  memcpy(r.d, data, len);
  r.d[len] = 0;
  xQueueSend(nowQ, &r, 0);
}

static void rxNow() {
  static NowRx r;
  while (nowQ && xQueueReceive(nowQ, &r, 0) == pdTRUE) {
    nowRssi = r.rssi;
    handle(r.d, r.len, IPAddress(0, 0, 0, 0));
  }
}

// Ping on every channel; lock on the first bridge that answers. Blocks ~0.4 s; only called while not linked.
static bool nowSweep() {
  if (!nowUp) {
    if (!WiFi.STA.started()) return false;
    if (!nowQ) nowQ = xQueueCreate(4, sizeof(NowRx));
    if (!nowQ || esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(nowRecv);
    esp_now_peer_info_t p = {};
    memset(p.peer_addr, 0xFF, 6);
    p.channel = 0;
    p.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&p);
    nowUp = true;
  }
  static const char PING[] = "{\"t5b\":\"ping\"}";
  uint8_t bc[6];
  memset(bc, 0xFF, 6);
  nowSweeps++;
  for (uint8_t ch = 1; ch <= 13; ch++) {
    if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) continue;
    pongSeen = false;
    esp_now_send(bc, (const uint8_t *)PING, sizeof PING - 1);
    uint32_t t0 = millis();
    while (millis() - t0 < 28 && !pongSeen) delay(1);
    if (!pongSeen) continue;
    uint8_t c = (pongCh >= 1 && pongCh <= 13) ? pongCh : ch;   // the bridge says which channel it really sits on
    esp_wifi_set_channel(c, WIFI_SECOND_CHAN_NONE);
    memcpy(nowPeer, pongMac, 6);
    nowCh = c;
    if (!esp_now_is_peer_exist(nowPeer)) {
      esp_now_peer_info_t p = {};
      memcpy(p.peer_addr, nowPeer, 6);
      p.channel = 0;
      p.ifidx = WIFI_IF_STA;
      esp_now_add_peer(&p);
    }
    return true;
  }
  return false;
}
#endif

static void handle(char *b, int n, const IPAddress &from) {
  uint32_t nowUs = micros();
  // tag: ...,"m":"<16 hex>"}  must be the tail
  if (n < 30 || strcmp(b + n - 2, "\"}")) return;
  char *mp = nullptr;
  for (char *p = b; (p = strstr(p, ",\"m\":\"")); p++) mp = p;
  if (!mp || (b + n) - mp != 6 + 16 + 2) return;
  char tag[17];
  hmacTag((const uint8_t *)b, mp - b, tag);
  uint8_t diff = 0;
  for (int i = 0; i < 16; i++) diff |= tag[i] ^ mp[6 + i];
  if (diff) return;
  *mp = 0;
  char v[24];
  if (!jstr(b, "ev", v, sizeof v) || strcmp(v, "ack")) return;
  if (!jstr(b, "dev", v, sizeof v) || strcmp(v, dev)) return;
  long long seq = 0, x;
  if (!jnum(b, "seq", &seq)) return;
  bool ok = strstr(b, "\"ok\":true") != nullptr;
  char err[12] = "";
  jstr(b, "err", err, sizeof err);

  if (helloPending && (uint32_t)seq == helloSeq) {
    helloPending = false;
    if (ok) {
      if (!jnum(b, "boot", &x) || (uint32_t)x != bootId) return;   // stale / replayed hello ack
      char nn[12];
      if (!jstr(b, "n", nn, sizeof nn)) return;
      bool fresh = lnk != L_LINKED;
      strlcpy(nonce, nn, sizeof nonce);
      peer = from;
      havePeer = true;
      helloMiss = 0;
      lnk = L_LINKED;
      parseState(b);
#ifdef T5R_ESPNOW
      if (fresh && viaNow) say("log linked via espnow ch=%u", nowCh);
      else
#endif
      if (fresh) say("log linked peer=%s", from.toString().c_str());
    } else if (!strcmp(err, "pair")) {
      if (lnk != L_PAIR) say("log pairing: accept %s on the Tab5", dev);
      lnk = L_PAIR;
    }
    return;
  }
  if (qc && (uint32_t)seq == q[qh].seq && q[qh].tries) {
    Ev &e = q[qh];
    if (ok) {
      uint32_t rtt = nowUs - e.firstTxUs;
      st.acked++;
      st.rttLastUs = rtt ? rtt : 1;
      st.rttSumUs += rtt;
      if (rtt < st.rttMinUs) st.rttMinUs = rtt;
      if (rtt > st.rttMaxUs) st.rttMaxUs = rtt;
      consecFail = 0;
      parseState(b);
      say("log ack seq=%u ev=%s rtt_us=%u tries=%u", (unsigned)e.seq, e.ev, (unsigned)rtt, e.tries);
      qh = (qh + 1) % QN;
      qc--;
    } else {
      st.rejected++;
      say("log nack seq=%u ev=%s err=%s", (unsigned)e.seq, e.ev, err);
      qh = (qh + 1) % QN;
      qc--;
      if (!strcmp(err, "nonce") || !strcmp(err, "pair")) unlink(err);
    }
  }
}

static void pump() {
  if (lnk != L_LINKED || !qc) return;
  Ev &e = q[qh];
  uint32_t now = millis();
  if (e.tries && now - e.lastTx < RETRY_MS) return;
  if (e.tries >= e.maxTries) {
    st.lost++;
    say("log lost seq=%u ev=%s tries=%u", (unsigned)e.seq, e.ev, e.tries);
    qh = (qh + 1) % QN;
    qc--;
    if (++consecFail >= 3) unlink("3 events without ack");
    return;
  }
  if (!e.tries) e.firstTxUs = micros();
  tx(peer, e.ev, e.seq, e.arg, e.tries);
  e.tries++;
  e.lastTx = now;
}

bool send(const char *ev, const char *arg, uint8_t tries) {
  if (lnk != L_LINKED || qc >= QN) {
    st.dropped++;
    return false;
  }
  Ev &e = q[(qh + qc) % QN];
  e.seq = ++seqCtr;
  strlcpy(e.ev, ev, sizeof e.ev);
  strlcpy(e.arg, arg, sizeof e.arg);
  e.tries = 0;
  e.maxTries = tries;
  e.lastTx = 0;
  qc++;
  st.sent++;
  pump();
  return true;
}

void ptt(bool down, int side, const char *shortEv, bool synthetic) {
  char a[64];
  uint32_t now = millis();
  if (down) {
    if (pHeld) return;
    snprintf(a, sizeof a, synthetic ? "{\"side\":%d,\"syn\":1}" : "{\"side\":%d}", side);
    if (!send("ptt_down", a)) return;
    pHeld = true;
    pSide = side;
    pSyn = synthetic;
    pT0 = pRep = now;
  } else {
    if (!pHeld) return;
    pHeld = false;
    uint32_t ms = now - pT0;
    bool cancel = ms < 250;
    snprintf(a, sizeof a, "{\"side\":%d,\"ms\":%u%s%s}", pSide, (unsigned)ms, cancel ? ",\"cancel\":true" : "", pSyn ? ",\"syn\":1" : "");
    send("ptt_up", a, 20);
    if (cancel && shortEv) send(shortEv, "{}");
  }
}

bool pttHeld() { return pHeld; }
bool busy() { return qc > 0; }
Link link() { return lnk; }
const State &state() { return stt; }
const Stats &stats() { return st; }
const char *devId() { return dev; }
const char *ssid() { return curSsid; }
int rssi() {
#ifdef T5R_ESPNOW
  if (wst == W_NOW) return nowRssi;
#endif
  return wst == W_UP ? WiFi.RSSI() : 0;
}
void setBattery(int pct, int mv, bool chg) { batPct = pct; batMv = mv; batChg = chg; }
void setExtraInfo(const char *json) { strlcpy(extra, json, sizeof extra); }

// ---------- Wi-Fi state machine ----------
static void wifiLoop() {
  uint32_t now = millis();
  switch (wst) {
    case W_SCAN_START:
#ifdef T5R_ESPNOW
      viaNow = false;
      if (nowMode && haveKey && (int32_t)(now - nowSkipUntil) >= 0 && nowSweep()) {
        viaNow = true;
        wst = W_NOW;
        lnk = L_DISCOVER;
        helloTx = 0;
        nowT = millis();
        snprintf(curSsid, sizeof curSsid, "ESP-NOW ch%u", nowCh);
        say("log espnow bridge found ch=%u mac=%02x%02x%02x%02x%02x%02x", nowCh, nowPeer[0], nowPeer[1], nowPeer[2], nowPeer[3], nowPeer[4], nowPeer[5]);
        break;
      }
#endif
      if (!slotCount()) { lnk = haveKey ? L_NOWIFI : L_NOKEY; wst = W_WAIT; wT = now; break; }
      WiFi.scanDelete();
      WiFi.scanNetworks(true, false);
      wst = W_SCANNING;
      wT = now;
      lnk = haveKey ? L_NOWIFI : L_NOKEY;
      break;
    case W_SCANNING: {
      int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) { if (now - wT > 15000) { wst = W_WAIT; wT = now; } break; }
      if (n < 0) { wst = W_WAIT; wT = now; break; }
      lastScanN = n;
      lastScanKnown = 0;
      int best = -999, bi = -1;
      char ss[33], pw[65];
      for (int i = 0; i < SLOTS; i++) {
        if (!slot(i, ss, sizeof ss, pw, sizeof pw)) continue;
        for (int a = 0; a < n; a++)
          if (WiFi.SSID(a) == ss) { lastScanKnown++; if (WiFi.RSSI(a) > best) { best = WiFi.RSSI(a); bi = i; } break; }
      }
      if (bi < 0) { say("log scan: %d APs, no provisioned SSID visible", n); memset(pw, 0, sizeof pw); wst = W_WAIT; wT = now; break; }
      slot(bi, ss, sizeof ss, pw, sizeof pw);
      strlcpy(curSsid, ss, sizeof curSsid);
      say("log joining slot %d ssid_len=%d rssi=%d", bi, (int)strlen(ss), best);
      WiFi.begin(ss, pw);
      memset(pw, 0, sizeof pw);
      wst = W_JOINING;
      wT = now;
      break;
    }
    case W_JOINING:
      if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false);
        udp.stop();
        udp.begin(PORT);
        wst = W_UP;
        wDownSince = 0;
        lnk = haveKey ? L_DISCOVER : L_NOKEY;
        helloTx = 0;
        say("log wifi up ip=%s rssi=%d ch=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
      } else if (now - wT > 15000) {
        say("log join timeout status=%d", (int)WiFi.status());
        WiFi.disconnect();
        wst = W_WAIT;
        wT = now;
      }
      break;
    case W_UP:
      if (WiFi.status() != WL_CONNECTED) {
        if (!wDownSince) wDownSince = now;
        if (now - wDownSince > 5000) {
          say("log wifi lost");
          unlink("wifi");
          udp.stop();
          WiFi.disconnect();
          lnk = haveKey ? L_NOWIFI : L_NOKEY;
          wst = W_WAIT;
          wT = now;
        }
      } else wDownSince = 0;
      break;
    case W_WAIT:
      if (now - wT > 4000) wst = W_SCAN_START;
      break;
#ifdef T5R_ESPNOW
    case W_NOW:
      // A bridge answered the ping but the Tab5 behind it stays silent for 6 s (bridge not wired, Tab5 off):
      // give Wi-Fi/UDP its turn and do not look for a bridge again for a minute.
      if (lnk == L_DISCOVER && now - nowT > 6000) {
        say("log espnow: bridge answers, Tab5 does not - trying Wi-Fi");
        viaNow = false;
        nowSkipUntil = now + 60000;
        wst = W_SCAN_START;
      }
      break;
#endif
  }
}

// ---------- serial console ----------
static void info() {
  uint8_t h[32];
  char kh[9] = "-";
  if (haveKey) {
    mbedtls_sha256(key, 16, h, 0);
    snprintf(kh, sizeof kh, "%02x%02x%02x%02x", h[0], h[1], h[2], h[3]);
  }
  static const char *LN[] = {"nokey", "nowifi", "discover", "pair", "linked"};
  say("info {\"dev\":\"%s\",\"fw\":\"%s\",\"link\":\"%s\",\"key\":%s,\"key_sha\":\"%s\",\"slots\":%d,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"ch\":%d,"
      "\"scan\":%d,\"scan_known\":%d,\"peer\":\"%s\",\"hint\":\"%s\",\"sent\":%u,\"acked\":%u,\"lost\":%u,\"rejected\":%u,\"dropped\":%u,"
      "\"rtt_us\":{\"last\":%u,\"min\":%u,\"avg\":%u,\"max\":%u},\"state\":{\"st\":\"%s\",\"pair\":\"%s\",\"vol\":%d},\"bat\":%d,\"up_s\":%u%s%s}",
      dev, FW, LN[lnk], haveKey ? "true" : "false", kh, slotCount(), wst == W_UP ? curSsid : "", wst == W_UP ? WiFi.localIP().toString().c_str() : "",
      rssi(), wst == W_UP ? WiFi.channel() : 0, lastScanN, lastScanKnown, havePeer ? peer.toString().c_str() : "", haveHint ? hint.toString().c_str() : "",
      (unsigned)st.sent, (unsigned)st.acked, (unsigned)st.lost, (unsigned)st.rejected, (unsigned)st.dropped,
      (unsigned)st.rttLastUs, st.acked ? (unsigned)st.rttMinUs : 0, st.acked ? (unsigned)(st.rttSumUs / st.acked) : 0, (unsigned)st.rttMaxUs,
      stt.st, stt.pair, stt.vol, batPct, (unsigned)(millis() / 1000), extra[0] ? "," : "", extra);
#ifdef T5R_ESPNOW
  // its own short line: the info line above already fills the console buffer, and a line that does not fit is dropped
  say("now {\"via\":\"%s\",\"mode\":%u,\"ch\":%u,\"sweeps\":%u,\"rssi\":%d,\"sent\":%u,\"acked\":%u,\"lost\":%u,\"rtt_us\":{\"min\":%u,\"avg\":%u,\"max\":%u}}",
      viaNow ? "espnow" : "udp", nowMode, nowCh, (unsigned)nowSweeps, nowRssi, (unsigned)st.sent, (unsigned)st.acked, (unsigned)st.lost,
      st.acked ? (unsigned)st.rttMinUs : 0, st.acked ? (unsigned)(st.rttSumUs / st.acked) : 0, (unsigned)st.rttMaxUs);
#endif
}

static void cmd(char *l) {
  if (strncmp(l, "T5R ", 4)) return;
  l += 4;
  if (!strncmp(l, "key ", 4)) {
    const char *hx = l + 4;
    uint8_t k[16];
    if (strlen(hx) != 32) { say("err key: need 32 hex chars (got len=%d)", (int)strlen(hx)); return; }
    for (int i = 0; i < 16; i++) {
      char t[3] = {hx[2 * i], hx[2 * i + 1], 0};
      char *end;
      k[i] = (uint8_t)strtoul(t, &end, 16);
      if (*end) { say("err key: not hex"); return; }
    }
    prefs.putBytes("key", k, 16);
    memset(k, 0, sizeof k);
    memset(l, 0, strlen(l));
    loadKey();
    uint8_t h[32];
    mbedtls_sha256(key, 16, h, 0);
    say("ok key len=16 sha=%02x%02x%02x%02x", h[0], h[1], h[2], h[3]);
    unlink("new key");
    if (wst == W_UP) lnk = L_DISCOVER;
  } else if (!strncmp(l, "wifi ", 5)) {            // wifi <idx> <ssid>\t<psk>   (ssid "-" clears the slot)
    int i = l[5] - '0';
    char *s = l + 7, *tab = strchr(l + 7, '\t');
    if (i < 0 || i >= SLOTS || l[6] != ' ') { say("err wifi: idx 0-%d", SLOTS - 1); return; }
    char ks[4], kp[4];
    snprintf(ks, sizeof ks, "s%d", i);
    snprintf(kp, sizeof kp, "p%d", i);
    if (!strcmp(s, "-")) { prefs.remove(ks); prefs.remove(kp); say("ok wifi %d cleared", i); return; }
    if (!tab) { say("err wifi: need <ssid><TAB><psk>"); return; }
    *tab = 0;
    prefs.putString(ks, s);
    prefs.putString(kp, tab + 1);
    say("ok wifi %d ssid_len=%d psk_len=%d", i, (int)strlen(s), (int)strlen(tab + 1));
    memset(tab + 1, 0, strlen(tab + 1));
  } else if (!strncmp(l, "host ", 5)) {
    if (!strcmp(l + 5, "-")) { prefs.remove("host"); haveHint = false; say("ok host cleared"); }
    else if (hint.fromString(l + 5)) { prefs.putString("host", l + 5); haveHint = true; say("ok host %s", hint.toString().c_str()); }
    else say("err host: not an IPv4 address");
  } else if (!strncmp(l, "name ", 5)) {
    strlcpy(devName, l + 5, sizeof devName);
    for (char *p = devName; *p; p++) if (*p == '"' || *p == '\\' || (uint8_t)*p < 0x20) *p = '_';
    prefs.putString("name", devName);
    say("ok name %s", devName);
  } else if (!strncmp(l, "set ", 4)) {             // set <key> <int>  (device-specific tunables, e.g. det, dir)
    char *sp = strchr(l + 4, ' ');
    if (!sp || sp - (l + 4) > 6) { say("err set"); return; }
    *sp = 0;
    prefs.putInt(l + 4, atoi(sp + 1));
    say("ok set %s=%d (reboot to apply)", l + 4, atoi(sp + 1));
  } else if (!strcmp(l, "info")) info();
#ifdef T5R_ESPNOW
  else if (!strcmp(l, "now 0") || !strcmp(l, "now 1")) { nowMode = l[4] - '0'; prefs.putUChar("now", nowMode); nowSkipUntil = 0; say("ok now %u", nowMode); }
#endif
  else if (!strcmp(l, "rejoin")) {
    unlink("rejoin"); udp.stop(); WiFi.disconnect(); wst = W_WAIT; wT = millis() - 3500;
#ifdef T5R_ESPNOW
    viaNow = false; nowSkipUntil = 0;
#endif
    say("ok rejoin");
  }
  else if (!strncmp(l, "bench ", 6)) { benchLeft = constrain(atoi(l + 6), 0, 200); benchDown = false; benchT = millis(); say("ok bench %d", benchLeft); }
  else if (!strcmp(l, "reboot")) { say("ok reboot"); delay(100); ESP.restart(); }
  else say("err unknown command");
}

static void consoleLoop() {
  static char line[200];
  static size_t n = 0;
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (n) { line[n] = 0; cmd(line); memset(line, 0, sizeof line); n = 0; }
    } else if (n + 1 < sizeof line) line[n++] = (char)c;
  }
}

int prefInt(const char *k, int def) { return prefs.getInt(k, def); }

void begin(const char *k, const char *nm) {
  strlcpy(kind, k, sizeof kind);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(dev, sizeof dev, "%s-%02x%02x", kind, mac[4], mac[5]);
  prefs.begin("t5r", false);
  strlcpy(devName, prefs.getString("name", nm).c_str(), sizeof devName);
  loadKey();
#ifdef T5R_ESPNOW
  nowMode = prefs.getUChar("now", 1);
#endif
  String h = prefs.getString("host", "");
  haveHint = h.length() && hint.fromString(h);
  bootId = esp_random() | 1;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  lnk = haveKey ? L_NOWIFI : L_NOKEY;
  say("ready dev=%s fw=%s key=%d slots=%d", dev, FW, haveKey ? 1 : 0, slotCount());
}

void loop() {
  consoleLoop();
  wifiLoop();
  if (wst != W_UP && wst != W_NOW) return;
  if (!haveKey) { lnk = L_NOKEY; return; }
#ifdef T5R_ESPNOW
  if (wst == W_NOW) rxNow(); else
#endif
  rx();
  uint32_t now = millis();
  uint32_t every = lnk == L_LINKED ? 5000 : (lnk == L_PAIR ? 2000 : 1000);
  if (!helloTx || now - helloTx >= every) {
    if (helloPending && lnk == L_LINKED && ++helloMiss >= 2) unlink("2 hellos without ack");
    if (!(lnk == L_LINKED && qc)) sendHello();   // do not interleave link checks with a press in flight
    else helloTx = now;
  }
  if (pHeld && now - pRep >= 500) {
    char a[48];
    snprintf(a, sizeof a, "{\"side\":%d,\"rep\":1}", pSide);
    send("ptt_down", a, 3);
    pRep = now;
  }
  if (lnk == L_LINKED && now - batT >= 60000) {
    char a[64];
    snprintf(a, sizeof a, "{\"pct\":%d,\"mv\":%d,\"chg\":%s}", batPct, batMv, batChg ? "true" : "false");
    send("battery", a, 3);
    batT = now;
  }
  if (benchLeft && lnk == L_LINKED) {
    if (!benchDown && now - benchT >= 400) { ptt(true, 0, nullptr, true); benchDown = pHeld; benchT = now; }
    else if (benchDown && now - benchT >= 300) { ptt(false, 0, nullptr, true); benchDown = false; benchT = now; if (!--benchLeft) say("log bench done"); }
  }
  pump();
}

}  // namespace t5r
