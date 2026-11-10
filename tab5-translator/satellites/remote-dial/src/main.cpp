// PAPP remote — M5Dial (protocol v1, ../PROTOCOL.md).
// Knob press: hold = talk (side 0), short press = replay.  Rotate = next / previous language pair.
// Long touch on the screen (>= 0.6 s) = volume mode (rotate = volume), back to pair mode after 4 s idle.
// Pins from M5Stack's M5Dial library (not measured here until first boot): encoder A G41 / B G40, knob button G42. NFC not used.
#include <M5Unified.h>
#include <driver/gpio.h>
#include "t5r.h"

static const int ENC_A = 41, ENC_B = 40, BTN = 42;
static volatile int32_t enc = 0;
static volatile uint8_t encSt = 0;
static const int8_t QTAB[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void IRAM_ATTR encIsr() {
  uint8_t s = (gpio_get_level((gpio_num_t)ENC_A) << 1) | gpio_get_level((gpio_num_t)ENC_B);
  encSt = ((encSt << 2) | s) & 0x0F;
  enc += QTAB[encSt];
}

static int det = 4, dir = 1;            // counts per detent / direction; tunable: "T5R set det N", "T5R set dir -1"
static int32_t encBase = 0;
static bool bSt = false, bRaw = false;
static uint32_t bT = 0;
static bool volMode = false;
static uint32_t volT = 0;
static uint32_t touchT0 = 0;
static bool touchDown = false, touchFired = false;

static void draw() {
  static char last[96] = "";
  char now[96];
  const t5r::State &s = t5r::state();
  t5r::Link l = t5r::link();
  bool talk = t5r::pttHeld();
  snprintf(now, sizeof now, "%d|%s|%s|%d|%d|%d", (int)l, s.st, s.pair, s.vol, talk, volMode);
  if (!strcmp(now, last)) return;
  strcpy(last, now);
  auto &d = M5.Display;
  static const char *LT[] = {"Sin clave", "Sin Wi-Fi", "Buscando Tab5", "Aceptar en Tab5", "Tab5 OK"};
  uint16_t ring = TFT_DARKGREY;         // the "ring" is drawn on the round LCD (no LED ring found in the vendor pin list)
  if (l == t5r::L_NOKEY || l == t5r::L_NOWIFI) ring = TFT_RED;
  else if (l != t5r::L_LINKED) ring = TFT_ORANGE;
  else if (talk || !strcmp(s.st, "listen")) ring = TFT_RED;
  else if (!strcmp(s.st, "speak") || !strcmp(s.st, "busy")) ring = TFT_BLUE;
  else ring = TFT_GREEN;
  int cx = d.width() / 2, cy = d.height() / 2;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.fillArc(cx, cy, cx, cx - 14, 0, 360, ring);
  d.setTextDatum(middle_center);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setFont(&fonts::Font2);
  d.drawString(LT[l], cx, cy - 62);
  d.setFont(&fonts::Font4);
  char b[32];
  if (l != t5r::L_LINKED) d.drawString("--", cx, cy - 8);
  else if (volMode) { snprintf(b, sizeof b, "VOL %d", s.vol); d.drawString(b, cx, cy - 8); }
  else d.drawString(s.pair, cx, cy - 8);
  d.drawString(talk ? "HABLA" : (l == t5r::L_LINKED ? s.st : ""), cx, cy + 30);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.drawString(volMode ? "girar = volumen" : "girar = idioma", cx, cy + 66);
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
  Serial.setTxTimeoutMs(0);
  pinMode(BTN, INPUT_PULLUP);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  encSt = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(ENC_A, encIsr, CHANGE);
  attachInterrupt(ENC_B, encIsr, CHANGE);
  M5.Display.setBrightness(120);
  t5r::begin("dial", "Dial");
  det = constrain(t5r::prefInt("det", 4), 1, 8);
  dir = t5r::prefInt("dir", 1) < 0 ? -1 : 1;
}

void loop() {
  uint32_t now = millis();
  // knob button
  bool r = digitalRead(BTN) == LOW;
  if (r != bRaw) { bRaw = r; bT = now; }
  if (bRaw != bSt && now - bT >= 15) {
    bSt = bRaw;
    t5r::ptt(bSt, 0, "replay");
  }
  // rotation: one event per detent
  int32_t e = enc;
  int32_t steps = (e - encBase) / det;
  if (steps) {
    encBase += steps * det;
    steps *= dir;
    for (int i = 0; i < abs(steps) && i < 6; i++) {
      if (volMode) t5r::send(steps > 0 ? "vol_up" : "vol_down", "{\"step\":5}");
      else t5r::send(steps > 0 ? "pair_next" : "pair_prev", "{}");
    }
    if (volMode) volT = now;
  }
  // touch: long touch = volume mode
  M5.update();
  bool td = M5.Touch.getCount() > 0;
  if (td && !touchDown) { touchDown = true; touchT0 = now; touchFired = false; }
  if (!td) touchDown = false;
  if (touchDown && !touchFired && now - touchT0 >= 600) { touchFired = true; volMode = !volMode; volT = now; }
  if (volMode && now - volT >= 4000) volMode = false;

  static uint32_t xT = 0;
  if (now - xT >= 500) {
    xT = now;
    char x[96];
    snprintf(x, sizeof x, "\"board\":%d,\"enc\":%ld,\"det\":%d,\"dir\":%d,\"btn\":%d,\"touch\":%d", (int)M5.getBoard(), (long)e, det, dir, bSt, (int)td);
    t5r::setExtraInfo(x);
    t5r::setBattery(-1, 0, false);
  }
  t5r::loop();
  if (!t5r::busy()) draw();            // a full redraw blocks ~25 ms; never let it sit between a press and its ack
  delay(1);
}
