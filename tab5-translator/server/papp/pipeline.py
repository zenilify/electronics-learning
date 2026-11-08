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

from . import cache, lid
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
AUTO_ASR = [("whisper-large-v3", "multi"), ("parakeet-1_1b-rnnt-multilingual-asr", "multi")]
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
        """Return (transcript, function used). Tries the chain, skipping cooled-down fns.
        lang="auto": whisper-large-v3 "multi" (then lid.detect on the transcript)."""
        chain = AUTO_ASR if lang == "auto" else LANGS[lang]["asr"]
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

    def asr_auto(self, audio: PcmAudio, allowed: tuple[str, ...] = ("es", "en", "zh")) -> tuple[str, str, str]:
        """(transcript, detected lang, fn). Measured 2026-11-07: whisper multi 0.5–1.3 s."""
        text, fn = self.asr(audio, "auto")
        return text, lid.detect(text, allowed), fn

    def recognise(self, pcm: bytes, lang: str, rate: int = 16000) -> tuple[str, str]:
        """For the stream/long-audio paths: (text, lang) from raw PCM16."""
        a = PcmAudio(pcm, rate)
        if lang == "auto":
            t, l, _ = self.asr_auto(a)
            return t, l
        return self.asr(a, lang)[0], lang

    def asr_long(self, audio: PcmAudio, lang: str, max_chunk_s: float = 25.0) -> tuple[str, str]:
        """Voice notes up to minutes: split at the quietest 100 ms frame in the last 5 s of each
        25 s window, recognise each piece. Returns (transcript, lang)."""
        import array
        pcm, rate = audio.pcm, audio.rate
        step, out, pos, det = int(max_chunk_s * rate) * 2, [], 0, lang
        while pos < len(pcm):
            end = min(len(pcm), pos + step)
            if end < len(pcm):
                best, best_e = end, None
                fr = int(0.1 * rate) * 2
                for cut in range(end - int(5 * rate) * 2, end - fr, fr):
                    a = array.array("h", pcm[cut:cut + fr])
                    e = sum(abs(x) for x in a) / max(1, len(a))
                    if best_e is None or e < best_e:
                        best, best_e = cut, e
                end = best
            piece = PcmAudio(pcm[pos:end], rate)
            if det == "auto":
                t, det, _ = self.asr_auto(piece)
            else:
                t = self.asr(piece, det)[0]
            if t:
                out.append(t)
            pos = end
        sep = "" if det == "zh" else " "
        return sep.join(out).strip(), det

    def translate(self, text: str, src: str, tgt: str) -> str:
        k = cache.key("nmt", NMT_FN, src, tgt, text)
        hit = cache.get_json("nmt", k)
        if hit is not None:
            return hit["t"]
        out = self._translate(text, src, tgt)
        cache.put_json("nmt", k, {"t": out, "src": src, "tgt": tgt, "s": text})
        return out

    def translate_many(self, texts: list[str], src: str, tgt: str) -> list[str]:
        """One NMT request for many lines (riva TranslateText takes a list); per-line cache."""
        out: list[str | None] = []
        miss = []
        for t in texts:
            hit = cache.get_json("nmt", cache.key("nmt", NMT_FN, src, tgt, t))
            out.append(hit["t"] if hit else None)
            if not hit:
                miss.append(t)
        if miss:
            nmt = riva.client.NeuralMachineTranslationClient(self._auth_for(NMT_FN))
            req = riva_nmt_pb2.TranslateTextRequest(texts=miss, model="", source_language=LANGS[src]["nmt"],
                                                    target_language=LANGS[tgt]["nmt"])
            try:
                resp = nmt.stub.TranslateText(req, metadata=self._md(NMT_FN), timeout=NMT_DEADLINE_S)
            except Exception as e:  # noqa: BLE001
                raise UpstreamError(f"NMT {NMT_FN}: {type(e).__name__}") from e
            got = iter(tr.text.strip() for tr in resp.translations)
            for i, t in enumerate(out):
                if t is None:
                    out[i] = next(got, "")
                    cache.put_json("nmt", cache.key("nmt", NMT_FN, src, tgt, texts[i]),
                                   {"t": out[i], "src": src, "tgt": tgt, "s": texts[i]})
        return [x or "" for x in out]

    def _translate(self, text: str, src: str, tgt: str) -> str:
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
        """Raw PCM16 mono. Disk-cached by (voice, lang, rate, text) — see cache.py."""
        spec = LANGS[lang]
        k = cache.key("tts", voice or spec["voice"], lang, rate, text)
        hit = cache.get("tts", k)
        if hit is not None:
            self.last_tts_cached = True
            return read_wav(hit).pcm
        self.last_tts_cached = False
        pcm = self._tts(text, lang, rate, voice)
        if pcm:
            cache.put("tts", k, to_wav(pcm, rate))
        return pcm

    def _tts(self, text: str, lang: str, rate: int = 16000, voice: str | None = None) -> bytes:
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
    def run(self, wav: bytes, src: str, tgt: str, out_rate: int = 16000,
            pair: tuple[str, str] | None = None) -> dict:
        """src="auto": detect es/en/zh; with pair=(a, b) the target is the other one of the pair
        (conversation mode), else tgt. detected == tgt -> no translation, text echoed."""
        t0 = time.perf_counter()
        audio = read_wav(wav)
        detected = ""
        if src == "auto":
            allowed = pair if pair else ("es", "en", "zh")
            src_text, src, asr_fn = self.asr_auto(audio, allowed)
            detected = src
            if pair:
                tgt = pair[1] if src == pair[0] else pair[0]
        else:
            src_text, asr_fn = self.asr(audio, src)
        t1 = time.perf_counter()
        if src == tgt or not src:
            t3 = time.perf_counter()
            return {"src": src, "tgt": tgt, "detected": detected, "src_text": src_text, "tgt_text": src_text,
                    "asr_model": asr_fn, "audio_in_s": round(len(audio.pcm) / 2 / audio.rate, 2),
                    "timings_ms": {"asr": round((t1 - t0) * 1000), "nmt": 0, "tts": 0,
                                   "total": round((t3 - t0) * 1000)}, "wav": b""}
        tgt_text = self.translate(src_text, src, tgt) if src_text else ""
        t2 = time.perf_counter()
        pcm = self.tts(tgt_text, tgt, out_rate) if tgt_text else b""
        t3 = time.perf_counter()
        return {
            "src": src, "tgt": tgt, "detected": detected, "src_text": src_text, "tgt_text": tgt_text,
            "asr_model": asr_fn, "tts_cached": getattr(self, "last_tts_cached", False), "audio_in_s": round(len(audio.pcm) / 2 / audio.rate, 2),
            "timings_ms": {"asr": round((t1 - t0) * 1000), "nmt": round((t2 - t1) * 1000),
                           "tts": round((t3 - t2) * 1000), "total": round((t3 - t0) * 1000)},
            "wav": to_wav(pcm, out_rate) if pcm else b"",
        }
