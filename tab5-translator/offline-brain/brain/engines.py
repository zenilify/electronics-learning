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
# Measured 11-09 on T5's z16 dialog clips: the plain prompt gave 开发版 / 低铁站; naming the trip vocabulary
# gives 开发板 / 地铁站 (cachy fixes the same words with a second ASR, which the brain does not have).
ZH_PROMPT = os.environ.get("PAPP_ZH_PROMPT", "以下是普通话的句子，可能提到地铁站、开发板、翻译器、深圳、华强北、M5Stack、Tab5。")
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
                                       initial_prompt=ZH_PROMPT if lang == "zh" else None)
        return (r.get("text") or "").strip()

    def auto(self, audio: np.ndarray, sr: int, allowed=("es", "en", "zh")) -> tuple[str, str]:
        """src=auto / pair=: Whisper's own language ID, restricted to `allowed`. If Whisper picks a language
        outside the set (e.g. pt for Spanish), re-read the text with the text LID and transcribe once more
        forced to that language."""
        import mlx_whisper
        import lid
        x = resample(audio, sr, 16000)
        if len(x) < 1600:
            return "", ""
        if len(x) <= 29 * 16000:
            # Turn-sized clip: ONE encoder pass gives both the language and the text (mlx_whisper.decode).
            # transcribe(language=None) runs the encoder twice — measured 11-09: auto 1.3–1.8 s vs forced ~0.5 s.
            import mlx.core as mx
            from mlx_whisper.audio import N_FRAMES, log_mel_spectrogram, pad_or_trim
            from mlx_whisper.decoding import DecodingOptions, decode, detect_language
            from mlx_whisper.transcribe import ModelHolder
            # decode(language=None) returns no language_probs (measured 11-09), so: encode once, detect on the
            # features, pick the best ALLOWED language, decode the same features forced (+ vocabulary prompt for zh).
            with GPU_LOCK:
                model = ModelHolder.get_model(self.repo, mx.float16)
                mel = pad_or_trim(log_mel_spectrogram(x, n_mels=model.dims.n_mels), N_FRAMES, axis=-2)
                feats = model.encoder(mel.astype(mx.float16)[None])
                _, probs = detect_language(model, feats)
                probs = probs[0] if isinstance(probs, list) else probs
                lang = max((l for l in allowed if l in probs), key=lambda l: probs[l], default="")
                r = decode(model, feats, DecodingOptions(task="transcribe", language=lang or None, temperature=0.0,
                                                         without_timestamps=True, fp16=True,
                                                         prompt=ZH_PROMPT if lang == "zh" else None))
                r = r[0] if isinstance(r, list) else r
            if lang and r.text.strip() and r.compression_ratio < 2.4:
                return r.text.strip(), lang
            if lang:   # the single-pass decode looks bad (empty or repetitive): full transcribe, forced
                return self(audio, sr, lang), lang
        with GPU_LOCK:
            r = mlx_whisper.transcribe(x, path_or_hf_repo=self.repo, language=None, task="transcribe",
                                       temperature=0.0, condition_on_previous_text=False)
        text, lang = (r.get("text") or "").strip(), (r.get("language") or "")
        if lang in allowed:
            return text, lang
        forced = lid.detect(text, tuple(allowed)) or allowed[0]
        return self(audio, sr, forced), forced

    def long(self, audio: np.ndarray, sr: int, lang: str) -> tuple[str, str]:
        """Voice notes up to 10 min: mlx-whisper windows long audio itself (30 s, with context)."""
        import mlx_whisper
        import lid
        x = resample(audio, sr, 16000)
        if len(x) < 1600:
            return "", lang if lang != "auto" else ""
        with GPU_LOCK:
            r = mlx_whisper.transcribe(x, path_or_hf_repo=self.repo, language=None if lang == "auto" else lang,
                                       task="transcribe", temperature=0.0, condition_on_previous_text=True)
        text = (r.get("text") or "").strip()
        det = lang if lang != "auto" else (r.get("language") or "")
        if det not in ("es", "en", "zh"):
            det = lid.detect(text) or "es"
        return text, det


# --- MT ---------------------------------------------------------------------
class LlmMT:
    """Instruction-tuned LLM (MLX 4-bit) used as a translator, greedy decoding."""

    def __init__(self, repo: str):
        from mlx_lm import load
        self.repo = repo
        self.name = repo.split("/")[-1]
        self.model, self.tok = load(repo)
        self.qwen3 = "Qwen3" in repo

    def chat(self, msgs: list[dict], max_tokens: int = 256) -> str:
        from mlx_lm import generate
        kw = {"enable_thinking": False} if self.qwen3 else {}
        prompt = self.tok.apply_chat_template(msgs, add_generation_prompt=True, tokenize=False, **kw)
        with GPU_LOCK:
            out = generate(self.model, self.tok, prompt=prompt, max_tokens=max_tokens, verbose=False)
        return out.replace("<think>", "").replace("</think>", "").strip()

    def speech(self, text: str, src: str, tgt: str) -> str:
        """mt=llm (cachy's default for pair=): the interpreter prompt that repairs ASR errors from context.
        Falls back to the plain translation prompt when the reply is not a bare translation."""
        import v2
        out = self.chat([{"role": "system", "content": v2.llm_mt_system(src, tgt)},
                         {"role": "user", "content": text}], 300).strip().strip('"').strip()
        return out if v2.mt_sane(out, text, tgt) else self(text, src, tgt)

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
