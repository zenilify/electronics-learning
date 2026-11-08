#!/usr/bin/env python3
"""zh ASR bake-off for the homophone problem (板/版, 买/卖) — lane T5-S v2.1, 2026-11-08.
Run on cachy from server/:  ~/papp/.venv/bin/python tools/probe_asr_zh.py [engine,...]
Clips: ~/t5/zhdialog/z16-{1..4}.wav (no reference text: printed for review) and ~/t5/zhmeeting/m16.wav
(reference ~/t5/zhmeeting/script.txt -> CER). Prints texts + ms; never prints secrets."""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import riva.client
from riva.client.proto import riva_asr_pb2
from papp.config import load_env_file
from papp.pipeline import Pipeline, PcmAudio, read_wav

key = load_env_file(os.path.expanduser("~/.config/zenilify/nvidia_build.env"))["API_KEY"]
p = Pipeline(key)
HOT = ["开发板", "M5Stack", "Tab5", "Erick", "LLM", "ESP32", "深圳", "明栈"]

def rec(fn, code, a, boost=False, custom=None, timeout=20):
    cfg = riva.client.RecognitionConfig(encoding=riva.client.AudioEncoding.LINEAR_PCM,
        sample_rate_hertz=a.rate, language_code=code, max_alternatives=1,
        enable_automatic_punctuation=True, audio_channel_count=1)
    if boost:
        riva.client.add_word_boosting_to_config(cfg, HOT, 20.0)
    for k, v in (custom or {}).items():
        cfg.custom_configuration[k] = v
    stub = riva.client.ASRService(p._auth_for(fn)).stub
    r = stub.Recognize(riva_asr_pb2.RecognizeRequest(config=cfg, audio=a.pcm), metadata=p._md(fn), timeout=timeout)
    return "".join(x.alternatives[0].transcript.strip() for x in r.results if x.alternatives)

PROMPT = "这是关于M5Stack开发板、Tab5、ESP32和LLM模块的对话，Erick在深圳买开发板。"
ENGINES = {
    "whisper-zh": lambda a: rec("whisper-large-v3", "zh-CN", a),
    "whisper-multi": lambda a: rec("whisper-large-v3", "multi", a),
    "whisper-zh-prompt": lambda a: rec("whisper-large-v3", "zh-CN", a, custom={"prompt": PROMPT}),
    "whisper-zh-initprompt": lambda a: rec("whisper-large-v3", "zh-CN", a, custom={"initial_prompt": PROMPT}),
    "parakeet-zh": lambda a: rec("parakeet-ctc-0_6b-zh-cn", "zh-CN", a),
    "parakeet-zh-boost": lambda a: rec("parakeet-ctc-0_6b-zh-cn", "zh-CN", a, boost=True),
    "rnnt-multi-zh": lambda a: rec("parakeet-1_1b-rnnt-multilingual-asr", "zh-CN", a),
    "canary-zh": lambda a: rec("canary-1b-asr", "zh-CN", a),
}

def chunks(a, sec=int(os.environ.get("CHUNK_S", "25"))):
    step = sec * a.rate * 2
    return [PcmAudio(a.pcm[i:i + step], a.rate) for i in range(0, len(a.pcm), step)]

def norm(s): return re.sub(r"[\s，。！？、,.!?：:；;“”\"'（）()]", "", s)

def cer(ref, hyp):
    r, h = norm(ref), norm(hyp)
    d = list(range(len(h) + 1))
    for i, rc in enumerate(r, 1):
        prev, d[0] = d[0], i
        for j, hc in enumerate(h, 1):
            prev, d[j] = d[j], min(d[j] + 1, d[j - 1] + 1, prev + (rc != hc))
    return d[len(h)] / max(1, len(r))

sel = sys.argv[1].split(",") if len(sys.argv) > 1 else list(ENGINES)
ref = open(os.path.expanduser("~/t5/zhmeeting/script.txt"), encoding="utf-8").read()
m16 = read_wav(open(os.path.expanduser("~/t5/zhmeeting/m16.wav"), "rb").read())
for name in sel:
    for i in range(1, 5):
        a = read_wav(open(os.path.expanduser(f"~/t5/zhdialog/z16-{i}.wav"), "rb").read())
        t = time.perf_counter()
        try: out = ENGINES[name](a)
        except Exception as e:
            det = getattr(e, "details", lambda: "")() if hasattr(e, "details") else ""
            code = getattr(e, "code", lambda: "")() if hasattr(e, "code") else ""
            out = f"FAIL {type(e).__name__} {code} {str(det)[:90]}"
        print(f"{name:22} z16-{i} {int((time.perf_counter()-t)*1000):5d} ms  {out}", flush=True)
    t = time.perf_counter(); parts = []
    try:
        for c in chunks(m16): parts.append(ENGINES[name](c))
        hyp = "".join(parts)
        print(f"{name:22} m16   {int((time.perf_counter()-t)*1000):5d} ms  CER={cer(ref, hyp):.3f}  "
              f"Erick={'Erick' in hyp} LLM={'LLM' in hyp} Tab5={'Tab5' in hyp} 十二月五={'十二月五' in norm(hyp)}  "
              f"head={hyp[:60]}", flush=True)
        if os.environ.get("FULL"): print("   FULL:", hyp, flush=True)
    except Exception as e:
        print(f"{name:22} m16   FAIL {type(e).__name__}", flush=True)
