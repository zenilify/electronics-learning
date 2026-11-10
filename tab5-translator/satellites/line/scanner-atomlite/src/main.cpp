// scanner — fixed QR scanner of the Traceability Cell (lane T5-I).
// AtomS3 Lite on the Atomic QRCode2 Base (UART 115200; host RX = G5, TX = G6 — measured 2026-11-10: begin() fails on the swapped order).
//   every decoded code  -> POST /scan {code, station, kind:"qr", result:"ok"}
//   button (G41) once   -> the NEXT scan is posted as result:"scrap", reason S01 (LED red while armed; press again = disarm)
//   the same code inside 3 s is ignored (the scanner repeats while the label stays in view)
// RGB LED G35: green ok · red scrap / armed · yellow duplicate or FIFO warning · blue tint = MOCK server · purple = no server
// Serial console (USB): type a code + Enter to inject a scan without the optics ("!code" = scrap) — for bench tests.
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <M5UnitQRCode.h>
#include "line_net.h"

#define PIN_BTN 41
#define PIN_LED 35
#define QR_RX 5
#define QR_TX 6

static M5UnitQRCodeUART qr;
static bool scrapArmed = false, qrOk = false;
static String lastCode;
static uint32_t lastAt = 0, nOk = 0, nScrap = 0, nFail = 0, rxBytes = 0;

static void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_LED, r, g, b); }
static void idle() {
  if (scrapArmed) led(40, 0, 0);
  else if (!lineUp()) led(10, 0, 10);
  else if (lineIsMock()) led(0, 2, 10);
  else led(0, 6, 0);
}

static void handle(String code, bool scrap) {
  code.trim();
  if (!code.length()) return;
  if (code == lastCode && millis() - lastAt < 3000) return;
  lastCode = code; lastAt = millis();
  String rep;
  int c = lineScan(code, "qr", scrap ? "scrap" : "ok", scrap ? "S01" : "", "", &rep);
  if (c == 200) {
    (scrap ? nScrap : nOk)++;
    bool warn = jtrue(rep, "duplicate") || jtrue(rep, "fifo_warning");
    Serial.printf("[scan] %s %s seq=%ld dup=%d fifo=%d good=%ld scrap=%ld | sent ok=%lu scrap=%lu fail=%lu\n",
                  scrap ? "SCRAP" : "OK", code.c_str(), jnum(rep, "seq"), jtrue(rep, "duplicate"),
                  jtrue(rep, "fifo_warning"), jnum(rep, "good"), jnum(rep, "scrap"),
                  (unsigned long)nOk, (unsigned long)nScrap, (unsigned long)nFail);
    if (warn) led(50, 35, 0); else if (scrap) led(60, 0, 0); else led(0, 60, 0);
  } else {
    nFail++;
    Serial.printf("[scan] POST failed %d for %s\n", c, code.c_str());
    for (int i = 0; i < 3; i++) { led(40, 0, 40); delay(80); led(0, 0, 0); delay(80); }
  }
  delay(250);
  scrapArmed = false;
  idle();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN, INPUT_PULLUP);
  led(10, 10, 0);
  delay(800);
  Serial.println("\n[scan] T5-I scanner station=" LINE_STATION);
  int rx = QR_RX, tx = QR_TX;
  for (int k = 0; k < 4 && !qrOk; k++) {       // try the documented order, then the swapped one, twice
    rx = (k & 1) ? QR_TX : QR_RX; tx = (k & 1) ? QR_RX : QR_TX;
    Serial1.end();
    qrOk = qr.begin(&Serial1, 115200, rx, tx);
    Serial.printf("[scan] qrcode2 begin=%d (rx=G%d tx=G%d)\n", qrOk, rx, tx);
    if (!qrOk) delay(300);
  }
  if (qrOk) qr.setTriggerMode(AUTO_SCAN_MODE);
  lineBegin();
  idle();
}

void loop() {
  static bool was = false;
  bool down = digitalRead(PIN_BTN) == LOW;
  if (down && !was) { scrapArmed = !scrapArmed; Serial.printf("[scan] scrap armed=%d\n", scrapArmed); idle(); delay(30); }
  was = down;

  if (qr.available()) {
    String d = qr.getDecodeData();
    rxBytes += d.length();
    handle(d, scrapArmed);
  }
  if (Serial.available()) {                    // bench injection
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s == "w") lineWifiScan();
    else if (s == "?") Serial.printf("[scan] status up=%d mock=%d qr=%d rx=%lu ok=%lu scrap=%lu fail=%lu\n", lineUp(),
                                lineIsMock(), qrOk, (unsigned long)rxBytes, (unsigned long)nOk, (unsigned long)nScrap,
                                (unsigned long)nFail);
    else if (s.startsWith("!")) handle(s.substring(1), true);
    else handle(s, scrapArmed);
  }
  static uint32_t t = 0;
  if (millis() - t > 500) { t = millis(); lineLoop(); if (millis() - lastAt > 600) idle(); }
  delay(5);
}
