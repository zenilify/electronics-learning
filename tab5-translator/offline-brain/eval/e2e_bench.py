#!/usr/bin/env python3
"""End-to-end speech bench — same WAVs against any PAPP backend (cloud or offline brain).

  e2e_bench.py make-audio --url <cloud>            # synthesise es/zh/en inputs once (Magpie, cloud /tts)
  e2e_bench.py run --url <backend> --tag brain [--n 30] [--dirs es>zh,zh>es]
Inputs: ~/papp-brain/testaudio/<lang>_NN.wav. Uses the Tab5 contract (raw audio/wav POST).
Scores: ASR CER vs the source sentence, chrF of the translation vs the reference, client RTT.
"""
import argparse, csv, json, os, statistics, time, urllib.parse, urllib.request, unicodedata, re
from pathlib import Path

HERE = Path(__file__).resolve().parent
AUD = Path(os.path.expanduser("~/papp-brain/testaudio"))


def tok():
    return open(os.path.expanduser("~/.config/zenilify/papp_token")).read().strip()


def post(url, path, params, body, ctype, t):
    req = urllib.request.Request(f"{url}{path}?{urllib.parse.urlencode(params)}", data=body, method="POST",
                                 headers={"Content-Type": ctype, "Authorization": f"Bearer {t}"})
    t0 = time.perf_counter()
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    return data, (time.perf_counter() - t0) * 1000


def norm(s):
    s = unicodedata.normalize("NFKC", s.lower())
    return re.sub(r"[\W_]+", "", s)


def cer(hyp, ref):
    h, r = norm(hyp), norm(ref)
    d = list(range(len(h) + 1))
    for i, rc in enumerate(r, 1):
        prev, d[0] = d[0], i
        for j, hc in enumerate(h, 1):
            prev, d[j] = d[j], min(d[j] + 1, d[j - 1] + 1, prev + (rc != hc))
    return d[len(h)] / max(1, len(r))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["make-audio", "run"])
    ap.add_argument("--url", required=True)
    ap.add_argument("--tag", default="x")
    ap.add_argument("--n", type=int, default=30)
    ap.add_argument("--dirs", default="es>zh,zh>es")
    a = ap.parse_args()
    rows = list(csv.DictReader(open(HERE / "sentences.tsv", encoding="utf-8"), delimiter="\t"))[: a.n]
    t = tok()
    if a.cmd == "make-audio":
        AUD.mkdir(parents=True, exist_ok=True)
        for r in rows:
            for lang in ("es", "zh", "en"):
                p = AUD / f"{lang}_{int(r['id']):02d}.wav"
                if not p.exists():
                    wav, _ = post(a.url, "/tts", {"lang": lang}, json.dumps({"text": r[lang]}).encode(),
                                  "application/json", t)
                    p.write_bytes(wav)
        print("audio OK", len(list(AUD.glob("*.wav"))))
        return
    import sacrebleu
    out, summ = [], {"tag": a.tag, "url": a.url}
    for d in a.dirs.split(","):
        src, tgt = d.split(">")
        rtt, cers, hyps, refs, srv = [], [], [], [], []
        for r in rows:
            body = (AUD / f"{src}_{int(r['id']):02d}.wav").read_bytes()
            data, ms = post(a.url, "/translate", {"src": src, "tgt": tgt, "format": "json"}, body, "audio/wav", t)
            j = json.loads(data)
            rtt.append(ms); srv.append(j["timings_ms"])
            cers.append(cer(j["src_text"], r[src])); hyps.append(j["tgt_text"]); refs.append(r[tgt])
            out.append([a.tag, d, r["id"], round(ms), j["src_text"], j["tgt_text"], r[tgt], j.get("asr_model")])
        med = lambda k: round(statistics.median(x[k] for x in srv))  # noqa: E731
        summ[d] = {"n": len(rows), "rtt_ms_median": round(statistics.median(rtt)), "rtt_ms_max": round(max(rtt)),
                   "asr_ms": med("asr"), "nmt_ms": med("nmt"), "tts_ms": med("tts"),
                   "asr_cer_mean": round(statistics.mean(cers), 3),
                   "chrF": round(sacrebleu.corpus_chrf(hyps, [refs]).score, 1)}
        print(json.dumps({d: summ[d]}), flush=True)
    (HERE / "results").mkdir(exist_ok=True)
    with open(HERE / "results" / f"e2e_{a.tag}.tsv", "w", encoding="utf-8", newline="") as fh:
        w = csv.writer(fh, delimiter="\t")
        w.writerow(["tag", "dir", "id", "rtt_ms", "asr_text", "translation", "reference", "asr_model"])
        w.writerows(out)
    json.dump(summ, open(HERE / "results" / f"e2e_{a.tag}.json", "w"), ensure_ascii=False, indent=1)
    print("SUMMARY", json.dumps(summ, ensure_ascii=False))


if __name__ == "__main__":
    main()
