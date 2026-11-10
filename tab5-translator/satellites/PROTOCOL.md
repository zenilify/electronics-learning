# PAPP remotes — protocol v1 (lane T5-R → T5)

Remotes for the Tab5 translator: **StickS3** (push-to-talk clicker) and **M5Dial** (knob = language pair / volume, press = push-to-talk).
The Tab5 is the receiver (lane T5 implements it in `client/tab5/**`); `tools/udp_sink.py` is a reference receiver (stand-in Tab5).

## 1 · Transport (decided 2026-11-10)

**Primary: UDP on the Wi-Fi the Tab5 is already on, port 47800.** One JSON object per datagram, ≤ 512 bytes, UTF-8.

- The Tab5's radio is an ESP32-C6 behind esp-hosted 2.12.13 (SDIO). Plain UDP sockets through lwIP are what the Tab5 already uses
  (HTTP, mDNS); nothing new has to work on the hosted link. **ESP-NOW through esp_wifi_remote was not tested** (by instruction —
  no time spent on it), so it is not claimed to work or to fail.
- **Plan B (documented, not built): ESP-NOW ↔ UART bridge.** A StampS3 on the Tab5 Port A receives ESP-NOW frames from the
  remotes and forwards the same JSON lines over UART (115200 8N1, one object per line). Same payload, same HMAC, same acks.
  Channel handling for plan B: ESP-NOW only works on the channel the bridge's radio sits on, so the bridge stays un-associated on a
  fixed channel (default 1) and the remotes use that channel; nothing depends on the AP. The Tab5 side then reads a UART instead of a socket.
- Channel handling for the primary: none — the AP owns the channel; remotes and Tab5 are plain stations.
- Known limit: a phone hotspot or guest SSID with **client isolation** blocks station-to-station UDP. Not measured on HEZ26u / iAirHEZ yet.

### Discovery and addressing

1. Remote joins Wi-Fi (same SSID list as the Tab5, strongest known SSID first) and sends `hello` to `255.255.255.255:47800`
   every 1 s (plus, when a unicast hint is provisioned, to that address — used for benches across a router).
2. The Tab5 listens on UDP 47800 and answers **to the datagram's source address and port**. The first valid `ack` to `hello`
   makes the Tab5's address the unicast target; every later datagram is unicast.
3. Remote falls back to step 1 after 3 consecutive events that got no ack, or on `err:"nonce"`.

## 2 · Datagram

Remote → Tab5:

```json
{"v":1,"dev":"stick-353c","seq":42,"ev":"ptt_down","arg":{"side":0},"n":"9f3a01c2","m":"0123456789abcdef"}
```

Tab5 → remote (always `ev:"ack"`):

```json
{"v":1,"dev":"stick-353c","seq":42,"ev":"ack","ok":true,"state":{"st":"listen","pair":"es-zh","vol":70,"side":0},"n":"9f3a01c2","m":"…"}
```

| Field | Type | Meaning |
|---|---|---|
| `v` | int | protocol version, `1` |
| `dev` | string | device id: `stick-` / `dial-` + last 2 MAC bytes (hex, lower case) |
| `seq` | uint32 | per-boot counter, starts at 1, +1 per **new** event; a retransmission repeats the same `seq` |
| `ev` | string | event name (§3) |
| `arg` | object | event arguments (may be `{}`) |
| `n` | string | session nonce issued by the Tab5 in the `hello` ack; absent in `hello` |
| `m` | string | 16 hex chars = first 8 bytes of HMAC-SHA256(key, every byte of the datagram before `,"m":"`); **always the last field** |
| `ok` / `err` | bool / string | ack only. `err`: `auth` (bad HMAC — sent only if cheap; may also stay silent), `pair` (not accepted yet), `nonce` (unknown session → say hello again), `unknown` (unknown event), `busy` |
| `state` | object | ack only: `st` = `idle` \| `listen` \| `busy` \| `speak`; `pair` e.g. `es-zh`; `vol` 0–100; `side` 0/1 (who is being listened to) |

Unknown fields are ignored by both sides (diagnostics such as `lr` = last ack round-trip in µs, `try` = retransmission count).

### Reliability

- Remote sends, then retransmits the same `seq` every **60 ms** until the ack arrives: 5 tries for normal events, 20 for `ptt_up`.
- Tab5 de-duplicates on `(dev, n, seq)`: `seq` ≤ last executed → re-send the ack, do **not** execute again.
- **Stuck-talk guard (Tab5 side):** while PTT is down the remote sends `ptt_down` again every 500 ms with a new `seq` and
  `arg.rep:1` (keep-alive, not a new press). No `ptt_down`/`ptt_up` from that device for 2 s → the Tab5 releases by itself.

## 3 · Events

| `ev` | `arg` | Sent when |
|---|---|---|
| `hello` | `{"kind":"stick"\|"dial","name":"…","fw":"0.1.0","boot":<uint32 random per boot>}` | discovery (every 1 s), then every 5 s as link check; 2 unanswered -> back to discovery (ack refreshes `state`) |
| `ptt_down` | `{"side":0\|1}` (+ `"rep":1` on keep-alives) | talk key goes down — sent immediately, no debounce delay beyond 15 ms |
| `ptt_up` | `{"side":0\|1,"ms":<held ms>}` (+ `"cancel":true` when held < 250 ms) | talk key released; `cancel` = discard the capture (it was a short press) |
| `replay` | `{}` | play the last translation again |
| `pair_next` / `pair_prev` | `{}` | next / previous language pair in the Tab5's list |
| `vol_up` / `vol_down` | `{"step":5}` | volume |
| `battery` | `{"pct":0–100 or -1,"mv":<int or 0>,"chg":true\|false}` | every 60 s |
| `ack` | — | Tab5 → remote only (§2) |

`side`: 0 = the owner (Erick), 1 = the other person. The Tab5 decides what each side means for the current pair.

Controls as built (fw 0.1.0):

| Device | Control | Event |
|---|---|---|
| StickS3 | KEY1 hold | `ptt_down` / `ptt_up` side 0 |
| StickS3 | KEY1 short press (< 250 ms) | `ptt_down`, `ptt_up` `cancel:true`, then `replay` |
| StickS3 | KEY2 hold / short | `ptt_down`/`ptt_up` side 1 / `pair_next` |
| M5Dial | knob press hold | `ptt_down` / `ptt_up` side 0 (short press → `cancel` + `replay`) |
| M5Dial | rotate | `pair_next` / `pair_prev` — one event per detent |
| M5Dial | long touch on the screen (≥ 0.6 s), then rotate | `vol_up` / `vol_down`; back to pair mode after 4 s idle |
| M5Dial | NFC | not used (D: left out) |

## 4 · Pairing and key

- Shared secret: **16 random bytes**, separate from the PAPP bearer. On cachy: `~/.config/zenilify/t5-remote-key`
  (0600, 32 hex chars + newline). Never printed; tools show `len` and a sha256 prefix only.
- Remote side: NVS namespace `t5r`, blob `key`. Written by `tools/provision.py` over USB serial from process memory (not argv, not a file in the repo).
- Tab5 side (T5): same 16 bytes in its NVS, provisioned by T5's own tooling from the same file.
- A datagram whose `m` does not verify is dropped.
- **First contact:** a `hello` with a valid `m` from a `dev` that is not in the Tab5's allow-list → the Tab5 shows
  `arg.name` + `dev` and asks to accept; until then it acks `ok:false, err:"pair"` (the remote shows "accept on Tab5").
  Accepted devices are stored in the Tab5's NVS.
- **Session nonce:** the Tab5 keeps, per device, the last `hello.arg.boot` and a random 8-hex `n`. A `hello` with a different
  `boot` → new `n`, last-seq reset to 0. The `hello` ack echoes `boot` (`"boot":<same>`) and carries `n`. Events with another `n`
  get `err:"nonce"`. This stops replay of recorded presses; a replayed `hello` can only force one extra hello round.

## 5 · Timing targets

Ack round trip on the same AP < 50 ms (remotes run with Wi-Fi modem sleep off while linked). Measured values: see the bench in the lane handoff.
