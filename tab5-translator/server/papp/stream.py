"""Live subtitles over plain HTTP chunks (what an ESP32 can do without WebSockets/gRPC).

  POST /stream/start?lang=es|en|zh|auto[&tgt=..]      -> {"sid": "...", "chunk_ms": 1000}
  POST /stream/chunk?sid=..   body: raw PCM16 mono 16 kHz (or a WAV)  -> {"seq", "partial", "final": [...]}
  POST /stream/stop?sid=..                              -> same shape, flushes the open segment

Server side: an energy VAD splits the audio into segments. While a segment is open it is
re-recognised at most once per PARTIAL_EVERY_S of new audio (partial caption). A segment is
finalised after TAIL_SIL_S of trailing silence or at MAX_SEG_S, then recognised once more and
(optionally) translated — each final carries {text, lang, tgt_text}. Sessions live in memory
(cachy: one process; Cloud Run: max-instances 1) and expire after IDLE_S without chunks.

How "speech" is decided (rewritten 2026-11-11, after "live subtitles show nothing" on the Tab5):

  * Loudness is judged against the room, not against a fixed number. The noise floor is the quiet
    end of the recent past: the NOISE_PCT percentile of the 20 ms frame levels of the last
    NOISE_WIN_S. Speech and bangs are the loud end of that window, so they cannot pull the floor
    up; a fan that starts is followed once it has filled the window.
    Before, the floor was a running average that every frame below the threshold fed, and the
    threshold never went under 400. The Tab5's microphone delivers speech at RMS 130–240 (measured,
    console `mic`): no segment ever opened. Louder but uneven sound pushed the average up until
    speech was "noise" as well.
  * A frame is speech above K_ON x floor; inside an open segment K_OFF x floor keeps it going
    (word endings are quieter than word starts). ABS_MIN only guards against digital silence.
  * PRE_ROLL_S of audio from before the first loud frame is kept: soft onsets ("s", "h", "f")
    used to be cut off because a segment started at the first frame over the threshold.
  * A segment is brought to a normal level before it is recognised (quiet microphone, far speaker).
"""
from __future__ import annotations

import array
import math
import secrets
import threading
import time
from collections import deque
from dataclasses import dataclass, field

RATE = 16000
FRAME = 320                 # 20 ms
FRAME_S = FRAME / RATE
TAIL_SIL_S = 0.6
MAX_SEG_S = 12.0
MIN_SEG_S = 0.35            # voiced time a segment needs (a click or a cough stays under it)
PARTIAL_EVERY_S = 1.0
IDLE_S = 60.0
PRE_ROLL_S = 0.3
NOISE_WIN_S = 5.0
NOISE_PCT = 0.08
K_ON = 3.0
K_OFF = 2.0
ABS_MIN = 40.0
ASR_TARGET_RMS = 2000.0     # level a segment is raised to before recognition
ASR_MAX_GAIN = 30.0


def _rms(pcm: bytes) -> float:
    a = array.array("h", pcm)
    if not a:
        return 0.0
    return math.sqrt(sum(x * x for x in a) / len(a))


def levelled(pcm: bytes) -> bytes:
    """The segment at ASR_TARGET_RMS (never more than ASR_MAX_GAIN, never quieter than it came in)."""
    a = array.array("h", pcm)
    if not a:
        return pcm
    rms = math.sqrt(sum(x * x for x in a) / len(a))
    g = min(ASR_TARGET_RMS / rms, ASR_MAX_GAIN) if rms > 0 else 1.0
    if g <= 1.2:
        return pcm
    return array.array("h", (max(-32768, min(32767, int(x * g))) for x in a)).tobytes()


@dataclass
class Session:
    lang: str
    tgt: str
    seg: bytearray = field(default_factory=bytearray)
    voiced_s: float = 0.0
    silence_s: float = 0.0
    since_partial_s: float = 0.0
    noise: float = 0.0          # 0 = not known yet (first 0.2 s of a session)
    partial: str = ""
    seq: int = 0
    last: float = field(default_factory=time.monotonic)
    detected: str = ""
    lock: threading.Lock = field(default_factory=threading.Lock)
    hist: deque = field(default_factory=lambda: deque(maxlen=int(NOISE_WIN_S / FRAME_S)))
    pre: deque = field(default_factory=lambda: deque(maxlen=int(PRE_ROLL_S / FRAME_S)))
    frames: int = 0
    finals: int = 0             # how many segments were recognised with text (for the log)
    dropped: int = 0            # segments that opened and were discarded as too short


_sessions: dict[str, Session] = {}


def start(lang: str, tgt: str) -> str:
    _gc()
    sid = secrets.token_urlsafe(9)
    _sessions[sid] = Session(lang=lang, tgt=tgt)
    return sid


def get(sid: str) -> Session | None:
    s = _sessions.get(sid)
    if s:
        s.last = time.monotonic()
    return s


def drop(sid: str) -> None:
    _sessions.pop(sid, None)


def _gc() -> None:
    now = time.monotonic()
    for k in [k for k, s in _sessions.items() if now - s.last > IDLE_S]:
        _sessions.pop(k, None)


def feed(s: Session, pcm: bytes, recognise, translate, flush: bool = False) -> dict:
    """recognise(pcm, lang) -> (text, lang); translate(text, src, tgt) -> str."""
    finals = []
    with s.lock:
        s.seq += 1
        level = _rms(pcm)
        for i in range(0, len(pcm) - FRAME * 2 + 1, FRAME * 2):
            fr = pcm[i:i + FRAME * 2]
            e = _rms(fr)
            s.hist.append(e)
            s.frames += 1
            if len(s.hist) >= 10 and (s.frames % 5 == 0 or s.noise <= 0):
                h = sorted(s.hist)
                s.noise = max(h[int(len(h) * NOISE_PCT)], 1.0)
            if s.noise <= 0:                                  # no floor yet: only remember the audio
                s.pre.append(fr)
                continue
            in_seg = bool(s.seg)
            speech = e > max((K_OFF if in_seg else K_ON) * s.noise, ABS_MIN)
            if not in_seg:
                if not speech:
                    s.pre.append(fr)                          # silence: keep the last PRE_ROLL_S
                    continue
                for p in s.pre:                               # first loud frame: open with the pre-roll
                    s.seg += p
                s.pre.clear()
            s.seg += fr
            s.since_partial_s += FRAME_S
            if speech:
                s.voiced_s += FRAME_S
                s.silence_s = 0.0
            else:
                s.silence_s += FRAME_S
            seg_s = len(s.seg) / 2 / RATE
            if (s.silence_s >= TAIL_SIL_S and s.voiced_s >= MIN_SEG_S) or seg_s >= MAX_SEG_S:
                finals.append(_finalise(s, recognise, translate))
            elif s.silence_s >= TAIL_SIL_S:                    # a click, not speech: reset
                s.seg.clear(); s.voiced_s = s.silence_s = s.since_partial_s = 0.0
                s.dropped += 1
        if flush and s.voiced_s >= MIN_SEG_S:
            finals.append(_finalise(s, recognise, translate))
        elif s.seg and s.since_partial_s >= PARTIAL_EVERY_S and s.voiced_s >= MIN_SEG_S:
            s.since_partial_s = 0.0
            try:
                s.partial, lang = recognise(levelled(bytes(s.seg)), s.lang)
                s.detected = s.detected or (lang if s.lang == "auto" else "")
            except Exception:  # noqa: BLE001 — keep the last partial
                pass
        good = [f for f in finals if f.get("text")]
        s.finals += len(good)
        return {"seq": s.seq, "partial": s.partial, "final": good,
                "open_s": round(len(s.seg) / 2 / RATE, 2), "lang": s.detected or s.lang,
                # what the detector saw, for whoever has to answer "why did nothing come up":
                "level": round(level), "noise": round(s.noise), "empty": len(finals) - len(good)}


def _finalise(s: Session, recognise, translate) -> dict:
    pcm = bytes(s.seg)
    s.seg.clear(); s.voiced_s = s.silence_s = s.since_partial_s = 0.0; s.partial = ""
    t0 = time.perf_counter()
    try:
        text, lang = recognise(levelled(pcm), s.lang)
    except Exception as e:  # noqa: BLE001
        return {"text": "", "error": type(e).__name__, "dur_s": round(len(pcm) / 2 / RATE, 2)}
    if s.lang == "auto" and lang:
        s.detected = lang
    out = {"text": text, "lang": lang, "dur_s": round(len(pcm) / 2 / RATE, 2)}
    if s.tgt and text and lang and s.tgt != lang:
        try:
            out["tgt_text"] = translate(text, lang, s.tgt)
        except Exception as e:  # noqa: BLE001
            out["tgt_error"] = type(e).__name__
    out["ms"] = round((time.perf_counter() - t0) * 1000)
    return out
