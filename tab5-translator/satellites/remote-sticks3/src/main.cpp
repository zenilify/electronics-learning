// PAPP remote — StickS3 push-to-talk clicker (protocol v1, ../PROTOCOL.md).
// KEY1 (G11): hold = talk (side 0), short press = replay.  KEY2 (G12): hold = talk (side 1), short press = next pair.
// Board facts (measured 2026-09-27, home-lab firmware/sticks3-bridge/README.md): ESP32-S3-PICO-1 8 MB, KEY1 G11, KEY2 G12, ST7789P3 135x240.
#include <M5Unified.h>
#include "t5r.h"

static const int K1 = 11, K2 = 12;

struct Btn {
  int pin;
  bool st, raw;
  uint32_t t;
  bool update() {                       // true on a debounced edge
    bool r = digitalRead(pin) == LOW;
    uint32_t now = millis();
    if (r != raw) { raw = r; t = now; }
    if (raw != st && now - t >= 15) { st = raw; return true; }
    return false;
  }
};
static Btn k1 = {K1, false, false, 0}, k2 = {K2, false, false, 0};
static int active = 0;                  // 0 none, 1 = KEY1 holds the PTT, 2 = KEY2
static uint32_t batT = 0;
static int batPct = -1;

static void draw() {
  static char last[96] = "";
  char now[96];
  const t5r::State &s = t5r::state();
  t5r::Link l = t5r::link();
  bool talk = t5r::pttHeld();
  snprintf(now, sizeof now, "%d|%s|%s|%d|%d|%d|%s", (int)l, s.st, s.pair, s.vol, batPct, talk ? 1 + active : 0, t5r::ssid());
  if (!strcmp(now, last)) return;
  strcpy(last, now);
  auto &d = M5.Display;
  static const char *LT[] = {"Sin clave", "Sin Wi-Fi", "Buscando Tab5", "Aceptar en Tab5", "Tab5 OK"};
  static const uint16_t LC[] = {TFT_RED, TFT_RED, TFT_ORANGE, TFT_ORANGE, TFT_GREEN};
  uint16_t bg = talk ? (active == 2 ? TFT_NAVY : TFT_MAROON) : TFT_BLACK;
  d.startWrite();
  d.fillScreen(bg);
  d.setTextDatum(top_center);
  d.setTextColor(TFT_WHITE, bg);
  d.setFont(&fonts::Font2);
  d.drawString("PAPP remote", d.width() / 2, 6);
  d.fillCircle(d.width() / 2, 44, 10, LC[l]);
  d.drawString(LT[l], d.width() / 2, 62);
  d.setTextColor(TFT_LIGHTGREY, bg);
  d.drawString(t5r::ssid(), d.width() / 2, 82);
  d.setTextColor(TFT_WHITE, bg);
  d.setFont(&fonts::Font4);
  d.drawString(l == t5r::L_LINKED ? s.pair : "--", d.width() / 2, 112);
  d.drawString(talk ? (active == 2 ? "OTRO" : "HABLA") : (l == t5r::L_LINKED ? s.st : ""), d.width() / 2, 150);
  d.setFont(&fonts::Font2);
  char b[32];
  if (s.vol >= 0 && l == t5r::L_LINKED) { snprintf(b, sizeof b, "vol %d", s.vol); d.drawString(b, d.width() / 2, 188); }
  if (batPct >= 0) snprintf(b, sizeof b, "bat %d%%", batPct); else snprintf(b, sizeof b, "bat --");
  d.drawString(b, d.width() / 2, 212);
  d.endWrite();
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;
  cfg.internal_imu = false;
  cfg.internal_mic = false;
  cfg.internal_spk = false;
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);             // never block when no host is reading the USB port
  pinMode(K1, INPUT_PULLUP);
  pinMode(K2, INPUT_PULLUP);
  M5.Display.setRotation(0);
  M5.Display.setBrightness(120);
  t5r::begin("stick", "Stick PTT");
  char x[64];
  snprintf(x, sizeof x, "\"board\":%d,\"lcd\":\"%dx%d\"", (int)M5.getBoard(), (int)M5.Display.width(), (int)M5.Display.height());
  t5r::setExtraInfo(x);
}

void loop() {
  if (k1.update()) {
    if (k1.st && !active) { t5r::ptt(true, 0); if (t5r::pttHeld()) active = 1; }
    else if (!k1.st && active == 1) { t5r::ptt(false, 0, "replay"); active = 0; }
  }
  if (k2.update()) {
    if (k2.st && !active) { t5r::ptt(true, 1); if (t5r::pttHeld()) active = 2; }
    else if (!k2.st && active == 2) { t5r::ptt(false, 1, "pair_next"); active = 0; }
  }
  if (active && !t5r::pttHeld()) active = 0;      // link dropped while holding
  t5r::loop();
  uint32_t now = millis();
  if (!batT || now - batT >= 10000) {
    batT = now ? now : 1;
    batPct = M5.Power.getBatteryLevel();
    if (batPct < 0 || batPct > 100) batPct = -1;
    t5r::setBattery(batPct, M5.Power.getBatteryVoltage(), M5.Power.isCharging() == m5::Power_Class::is_charging);
  }
  if (!t5r::busy()) draw();            // a full redraw blocks ~25 ms; never let it sit between a press and its ack
  delay(1);
}
