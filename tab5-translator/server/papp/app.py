"""PAPP pocket translator — FastAPI front for the NVCF ASR -> NMT -> TTS pipeline.

Endpoints
  GET  /health                      no auth; {"ok": true, ...} (no secrets, no upstream call)
  POST /translate?src=es&tgt=zh     bearer auth. Body either
        * raw  `audio/wav` (simplest for the Tab5: no multipart encoder needed), or
        * multipart/form-data with field `audio` (+ optional form fields src/tgt).
      ?format=json (default) -> JSON {src_text, tgt_text, timings_ms, asr_model, audio_b64}
      ?format=wav            -> audio/wav body, texts in X-Papp-* headers (URL-encoded UTF-8)
      ?rate=16000            -> output sample rate (8000..44100)
  POST /tts?lang=zh  {"text": "..."}  bearer auth; audio/wav (handy for test audio)
  POST /translate-text?src=en&tgt=zh&format=wav  {"text": "..."}  bearer auth; NMT + TTS, same
        reply shape as /translate (no ASR) — the Tab5 "Type & Translate" app
  POST /fx  bearer auth; {"base": "USD", "date": "...", "rates": {...}} cached 6 h (converter app)
"""
from __future__ import annotations

import base64
import hmac
import json
import logging
import os
import time
import urllib.parse

import requests

from fastapi import Depends, FastAPI, HTTPException, Request, Response
from fastapi.concurrency import run_in_threadpool
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

from .config import load_settings
from .pipeline import LANGS, BadAudio, Pipeline, UpstreamError, to_wav

log = logging.getLogger("papp")
logging.basicConfig(level=os.environ.get("PAPP_LOG_LEVEL", "INFO"))

settings = load_settings()
pipeline = Pipeline(settings.nvidia_api_key, settings.grpc_server)
app = FastAPI(title="PAPP translator", version="0.1.0", docs_url=None, redoc_url=None)
_bearer = HTTPBearer(auto_error=False)
MAX_AUDIO_BYTES = 30 * 16000 * 2 + 1024  # 30 s of 16 kHz PCM16


def auth(cred: HTTPAuthorizationCredentials | None = Depends(_bearer)) -> None:
    if cred is None or not hmac.compare_digest(cred.credentials.encode(),
                                               settings.bearer_token.encode()):
        raise HTTPException(401, "bad or missing bearer token",
                            headers={"WWW-Authenticate": "Bearer"})


@app.on_event("startup")
def _warm() -> None:
    try:
        pipeline.warm()
    except Exception as e:  # noqa: BLE001 — never block startup on NVCF
        log.warning("warm-up failed: %s", type(e).__name__)


@app.get("/health")
def health() -> dict:
    return {"ok": True, "service": "papp", "version": app.version,
            "langs": sorted(LANGS), "cooling_down": pipeline.status()}


def _check_lang(src: str, tgt: str) -> None:
    if src not in LANGS or tgt not in LANGS or src == tgt:
        raise HTTPException(400, f"src/tgt must be two different of {sorted(LANGS)}")


async def _read_audio(request: Request) -> tuple[bytes, dict]:
    ctype = request.headers.get("content-type", "")
    form_fields: dict = {}
    if ctype.startswith("multipart/form-data"):
        form = await request.form()
        up = form.get("audio")
        if up is None or isinstance(up, str):
            raise HTTPException(400, "multipart field 'audio' (WAV file) required")
        data = await up.read()
        form_fields = {k: v for k, v in form.items() if isinstance(v, str)}
    else:
        data = await request.body()
    if not data:
        raise HTTPException(400, "empty audio")
    if len(data) > MAX_AUDIO_BYTES:
        raise HTTPException(413, "audio longer than ~30 s")
    return data, form_fields


@app.post("/translate", dependencies=[Depends(auth)])
async def translate(request: Request, src: str | None = None, tgt: str | None = None,
                    format: str = "json", rate: int = 16000):
    data, fields = await _read_audio(request)
    src = (src or fields.get("src") or "").lower()
    tgt = (tgt or fields.get("tgt") or "").lower()
    _check_lang(src, tgt)
    if not 8000 <= rate <= 44100:
        raise HTTPException(400, "rate must be 8000..44100")
    try:
        res = await run_in_threadpool(pipeline.run, data, src, tgt, rate)
    except BadAudio as e:
        raise HTTPException(400, str(e)) from e
    except UpstreamError as e:
        raise HTTPException(502, str(e)) from e
    wav = res.pop("wav")
    log.info("translate %s>%s in=%.2fs asr=%s t=%s", src, tgt, res["audio_in_s"],
             res["asr_model"], res["timings_ms"])
    if format == "wav":
        q = urllib.parse.quote
        return Response(wav, media_type="audio/wav", headers={
            "X-Papp-Src-Text": q(res["src_text"]), "X-Papp-Tgt-Text": q(res["tgt_text"]),
            "X-Papp-Timings": json.dumps(res["timings_ms"]), "X-Papp-Asr-Model": res["asr_model"]})
    res["audio_b64"] = base64.b64encode(wav).decode()
    res["audio_rate"] = rate
    return res


@app.post("/tts", dependencies=[Depends(auth)])
async def tts(request: Request, lang: str, rate: int = 16000):
    body = await request.json()
    text = (body or {}).get("text", "").strip()
    if lang not in LANGS or not text:
        raise HTTPException(400, "need ?lang in es|en|zh and JSON {text}")
    try:
        pcm = await run_in_threadpool(pipeline.tts, text, lang, rate)
    except UpstreamError as e:
        raise HTTPException(502, str(e)) from e
    return Response(to_wav(pcm, rate), media_type="audio/wav")


@app.post("/translate-text", dependencies=[Depends(auth)])
async def translate_text(request: Request, src: str, tgt: str, format: str = "json", rate: int = 16000):
    body = await request.json()
    text = (body or {}).get("text", "").strip()
    _check_lang(src, tgt)
    if not text or len(text) > 2000:
        raise HTTPException(400, "need JSON {text} (1..2000 chars)")
    if not 8000 <= rate <= 44100:
        raise HTTPException(400, "rate must be 8000..44100")
    t0 = time.perf_counter()
    try:
        tgt_text = await run_in_threadpool(pipeline.translate, text, src, tgt)
        t1 = time.perf_counter()
        pcm = await run_in_threadpool(pipeline.tts, tgt_text, tgt, rate) if tgt_text else b""
    except UpstreamError as e:
        raise HTTPException(502, str(e)) from e
    t2 = time.perf_counter()
    timings = {"asr": 0, "nmt": round((t1 - t0) * 1000), "tts": round((t2 - t1) * 1000),
               "total": round((t2 - t0) * 1000)}
    log.info("translate-text %s>%s chars=%d t=%s", src, tgt, len(text), timings)
    wav = to_wav(pcm, rate) if pcm else b""
    if format == "wav":
        q = urllib.parse.quote
        return Response(wav, media_type="audio/wav", headers={
            "X-Papp-Src-Text": q(text), "X-Papp-Tgt-Text": q(tgt_text),
            "X-Papp-Timings": json.dumps(timings), "X-Papp-Asr-Model": "text"})
    return {"src": src, "tgt": tgt, "src_text": text, "tgt_text": tgt_text, "timings_ms": timings,
            "audio_b64": base64.b64encode(wav).decode(), "audio_rate": rate}


_FX: dict = {}
_FX_TTL_S = 6 * 3600
_FX_KEEP = ("USD", "CNY", "MXN", "EUR", "CHF", "HKD", "JPY")


def _fx_fetch() -> dict:
    r = requests.get("https://open.er-api.com/v6/latest/USD", timeout=10)
    r.raise_for_status()
    j = r.json()
    return {"base": "USD", "date": j.get("time_last_update_utc", ""), "source": "open.er-api.com",
            "rates": {k: j["rates"][k] for k in _FX_KEEP if k in j.get("rates", {})}}


@app.post("/fx", dependencies=[Depends(auth)])
async def fx() -> dict:
    now = time.time()
    if not _FX or now - _FX.get("_t", 0) > _FX_TTL_S:
        try:
            _FX.update(await run_in_threadpool(_fx_fetch), _t=now)
        except Exception as e:  # noqa: BLE001
            if not _FX:
                raise HTTPException(502, f"fx upstream: {type(e).__name__}") from e
            log.warning("fx refresh failed, serving cached: %s", type(e).__name__)
    return {k: v for k, v in _FX.items() if k != "_t"}
