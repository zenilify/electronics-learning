"""Live subtitles over plain HTTP chunks (what an ESP32 can do without WebSockets/gRPC).

  POST /stream/start?lang=es|en|zh|auto[&tgt=..]      -> {"sid": "...", "chunk_ms": 1000}
  POST /stream/chunk?sid=..   body: raw PCM16 mono 16 kHz (or a WAV)  -> {"seq", "partial", "final": [...]}
  POST /stream/stop?sid=..                              -> same shape, flushes the open segment

Server side: an energy VAD splits the audio into segments. While a segment is open it is
re-recognised at most once per PARTIAL_EVERY_S of new audio (partial caption). A segment is
finalised after TAIL_SIL_S of trailing silence or at MAX_SEG_S, then recognised once more and
(optionally) translated — each final carries {text, lang, tgt_text}. Sessions live in memory
(cachy: one process; Cloud Run: max-instances 1) and expire after IDLE_S without chunks.
"""
from __future__ import annotations

import array
import math
import secrets
import threading
import time
from dataclasses import dataclass, field

RATE = 16000
FRAME = 320                 # 20 ms
TAIL_SIL_S = 0.6
MAX_SEG_S = 12.0
MIN_SEG_S = 0.5
PARTIAL_EVERY_S = 1.0
IDLE_S = 60.0


def _rms(pcm: bytes) -> float:
    a = array.array("h", pcm)
    if not a:
        return 0.0
    return math.sqrt(sum(x * x for x in a) / len(a))


@dataclass
class Session:
    lang: str
    tgt: str
    seg: bytearray = field(default_factory=bytearray)
    voiced_s: float = 0.0
    silence_s: float = 0.0
    since_partial_s: float = 0.0
    noise: float = 300.0
    partial: str = ""
    seq: int = 0
    last: float = field(default_factory=time.monotonic)
    detected: str = ""
    lock: threading.Lock = field(default_factory=threading.Lock)


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
        for i in range(0, len(pcm) - FRAME * 2 + 1, FRAME * 2):
            fr = pcm[i:i + FRAME * 2]
            e = _rms(fr)
            speech = e > max(3.0 * s.noise, 400.0)
            if not speech:
                s.noise = 0.95 * s.noise + 0.05 * e
            if s.voiced_s == 0 and not speech:
                continue                                  # leading silence: drop
            s.seg += fr
            s.since_partial_s += 0.02
            if speech:
                s.voiced_s += 0.02
                s.silence_s = 0.0
            else:
                s.silence_s += 0.02
            seg_s = len(s.seg) / 2 / RATE
            if (s.silence_s >= TAIL_SIL_S and s.voiced_s >= MIN_SEG_S) or seg_s >= MAX_SEG_S:
                finals.append(_finalise(s, recognise, translate))
            elif s.silence_s >= TAIL_SIL_S:                # a click, not speech: reset
                s.seg.clear(); s.voiced_s = s.silence_s = 0.0
        if flush and s.voiced_s >= MIN_SEG_S:
            finals.append(_finalise(s, recognise, translate))
        elif s.seg and s.since_partial_s >= PARTIAL_EVERY_S and s.voiced_s >= MIN_SEG_S:
            s.since_partial_s = 0.0
            try:
                s.partial, lang = recognise(bytes(s.seg), s.lang)
                s.detected = s.detected or (lang if s.lang == "auto" else "")
            except Exception:  # noqa: BLE001 — keep the last partial
                pass
        return {"seq": s.seq, "partial": s.partial, "final": [f for f in finals if f.get("text")],
                "open_s": round(len(s.seg) / 2 / RATE, 2), "lang": s.detected or s.lang}


def _finalise(s: Session, recognise, translate) -> dict:
    pcm = bytes(s.seg)
    s.seg.clear(); s.voiced_s = s.silence_s = s.since_partial_s = 0.0; s.partial = ""
    t0 = time.perf_counter()
    try:
        text, lang = recognise(pcm, s.lang)
    except Exception as e:  # noqa: BLE001
        return {"text": "", "error": type(e).__name__}
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
