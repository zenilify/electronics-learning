#!/usr/bin/env python3
"""Offline check of the live-subtitles speech detector (server/papp/stream.py) - no network, no ASR.

  python3 server/tools/test_stream_vad.py        exit code 0 = every case as expected

Why it exists: on 2026-11-11 live subtitles showed nothing on the Tab5. Its microphone delivers speech at RMS
130-240 over a room floor of about 30 (measured with the firmware's console `mic`); the detector of that day
wanted frames above max(3 x noise, 400). The cases below rebuild that situation from the demo clips of the
firmware (client/tab5/main/assets/demo) and from generated noise, and run the detector of that day next to the
current one - a test that the old code passes would prove nothing.

The recogniser is a stand-in that only reports the length it was given, so this checks WHERE segments start
and end, not what is recognised.
"""
import array
import math
import os
import random
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from papp import stream  # noqa: E402

DEMO = os.path.join(HERE, "..", "..", "client", "tab5", "main", "assets", "demo")
RATE, FR = 16000, 320


def clip(n):
    w = wave.open(os.path.join(DEMO, f"d{n}_src.wav"))
    assert w.getframerate() == RATE and w.getnchannels() == 1
    return array.array("h", w.readframes(w.getnframes()))


def rms(a):
    return math.sqrt(sum(x * x for x in a) / max(1, len(a)))


def scaled(a, target_rms):
    g = target_rms / rms(a)
    return array.array("h", (max(-32768, min(32767, int(x * g))) for x in a))


def noise(seconds, level, rnd):
    return array.array("h", (int(rnd.gauss(0, level)) for _ in range(int(seconds * RATE))))


def mix(base, add, at_s):
    o = int(at_s * RATE)
    for i, x in enumerate(add):
        if o + i < len(base):
            base[o + i] = max(-32768, min(32767, base[o + i] + x))
    return base


def onset_s(a):
    """first 20 ms frame of a clean clip that is clearly speech"""
    lim = 0.1 * rms(a)
    for i in range(0, len(a) - FR, FR):
        if rms(a[i:i + FR]) > lim:
            return i / RATE
    return 0.0


def run_new(sig, chunk_s=0.02):
    """-> [(start_s, dur_s)] of the segments the current detector hands to the recogniser, and the session"""
    s = stream.Session(lang="es", tgt="")
    segs, fed = [], [0.0]

    def rec(pcm, lang):
        d = len(pcm) / 2 / RATE
        segs.append((round(fed[0] - d, 2), round(d, 2)))
        return f"{d:.1f}s", "es"

    step = int(chunk_s * RATE)
    for i in range(0, len(sig), step):
        part = sig[i:i + step]
        fed[0] = (i + len(part)) / RATE
        partial_before = len(segs)
        out = stream.feed(s, part.tobytes(), rec, lambda t, a, b: t)
        if len(segs) > partial_before and not out["final"]:      # that call was a partial, not a segment
            del segs[partial_before:]
    return segs, s


def run_old(sig):
    """The detector as it was on 2026-11-11 (frame decision only): returns the segments it would have recognised."""
    noise_, seg_n, voiced, sil, segs, t0 = 300.0, 0, 0.0, 0.0, [], 0.0
    for i in range(0, len(sig) - FR + 1, FR):
        e = rms(sig[i:i + FR])
        speech = e > max(3.0 * noise_, 400.0)
        if not speech:
            noise_ = 0.95 * noise_ + 0.05 * e
        if voiced == 0 and not speech:
            continue
        if seg_n == 0:
            t0 = i / RATE
        seg_n += 1
        if speech:
            voiced += 0.02; sil = 0.0
        else:
            sil += 0.02
        if (sil >= 0.6 and voiced >= 0.5) or seg_n * 0.02 >= 12.0:
            segs.append((round(t0, 2), round(seg_n * 0.02, 2))); seg_n = 0; voiced = sil = 0.0
        elif sil >= 0.6:
            seg_n = 0; voiced = sil = 0.0
    return segs


def covered(segs, a, b):
    """fraction of the stretch a..b (seconds) that lies inside one of the segments"""
    got = 0.0
    for st, d in segs:
        got += max(0.0, min(b, st + d) - max(a, st))
    return got / (b - a)


fails = []


def check(name, ok, detail):
    print(f"{'ok  ' if ok else 'FAIL'} {name}: {detail}")
    if not ok:
        fails.append(name)


def main():
    if not os.path.isdir(DEMO):
        print("demo clips not found:", DEMO); return 2
    rnd = random.Random(11)
    d1, d2 = clip(1), clip(2)
    len1, on1 = len(d1) / RATE, onset_s(d1)

    # 1. the Tab5 on 11 Nov: speech RMS 200 over a floor of 70 (its numbers at 37.5 dB), starting at 3.0 s
    sig = mix(noise(10, 70, rnd), scaled(d1, 200), 3.0)
    new, _ = run_new(sig); old = run_old(sig)
    want = 3.0 + on1                       # the clip holds two sentences with a pause: one or two segments are both right
    cov = covered(new, want, 3.0 + len1)
    check("1 quiet speech (Tab5 level) is found", 1 <= len(new) <= 2 and want - 0.35 <= new[0][0] <= want + 0.1 and cov >= 0.9,
          f"new {new} cover {cov:.0%} of the speech ({want:.2f} s to {3.0 + len1:.2f} s)")
    check("1b the detector of 11 Nov misses it", old == [], f"old {old}")

    # 2. a speaker further away: RMS 100 over a floor of 30 (the 30 dB numbers)
    sig = mix(noise(10, 30, rnd), scaled(d1, 100), 3.0)
    new, _ = run_new(sig)
    cov = covered(new, 3.0 + on1, 3.0 + len1)
    check("2 far speech is found", 1 <= len(new) <= 2 and cov >= 0.8, f"new {new} cover {cov:.0%}")

    # 3. a room that is not steady: 120 ms knocks at RMS 400 every 1.5 s, no speech -> nothing to recognise
    sig = noise(20, 70, rnd)
    for k in range(12):
        mix(sig, noise(0.12, 400, rnd), 1.0 + 1.5 * k)
    new, s = run_new(sig)
    check("3 short knocks are not sent to the recogniser", new == [], f"new {new}, opened and dropped {s.dropped}")

    # 4. a fan starts: 6 s at 70, then 25 s at 300 -> at most one false segment, then quiet again
    sig = noise(6, 70, rnd) + noise(25, 300, rnd)
    new, s = run_new(sig)
    check("4 a noise step is followed", len(new) <= 1 and not s.seg and 200 <= s.noise <= 400,
          f"new {new}, floor at the end {s.noise:.0f}, open {len(s.seg) / 2 / RATE:.1f} s")

    # 5. the desktop / injector case: a clean loud clip over digital silence
    sig = mix(array.array("h", bytes(2 * RATE * 8)), d1, 2.0)
    new, _ = run_new(sig); old = run_old(sig)
    check("5 a clean loud clip still works", len(new) == 1 and new[0][1] >= 0.8 * (len1 - on1), f"new {new} · old {old}")

    # 6. two sentences with a 1.2 s pause, in 1 s chunks as the Tab5 sends them
    sig = noise(16, 70, rnd)
    mix(sig, scaled(d1, 220), 2.0); mix(sig, scaled(d2, 220), 2.0 + len1 + 1.2)
    new, _ = run_new(sig, chunk_s=1.0)
    c1, c2 = covered(new, 2.0 + on1, 2.0 + len1), covered(new, 3.2 + len1 + onset_s(d2), 3.2 + len1 + len(d2) / RATE)
    # run_new() dates a segment by the end of the chunk that finished it, so with 1 s chunks only the count is exact
    check("6 two clips, 1 s chunks as the Tab5 sends them", 2 <= len(new) <= 3, f"{len(new)} segments, {[d for _, d in new]} s long (cover by chunk-end dating: {c1:.0%} / {c2:.0%})")

    # 7. speech over a louder room: RMS 600 over a floor of 250 (factory floor, the VAD's limit is 3 x)
    sig = mix(noise(10, 250, rnd), scaled(d1, 900), 3.0)
    new, _ = run_new(sig)
    check("7 speech in a loud room is found", len(new) >= 1, f"new {new}")

    # 8. a segment is raised to a normal level before recognition, a normal one is left alone
    q = scaled(d1, 150).tobytes()
    lv = rms(array.array("h", stream.levelled(q)))
    same = stream.levelled(d1.tobytes()) == d1.tobytes() or abs(rms(array.array("h", stream.levelled(d1.tobytes()))) - 2000) < 200
    check("8 quiet segments are levelled", 1700 <= lv <= 2100 and same, f"RMS 150 -> {lv:.0f}")

    # 9. what a recogniser makes of non-speech is not a caption (all four were on the Tab5's screen on 11 Nov)
    junk = ["-", "...", "*Gunshot*", "[Music]", "(risas)", "  ", "なお", "- 마리 마리.", "이렇게 해서", "Субтитры сделал DimaTorzok",
            "Subtítulos realizados por la comunidad de Amara.org", "Thanks for watching!"]
    real = ["Hola.", "你好", "Este sí.", "OK (creo)", "3", "¿Dónde está la estación de tren?", "你好,请问洗手间在哪里?", "USB-C 电缆的价格是多少？"]
    check("9 non-speech output is dropped, speech is kept", all(stream.spoken(t) == "" for t in junk) and all(stream.spoken(t) == t for t in real),
          f"dropped {[t for t in junk if stream.spoken(t) == '']} · kept {[t for t in real if stream.spoken(t)]}")

    print("\n" + ("ALL OK" if not fails else f"{len(fails)} FAILED: " + ", ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
