"""Direct pipeline smoke test (no HTTP): synthesize a test phrase with Magpie, then
run ASR -> NMT -> TTS on it. Writes test WAVs to ./testaudio/.
Key read in-process from $NVIDIA_KEY_FILE (default ~/.config/zenilify/nvidia_build.env)."""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from papp.config import load_env_file  # noqa: E402
from papp.pipeline import Pipeline, to_wav  # noqa: E402

PHRASES = {
    "es": "Buenos días, ¿dónde está la fábrica de baterías? Necesito hablar con el ingeniero.",
    "en": "Good morning, where is the battery factory? I need to talk to the engineer.",
    "zh": "早上好，电池工厂在哪里？我需要和工程师谈谈。",
}
key = load_env_file(os.environ.get("NVIDIA_KEY_FILE",
                                   os.path.expanduser("~/.config/zenilify/nvidia_build.env")))["API_KEY"]
p = Pipeline(key)
p.warm()
os.makedirs("testaudio", exist_ok=True)
pairs = [a.split(">") for a in sys.argv[1:]] or [["es", "zh"], ["zh", "es"], ["en", "zh"], ["zh", "en"]]
for src, tgt in pairs:
    path = f"testaudio/{src}.wav"
    if not os.path.exists(path):
        with open(path, "wb") as fh:
            fh.write(to_wav(p.tts(PHRASES[src], src, 16000), 16000))
    with open(path, "rb") as fh:
        res = p.run(fh.read(), src, tgt)
    with open(f"testaudio/out_{src}_{tgt}.wav", "wb") as fh:
        fh.write(res.pop("wav"))
    print(json.dumps(res, ensure_ascii=False))
