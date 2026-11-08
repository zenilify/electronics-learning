#!/usr/bin/env python3
"""PAPP v2 probes (run on cachy from server/): whisper LID, TTS latency, LLM latency.
No secret is printed; exceptions print type names only."""
import os, sys, time, json, requests
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import riva.client
from riva.client.proto import riva_asr_pb2, riva_tts_pb2
from papp.pipeline import Pipeline, read_wav
KEYF = os.path.expanduser("~/.config/zenilify/nvidia_build.env")
key = [l.split("=", 1)[1].strip() for l in open(KEYF) if l.startswith("API_KEY=")][0]
p = Pipeline(key)
what = sys.argv[1:] or ["lid", "tts", "llm"]

if "lid" in what:
    for lang in ("es", "en", "zh"):
        a = read_wav(open(f"testaudio/{lang}.wav", "rb").read())
        for fn, code in (("whisper-large-v3", "multi"), ("parakeet-1_1b-rnnt-multilingual-asr", "multi")):
            t = time.perf_counter()
            try:
                cfg = riva.client.RecognitionConfig(encoding=riva.client.AudioEncoding.LINEAR_PCM,
                    sample_rate_hertz=a.rate, language_code=code, max_alternatives=1,
                    enable_automatic_punctuation=True, audio_channel_count=1)
                stub = riva.client.ASRService(p._auth_for(fn)).stub
                r = stub.Recognize(riva_asr_pb2.RecognizeRequest(config=cfg, audio=a.pcm),
                                   metadata=p._md(fn), timeout=15)
                res = r.results[0] if r.results else None
                txt = res.alternatives[0].transcript if res and res.alternatives else ""
                lc = getattr(res, "language_code", "?") if res else "?"
                print(f"LID {lang} {fn}: {int((time.perf_counter()-t)*1000)} ms lc={lc!r} text={txt[:60]!r}")
            except Exception as e:
                print(f"LID {lang} {fn}: FAIL {type(e).__name__} {int((time.perf_counter()-t)*1000)} ms")

if "tts" in what:
    for lang, text in (("zh", "你好，谢谢"), ("zh", "请带我去上海虹桥站"), ("es", "Buenos días, mucho gusto"),
                       ("zh", "你好，谢谢")):
        t = time.perf_counter()
        try:
            pcm = p.tts(text, lang, 16000)
            print(f"TTS batch {lang} {len(text)}ch: {int((time.perf_counter()-t)*1000)} ms audio={len(pcm)/32000:.2f}s")
        except Exception as e:
            print("TTS batch FAIL", type(e).__name__)
        t = time.perf_counter()
        try:
            from papp.pipeline import LANGS, TTS_FN
            svc = riva.client.SpeechSynthesisService(p._auth_for(TTS_FN))
            req = riva_tts_pb2.SynthesizeSpeechRequest(text=text, voice_name=LANGS[lang]["voice"],
                language_code=LANGS[lang]["tts_code"], sample_rate_hz=16000,
                encoding=riva.client.AudioEncoding.LINEAR_PCM)
            first = None; n = 0
            for ch in svc.stub.SynthesizeOnline(req, metadata=p._md(TTS_FN), timeout=20):
                if first is None: first = time.perf_counter() - t
                n += len(ch.audio)
            print(f"TTS stream {lang}: first={int(first*1000)} ms total={int((time.perf_counter()-t)*1000)} ms audio={n/32000:.2f}s")
        except Exception as e:
            print("TTS stream FAIL", type(e).__name__)

if "llm" in what:
    for model in sys.argv[2:] or ["nvidia/nemotron-nano-3-30b-a3b", "google/gemma-4-31b-it",
                                  "nvidia/riva-translate-4b-instruct-v2"]:
        t = time.perf_counter()
        try:
            r = requests.post("https://integrate.api.nvidia.com/v1/chat/completions",
                headers={"Authorization": f"Bearer {key}"}, timeout=45, json={
                "model": model, "max_tokens": 600, "temperature": 0.2,
                "chat_template_kwargs": {"enable_thinking": False, "thinking": False},
                "messages": [{"role": "system", "content": "/no_think"},
                             {"role": "user", "content": "Resume en 2 viñetas en español: Mañana a las 10 nos vemos con M5Stack en Shenzhen, llevar la Tab5 cargada y el adaptador USB-C. Preguntar por el LLM630 y precios de mayoreo."}]})
            j = r.json()
            m = j["choices"][0]["message"] if r.ok else {}
            msg = (m.get("content") or ("[reasoning only] " + str(m.get("reasoning_content"))[:80])) if r.ok else j
            print(f"LLM {model}: HTTP {r.status_code} {int((time.perf_counter()-t)*1000)} ms -> {str(msg)[:220]!r}")
        except Exception as e:
            print(f"LLM {model}: FAIL {type(e).__name__}")
