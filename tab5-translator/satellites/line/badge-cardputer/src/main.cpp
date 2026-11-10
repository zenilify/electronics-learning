// badge — operator badge login, RFID kanban and andon keyboard of the Traceability Cell (lane T5-I).
// Cardputer ADV + Unit RFID2 (WS1850S, I2C 0x28) on the Grove port (SDA G2, SCL G1).
//
//   RFID  first card after boot (or after key `b`) = the operator badge -> POST /line/event login
//         the same badge again                                          -> logout
//         any other card = a kanban bin tag                             -> kanban, text "bin <uid>"
//   KEYS  1 S01 material defect   2 D01 no material   3 D02 changeover   4 D03 breakdown   -> andon + reason
//         type any code (e.g. S04) + Enter -> andon with that reason code (the server owns the es/zh/en texts)
//         c = andon_clear · r = run · s = stop · b = next card is the badge · Del = erase typed code
//   SCAN  serial console: a line of text is posted as a QR scan ("!code" = scrap) — the scanner fallback role.
// The top bar says LIVE (N4's server) or MOCK (tools/mock_line.py) — never mistake one for the other.
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <M5Cardputer.h>
#include <MFRC522_I2C.h>
#include "line_net.h"

static MFRC522_I2C rfid(0x28, -1, &Wire);
static bool rfidOk = false, armBadge = true;
static String op, typed, lastMsg = "listo · 就绪", lastUid;
static uint32_t lastTap = 0;
static long andonOpen = 0;

struct Quick { char key; const char *code; const char *es; const char *zh; };
static const Quick QUICK[] = {            // the four codes named in N4's contract §3
  {'1', "S01", "Defecto material", "材料缺陷"},
  {'2', "D01", "Falta material", "缺料"},
  {'3', "D02", "Cambio modelo", "换型"},
  {'4', "D03", "Falla equipo", "设备故障"},
};

static void draw() {
  auto &d = M5Cardputer.Display;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  uint16_t bar = !lineUp() ? TFT_PURPLE : lineIsMock() ? TFT_NAVY : TFT_DARKGREEN;
  d.fillRect(0, 0, 240, 20, bar);
  d.setTextColor(TFT_WHITE, bar);
  d.setFont(&fonts::efontCN_14);
  d.setCursor(4, 3);
  d.printf("Línea·产线 %s  %s", LINE_STATION, !lineUp() ? "SIN RED" : lineIsMock() ? "MOCK" : "LIVE");
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(4, 26);
  d.printf("Operador·操作员: %s", op.length() ? op.c_str() : "—");
  d.setCursor(4, 44);
  d.setTextColor(andonOpen > 0 ? TFT_RED : TFT_GREEN, TFT_BLACK);
  d.printf("Andon·安灯: %ld   RFID %s", andonOpen < 0 ? 0 : andonOpen, rfidOk ? "ok" : "--");
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(4, 62);
  d.print(lastMsg);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.setCursor(4, 82);
  d.print("1 S01 材料缺陷  2 D01 缺料");
  d.setCursor(4, 98);
  d.print("3 D02 换型  4 D03 设备故障");
  d.setCursor(4, 116);
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.printf("c clr r run s stop b badge >%s", typed.c_str());
  d.endWrite();
}

static int countAndon(const String &rep) {       // entries in "andon_open":[{...},{...}]
  int i = rep.indexOf("\"andon_open\"");
  if (i < 0) return -1;
  int e = rep.indexOf(']', i), n = 0;
  for (int k = rep.indexOf('{', i); k >= 0 && k < e; k = rep.indexOf('{', k + 1)) n++;
  return n;
}

static void ev(const char *type, const String &reason, const String &text, const String &msg) {
  String rep;
  int c = lineEvent(type, op, reason, text, "", &rep);
  if (c == 200) { int n = countAndon(rep); if (n >= 0) andonOpen = n; lastMsg = msg; }
  else lastMsg = String("ERROR ") + c + " · " + type;
  M5Cardputer.Speaker.tone(c == 200 ? 2400 : 400, 80);
  draw();
}

static void onCard(const String &uid) {
  if (uid == lastUid && millis() - lastTap < 2500) return;   // card still on the reader
  lastUid = uid; lastTap = millis();
  Serial.printf("[badge] card %s\n", uid.c_str());
  if (armBadge || !op.length()) {
    op = uid; armBadge = false;
    ev("login", "", "", "login · 登录 " + uid);
  } else if (uid == op) {
    ev("logout", "", "", "logout · 登出 " + uid);
    op = ""; armBadge = true;
    draw();
  } else {
    ev("kanban", "", "bin " + uid, "kanban · 看板 " + uid);
  }
}

static void onKey(char k) {
  if (typed.length() == 0) {
    for (auto &q : QUICK)
      if (k == q.key) return ev("andon", q.code, "", String("ANDON ") + q.code + " " + q.zh);
    if (k == 'c') return ev("andon_clear", "", "", "andon clear · 解除");
    if (k == 'r') return ev("run", "", "", "RUN · 运行");
    if (k == 'b') { armBadge = true; lastMsg = "acerca el gafete · 请刷卡"; return draw(); }
    if (k == 's') { typed = "S"; return draw(); }            // 's' starts a code; Enter on a lone S = stop
  }
  if (isalnum((unsigned char)k) && typed.length() < 6) typed += (char)toupper((unsigned char)k);
  draw();
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(120);
  M5Cardputer.Speaker.setVolume(90);
  lastMsg = "conectando · 连接中";
  draw();
  Serial.println("\n[badge] T5-I badge-cardputer station=" LINE_STATION);
  Wire.begin(2, 1, 100000);
  Wire.beginTransmission(0x28);
  rfidOk = Wire.endTransmission() == 0;
  if (rfidOk) rfid.PCD_Init();
  Serial.printf("[badge] rfid2 @0x28 ack=%d board=%d\n", rfidOk, (int)M5.getBoard());
  lineBegin();
  lastMsg = "listo · 就绪";
  draw();
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();
    if (st.del && typed.length()) { typed.remove(typed.length() - 1); draw(); }
    else if (st.enter && typed.length()) {
      String code = typed; typed = "";
      if (code == "S") ev("stop", "", "", "STOP · 停机");
      else ev("andon", code, "", "ANDON " + code);
    } else for (char k : st.word) onKey(k);
  }

  static uint32_t tR = 0;
  if (rfidOk && millis() - tR > 120) {
    tR = millis();
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String uid;
      for (byte i = 0; i < rfid.uid.size; i++) { char b[3]; sprintf(b, "%02X", rfid.uid.uidByte[i]); uid += b; }
      rfid.PICC_HaltA();
      onCard(uid);
    }
  }

  if (Serial.available()) {                    // scanner fallback / bench injection
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s == "?") Serial.printf("[badge] status up=%d mock=%d rfid=%d op=%s andon=%ld\n", lineUp(), lineIsMock(), rfidOk,
                                op.c_str(), andonOpen);
    else if (s == "w") lineWifiScan();
    else if (s == "i") {                       // bench: I2C scan of the Grove port, both pin orders
      for (int o = 0; o < 2; o++) {
        Wire.end(); Wire.begin(o ? 1 : 2, o ? 2 : 1, 100000);
        Serial.printf("[badge] i2c sda=G%d scl=G%d:", o ? 1 : 2, o ? 2 : 1);
        for (int a = 8; a < 120; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a); }
        Serial.println();
      }
      Wire.end(); Wire.begin(2, 1, 100000);
    }
    else if (s.startsWith("card:")) onCard(s.substring(5));
    else if (s.startsWith("key:")) { for (size_t i = 4; i < s.length(); i++) onKey(s[i]); }
    else if (s.length()) {
      bool scrap = s.startsWith("!");
      String rep;
      int c = lineScan(scrap ? s.substring(1) : s, "qr", scrap ? "scrap" : "ok", scrap ? "S01" : "", op, &rep);
      lastMsg = c == 200 ? String(scrap ? "SCRAP " : "OK ") + "#" + jnum(rep, "seq") : String("ERROR ") + c;
      draw();
    }
  }

  static uint32_t t = 0;
  static bool wasUp = false, wasMock = true;
  if (millis() - t > 1000) {
    t = millis();
    lineLoop();
    if (wasUp != lineUp() || wasMock != lineIsMock()) { wasUp = lineUp(); wasMock = lineIsMock(); draw(); }
  }
  delay(5);
}
