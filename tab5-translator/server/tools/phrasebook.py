#!/usr/bin/env python3
"""Pre-render the PAPP phrasebook into an SD-card folder (run on cachy from server/).

  ~/papp/.venv/bin/python tools/phrasebook.py phrasebook/phrases.psv ~/papp/phrasebook-pack

Output (copy the `papp/` folder to the TF card root, it merges with the Tab5's own folder):
  papp/tts/<fnv32("<lang>|<text>")>.wav   16 kHz PCM16 mono — the SAME names the firmware's speech
                                          cache uses (client/tab5/main/sdcard.c), so v0.2.0 already
                                          plays them offline with no firmware change
  papp/phrasebook/index.json              categories + entries {id, cat, zh, py, es, en, audio_zh, audio_es}
  papp/phrasebook/MANIFEST.sha256         sha256 of every file above (sha256sum -c compatible)
TTS goes through papp.pipeline (server disk cache warmed too, PAPP_CACHE_DIR) and is serialised
at <= RPM requests/minute for the NVIDIA free tier. Resumable: existing WAVs are kept.
"""
from __future__ import annotations

import hashlib, json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("PAPP_CACHE_DIR", os.path.expanduser("~/papp/cache"))
from papp.config import load_env_file  # noqa: E402
from papp.ocr import pinyin  # noqa: E402
from papp.pipeline import Pipeline, to_wav  # noqa: E402
from papp import cache  # noqa: E402

RATE = 16000
RPM = int(os.environ.get("PAPP_RPM", "30"))
CATS = {"basico": ("Básico", "Basics", "基本"), "comida": ("Comida", "Food", "吃饭"),
        "bebidas": ("Bebidas", "Drinks", "饮料"), "transporte": ("Transporte", "Getting around", "交通"),
        "hotel": ("Hotel", "Hotel", "酒店"), "compras": ("Compras", "Shopping", "购物"),
        "electronica": ("Electrónica", "Electronics", "电子"), "salud": ("Salud", "Health", "健康"),
        "emergencia": ("Emergencia", "Emergency", "紧急"), "tiempo": ("Números y tiempo", "Numbers & time", "数字和时间"),
        "negocios": ("Negocios", "Business", "商务"), "reuniones": ("Reuniones", "Meetings", "会议"),
        "fabrica": ("Visita a fábrica", "Factory visit", "参观工厂"), "demo": ("Demo M5Stack", "M5Stack demo", "演示"),
        "charla": ("Conversación", "Small talk", "聊天"), "lugares": ("Llévame a", "Take me to", "带我去")}


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
        p = [x.strip() for x in ln.rstrip("\n").split(" | ")]
        p += [""] * (5 - len(p))
        yield p[:5]


def main(src, out):
    key = load_env_file(os.path.expanduser("~/.config/zenilify/nvidia_build.env"))["API_KEY"]
    pl = Pipeline(key)
    root = os.path.join(out, "papp")
    os.makedirs(os.path.join(root, "tts"), exist_ok=True)
    os.makedirs(os.path.join(root, "phrasebook"), exist_ok=True)
    entries, n_cat, last, calls, fails = [], {}, 0.0, 0, []
    for cat, es, en, zh, py in rows(src):
        n_cat[cat] = n_cat.get(cat, 0) + 1
        e = {"id": f"{cat}-{n_cat[cat]:03d}", "cat": cat, "zh": zh, "py": py or pinyin(zh), "es": es, "en": en,
             "py_src": "hand" if py else "pypinyin"}
        for lang, text in (("zh", zh), ("es", es)):
            name = f"tts/{fnv32(f'{lang}|{text}'):08x}.wav"
            dst = os.path.join(root, name)
            if not os.path.exists(dst):
                k = cache.key("tts", {"zh": "Magpie-Multilingual.ZH-CN.HouZhen", "es": "Magpie-Multilingual.ES-US.Isabela"}[lang], lang, RATE, text)
                cached = cache.get("tts", k) is not None
                if not cached:
                    wait = 60.0 / RPM - (time.monotonic() - last)
                    if wait > 0:
                        time.sleep(wait)
                    last = time.monotonic()
                    calls += 1
                for attempt in range(3):
                    try:
                        pcm = pl.tts(text, lang, RATE)
                        break
                    except Exception as ex:  # noqa: BLE001
                        pcm = b""
                        time.sleep(5 * (attempt + 1))
                        err = type(ex).__name__
                if not pcm:
                    fails.append(f"{e['id']} {lang}: {err}")
                    e[f"audio_{lang}"] = ""
                    continue
                with open(dst + ".tmp", "wb") as fh:
                    fh.write(to_wav(pcm, RATE))
                os.replace(dst + ".tmp", dst)
            e[f"audio_{lang}"] = name
        entries.append(e)
        print(f"{len(entries):3d} {e['id']:16} {zh}", flush=True)
    cats = [{"id": c, "es": CATS.get(c, (c, c, c))[0], "en": CATS.get(c, (c, c, c))[1], "zh": CATS.get(c, (c, c, c))[2],
             "count": n} for c, n in n_cat.items()]
    idx = {"version": 1, "generated": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "rate": RATE,
           "voices": {"zh": "Magpie-Multilingual.ZH-CN.HouZhen", "es": "Magpie-Multilingual.ES-US.Isabela"},
           "cache_key": "fnv32('<lang>|<text>') -> papp/tts/%08x.wav", "categories": cats, "entries": entries}
    with open(os.path.join(root, "phrasebook", "index.json"), "w", encoding="utf-8") as fh:
        json.dump(idx, fh, ensure_ascii=False, indent=1)
    files = sorted({e["audio_zh"] for e in entries if e["audio_zh"]} | {e["audio_es"] for e in entries if e["audio_es"]}
                   | {"phrasebook/index.json"})
    with open(os.path.join(root, "phrasebook", "MANIFEST.sha256"), "w") as fh:
        for f in files:
            fh.write(f"{hashlib.sha256(open(os.path.join(root, f), 'rb').read()).hexdigest()}  {f}\n")
    print(f"DONE entries={len(entries)} files={len(files)} new_tts_calls={calls} fails={len(fails)}")
    for f in fails:
        print("FAIL", f)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
