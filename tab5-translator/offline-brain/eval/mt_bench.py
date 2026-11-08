#!/usr/bin/env python3
"""MT bake-off on eval/sentences.tsv (30 trip/business sentences, my own references).

  mt_bench.py nllb | qwen25 | qwen3 | cloud [--url http://100.69.244.21:8787]
Writes eval/results/mt_<engine>.tsv (outputs) and prints chrF per direction + latency.
cloud = the production NVIDIA riva-translate-1_6b via the papp server's /translate-text
(token read from ~/.config/zenilify/papp_token in-process, never printed).
"""
import csv, json, os, statistics, sys, time, urllib.parse, urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "brain"))
DIRS = [("es", "zh"), ("zh", "es"), ("en", "zh"), ("zh", "en")]


def load_engine(name, url):
    if name == "cloud":
        tok = open(os.path.expanduser("~/.config/zenilify/papp_token")).read().strip()

        def f(text, src, tgt):
            q = urllib.parse.urlencode({"src": src, "tgt": tgt, "format": "json"})
            req = urllib.request.Request(f"{url}/translate-text?{q}", method="POST",
                                         data=json.dumps({"text": text}).encode(),
                                         headers={"Content-Type": "application/json",
                                                  "Authorization": f"Bearer {tok}"})
            with urllib.request.urlopen(req, timeout=60) as r:
                j = json.load(r)
            f.last_ms = j["timings_ms"]["nmt"]
            return j["tgt_text"]
        return f
    import engines
    return {"nllb": lambda: engines.NllbMT(),
            "nllb13": lambda: engines.NllbMT("facebook/nllb-200-distilled-1.3B"),
            "qwen25": lambda: engines.LlmMT("mlx-community/Qwen2.5-7B-Instruct-4bit"),
            "qwen3": lambda: engines.LlmMT("mlx-community/Qwen3-8B-4bit")}[name]()


def main():
    import sacrebleu
    name = sys.argv[1]
    url = sys.argv[sys.argv.index("--url") + 1] if "--url" in sys.argv else "http://100.69.244.21:8787"
    rows = list(csv.DictReader(open(HERE / "sentences.tsv", encoding="utf-8"), delimiter="\t"))
    t = time.perf_counter()
    eng = load_engine(name, url)
    load_s = time.perf_counter() - t
    eng("Hola", "es", "zh")  # warm-up
    out, summary = [], {"engine": name, "load_s": round(load_s, 1)}
    for src, tgt in DIRS:
        hyps, lat = [], []
        for r in rows:
            t = time.perf_counter()
            h = eng(r[src], src, tgt)
            lat.append(getattr(eng, "last_ms", None) or (time.perf_counter() - t) * 1000)
            hyps.append(h)
            out.append([r["id"], f"{src}>{tgt}", r[src], h, r[tgt]])
        refs = [r[tgt] for r in rows]
        chrf = sacrebleu.corpus_chrf(hyps, [refs]).score
        summary[f"{src}>{tgt}"] = {"chrF": round(chrf, 1), "lat_ms_median": round(statistics.median(lat)),
                                   "lat_ms_max": round(max(lat))}
        print(json.dumps({f"{src}>{tgt}": summary[f"{src}>{tgt}"]}), flush=True)
    (HERE / "results").mkdir(exist_ok=True)
    with open(HERE / "results" / f"mt_{name}.tsv", "w", encoding="utf-8", newline="") as fh:
        w = csv.writer(fh, delimiter="\t")
        w.writerow(["id", "dir", "source", "hypothesis", "reference"])
        w.writerows(out)
    json.dump(summary, open(HERE / "results" / f"mt_{name}.json", "w"), ensure_ascii=False, indent=1)
    print("SUMMARY", json.dumps(summary))


if __name__ == "__main__":
    main()
