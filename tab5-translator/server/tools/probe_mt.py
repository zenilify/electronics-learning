#!/usr/bin/env python3
"""MT guard A/B for conversation mode (lane T5-S v2.1). Run on cachy from server/.
Variants: nmt (Riva) · llm (current translate_llm prompt, whisper text) · llm+gloss · llm+2asr (whisper + canary
hypotheses + glossary). zh clips z16-1..4 -> es. Bypasses the cache; prints text + ms per call; N runs."""
import os, sys, time, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from papp.config import load_env_file
from papp.pipeline import Pipeline, read_wav, MT_MODELS
from papp import llm
import riva.client
from riva.client.proto import riva_asr_pb2

key = load_env_file(os.path.expanduser("~/.config/zenilify/nvidia_build.env"))["API_KEY"]
p = Pipeline(key)
GLOSS = "开发板 = placa de desarrollo / dev board; M5Stack; Tab5; Erick; LLM; ESP32; 深圳 = Shenzhen; 明栈 = M5Stack (company)"
BASE = ("You are the interpreter in a face-to-face conversation between a foreign visitor and local people (shops, "
        "electronics markets, taxis, business meetings). Translate spoken Simplified Chinese into natural spoken Spanish. "
        "The input is a speech-recognition transcript and often has recognition errors: homophones (板/版), wrong tones "
        "(买 buy / 卖 sell — pick what fits who is speaking), missing punctuation. Silently repair the transcript from "
        "context, then translate the meaning faithfully, keeping numbers, prices and names. Reply with ONLY the translation.")

def canary(a):
    cfg = riva.client.RecognitionConfig(encoding=riva.client.AudioEncoding.LINEAR_PCM, sample_rate_hertz=a.rate,
          language_code="zh-CN", max_alternatives=1, enable_automatic_punctuation=True, audio_channel_count=1)
    r = riva.client.ASRService(p._auth_for("canary-1b-asr")).stub.Recognize(
        riva_asr_pb2.RecognizeRequest(config=cfg, audio=a.pcm), metadata=p._md("canary-1b-asr"), timeout=8)
    return "".join(x.alternatives[0].transcript for x in r.results if x.alternatives)

def chat(sys_, user, models):
    t = time.perf_counter()
    out, m = llm.nv_chat(key, [{"role": "system", "content": sys_}, {"role": "user", "content": user}], 300, 15, models=models)
    return out.strip(), m.split("/")[-1], int((time.perf_counter() - t) * 1000)

N = int(os.environ.get("N", "2"))
models_list = [MT_MODELS] + [[m] for m in os.environ.get("ALT", "").split(",") if m]
for i in range(1, 5):
    a = read_wav(open(os.path.expanduser(f"~/t5/zhdialog/z16-{i}.wav"), "rb").read())
    w, _ = p.asr(a, "zh"); c = canary(a)
    print(f"\n## z16-{i}  whisper={w}  canary={c}", flush=True)
    t = time.perf_counter(); print(f"  nmt  {int((time.perf_counter()-t)*1000) + 0:5d}ms {p._translate(w, 'zh', 'es')}", flush=True)
    for models in models_list:
        for n in range(N):
            for name, s_, u in (
                ("llm", BASE, w),
                ("llm+gloss", BASE + " Glossary (names/products that may appear): " + GLOSS, w),
                ("llm+2asr", BASE + " You get two independent recognitions of the SAME utterance; where they differ, "
                 "choose what makes sense. Glossary: " + GLOSS, f"ASR A: {w}\nASR B: {c}")):
                try: out, m, ms = chat(s_, u, models)
                except Exception as e: out, m, ms = f"FAIL {type(e).__name__}", "-", 0
                print(f"  {name:10} {m[:24]:24} {ms:5d}ms {out}", flush=True)
