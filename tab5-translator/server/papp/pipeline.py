"""ASR -> NMT -> TTS against NVIDIA hosted Riva / Nemotron Speech (NVCF gRPC).

Hosted NVCF functions can be unavailable for minutes ("failed to establish link to
worker" after a ~31 s DEADLINE_EXCEEDED, measured 2026-11-07 on parakeet-ctc-0_6b-es).
So every ASR language has an ordered fallback chain, each call carries its own short
gRPC deadline, and a function that just failed is skipped for COOLDOWN_S.
"""
from __future__ import annotations

import io
import threading
import time
import wave
from dataclasses import dataclass, field

import riva.client
from riva.client.proto import riva_asr_pb2, riva_nmt_pb2, riva_tts_pb2

from .nvcf import resolve

# (function-name fragment, language code). riva-translate-1_6b only accepts regional
# codes (plain "es" fails) — measured 2026-11-06.
LANGS = {
    "es": {"asr": [("parakeet-ctc-0_6b-es", "es-US"),
                   ("parakeet-1_1b-rnnt-multilingual-asr", "es-US"),
                   ("whisper-large-v3", "es-US")],
           "nmt": "es-US", "tts_code": "es-US", "voice": "Magpie-Multilingual.ES-US.Isabela"},
    "en": {"asr": [("parakeet-ctc-1_1b-asr", "en-US"),
                   ("parakeet-1_1b-rnnt-multilingual-asr", "en-US"),
                   ("whisper-large-v3", "en-US")],
           "nmt": "en-US", "tts_code": "en-US", "voice": "Magpie-Multilingual.EN-US.Aria"},
    "zh": {"asr": [("parakeet-ctc-0_6b-zh-cn", "zh-CN"),
                   ("whisper-large-v3", "zh-CN"),
                   ("parakeet-1_1b-rnnt-multilingual-asr", "zh-CN")],
           "nmt": "zh-CN", "tts_code": "zh-CN", "voice": "Magpie-Multilingual.ZH-CN.HouZhen"},
}
NMT_FN = "riva-translate-1_6b"
TTS_FN = "magpie-tts-multilingual"
ASR_DEADLINE_S = 8.0
NMT_DEADLINE_S = 15.0
TTS_DEADLINE_S = 20.0
COOLDOWN_S = 300.0


class BadAudio(ValueError):
    pass


class UpstreamError(RuntimeError):
    pass


@dataclass
class PcmAudio:
    pcm: bytes
    rate: int


def read_wav(data: bytes) -> PcmAudio:
    try:
        with wave.open(io.BytesIO(data), "rb") as w:
            if w.getnchannels() != 1 or w.getsampwidth() != 2:
                raise BadAudio("WAV must be mono 16-bit PCM (16 kHz recommended)")
            return PcmAudio(w.readframes(w.getnframes()), w.getframerate())
    except (wave.Error, EOFError) as e:
        raise BadAudio(f"not a PCM WAV: {e}") from e


def to_wav(pcm: bytes, rate: int) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return buf.getvalue()


@dataclass
class Pipeline:
    api_key: str
    server: str = "grpc.nvcf.nvidia.com:443"
    _auth: dict = field(default_factory=dict)
    _down: dict = field(default_factory=dict)  # fn -> monotonic time of last failure
    _lock: threading.Lock = field(default_factory=threading.Lock)

    def _auth_for(self, fn: str) -> riva.client.Auth:
        with self._lock:
            if fn not in self._auth:
                fid = resolve(self.api_key, fn)
                self._auth[fn] = riva.client.Auth(
                    uri=self.server, use_ssl=True,
                    metadata_args=[["function-id", fid], ["authorization", f"Bearer {self.api_key}"]])
            return self._auth[fn]

    def _md(self, fn: str):
        return self._auth_for(fn).get_auth_metadata()

    def warm(self) -> None:
        """Resolve every function id once (one NVCF list call, cached 1 h)."""
        fns = {f for s in LANGS.values() for f, _ in s["asr"]} | {NMT_FN, TTS_FN}
        for fn in fns:
            try:
                self._auth_for(fn)
            except LookupError:
                pass

    def status(self) -> dict:
        now = time.monotonic()
        return {fn: round(COOLDOWN_S - (now - t)) for fn, t in self._down.items()
                if now - t < COOLDOWN_S}

    # --- stages -----------------------------------------------------------
    def _asr_one(self, fn: str, code: str, audio: PcmAudio) -> str:
        cfg = riva.client.RecognitionConfig(
            encoding=riva.client.AudioEncoding.LINEAR_PCM, sample_rate_hertz=audio.rate,
            language_code=code, max_alternatives=1, enable_automatic_punctuation=True,
            audio_channel_count=1)
        stub = riva.client.ASRService(self._auth_for(fn)).stub
        resp = stub.Recognize(riva_asr_pb2.RecognizeRequest(config=cfg, audio=audio.pcm),
                              metadata=self._md(fn), timeout=ASR_DEADLINE_S)
        return " ".join(r.alternatives[0].transcript.strip()
                        for r in resp.results if r.alternatives).strip()

    def asr(self, audio: PcmAudio, lang: str) -> tuple[str, str]:
        """Return (transcript, function used). Tries the chain, skipping cooled-down fns."""
        chain = LANGS[lang]["asr"]
        now = time.monotonic()
        ready = [c for c in chain if now - self._down.get(c[0], -1e9) > COOLDOWN_S]
        errors = []
        for fn, code in ready or chain:
            try:
                return self._asr_one(fn, code, audio), fn
            except Exception as e:  # noqa: BLE001 — upstream gRPC / lookup failures
                self._down[fn] = time.monotonic()
                errors.append(f"{fn}: {type(e).__name__}")
        raise UpstreamError("all ASR functions failed: " + "; ".join(errors))

    def translate(self, text: str, src: str, tgt: str) -> str:
        nmt = riva.client.NeuralMachineTranslationClient(self._auth_for(NMT_FN))
        req = riva_nmt_pb2.TranslateTextRequest(texts=[text], model="",
                                                source_language=LANGS[src]["nmt"],
                                                target_language=LANGS[tgt]["nmt"])
        try:
            resp = nmt.stub.TranslateText(req, metadata=self._md(NMT_FN), timeout=NMT_DEADLINE_S)
        except Exception as e:  # noqa: BLE001
            raise UpstreamError(f"NMT {NMT_FN}: {type(e).__name__}") from e
        return resp.translations[0].text.strip()

    def tts(self, text: str, lang: str, rate: int = 16000, voice: str | None = None) -> bytes:
        spec = LANGS[lang]
        svc = riva.client.SpeechSynthesisService(self._auth_for(TTS_FN))
        req = riva_tts_pb2.SynthesizeSpeechRequest(
            text=text, voice_name=voice or spec["voice"], language_code=spec["tts_code"],
            sample_rate_hz=rate, encoding=riva.client.AudioEncoding.LINEAR_PCM)
        try:
            resp = svc.stub.Synthesize(req, metadata=self._md(TTS_FN), timeout=TTS_DEADLINE_S)
        except Exception as e:  # noqa: BLE001
            raise UpstreamError(f"TTS {TTS_FN}: {type(e).__name__}") from e
        return resp.audio  # raw PCM16 mono

    # --- end to end -------------------------------------------------------
    def run(self, wav: bytes, src: str, tgt: str, out_rate: int = 16000) -> dict:
        t0 = time.perf_counter()
        audio = read_wav(wav)
        src_text, asr_fn = self.asr(audio, src)
        t1 = time.perf_counter()
        tgt_text = self.translate(src_text, src, tgt) if src_text else ""
        t2 = time.perf_counter()
        pcm = self.tts(tgt_text, tgt, out_rate) if tgt_text else b""
        t3 = time.perf_counter()
        return {
            "src": src, "tgt": tgt, "src_text": src_text, "tgt_text": tgt_text,
            "asr_model": asr_fn, "audio_in_s": round(len(audio.pcm) / 2 / audio.rate, 2),
            "timings_ms": {"asr": round((t1 - t0) * 1000), "nmt": round((t2 - t1) * 1000),
                           "tts": round((t3 - t2) * 1000), "total": round((t3 - t0) * 1000)},
            "wav": to_wav(pcm, out_rate) if pcm else b"",
        }
