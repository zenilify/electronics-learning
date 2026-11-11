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

Second pass, same day, after the first test with a person (Erick, 16:48): his sentence was captioned only when
the segment hit MAX_SEG_S, 12 s later, and the room alone then produced a 12 s "segment" every 12 s, which the
recogniser turned into invented English sentences. The room was restless (20 ms frames between 40 and 300 while
the quiet end sat at 40): a floor taken from the quiet end is right for hearing quiet speech, and wrong as the
only yardstick for "this is still speech". So:

  * Two more yardsticks, both taken from ALL frame levels of the last BG_WIN_S: `bg`, their median, and `bg_hi`,
    their 75th percentile. A segment opens above K_ON x floor AND K_HI_ON x bg_hi; inside a segment a frame is a
    pause under K_BG_OFF x bg. In a steady room all three sit next to each other and the floor decides, as
    before. In a restless room they spread, and the upper two keep the room's own bumps from opening a segment
    and from holding one open. (Of everything, not of "what was not speech": a level that only learns from
    frames it already called non-speech can never learn that the room got louder. Somebody talking without a
    break raises them to the weak and middle part of their own speech; the strong syllables still open
    segments and real pauses still close them.)
  * Inside a segment a frame is also a pause when it is under REL_OFF of that segment's own speech level
    (`ref`, the loudest recent frame, fading by half in 2.8 s): when a speaker at 1500 stops, the room at 115
    ends the segment instead of extending it.
  * The pause that ends a segment shrinks as the segment grows (0.6 s, 0.45 s past 5 s, 0.3 s past 8 s).
  * A segment whose loud and quiet frames differ by less than FLAT_RATIO is a noise (a fan that started): it is
    not recognised.
  * The same caption twice in a row within 30 s is dropped (what a recogniser does with noise).

Third pass, 17:20, after Erick's verdict on the second ("it worked after my first sentence, then it started
inventing stuff"): the captions that followed his sentence were short phrases made from much quieter sound
(a television elsewhere in the house, the room itself), 15-25 dB under his voice.

  * `dom`, the level of the strongest recent speech that produced words (90th percentile of that segment's
    frames), halving every DOM_HALF_S. A segment does not open under K_DOM x dom: for a while after somebody
    spoke at the device, sound a tenth as strong is background, not the next speaker. Somebody at 3 m is about a
    sixth of somebody at 0.5 m and still gets through (just: 0.17 against K_DOM 0.15); with nobody near, `dom`
    is small and changes nothing. A compromise between "ignore the television" and "hear the far end of the
    table" - the clean answer is a near / room switch on the Tab5, not built yet.
  * A word or phrase repeated more than three times in a row inside one caption is cut back to three (a
    recogniser stuck in a loop: "no, no, no, ..." thirty times on 11 Nov).
"""
from __future__ import annotations

import array
import math
import re
import secrets
import threading
import time
from collections import deque
from dataclasses import dataclass, field

RATE = 16000
FRAME = 320                 # 20 ms
FRAME_S = FRAME / RATE
TAIL_SIL_S = 0.6            # the pause that ends a segment ...
TAIL_MID_S = 0.45           # ... once it is longer than 5 s
TAIL_LONG_S = 0.3           # ... and longer than 8 s
MAX_SEG_S = 12.0
MIN_SEG_S = 0.35            # voiced time a segment needs (a click or a cough stays under it)
PARTIAL_EVERY_S = 1.0
IDLE_S = 60.0
PRE_ROLL_S = 0.3
NOISE_WIN_S = 5.0
NOISE_PCT = 0.08
K_ON = 3.0                  # a segment opens above this many times the floor (the room's quiet end) ...
K_HI_ON = 2.0               # ... and this many times bg_hi (75th percentile of the last BG_WIN_S)
K_OFF = 2.0                 # inside a segment a frame is a pause under K_OFF x floor,
K_BG_OFF = 1.6              # under K_BG_OFF x bg (the median of the last BG_WIN_S),
REL_OFF = 0.2               # or under this share of the segment's own speech level
REF_DECAY = 0.995           # that level follows the loudest recent frame and halves in 2.8 s
BG_WIN_S = 10.0
FLAT_RATIO = 2.5            # 90th / 10th percentile of a segment's frame levels; under it = a noise, not speech
REPEAT_S = 30.0
K_DOM = 0.15                # a segment does not open under this share of the strongest recent speech
DOM_HALF_S = 45.0
DOM_DECAY = 0.5 ** (FRAME_S / DOM_HALF_S)
DOM_MIN_VOICED_S = 0.6      # only a segment with this much speech can set that level
ABS_MIN = 40.0
ASR_TARGET_RMS = 2000.0     # level a segment is raised to before recognition
ASR_MAX_GAIN = 30.0


_SOUND_TAG = re.compile(r"^[\*\[\(（【].{0,40}[\*\]\)）】]$")


# What speech recognisers of the Whisper family write for music and silence: the credit lines of the subtitle files
# they were trained on. Lower case, matched anywhere in the text.
_CREDITS = ("amara.org", "dimatorzok", "thanks for watching", "thank you for watching", "gracias por ver",
            "subtítulos realizados", "subtitulos realizados", "suscríbete", "字幕由", "谢谢观看", "請不吝", "请不吝")


_LOOP = re.compile(r"((?:\b[\w']+\b[,;]?\s+){0,3}\b[\w']+\b[,;.!?]?)(?:\s+\1){3,}", re.IGNORECASE)


def unlooped(text: str) -> str:
    """'no, no, no, no, no, no' -> 'no, no, no': a run of the same one to four words is cut back to three."""
    prev = None
    while prev != text:
        prev = text
        text = _LOOP.sub(lambda m: " ".join([m.group(1)] * 3), text)
    return text


def _our_script(ch: str) -> bool:
    """a letter of the three languages this service speaks: Latin (with accents) or a Chinese character"""
    o = ord(ch)
    return o < 0x250 or 0x3400 <= o <= 0x4DBF or 0x4E00 <= o <= 0x9FFF or 0xFF00 <= o <= 0xFFEF


def spoken(text: str) -> str:
    """'' for what a recogniser makes of sound that is not speech in es / en / zh: a lone dash, dots, a sound tag
    such as *Gunshot* or [Music], text in another script (Korean, Japanese and Russian came back for music), or a
    subtitle credit line. All of these were captions on the Tab5 on 2026-11-11 while a television ran in the room."""
    t = (text or "").strip()
    if not any(ch.isalnum() for ch in t) or _SOUND_TAG.match(t):
        return ""
    if any(ch.isalpha() and not _our_script(ch) for ch in t):
        return ""
    low = t.lower()
    if any(c in low for c in _CREDITS):
        return ""
    return unlooped(t)


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
    bg: float = 0.0             # median of all frame levels of the last BG_WIN_S; 0 = not known yet
    bg_hi: float = 0.0          # their 75th percentile
    bg_hist: deque = field(default_factory=lambda: deque(maxlen=int(BG_WIN_S / FRAME_S)))
    ref: float = 0.0            # speech level of the open segment
    dom: float = 0.0            # level of the strongest recent speech that produced words; halves every DOM_HALF_S
    seg_e: list = field(default_factory=list)   # frame levels of the open segment, from its first loud frame
    flat: int = 0               # segments discarded as a steady noise
    repeats: int = 0            # captions discarded as a repeat of the one before
    last_text: str = ""
    last_text_t: float = 0.0


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
            s.bg_hist.append(e)
            s.frames += 1
            s.dom *= DOM_DECAY
            if len(s.hist) >= 10 and (s.frames % 5 == 0 or s.noise <= 0):
                h = sorted(s.hist)
                s.noise = max(h[int(len(h) * NOISE_PCT)], 1.0)
                b = sorted(s.bg_hist)
                s.bg, s.bg_hi = b[len(b) // 2], b[len(b) * 3 // 4]
            if s.noise <= 0:                                  # no floor yet: only remember the audio
                s.pre.append(fr)
                continue
            bg = s.bg or s.noise
            if not s.seg:
                if e <= max(K_ON * s.noise, K_HI_ON * (s.bg_hi or s.noise), K_DOM * s.dom, ABS_MIN):
                    s.pre.append(fr)                          # not speech: keep the last PRE_ROLL_S
                    continue
                for p in s.pre:                               # first loud frame: open with the pre-roll
                    s.seg += p
                s.pre.clear()
                s.ref = e; s.seg_e = []
                speech = True
            else:
                s.ref = max(s.ref * REF_DECAY, e)
                speech = e > max(K_OFF * s.noise, K_BG_OFF * bg, REL_OFF * s.ref, ABS_MIN)
            s.seg += fr
            s.seg_e.append(e)
            s.since_partial_s += FRAME_S
            if speech:
                s.voiced_s += FRAME_S
                s.silence_s = 0.0
            else:
                s.silence_s += FRAME_S
            seg_s = len(s.seg) / 2 / RATE
            tail = TAIL_SIL_S if seg_s < 5.0 else TAIL_MID_S if seg_s < 8.0 else TAIL_LONG_S
            if (s.silence_s >= tail and s.voiced_s >= MIN_SEG_S) or seg_s >= MAX_SEG_S:
                finals.append(_finalise(s, recognise, translate))
            elif s.silence_s >= tail:                          # a click, not speech: reset
                s.seg.clear(); s.voiced_s = s.silence_s = s.since_partial_s = 0.0
                s.dropped += 1
        if flush and s.voiced_s >= MIN_SEG_S:
            finals.append(_finalise(s, recognise, translate))
        # a long open segment (continuous talk, a television) is re-recognised every 2 s instead of every second:
        # each partial is a full recognition of everything open so far
        elif (s.seg and s.voiced_s >= MIN_SEG_S
              and s.since_partial_s >= (PARTIAL_EVERY_S if len(s.seg) / 2 / RATE < 6.0 else 2 * PARTIAL_EVERY_S)):
            s.since_partial_s = 0.0
            try:
                text, lang = recognise(levelled(bytes(s.seg)), s.lang)
                s.partial = spoken(text)
                s.detected = s.detected or (lang if s.lang == "auto" else "")
            except Exception:  # noqa: BLE001 — keep the last partial
                pass
        good = [f for f in finals if f.get("text")]
        s.finals += len(good)
        return {"seq": s.seq, "partial": s.partial, "final": good,
                "open_s": round(len(s.seg) / 2 / RATE, 2), "lang": s.detected or s.lang,
                # what the detector saw, for whoever has to answer "why did nothing come up":
                "level": round(level), "noise": round(s.noise), "bg": round(s.bg), "dom": round(s.dom),
                "empty": len(finals) - len(good)}


def _finalise(s: Session, recognise, translate) -> dict:
    pcm = bytes(s.seg)
    lv = sorted(s.seg_e[:max(1, len(s.seg_e) - int(s.silence_s / FRAME_S))])       # without the closing pause
    dur, voiced = round(len(pcm) / 2 / RATE, 2), round(s.voiced_s, 2)
    s.seg.clear(); s.voiced_s = s.silence_s = s.since_partial_s = 0.0; s.partial = ""
    if len(lv) >= 25 and lv[int(len(lv) * 0.9)] < FLAT_RATIO * max(lv[int(len(lv) * 0.1)], 1.0):
        s.flat += 1
        return {"text": "", "flat": True, "dur_s": dur}
    t0 = time.perf_counter()
    try:
        text, lang = recognise(levelled(pcm), s.lang)
    except Exception as e:  # noqa: BLE001
        return {"text": "", "error": type(e).__name__, "dur_s": dur}
    text = spoken(text)
    now = time.monotonic()
    if text and text == s.last_text and now - s.last_text_t < REPEAT_S:
        s.repeats += 1
        text = ""
    elif text:
        s.last_text, s.last_text_t = text, now
    if s.lang == "auto" and lang and text:
        s.detected = lang
    p90 = lv[int(len(lv) * 0.9)] if lv else 0.0
    if text and voiced >= DOM_MIN_VOICED_S:
        s.dom = max(s.dom, p90)
    out = {"text": text, "lang": lang, "dur_s": dur, "voiced_s": voiced, "p90": round(p90)}
    if s.tgt and text and lang and s.tgt != lang:
        try:
            out["tgt_text"] = translate(text, lang, s.tgt)
        except Exception as e:  # noqa: BLE001
            out["tgt_error"] = type(e).__name__
    out["ms"] = round((time.perf_counter() - t0) * 1000)
    return out
