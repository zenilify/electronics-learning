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
import os as _os

# speech-aware MT for conversation mode: 11-08 A/B on Erick's dev-board clip — super-vl fixed 版→板 (placa) and
# punctuation in 0.6 s; lightning kept "versión de desarrollo". Override with PAPP_MT_MODELS.
MT_MODELS = _os.environ.get("PAPP_MT_MODELS",
                            "nvidia/nemotron-3.5-super-vl-120b-a12b,nvidia/nemotron-3.5-lightning-30b-a3b").split(",")
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
# Second, independent recognition for the speech-aware MT guard (T5-S v2.1, 11-08). Measured on z16-1..4 (cachy):
# whisper-large-v3 hears 开发板->开发版 and 买->卖 (z16-2); canary-1b-asr zh-CN hears 开发板 + 买 right but is worse
# elsewhere (换音, 网游, 饭一区; m16 CER 0.35 vs whisper 0.12) -> never the primary, only a hint to the LLM.
# super-vl given both hypotheses: "placa" + "compra(s)" 2/2 runs; given whisper alone: "vende" 4/4.
# Hosted parakeet-ctc-0_6b-zh-cn answers INVALID_ARGUMENT "Unavailable model" and whisper ignores
# custom_configuration prompts (output identical) — so no hotword boosting is available for zh on NVCF today.
ALT_ASR = {"zh": ("canary-1b-asr", "zh-CN")}
ALT_ASR_LANGS = tuple(x for x in _os.environ.get("PAPP_ALT_ASR_LANGS", "zh").split(",") if x in ALT_ASR)
ALT_DEADLINE_S = 4.0
# Names and product terms the speakers use (MT guard + summaries). Override with PAPP_HOTWORDS (comma list).
HOTWORDS = _os.environ.get("PAPP_HOTWORDS", "开发板 (dev board / placa de desarrollo), M5Stack, 明栈 (M5Stack), Tab5, "
                           "Erick (ASR writes Eric/Erik), LLM (ASR writes LM), ESP32, ESP32-P4, 深圳 (Shenzhen), 华强北 (Huaqiangbei), NVIDIA, Zenilify")
NMT_FN = "riva-translate-1_6b"
TTS_FN = "magpie-tts-multilingual"
ASR_DEADLINE_S = 8.0
NMT_DEADLINE_S = 15.0
TTS_DEADLINE_S = 20.0
COOLDOWN_S = 300.0


_CJK_RE = __import__("re").compile(r"[\u3400-\u9fff\uf900-\ufaff]")


def _mt_sane(out: str, src_text: str, tgt: str) -> bool:
    """Reject LLM replies that are not a bare translation (echoed prompt parts, notes, wrong script)."""
    if not out or len(out) > 3 * len(src_text) + 80:
        return False
    low = out.lower()
    if "asr a" in low or "asr b" in low or "\n\n" in out or "traducción:" in low or "translation:" in low:
        return False
    if tgt != "zh" and _CJK_RE.search(out):
        return False
    return True


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
        fns = {f for s in LANGS.values() for f, _ in s["asr"]} | {NMT_FN, TTS_FN} | {f for f, _ in ALT_ASR.values()}
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

    def asr_alt(self, audio: PcmAudio, lang: str) -> str:
        """Second recognition (canary) for the MT guard; '' when not configured or on any failure."""
        if lang not in ALT_ASR_LANGS:
            return ""
        fn, code = ALT_ASR[lang]
        if time.monotonic() - self._down.get(fn, -1e9) <= COOLDOWN_S:
            return ""
        try:
            cfg = riva.client.RecognitionConfig(
                encoding=riva.client.AudioEncoding.LINEAR_PCM, sample_rate_hertz=audio.rate, language_code=code,
                max_alternatives=1, enable_automatic_punctuation=True, audio_channel_count=1)
            r = riva.client.ASRService(self._auth_for(fn)).stub.Recognize(
                riva_asr_pb2.RecognizeRequest(config=cfg, audio=audio.pcm), metadata=self._md(fn),
                timeout=ALT_DEADLINE_S)
            return "".join(x.alternatives[0].transcript.strip() for x in r.results if x.alternatives)
        except Exception:  # noqa: BLE001 — a hint, never a failure
            self._down[fn] = time.monotonic()
            return ""

    def translate(self, text: str, src: str, tgt: str) -> str:
        k = cache.key("nmt", NMT_FN, src, tgt, text)
        hit = cache.get_json("nmt", k)
        if hit is not None:
            return hit["t"]
        out = self._translate(text, src, tgt)
        cache.put_json("nmt", k, {"t": out, "src": src, "tgt": tgt, "s": text})
        return out

    def translate_llm(self, text: str, src: str, tgt: str, alt: str = "") -> str:  # noqa: C901
        """Speech-aware MT (T5, 11-08; T5-S v2.1 adds `alt` + glossary): the input is an ASR transcript, so let the
        LLM repair recognition errors from context (homophones, tones: 开发版->开发板, 卖<->买, missing punctuation)
        and then translate. `alt` = a second, independent recognition of the same audio (canary for zh) — the
        measured fix for 买/卖. Falls back to Riva NMT on any LLM failure."""
        from . import llm
        names = {"es": "Spanish", "en": "English", "zh": "Simplified Chinese"}
        alt = alt if alt and alt != text else ""
        k = cache.key("llmmt3", ",".join(MT_MODELS), HOTWORDS, src, tgt, text, alt)   # bump on prompt/model change
        hit = cache.get_json("nmt", k)
        if hit is not None:
            return hit["t"]
        base = ("You are the interpreter in a face-to-face conversation between a foreign visitor and local "
                f"people (shops, electronics markets, taxis, business meetings). Translate spoken {names[src]} "
                f"into natural spoken {names[tgt]}. The input is a speech-recognition transcript and often has "
                "recognition errors: homophones (板/版), wrong tones (买 buy / 卖 sell — pick what fits who is "
                "speaking), missing punctuation. Silently repair the transcript from context, then translate the "
                "meaning faithfully, keeping numbers, prices and names. Reply with ONLY the translation.")
        gloss = f" Glossary (names/products that may appear): {HOTWORDS}."
        # Attempts in order. Measured 11-08 (probe_mt.py, z16-1..4): super-vl + both hypotheses is clean 8/8;
        # lightning given two hypotheses echoes "ASR A/B", adds notes or "Soy un modelo de lenguaje" -> lightning only
        # ever gets the single-transcript prompt.
        attempts = []
        if alt:
            attempts.append((MT_MODELS[:1], base + " You get two independent recognitions of the SAME utterance; "
                             "where they differ, choose what makes sense." + gloss, f"ASR A: {text}\nASR B: {alt}"))
        attempts.append((MT_MODELS[1:] if alt else MT_MODELS, base + gloss, text))
        out = ""
        for models, sys_, user in attempts:
            if not models:
                continue
            try:
                o, _model = llm.nv_chat(self.api_key, [{"role": "system", "content": sys_},
                                                       {"role": "user", "content": user}], 300, 15, models=models)
                o = o.strip().strip('"').strip()
            except Exception:  # noqa: BLE001
                continue
            if _mt_sane(o, text, tgt):
                out = o
                break
        if not out:   # never lose a turn to the LLM
            return self.translate(text, src, tgt)
        cache.put_json("nmt", k, {"t": out, "src": src, "tgt": tgt, "s": text, "alt": alt, "mt": "llm"})
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
            pair: tuple[str, str] | None = None, mt: str = "nmt", nocache: bool = False) -> dict:
        cache.local.bypass = nocache      # bench only: MT/TTS cache reads miss, writes still happen
        try:
            return self._run(wav, src, tgt, out_rate, pair, mt)
        finally:
            cache.local.bypass = False

    def _run(self, wav: bytes, src: str, tgt: str, out_rate: int = 16000,
             pair: tuple[str, str] | None = None, mt: str = "nmt") -> dict:
        """src="auto": detect es/en/zh; with pair=(a, b) the target is the other one of the pair
        (conversation mode), else tgt. detected == tgt -> no translation, text echoed."""
        from concurrent.futures import ThreadPoolExecutor
        t0 = time.perf_counter()
        audio = read_wav(wav)
        detected = ""
        # mt=llm: start the second recognition (canary zh) in parallel with the primary ASR, speculatively for
        # src=auto when zh is a candidate; its result is used only if the detected language matches.
        alt_lang = ""
        if mt == "llm":
            cands = (pair if pair else ("es", "en", "zh")) if src == "auto" else (src,)
            alt_lang = next((l for l in cands if l in ALT_ASR_LANGS), "")
        pool = ThreadPoolExecutor(1) if alt_lang else None
        fut = pool.submit(self.asr_alt, audio, alt_lang) if pool else None
        if src == "auto":
            allowed = pair if pair else ("es", "en", "zh")
            src_text, src, asr_fn = self.asr_auto(audio, allowed)
            detected = src
            if pair:
                tgt = pair[1] if src == pair[0] else pair[0]
        else:
            src_text, asr_fn = self.asr(audio, src)
        alt = ""
        if fut is not None:
            try:
                alt = fut.result(timeout=ALT_DEADLINE_S) if src == alt_lang else ""
            except Exception:  # noqa: BLE001
                alt = ""
            pool.shutdown(wait=False)
        t1 = time.perf_counter()
        if src == tgt or not src:
            t3 = time.perf_counter()
            return {"src": src, "tgt": tgt, "detected": detected, "src_text": src_text, "tgt_text": src_text,
                    "asr_model": asr_fn, "audio_in_s": round(len(audio.pcm) / 2 / audio.rate, 2),
                    "timings_ms": {"asr": round((t1 - t0) * 1000), "nmt": 0, "tts": 0,
                                   "total": round((t3 - t0) * 1000)}, "wav": b""}
        if not src_text:
            tgt_text = ""
        elif mt == "llm":
            tgt_text = self.translate_llm(src_text, src, tgt, alt)
        else:
            tgt_text = self.translate(src_text, src, tgt)
        t2 = time.perf_counter()
        pcm = self.tts(tgt_text, tgt, out_rate) if tgt_text else b""
        t3 = time.perf_counter()
        return {
            "src": src, "tgt": tgt, "detected": detected, "src_text": src_text, "tgt_text": tgt_text,
            "asr_model": asr_fn, "asr_alt": alt, "mt": mt,
            "tts_cached": getattr(self, "last_tts_cached", False), "audio_in_s": round(len(audio.pcm) / 2 / audio.rate, 2),
            "timings_ms": {"asr": round((t1 - t0) * 1000), "nmt": round((t2 - t1) * 1000),
                           "tts": round((t3 - t2) * 1000), "total": round((t3 - t0) * 1000)},
            "wav": to_wav(pcm, out_rate) if pcm else b"",
        }
