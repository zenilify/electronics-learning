"""PAPP offline brain — the cachy/Cloud Run API, served from the MacBook with no network.

Same contract as server/papp/app.py (copied, not imported — server/** belongs to lane T5-S):
  GET  /health                                  no auth
  POST /translate?src=es&tgt=zh[&format=wav|json][&rate=16000]   raw audio/wav or multipart `audio`
  POST /translate-text?src=&tgt=[&format=]      {"text": ...}
  POST /tts?lang=zh                             {"text": ...} -> audio/wav
  POST /fx                                      last cached rates (cache/fx.json, refreshed while online)
Bearer = the same token as cachy (~/.config/zenilify/papp_token, 0600, read once, never logged).
mDNS: advertises `_papp._tcp` on startup via macOS `dns-sd -R` (TXT path=/ backend=offline-brain).
Engines via env: PAPP_MT=qwen3|qwen25|nllb · PAPP_TTS=piper|say.
"""
from __future__ import annotations

import base64
import hmac
import json
import logging
import os
import socket
import subprocess
import time
import urllib.parse
from pathlib import Path

import numpy as np
from fastapi import Depends, FastAPI, HTTPException, Request, Response
from fastapi.concurrency import run_in_threadpool
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

import engines

log = logging.getLogger("papp-brain")
logging.basicConfig(level=os.environ.get("PAPP_LOG_LEVEL", "INFO"),
                    format="%(asctime)s %(levelname)s %(message)s")

LANGS = ("en", "es", "zh")
PORT = int(os.environ.get("PAPP_PORT", "8787"))
MAX_AUDIO_BYTES = 30 * 16000 * 2 + 1024
STATE = Path(os.environ.get("PAPP_STATE", os.path.expanduser("~/papp-brain")))
MT_REPOS = {"qwen25": "mlx-community/Qwen2.5-7B-Instruct-4bit",
            "qwen3": "mlx-community/Qwen3-8B-4bit"}

app = FastAPI(title="PAPP offline brain", version="0.1.0", docs_url=None, redoc_url=None)
_bearer = HTTPBearer(auto_error=False)
E: dict = {}
_TOKEN = open(os.environ.get("PAPP_TOKEN_FILE", os.path.expanduser("~/.config/zenilify/papp_token")),
              encoding="utf-8").read().strip()


def auth(cred: HTTPAuthorizationCredentials | None = Depends(_bearer)) -> None:
    if cred is None or not hmac.compare_digest(cred.credentials.encode(), _TOKEN.encode()):
        raise HTTPException(401, "bad or missing bearer token", headers={"WWW-Authenticate": "Bearer"})


def _mdns_start() -> None:
    name = os.environ.get("PAPP_MDNS_NAME", f"PAPP offline brain ({socket.gethostname().split('.')[0]})")
    # dns-sd -R <name> <type> <domain> <port> [txt...]; lives as long as this process
    E["mdns"] = subprocess.Popen(["dns-sd", "-R", name, "_papp._tcp", "local", str(PORT),
                                  "path=/", "backend=offline-brain", "v=1", "prio=0"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log.info("mDNS _papp._tcp '%s' port %d (pid %d)", name, PORT, E["mdns"].pid)


@app.on_event("startup")
def _load() -> None:
    t = time.perf_counter()
    E["asr"] = engines.WhisperASR()
    mt = os.environ.get("PAPP_MT", "qwen3")
    E["mt"] = engines.NllbMT() if mt == "nllb" else engines.LlmMT(MT_REPOS[mt])
    E["tts"] = engines.PiperTTS() if os.environ.get("PAPP_TTS", "piper") == "piper" else engines.SayTTS()
    # warm every stage once so the first Tab5 turn is not the slow one
    E["asr"](np.zeros(16000, np.float32), 16000, "es")
    E["mt"]("Hola", "es", "zh")
    E["tts"]("你好", "zh")
    E["loaded_s"] = round(time.perf_counter() - t, 1)
    log.info("engines ready in %ss: asr=%s mt=%s tts=%s", E["loaded_s"], E["asr"].name,
             E["mt"].name, E["tts"].name)
    if os.environ.get("PAPP_MDNS", "1") == "1":
        _mdns_start()


@app.on_event("shutdown")
def _stop() -> None:
    p = E.get("mdns")
    if p and p.poll() is None:
        p.terminate()


@app.get("/health")
def health() -> dict:
    return {"ok": True, "service": "papp", "backend": "offline-brain", "version": app.version,
            "langs": sorted(LANGS), "cooling_down": {},
            "engines": {k: E[k].name for k in ("asr", "mt", "tts") if k in E},
            "loaded_s": E.get("loaded_s")}


def _check_lang(src: str, tgt: str) -> None:
    if src not in LANGS or tgt not in LANGS or src == tgt:
        raise HTTPException(400, f"src/tgt must be two different of {sorted(LANGS)}")


def _reply(src, tgt, src_text, tgt_text, wav, timings, model, fmt, rate, audio_in_s=None):
    log.info("%s>%s asr=%s t=%s", src, tgt, model, timings)
    if fmt == "wav":
        q = urllib.parse.quote
        return Response(wav, media_type="audio/wav", headers={
            "X-Papp-Src-Text": q(src_text), "X-Papp-Tgt-Text": q(tgt_text),
            "X-Papp-Timings": json.dumps(timings), "X-Papp-Asr-Model": model,
            "X-Papp-Backend": "offline-brain"})
    out = {"src": src, "tgt": tgt, "src_text": src_text, "tgt_text": tgt_text, "asr_model": model,
           "timings_ms": timings, "audio_b64": base64.b64encode(wav).decode(), "audio_rate": rate}
    if audio_in_s is not None:
        out["audio_in_s"] = audio_in_s
    return out


async def _read_audio(request: Request) -> tuple[bytes, dict]:
    ctype = request.headers.get("content-type", "")
    fields: dict = {}
    if ctype.startswith("multipart/form-data"):
        form = await request.form()
        up = form.get("audio")
        if up is None or isinstance(up, str):
            raise HTTPException(400, "multipart field 'audio' (WAV file) required")
        data = await up.read()
        fields = {k: v for k, v in form.items() if isinstance(v, str)}
    else:
        data = await request.body()
    if not data:
        raise HTTPException(400, "empty audio")
    if len(data) > MAX_AUDIO_BYTES:
        raise HTTPException(413, "audio longer than ~30 s")
    return data, fields


def _run(data: bytes, src: str, tgt: str, rate: int) -> dict:
    t0 = time.perf_counter()
    try:
        audio, sr = engines.read_wav(data)
    except Exception as e:  # noqa: BLE001
        raise HTTPException(400, f"not a PCM WAV: {type(e).__name__}") from e
    src_text = E["asr"](audio, sr, src)
    t1 = time.perf_counter()
    tgt_text = E["mt"](src_text, src, tgt) if src_text else ""
    t2 = time.perf_counter()
    pcm = E["tts"](tgt_text, tgt, rate) if tgt_text else np.zeros(0, np.float32)
    t3 = time.perf_counter()
    ms = lambda a, b: round((b - a) * 1000)  # noqa: E731
    return {"src_text": src_text, "tgt_text": tgt_text, "wav": engines.to_wav(pcm, rate) if len(pcm) else b"",
            "audio_in_s": round(len(audio) / sr, 2),
            "timings": {"asr": ms(t0, t1), "nmt": ms(t1, t2), "tts": ms(t2, t3), "total": ms(t0, t3)}}


@app.post("/translate", dependencies=[Depends(auth)])
async def translate(request: Request, src: str | None = None, tgt: str | None = None,
                    format: str = "json", rate: int = 16000):
    data, fields = await _read_audio(request)
    src = (src or fields.get("src") or "").lower()
    tgt = (tgt or fields.get("tgt") or "").lower()
    _check_lang(src, tgt)
    if not 8000 <= rate <= 44100:
        raise HTTPException(400, "rate must be 8000..44100")
    r = await run_in_threadpool(_run, data, src, tgt, rate)
    return _reply(src, tgt, r["src_text"], r["tgt_text"], r["wav"], r["timings"], E["asr"].name,
                  format, rate, r["audio_in_s"])


@app.post("/translate-text", dependencies=[Depends(auth)])
async def translate_text(request: Request, src: str, tgt: str, format: str = "json", rate: int = 16000):
    text = ((await request.json()) or {}).get("text", "").strip()
    _check_lang(src, tgt)
    if not text or len(text) > 2000:
        raise HTTPException(400, "need JSON {text} (1..2000 chars)")
    if not 8000 <= rate <= 44100:
        raise HTTPException(400, "rate must be 8000..44100")
    t0 = time.perf_counter()
    tgt_text = await run_in_threadpool(E["mt"], text, src, tgt)
    t1 = time.perf_counter()
    pcm = await run_in_threadpool(E["tts"], tgt_text, tgt, rate) if tgt_text else np.zeros(0)
    t2 = time.perf_counter()
    timings = {"asr": 0, "nmt": round((t1 - t0) * 1000), "tts": round((t2 - t1) * 1000),
               "total": round((t2 - t0) * 1000)}
    return _reply(src, tgt, text, tgt_text, engines.to_wav(pcm, rate) if len(pcm) else b"",
                  timings, "text", format, rate)


@app.post("/tts", dependencies=[Depends(auth)])
async def tts(request: Request, lang: str, rate: int = 16000):
    text = ((await request.json()) or {}).get("text", "").strip()
    if lang not in LANGS or not text:
        raise HTTPException(400, "need ?lang in es|en|zh and JSON {text}")
    pcm = await run_in_threadpool(E["tts"], text, lang, rate)
    return Response(engines.to_wav(pcm, rate), media_type="audio/wav")


@app.post("/fx", dependencies=[Depends(auth)])
async def fx() -> dict:
    """Offline: serve the last snapshot start.sh saved while online (stale is better than nothing)."""
    p = STATE / "cache" / "fx.json"
    if not p.exists():
        raise HTTPException(503, "no cached fx snapshot (run start.sh once while online)")
    j = json.loads(p.read_text())
    j["stale_s"] = round(time.time() - p.stat().st_mtime)
    return j
