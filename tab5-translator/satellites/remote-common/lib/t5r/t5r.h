// t5r — PAPP remote core (protocol v1, see ../../PROTOCOL.md). Shared by remote-sticks3 and remote-dial.
// Wi-Fi join (provisioned slots, strongest first), UDP 47800 discovery + unicast, HMAC-SHA256 tag, ack/retry queue,
// PTT keep-alive, serial provisioning console ("T5R ..." lines on USB). Secrets are never echoed.
#pragma once
#include <Arduino.h>

namespace t5r {

enum Link : uint8_t { L_NOKEY, L_NOWIFI, L_DISCOVER, L_PAIR, L_LINKED };

struct State {            // last state reported by the Tab5 in an ack
  char st[8];             // idle | listen | busy | speak
  char pair[12];          // e.g. es-zh
  int vol;                // 0-100, -1 unknown
  int side;               // 0/1
};

struct Stats {
  uint32_t sent, acked, lost, rejected, dropped;   // new events / acked / retries exhausted / ok:false / not linked
  uint32_t rttLastUs, rttMinUs, rttMaxUs; uint64_t rttSumUs;
};

void begin(const char *kind, const char *name);    // kind: "stick" | "dial"
void loop();                                       // call as often as possible

bool send(const char *ev, const char *argJson = "{}", uint8_t tries = 5);
// Push-to-talk. down: sent at once. up: held < 250 ms -> cancel:true and then shortEv (if not null).
void ptt(bool down, int side, const char *shortEv = "replay", bool synthetic = false);
bool pttHeld();

Link link();
const State &state();
const Stats &stats();
const char *devId();
const char *ssid();
int rssi();
void setBattery(int pct, int mv, bool chg);        // reported in the 60 s battery event
void setExtraInfo(const char *json);               // appended to "T5R info" (device-specific diagnostics, no secrets)
int prefInt(const char *key, int def);                  // device tunables stored with "T5R set <key> <int>"

}  // namespace t5r
