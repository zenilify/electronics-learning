#!/usr/bin/env python3
"""v2 acceptance for the offline brain: pair=/src=auto (30 WAVs per language), translate-text auto,
/sd/index + /sd/file (hash), 404 for the routes it lacks, /summarize (text + audio).

  v2_smoke.py --url http://127.0.0.1:8787 [--n 30]
Token from ~/.config/zenilify/papp_token (never printed). Inputs: ~/papp-brain/testaudio/<lang>_NN.wav.
"""
import argparse, csv, hashlib, io, json, os, statistics, time, urllib.error, urllib.parse, urllib.request, wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
AUD = Path(os.path.expanduser("~/papp-brain/testaudio"))
TOK = open(os.path.expanduser("~/.config/zenilify/papp_token")).read().strip()


def call(url, path, params=None, body=None, ctype=None, method=None):
    q = ("?" + urllib.parse.urlencode(params)) if params else ""
    h = {"Authorization": f"Bearer {TOK}"}
    if ctype:
        h["Content-Type"] = ctype
    req = urllib.request.Request(f"{url}{path}{q}", data=body, headers=h, method=method or ("POST" if body else "GET"))
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            return r.status, r.read(), (time.perf_counter() - t0) * 1000
    except urllib.error.HTTPError as e:
        return e.code, e.read(), (time.perf_counter() - t0) * 1000


def concat(paths, gap_s=0.6):
    pcm, rate = b"", 16000
    for p in paths:
        with wave.open(str(p)) as w:
            rate = w.getframerate()
            pcm += w.readframes(w.getnframes()) + b"\0\0" * int(gap_s * rate)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm)
    return buf.getvalue()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8787")
    ap.add_argument("--n", type=int, default=30)
    a = ap.parse_args()
    rows = list(csv.DictReader(open(HERE / "sentences.tsv", encoding="utf-8"), delimiter="\t"))[: a.n]
    res = {}
    # 1 conversation mode: pair=es,zh with Spanish AND Chinese speakers, plus src=auto with all three languages
    for mode, langs in (("pair=es,zh", ("es", "zh")), ("src=auto>en", ("es", "zh")), ("pair=es,en", ("en",))):
        ok, n, rtt, bad = 0, 0, [], []
        for lang in langs:
            for r in rows:
                body = (AUD / f"{lang}_{int(r['id']):02d}.wav").read_bytes()
                if mode.startswith("pair"):
                    params = {"pair": mode.split("=")[1], "format": "json"}
                    want_src = lang
                    want_tgt = [x for x in mode.split("=")[1].split(",") if x != lang][0]
                else:
                    params = {"src": "auto", "tgt": "en", "format": "json"}
                    want_src, want_tgt = lang, "en"
                st, data, ms = call(a.url, "/translate", params, body, "audio/wav")
                n += 1
                j = json.loads(data) if st == 200 else {}
                good = st == 200 and j.get("detected") == want_src and j.get("tgt") == want_tgt and j.get("tgt_text")
                ok += bool(good)
                rtt.append(ms)
                if not good:
                    bad.append(f"{lang}_{r['id']}: {st} det={j.get('detected')} tgt={j.get('tgt')} '{j.get('src_text', '')[:40]}'")
        res[mode] = {"ok": f"{ok}/{n}", "rtt_ms_median": round(statistics.median(rtt)), "rtt_ms_max": round(max(rtt)),
                     "misses": bad[:6]}
        print(json.dumps({mode: res[mode]}, ensure_ascii=False), flush=True)
    # 2 translate-text src=auto
    ok = 0
    for r in rows[:10]:
        for lang, tgt in (("es", "zh"), ("zh", "es")):
            st, data, _ = call(a.url, "/translate-text", {"src": "auto", "tgt": tgt}, json.dumps({"text": r[lang]}).encode(),
                               "application/json")
            j = json.loads(data) if st == 200 else {}
            ok += st == 200 and j.get("detected") == lang and bool(j.get("tgt_text"))
    res["translate-text auto"] = f"{ok}/20"
    print(json.dumps({"translate-text auto": res["translate-text auto"]}), flush=True)
    # 3 SD sync: index, then fetch 5 files and check their sha256 against the index
    st, data, ms = call(a.url, "/sd/index")
    idx = json.loads(data)
    good = 0
    for it in idx["items"][:: max(1, len(idx["items"]) // 5)][:5]:
        st2, blob, _ = call(a.url, "/sd/file", {"path": it["path"]})
        good += st2 == 200 and hashlib.sha256(blob).hexdigest() == it["sha256"]
    st3, _, _ = call(a.url, "/sd/file", {"path": "../../.config/zenilify/papp_token"})
    res["sd"] = {"index_status": st, "count": idx["count"], "bytes": idx["bytes"], "index_ms": round(ms),
                 "files_hash_ok": f"{good}/5", "traversal_status": st3}
    print(json.dumps({"sd": res["sd"]}), flush=True)
    # 4 the routes the brain does not have must say 404 (Tab5 then falls back)
    res["404"] = {p: call(a.url, p, {"x": "1"}, b"x", "application/octet-stream")[0]
                  for p in ("/ocr-translate", "/stream/start", "/stream/chunk")}
    print(json.dumps({"404": res["404"]}), flush=True)
    # 5 summarize: JSON text (es transcript -> out=en) and a ~1 min audio note (10 es sentences -> out=es)
    text = " ".join(r["es"] for r in rows[:15])
    st, data, ms = call(a.url, "/summarize", {"lang": "auto", "out": "en"}, json.dumps({"text": text}).encode(),
                        "application/json")
    j = json.loads(data) if st == 200 else {"err": data[:200].decode(errors="replace")}
    res["summarize text es->en"] = {"status": st, "ms": round(ms), "title": j.get("title"), "summary": j.get("summary"),
                                    "bullets": len(j.get("bullets") or []), "actions": j.get("actions")}
    print(json.dumps({"summarize text": res["summarize text es->en"]}, ensure_ascii=False), flush=True)
    note = concat([AUD / f"zh_{i:02d}.wav" for i in (15, 16, 17, 18, 28, 29, 30)])
    st, data, ms = call(a.url, "/summarize", {"lang": "auto", "out": "es"}, note, "audio/wav")
    j = json.loads(data) if st == 200 else {"err": data[:200].decode(errors="replace")}
    res["summarize audio zh->es"] = {"status": st, "ms": round(ms), "audio_s": j.get("audio_s"), "lang": j.get("lang"),
                                     "title": j.get("title"), "summary": j.get("summary"),
                                     "actions": j.get("actions"), "timings": j.get("timings_ms")}
    print(json.dumps({"summarize audio": res["summarize audio zh->es"]}, ensure_ascii=False), flush=True)
    (HERE / "results").mkdir(exist_ok=True)
    json.dump(res, open(HERE / "results" / "v2_smoke.json", "w"), ensure_ascii=False, indent=1)
    print("V2_DONE")


if __name__ == "__main__":
    main()
