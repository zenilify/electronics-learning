"""Probe which hosted ASR functions answer right now for a given WAV + language code.
Usage: python tools/probe_asr.py testaudio/es.wav es-US fn1 fn2 ...  (prints status per function)"""
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import riva.client  # noqa: E402
from papp.config import load_env_file  # noqa: E402
from papp.nvcf import resolve  # noqa: E402
from papp.pipeline import read_wav  # noqa: E402

key = load_env_file(os.environ.get("NVIDIA_KEY_FILE",
                                   os.path.expanduser("~/.config/zenilify/nvidia_build.env")))["API_KEY"]
wav, code, fns = sys.argv[1], sys.argv[2], sys.argv[3:]
audio = read_wav(open(wav, "rb").read())


def one(fn):
    t = time.perf_counter()
    try:
        auth = riva.client.Auth(uri="grpc.nvcf.nvidia.com:443", use_ssl=True,
                                metadata_args=[["function-id", resolve(key, fn)], ["authorization", f"Bearer {key}"]])
        svc = riva.client.ASRService(auth)
        cfg = riva.client.RecognitionConfig(encoding=riva.client.AudioEncoding.LINEAR_PCM,
                                            sample_rate_hertz=audio.rate, language_code=code,
                                            max_alternatives=1, enable_automatic_punctuation=True)
        mode = "offline"
        try:
            r = svc.offline_recognize(audio.pcm, cfg)
            txt = " ".join(x.alternatives[0].transcript for x in r.results if x.alternatives)
        except Exception as e:
            if "DEADLINE" in str(e) or "worker" in str(e):
                raise
            mode = "stream"
            s = riva.client.StreamingRecognitionConfig(config=cfg, interim_results=False)
            ch = [audio.pcm[i:i + 6400] for i in range(0, len(audio.pcm), 6400)]
            txt = " ".join(x.alternatives[0].transcript for rr in svc.streaming_response_generator(ch, s)
                           for x in rr.results if x.is_final and x.alternatives)
        return f"OK   {fn:40s} {mode} {int((time.perf_counter()-t)*1000)} ms | {txt.strip()}"
    except Exception as e:
        msg = str(e).split("details = ")[-1].split("\n")[0][:110]
        return f"FAIL {fn:40s} {int((time.perf_counter()-t)*1000)} ms | {msg}"


with ThreadPoolExecutor(len(fns)) as ex:
    for line in ex.map(one, fns):
        print(line, flush=True)
