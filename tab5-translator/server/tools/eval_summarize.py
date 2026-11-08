#!/usr/bin/env python3
"""/summarize?out=es acceptance check (lane T5-S v2.1, DONE (c)). stdlib only.

  python3 tools/eval_summarize.py --url http://192.168.1.5:8787 --token-file ~/.config/zenilify/papp_token \\
      ~/t5/zhmeeting/m16.wav notes/en.wav notes/de.wav [--expect m16.wav=Erick,LLM]

Per note: language of every output field (no CJK; no German/English sentence in an es summary), actions are
plain strings, expected names present, deadlines kept as said. Exit 1 if any check fails."""
import argparse, json, re, ssl, sys, time, urllib.request, os

_CTX = ssl.create_default_context()
if not _CTX.get_ca_certs() and os.path.exists("/etc/ssl/cert.pem"):
    _CTX = ssl.create_default_context(cafile="/etc/ssl/cert.pem")
CJK = re.compile(r"[㐀-鿿豈-﫿]")
DE = set("der die das und ist nicht wir ich sie ein eine mit für auf zu den dem bis wird werden haben soll muss".split())
EN = set("the and is are we will should must to of for with by this that next have be".split())
ES = set("el la los las de y que en un una para con por se del al es son debe hasta próxima".split())


def lang_of(s):
    w = re.findall(r"[a-záéíóúüñäöß]+", s.lower())
    sc = {"es": sum(x in ES for x in w) + 2 * len(re.findall(r"[áéíóúñ¿¡]", s.lower())),
          "en": sum(x in EN for x in w), "de": sum(x in DE for x in w) + 2 * len(re.findall(r"[äöüß]", s.lower()))}
    return max(sc, key=sc.get) if max(sc.values()) else "?"


def check(res, out, expect):
    fails = []
    fields = [("title", res.get("title", "")), ("summary", res.get("summary", ""))]
    fields += [(f"bullet{i}", b) for i, b in enumerate(res.get("bullets", []))]
    fields += [(f"action{i}", a) for i, a in enumerate(res.get("actions", []))]
    for name, v in fields:
        if not isinstance(v, str):
            fails.append(f"{name} not a string ({type(v).__name__})"); continue
        if out != "zh" and CJK.search(v):
            fails.append(f"{name} has CJK: {v[:50]}")
        if out == "es" and len(v.split()) >= 4 and lang_of(v) not in ("es", "?"):
            fails.append(f"{name} looks {lang_of(v)}: {v[:60]}")
    blob = " ".join(v for _, v in fields if isinstance(v, str))
    for e in expect:
        if e.lower() not in blob.lower():
            fails.append(f"missing '{e}'")
    if re.search(r"\b20\d\d\b", " ".join(a for a in res.get("actions", []) if isinstance(a, str))):
        fails.append("action carries an invented year")
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", required=True); ap.add_argument("--token-file", required=True)
    ap.add_argument("--out", default="es"); ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    tok = open(os.path.expanduser(a.token_file)).read().strip()
    exp = {}
    for e in a.expect:
        k, v = e.split("=", 1); exp[k] = [x for x in v.split(",") if x]
    bad = 0
    for f in a.files:
        data = open(os.path.expanduser(f), "rb").read()
        req = urllib.request.Request(a.url.rstrip("/") + f"/summarize?lang=auto&out={a.out}", data=data, method="POST",
                                     headers={"Authorization": f"Bearer {tok}", "Content-Type": "audio/wav"})
        t = time.perf_counter()
        try:
            with urllib.request.urlopen(req, timeout=180, context=_CTX) as r:
                res = json.loads(r.read())
        except Exception as e:  # noqa: BLE001
            print(f"## {os.path.basename(f)}  ERR {type(e).__name__}"); bad += 1; continue
        ms = int((time.perf_counter() - t) * 1000)
        fails = check(res, a.out, exp.get(os.path.basename(f), []))
        bad += bool(fails)
        print(f"## {os.path.basename(f)}  lang={res.get('lang')} {ms} ms model={res.get('model','').split('/')[-1]} "
              f"t={res.get('timings_ms')}  {'PASS' if not fails else 'FAIL ' + '; '.join(fails)}")
        print("  title:", res.get("title")); print("  summary:", res.get("summary"))
        for b in res.get("bullets", []): print("  -", b)
        for x in res.get("actions", []): print("  [ ]", x)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
