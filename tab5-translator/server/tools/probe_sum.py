#!/usr/bin/env python3
"""/summarize model A/B (lane T5-S v2.1). Run on cachy from server/ with the secrets env:
  NVIDIA_KEY_FILE=~/.config/zenilify/nvidia_build.env PAPP_TOKEN_FILE=~/.config/zenilify/papp_token \\
  ~/papp/.venv/bin/python tools/probe_sum.py m16.wav en.wav de.wav
Transcribes once (cached in ~/papp/logs/sum_tx.json), then runs the server's own prompt on each model."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from papp import app as A
from papp import llm
from papp.pipeline import read_wav
from eval_summarize import check

TX = os.path.expanduser("~/papp/logs/sum_tx.json")
tx = json.load(open(TX)) if os.path.exists(TX) else {}
files = sys.argv[1:]
for f in files:
    b = os.path.basename(f)
    if b not in tx:
        tx[b] = A.pipeline.asr_long(read_wav(open(os.path.expanduser(f), "rb").read()), "auto")
json.dump(tx, open(TX, "w"), ensure_ascii=False)
models = os.environ.get("MODELS", "nvidia/nemotron-3.5-super-vl-120b-a12b,nvidia/nemotron-3.5-lightning-30b-a3b").split(",")
out = os.environ.get("OUT", "es")
for b, (text, det) in tx.items():
    print(f"\n## {b} det={det} transcript: {text[:160]}")
    for m in models:
        msgs = [{"role": "system", "content": A._SUM_PROMPT[out] + A._SUM_GLOSS + ' Reply ONLY JSON: {"title": "<= 8 words", '
                 '"summary": "2-3 sentences", "bullets": ["key points, max 6"], "actions": ["plain strings like '
                 '\'Owner: task — deadline as said (e.g. before 5 December)\'; never invent a year; [] if none"]}'},
                {"role": "user", "content": f"Transcript (summarise it in {A._SUM_LANG[out]}):\n{text}"}]
        t = time.perf_counter()
        try:
            raw, used = llm.nv_chat(A.settings.nvidia_api_key, msgs, 900, 60, models=[m], json_mode=True)
            j = llm.parse_json(raw)
            res = {"title": A._flat(j.get("title", "")), "summary": A._flat(j.get("summary", "")),
                   "bullets": [A._flat(x) for x in j.get("bullets") or []], "actions": [A._flat(x) for x in j.get("actions") or []]}
            fails = check(res, out, ["Erick", "LLM"] + (["OEE"] if b.startswith("de") else []))
            print(f"  {m.split('/')[-1]:34} {int((time.perf_counter()-t)*1000):5d} ms {'PASS' if not fails else 'FAIL ' + '; '.join(fails)}")
            print("    title:", res["title"]); [print("    [ ]", a) for a in res["actions"]]
        except Exception as e:  # noqa: BLE001
            print(f"  {m.split('/')[-1]:34} FAIL {type(e).__name__}")
