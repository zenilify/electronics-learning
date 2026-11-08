#!/usr/bin/env python3
"""Render the phrasebook's es/en side in a VoiceStudio (OmniVoice) cloned voice -> alternate SD folder.

Lane N4, 2026-11-08. Runs ON cachy (VoiceStudio :3900 is loopback there). Stdlib + ffmpeg only.

  python3 tools/phrasebook_voice.py render  phrasebook/phrases.psv ~/papp/voice-pack-erick [--voice 2b88f5be]
  python3 tools/phrasebook_voice.py measure ~/papp/voice-pack-erick [--n 24]

Output (PERSONAL cloned-voice audio: stays on cachy / Erick's SD card; never git, never a public bucket):
  papp/voices/erick/tts/<fnv32("<lang>|<text>")>.wav   16 kHz PCM16 mono, same key as papp/tts/ (firmware cache)
  papp/voices/erick/index.json                         {voice, langs, entries[{id,cat,es,en,audio_es,audio_en}]}
  papp/voices/erick/MANIFEST.sha256                    cd papp/voices/erick && sha256sum -c MANIFEST.sha256
Firmware rule (for T5): voice mode "erick" -> look up papp/voices/erick/tts/%08x.wav first for es/en,
fall back to papp/tts/%08x.wav (zh always from papp/tts/). Bearer read from ~/.config/zenilify/voicestudio.bearer
(full header line) in-process only; errors print only the exception type.
"""
from __future__ import annotations

import hashlib, json, os, random, re, subprocess, sys, tempfile, time, unicodedata, urllib.request

VS = os.environ.get("VS_URL", "http://127.0.0.1:3900")
HDR = os.path.expanduser("~/.config/zenilify/voicestudio.bearer")
RATE = 16000
SUB = "papp/voices/erick"


def fnv32(s: str) -> int:
    h = 2166136261
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def rows(path):
    for ln in open(path, encoding="utf-8"):
        if not ln.strip() or ln.startswith("#"):
            continue
        p = [x.strip() for x in ln.rstrip("\n").split("|")]
        p += [""] * (5 - len(p))
        yield p[:5]


def _auth():
    k, v = open(HDR).read().strip().split(":", 1)
    return {k.strip(): v.strip()}


def speech(text, lang, voice, seed=7):
    body = json.dumps({"model": "omnivoice", "input": text, "voice": voice, "response_format": "wav",
                       "language": lang, "seed": seed}).encode()
    r = urllib.request.Request(f"{VS}/v1/audio/speech", data=body,
                               headers={**_auth(), "Content-Type": "application/json"})
    return urllib.request.urlopen(r, timeout=180).read()


def transcribe(path, lang):
    bnd = "----n4" + hashlib.md5(path.encode()).hexdigest()
    data = open(path, "rb").read()
    parts = [f'--{bnd}\r\nContent-Disposition: form-data; name="language"\r\n\r\n{lang}\r\n'.encode(),
             f'--{bnd}\r\nContent-Disposition: form-data; name="file"; filename="a.wav"\r\n'
             f'Content-Type: audio/wav\r\n\r\n'.encode() + data + b"\r\n", f"--{bnd}--\r\n".encode()]
    r = urllib.request.Request(f"{VS}/v1/audio/transcriptions", data=b"".join(parts),
                               headers={**_auth(), "Content-Type": f"multipart/form-data; boundary={bnd}"})
    return json.loads(urllib.request.urlopen(r, timeout=180).read())["text"]


def to16k(wav_bytes, dst):
    with tempfile.NamedTemporaryFile(suffix=".wav") as t:
        t.write(wav_bytes); t.flush()
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", t.name, "-ac", "1", "-ar", str(RATE),
                        "-af", "loudnorm=I=-18:TP=-1.5:LRA=11", "-sample_fmt", "s16", dst + ".tmp.wav"], check=True)
    os.replace(dst + ".tmp.wav", dst)


def render(src, out, voice):
    root = os.path.join(out, SUB)
    os.makedirs(os.path.join(root, "tts"), exist_ok=True)
    entries, n_cat, fails, calls, t0 = [], {}, [], 0, time.time()
    for cat, es, en, zh, _py in rows(src):
        n_cat[cat] = n_cat.get(cat, 0) + 1
        e = {"id": f"{cat}-{n_cat[cat]:03d}", "cat": cat, "es": es, "en": en, "zh": zh}
        for lang, text in (("es", es), ("en", en)):
            if not text:
                e[f"audio_{lang}"] = ""
                continue
            name = f"tts/{fnv32(f'{lang}|{text}'):08x}.wav"
            dst = os.path.join(root, name)
            if not os.path.exists(dst):
                err = ""
                for attempt in range(3):
                    try:
                        to16k(speech(text, lang, voice), dst); calls += 1; err = ""
                        break
                    except Exception as ex:  # noqa: BLE001
                        err = type(ex).__name__
                        time.sleep(3 * (attempt + 1))
                if err:
                    fails.append(f"{e['id']} {lang}: {err}")
                    e[f"audio_{lang}"] = ""
                    continue
            e[f"audio_{lang}"] = name
        entries.append(e)
        print(f"{len(entries):3d} {e['id']:16} {es}", flush=True)
    idx = {"version": 1, "generated": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "rate": RATE,
           "voice": {"name": "erick", "engine": "omnivoice", "profile": voice, "langs": ["es", "en"]},
           "cache_key": "fnv32('<lang>|<text>') -> papp/voices/erick/tts/%08x.wav (fallback papp/tts/)",
           "entries": entries}
    json.dump(idx, open(os.path.join(root, "index.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    files = sorted({x for e in entries for x in (e["audio_es"], e["audio_en"]) if x} | {"index.json"})
    with open(os.path.join(root, "MANIFEST.sha256"), "w") as fh:
        for f in files:
            fh.write(f"{hashlib.sha256(open(os.path.join(root, f), 'rb').read()).hexdigest()}  {f}\n")
    print(f"DONE entries={len(entries)} files={len(files)} new_calls={calls} fails={len(fails)} "
          f"wall_s={time.time() - t0:.0f}")
    for f in fails:
        print("FAIL", f)


def norm(s):
    s = unicodedata.normalize("NFKD", s.lower())
    s = "".join(c for c in s if not unicodedata.combining(c))
    return re.sub(r"[^\w]+", "", s)


def lev(a, b):
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def measure(out, n):
    root = os.path.join(out, SUB)
    idx = json.load(open(os.path.join(root, "index.json"), encoding="utf-8"))
    items = [(e["id"], l, e[l], e[f"audio_{l}"]) for e in idx["entries"] for l in ("es", "en") if e[f"audio_{l}"]]
    random.Random(8).shuffle(items)
    tot_e = tot_n = exact = 0
    for i, lang, ref, rel in items[:n]:
        hyp = transcribe(os.path.join(root, rel), lang)
        a, b = norm(ref), norm(hyp)
        d = lev(a, b); tot_e += d; tot_n += len(a); exact += d == 0
        print(f"{i:16} {lang} cer={d / max(1, len(a)):.3f} | {ref} | {hyp}", flush=True)
    print(f"MEASURE n={min(n, len(items))} CER={tot_e / max(1, tot_n):.4f} exact={exact}")


def manifest(root):
    idx = json.load(open(os.path.join(root, "index.json"), encoding="utf-8"))
    files = sorted({x for e in idx["entries"] for x in (e["audio_es"], e["audio_en"]) if x} | {"index.json"})
    with open(os.path.join(root, "MANIFEST.sha256"), "w") as fh:
        for f in files:
            fh.write(f"{hashlib.sha256(open(os.path.join(root, f), 'rb').read()).hexdigest()}  {f}\n")
    return len(files)


def _retry(fn, *a, tries=4, **k):
    """cachy's 5090 is shared (a2f, comfyui, rag-r): VoiceStudio answers 500 GPU_OOM under pressure."""
    for t in range(tries):
        try:
            return fn(*a, **k)
        except Exception as ex:  # noqa: BLE001
            if t == tries - 1:
                raise RuntimeError(type(ex).__name__) from None
            time.sleep(15 * (t + 1))


def verify(out, voice, thr=0.12, seeds=(11, 23, 37, 53)):
    """STT every clip; re-render any whose CER > thr (OmniVoice sometimes prepends a word from the
    reference prompt, e.g. "was ..."); keep the best seed. Rewrites MANIFEST."""
    root = os.path.join(out, SUB)
    idx = json.load(open(os.path.join(root, "index.json"), encoding="utf-8"))
    bad = fixed = 0
    left = []
    for e in idx["entries"]:
        for lang in ("es", "en"):
            rel, ref = e[f"audio_{lang}"], e[lang]
            if not rel:
                continue
            p = os.path.join(root, rel)
            try:
                cer = lev(norm(ref), norm(_retry(transcribe, p, lang))) / max(1, len(norm(ref)))
            except RuntimeError as ex:
                left.append(f"{e['id']} {lang} stt-error {ex}")
                continue
            if cer <= thr:
                continue
            bad += 1
            best = (cer, None)
            for s in seeds:
                tmp = p + f".s{s}.wav"
                try:
                    to16k(_retry(speech, ref, lang, voice, seed=s), tmp)
                    c = lev(norm(ref), norm(_retry(transcribe, tmp, lang))) / max(1, len(norm(ref)))
                except RuntimeError:
                    if os.path.exists(tmp):
                        os.remove(tmp)
                    continue
                if c < best[0]:
                    if best[1]:
                        os.remove(best[1])
                    best = (c, tmp)
                else:
                    os.remove(tmp)
                if c <= thr:
                    break
            if best[1]:
                os.replace(best[1], p)
            fixed += best[0] <= thr
            if best[0] > thr:
                left.append(f"{e['id']} {lang} cer={best[0]:.2f} {ref}")
            print(f"{e['id']:16} {lang} {cer:.2f} -> {best[0]:.2f}", flush=True)
    n = manifest(root)
    print(f"VERIFY bad={bad} fixed={fixed} still_over={len(left)} manifest_files={n}")
    for x in left:
        print("LEFT", x)


if __name__ == "__main__":
    a = sys.argv[1:]
    voice = a[a.index("--voice") + 1] if "--voice" in a else "2b88f5be"
    if a[0] == "render":
        render(a[1], os.path.expanduser(a[2]), voice)
    elif a[0] == "verify":
        verify(os.path.expanduser(a[1]), voice)
    else:
        measure(os.path.expanduser(a[1]), int(a[a.index("--n") + 1]) if "--n" in a else 24)
