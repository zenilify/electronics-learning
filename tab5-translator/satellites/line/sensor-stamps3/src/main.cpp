// sensor-stamps3 — line sensor node of the Traceability Cell (lane T5-I).
// StampS3 on the Grove Breakout, nothing attached yet (Erick D3, 2026-11-10):
//   button G0  short press = one piece through the gate (count++)
//              long press (>1.5 s) = toggle line RUN <-> STOP (simulated stoppage)
//   chip temperature = the "process value" until a real Grove probe is fitted
//   RGB LED G21: green RUN · red STOP · blue = posting to the MOCK · off-blink = no server
// Every 10 s: POST /line/event {station, type: run|stop, value: <°C>, unit:"C", count:<n>}
// (value/unit/count are an extension asked of N4 on 11-10; a server that ignores them still gets run/stop.)
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include "line_net.h"

#define PIN_BTN 0
#define PIN_LED 21
#define PERIOD_MS 10000

static bool running = true;
static uint32_t count = 0, lastPost = 0, seq = 0, okPosts = 0, badPosts = 0;

static void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_LED, r, g, b); }
static void showState() {
  if (!lineUp()) led(8, 0, 8);
  else if (lineIsMock()) led(0, 0, running ? 24 : 4);
  else if (running) led(0, 24, 0);
  else led(24, 0, 0);
}

static void post() {
  float t = temperatureRead();
  String extra = "\"value\":" + String(t, 1) + ",\"unit\":\"C\",\"count\":" + String(count) + ",\"seq\":" + String(seq++);
  int c = lineEvent(running ? "run" : "stop", "", "", "", extra);
  (c == 200 ? okPosts : badPosts)++;
  Serial.printf("[sensor] t=%.1fC count=%lu state=%s ok=%lu bad=%lu up_ms=%lu\n", t, (unsigned long)count,
                running ? "run" : "stop", (unsigned long)okPosts, (unsigned long)badPosts, (unsigned long)millis());
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN, INPUT_PULLUP);
  led(8, 8, 0);
  delay(800);
  Serial.println("\n[sensor] T5-I sensor-stamps3 station=" LINE_STATION);
  lineBegin();
  showState();
  post();
  lastPost = millis();
}

void loop() {
  static bool was = false;
  static uint32_t downAt = 0;
  bool down = digitalRead(PIN_BTN) == LOW;
  if (down && !was) downAt = millis();
  if (!down && was) {
    uint32_t held = millis() - downAt;
    if (held > 1500) {                      // toggle run/stop and say so at once
      running = !running;
      lineEvent(running ? "run" : "stop", "", "", "");
    } else if (held > 30) {
      count++;
      led(24, 24, 24); delay(40);
    }
    showState();
  }
  was = down;

  if (millis() - lastPost >= PERIOD_MS) {
    lastPost += PERIOD_MS;                  // fixed cadence: a slow POST does not push the next one out
    post();
    showState();
  }
  lineLoop();
  delay(5);
}
