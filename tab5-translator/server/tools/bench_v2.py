#!/usr/bin/env python3
"""PAPP v2 endpoint bench — stdlib only, runs on the MacBook (the "desktop client") or anywhere.

  ssh cachy cat .config/zenilify/papp_token | python3 tools/bench_v2.py --token-file - \\
      --url http://100.69.244.21:8787 --url https://papp-....run.app [--image sign.jpg] [--md out.md]

Self-contained: test speech comes from /tts itself. Prints one latency row per endpoint and URL;
--md writes the table as Markdown. The token is read from a file/stdin, never argv, never printed.
"""
from __future__ import annotations

import argparse, base64, io, json, os, ssl, sys, time, urllib.error, urllib.request, wave

# python.org macOS builds ship without a CA store ("Install Certificates.command" not run):
# fall back to the system bundle so HTTPS (Cloud Run) verifies instead of failing with URLError.
_CTX = ssl.create_default_context()
if not _CTX.get_ca_certs() and os.path.exists("/etc/ssl/cert.pem"):
    _CTX = ssl.create_default_context(cafile="/etc/ssl/cert.pem")

def call(url, path, token, body=None, ctype="application/json", method="POST", timeout=90):
    data = json.dumps(body).encode() if isinstance(body, (dict, list)) else body
    req = urllib.request.Request(url + path, data=data, method=method,
                                 headers={"Authorization": f"Bearer {token}", "Content-Type": ctype})
    t = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout, context=_CTX) as r:
            raw, hdr, code = r.read(), {k.lower(): v for k, v in r.headers.items()}, r.status
    except urllib.error.HTTPError as e:
        raw, hdr, code = e.read(), {k.lower(): v for k, v in e.headers.items()}, e.code
    except Exception as e:  # noqa: BLE001
        return None, {}, f"ERR {type(e).__name__}:{str(getattr(e, 'reason', ''))[:40]}", round((time.perf_counter() - t) * 1000)
    return raw, hdr, code, round((time.perf_counter() - t) * 1000)

def pcm_of(wav_bytes):
    with wave.open(io.BytesIO(wav_bytes)) as w:
        return w.readframes(w.getnframes()), w.getframerate()

def wav_of(pcm, rate=16000):
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm)
    return b.getvalue()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", action="append", required=True)
    ap.add_argument("--token-file", required=True)
    ap.add_argument("--image")
    ap.add_argument("--md")
    ap.add_argument("--clip", action="append", default=[], help="WAV for the pair=es,zh mt=llm vs mt=nmt A/B (repeatable)")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--ab-only", action="store_true", help="only the mt A/B rows")
    a = ap.parse_args()
    tok = (sys.stdin if a.token_file == "-" else open(os.path.expanduser(a.token_file))).read().strip()
    rows = []
    if a.clip:
        import statistics, urllib.parse
        for url in a.url:
            url = url.rstrip("/")
            for mt in ("llm", "nmt"):
                tot, stages = [], []
                for f in a.clip:
                    wav = open(os.path.expanduser(f), "rb").read()
                    for r in range(a.reps):
                        raw, h, c, ms = call(url, f"/translate?pair=es,zh&mt={mt}&format=wav&rate=16000&nocache=1", tok, wav, "audio/wav")
                        if c != 200:
                            print(f"{url[:30]} {mt} {os.path.basename(f)} HTTP {c}", flush=True); continue
                        t = json.loads(h.get("x-papp-timings", "{}") or "{}")
                        tot.append(ms); stages.append(t)
                        if r == 0:
                            print(f"{url[:30]:30} mt={mt} {os.path.basename(f):14} {ms:5d} ms {t} det={h.get('x-papp-detected')} "
                                  f"-> {urllib.parse.unquote(h.get('x-papp-tgt-text', ''))[:90]}"
                                  f"{'  [alt: ' + urllib.parse.unquote(h.get('x-papp-asr-alt', ''))[:40] + ']' if h.get('x-papp-asr-alt') else ''}", flush=True)
                if tot:
                    med = {k: int(statistics.median([s_.get(k, 0) for s_ in stages])) for k in ("asr", "nmt", "tts", "total")}
                    note = (f"n={len(tot)} median client {int(statistics.median(tot))} ms (p90 {sorted(tot)[int(0.9 * (len(tot) - 1))]}) "
                            f"server median asr {med['asr']} · mt {med['nmt']} · tts {med['tts']} · total {med['total']}")
                    print(f"{url[:30]:30} mt={mt} {note}", flush=True)
                    rows.append((url.split("//")[1][:38], f"/translate pair=es,zh mt={mt} (nocache)", 200, int(statistics.median(tot)), note))
        if a.ab_only:
            a.url = []
    for url in a.url:
        url = url.rstrip("/")
        def row(name, code, ms, note=""):
            rows.append((url.split("//")[1][:38], name, code, ms, note))
            print(f"{url[:40]:40} {name:28} {code!s:6} {ms:6} ms  {note}", flush=True)
        raw, h, c, ms = call(url, "/health", tok, method="GET"); row("GET /health", c, ms,
            (json.loads(raw).get("version", "?") if c == 200 else ""))
        # speech fixtures from /tts (first call per text may miss, second must hit)
        fx = {}
        for lang, text in (("es", "Buenos días, ¿dónde está la estación de tren?"),
                           ("zh", "你好，请问洗手间在哪里？"), ("en", "Nice to meet you, this is my translator.")):
            raw, h, c, ms = call(url, f"/tts?lang={lang}", tok, {"text": text}); row(f"/tts {lang} (1st)", c, ms, h.get("x-papp-cache", ""))
            raw2, h2, c2, ms2 = call(url, f"/tts?lang={lang}", tok, {"text": text}); row(f"/tts {lang} (repeat)", c2, ms2, h2.get("x-papp-cache", ""))
            if c == 200: fx[lang] = raw
        for lang in ("es", "zh"):
            if lang not in fx: continue
            raw, h, c, ms = call(url, "/translate?src=auto&pair=es,zh&format=wav", tok, fx[lang], "audio/wav")
            row(f"/translate auto pair=es,zh [{lang}]", c, ms,
                f"det={h.get('x-papp-detected','')} -> {h.get('x-papp-tgt','')} t={h.get('x-papp-timings','')}")
        if "en" in fx:
            raw, h, c, ms = call(url, "/translate?src=auto&tgt=zh", tok, fx["en"], "audio/wav")
            j = json.loads(raw) if c == 200 else {}
            row("/translate auto tgt=zh [en]", c, ms, f"det={j.get('detected')} {j.get('tgt_text','')[:24]}")
            raw, h, c, ms = call(url, "/translate?src=en&tgt=es&format=wav", tok, fx["en"], "audio/wav")
            row("/translate en>es (v1 path)", c, ms, h.get("x-papp-timings", ""))
        raw, h, c, ms = call(url, "/translate-text?src=auto&tgt=zh", tok, {"text": "¿Cuánto cuesta este cable USB-C?"})
        j = json.loads(raw) if c == 200 else {}
        row("/translate-text auto>zh", c, ms, f"det={j.get('detected')} {j.get('tgt_text','')}")
        raw, h, c, ms = call(url, "/fx", tok, method="GET")
        j = json.loads(raw) if c == 200 else {}
        row("GET /fx", c, ms, f"CNY>MXN={j.get('pairs',{}).get('CNY>MXN')} stale={j.get('stale')}")
        if "es" in fx and "en" in fx:
            note = b"".join(pcm_of(fx[k])[0] + b"\0" * 16000 for k in ("es", "es", "es"))
            raw, h, c, ms = call(url, "/summarize?lang=auto&out=es", tok, wav_of(note), "audio/wav")
            j = json.loads(raw) if c == 200 else {}
            row("/summarize (voice note)", c, ms, f"{j.get('title','')[:30]} model={str(j.get('model','')).split('/')[-1]} t={j.get('timings_ms')}")
        if a.image:
            img = open(a.image, "rb").read()
            for eng in ("ocr", "vl", "best"):
                raw, h, c, ms = call(url, f"/ocr-translate?engine={eng}", tok, img, "image/jpeg")
                j = json.loads(raw) if c == 200 else {}
                it = j.get("items") or []
                row(f"/ocr-translate engine={eng}", c, ms,
                    f"{j.get('engine','')} n={len(it)} {(it[0]['zh']+'='+it[0]['es']) if it else raw[:60] if raw else ''}")
        if "zh" in fx:
            raw, h, c, ms = call(url, "/stream/start?lang=auto&tgt=es", tok, {})
            sid = json.loads(raw)["sid"] if c == 200 else ""
            row("/stream/start", c, ms)
            pcm = pcm_of(fx["zh"])[0] + b"\0" * 32000
            finals, worst, n, partial = [], 0, 0, ""
            for i in range(0, len(pcm), 32000):
                raw, h, c, ms = call(url, f"/stream/chunk?sid={sid}", tok, pcm[i:i + 32000], "application/octet-stream")
                if c != 200: row("/stream/chunk", c, ms, raw[:80] if raw else ""); break
                j = json.loads(raw); finals += j["final"]; worst = max(worst, ms); n += 1; partial = j["partial"] or partial
            raw, h, c, ms = call(url, f"/stream/stop?sid={sid}", tok, b"", "application/octet-stream")
            if c == 200: finals += json.loads(raw)["final"]
            row("/stream/chunk (1 s chunks)", 200 if n else "-", worst,
                f"{n} chunks, worst shown; partial='{partial[:16]}' finals={[(f.get('text'), f.get('tgt_text')) for f in finals]}"[:150])
    if a.md:
        with open(a.md, "w") as fh:
            fh.write("| target | endpoint | HTTP | ms | note |\n|---|---|---|---|---|\n")
            for r in rows:
                fh.write("| " + " | ".join(str(x).replace("|", "/") for x in r) + " |\n")

if __name__ == "__main__":
    main()
