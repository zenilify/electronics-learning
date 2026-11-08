"""Local (no-network) ASR -> MT -> TTS engines for the PAPP offline brain on Apple Silicon.

Every engine loads from the local Hugging Face cache only (HF_HUB_OFFLINE=1 is set by
start.sh), so a missing model fails loudly instead of silently downloading.
"""
from __future__ import annotations

import io
import os
import subprocess
import tempfile
import threading
import wave

import numpy as np

LANG_NAME = {"es": "Spanish", "en": "English", "zh": "Simplified Chinese (Mandarin)"}
NLLB_CODE = {"es": "spa_Latn", "en": "eng_Latn", "zh": "zho_Hans"}


# --- audio helpers ----------------------------------------------------------
def read_wav(data: bytes) -> tuple[np.ndarray, int]:
    with wave.open(io.BytesIO(data), "rb") as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2:
            raise ValueError("WAV must be mono 16-bit PCM (16 kHz recommended)")
        pcm = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
        return pcm.astype(np.float32) / 32768.0, w.getframerate()


def resample(x: np.ndarray, sr: int, to: int) -> np.ndarray:
    if sr == to or len(x) == 0:
        return x
    n = int(round(len(x) * to / sr))
    return np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x).astype(np.float32)


def to_wav(x: np.ndarray, rate: int) -> bytes:
    pcm = (np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes()
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return buf.getvalue()


# MLX / MPS work is serialised: one Tab5 talks at a time, and Metal streams are not
# meant to be shared across Python threads.
GPU_LOCK = threading.Lock()


# --- ASR --------------------------------------------------------------------
class WhisperASR:
    name = "mlx-whisper-large-v3-turbo"

    def __init__(self, repo: str = "mlx-community/whisper-large-v3-turbo"):
        import mlx_whisper  # noqa: F401  (import cost paid at startup)
        self.repo = repo

    def __call__(self, audio: np.ndarray, sr: int, lang: str) -> str:
        import mlx_whisper
        x = resample(audio, sr, 16000)
        if len(x) < 1600:
            return ""
        with GPU_LOCK:
            r = mlx_whisper.transcribe(x, path_or_hf_repo=self.repo, language=lang,
                                       task="transcribe", temperature=0.0,
                                       condition_on_previous_text=False,
                                       initial_prompt="以下是普通话的句子。" if lang == "zh" else None)
        return (r.get("text") or "").strip()


# --- MT ---------------------------------------------------------------------
class LlmMT:
    """Instruction-tuned LLM (MLX 4-bit) used as a translator, greedy decoding."""

    def __init__(self, repo: str):
        from mlx_lm import load
        self.repo = repo
        self.name = repo.split("/")[-1]
        self.model, self.tok = load(repo)
        self.qwen3 = "Qwen3" in repo

    def __call__(self, text: str, src: str, tgt: str) -> str:
        from mlx_lm import generate
        sysmsg = (f"You are a professional interpreter. Translate the user's {LANG_NAME[src]} text into "
                  f"{LANG_NAME[tgt]}. Keep names, numbers and product codes unchanged. Use natural, "
                  f"polite spoken style. Output only the translation, nothing else.")
        msgs = [{"role": "system", "content": sysmsg}, {"role": "user", "content": text}]
        kw = {"enable_thinking": False} if self.qwen3 else {}
        prompt = self.tok.apply_chat_template(msgs, add_generation_prompt=True, tokenize=False, **kw)
        with GPU_LOCK:
            out = generate(self.model, self.tok, prompt=prompt, max_tokens=256, verbose=False)
        out = out.replace("<think>", "").replace("</think>", "").strip()
        return out.splitlines()[0].strip() if out else ""


class NllbMT:
    def __init__(self, repo: str = "facebook/nllb-200-distilled-600M"):
        import torch
        from transformers import AutoModelForSeq2SeqLM, AutoTokenizer
        self.repo, self.name = repo, repo.split("/")[-1]
        self.dev = "mps" if torch.backends.mps.is_available() else "cpu"
        self.tok = AutoTokenizer.from_pretrained(repo)
        self.model = AutoModelForSeq2SeqLM.from_pretrained(repo).to(self.dev).eval()

    def __call__(self, text: str, src: str, tgt: str) -> str:
        import torch
        self.tok.src_lang = NLLB_CODE[src]
        enc = self.tok(text, return_tensors="pt").to(self.dev)
        with GPU_LOCK, torch.no_grad():
            out = self.model.generate(**enc, max_new_tokens=200, num_beams=4,
                                      forced_bos_token_id=self.tok.convert_tokens_to_ids(NLLB_CODE[tgt]))
        return self.tok.batch_decode(out, skip_special_tokens=True)[0].strip()


# --- TTS --------------------------------------------------------------------
class SayTTS:
    """macOS built-in speech (`say`) — on-device, zero download, works with no network."""
    name = "macos-say"
    VOICES = {"zh": "Tingting", "es": "Paulina", "en": "Samantha"}

    def __call__(self, text: str, lang: str, rate: int = 16000) -> np.ndarray:
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "o.wav")
            # text goes in on stdin (-f -), never on argv
            subprocess.run(["say", "-v", self.VOICES[lang], "-o", out, "--file-format=WAVE",
                            f"--data-format=LEI16@{rate}", "-f", "-"],
                           input=text.encode("utf-8"), check=True, timeout=30)
            x, sr = read_wav(open(out, "rb").read())
        return resample(x, sr, rate)


class PiperTTS:
    name = "piper"
    VOICES = {"zh": "zh/zh_CN/huayan/medium/zh_CN-huayan-medium.onnx",
              "es": "es/es_MX/claude/high/es_MX-claude-high.onnx",
              "en": "en/en_US/lessac/medium/en_US-lessac-medium.onnx"}

    def __init__(self):
        from huggingface_hub import hf_hub_download
        from piper import PiperVoice
        self.v = {}
        for lang, f in self.VOICES.items():
            onnx = hf_hub_download("rhasspy/piper-voices", f)
            hf_hub_download("rhasspy/piper-voices", f + ".json")
            self.v[lang] = PiperVoice.load(onnx)

    def __call__(self, text: str, lang: str, rate: int = 16000) -> np.ndarray:
        v = self.v[lang]
        chunks = [c.audio_float_array for c in v.synthesize(text)]
        x = np.concatenate(chunks) if chunks else np.zeros(0, np.float32)
        return resample(x.astype(np.float32), v.config.sample_rate, rate)
