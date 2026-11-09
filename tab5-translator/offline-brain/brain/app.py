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
import lid
import v2

log = logging.getLogger("papp-brain")
logging.basicConfig(level=os.environ.get("PAPP_LOG_LEVEL", "INFO"),
                    format="%(asctime)s %(levelname)s %(message)s")

LANGS = ("en", "es", "zh")
PORT = int(os.environ.get("PAPP_PORT", "8787"))
MAX_AUDIO_BYTES = 30 * 16000 * 2 + 1024
STATE = Path(os.environ.get("PAPP_STATE", os.path.expanduser("~/papp-brain")))
MT_REPOS = {"qwen25": "mlx-community/Qwen2.5-7B-Instruct-4bit",
            "qwen3": "mlx-community/Qwen3-8B-4bit"}

app = FastAPI(title="PAPP offline brain", version="0.2.0", docs_url=None, redoc_url=None)
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
            "features": ["translate", "auto", "translate-text", "summarize", "fx", "mt-llm", "sd"],
            "missing": ["ocr-translate", "stream", "tts-cache", "asr-alt"],   # answered 404 -> Tab5 falls back
            "engines": {k: E[k].name for k in ("asr", "mt", "tts") if k in E},
            "loaded_s": E.get("loaded_s")}


def _check_lang(src: str, tgt: str) -> None:
    if src == "auto" and tgt in LANGS:
        return
    if src not in LANGS or tgt not in LANGS or src == tgt:
        raise HTTPException(400, f"src/tgt must be two different of {sorted(LANGS)}")


def _reply(src, tgt, src_text, tgt_text, wav, timings, model, fmt, rate, audio_in_s=None, detected="", mt=""):
    log.info("%s>%s det=%s mt=%s asr=%s t=%s", src, tgt, detected, mt, model, timings)
    if fmt == "wav":
        q = urllib.parse.quote
        return Response(wav, media_type="audio/wav", headers={
            "X-Papp-Src-Text": q(src_text), "X-Papp-Tgt-Text": q(tgt_text),
            "X-Papp-Timings": json.dumps(timings), "X-Papp-Asr-Model": model,
            "X-Papp-Src": src, "X-Papp-Tgt": tgt, "X-Papp-Detected": detected, "X-Papp-Mt": mt,
            "X-Papp-Backend": "offline-brain"})
    out = {"src": src, "tgt": tgt, "detected": detected, "mt": mt, "src_text": src_text, "tgt_text": tgt_text,
           "asr_model": model, "timings_ms": timings, "audio_b64": base64.b64encode(wav).decode(), "audio_rate": rate}
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


def _mt(text: str, src: str, tgt: str, mt: str) -> str:
    m = E["mt"]
    return m.speech(text, src, tgt) if mt == "llm" and hasattr(m, "speech") else m(text, src, tgt)


def _run(data: bytes, src: str, tgt: str, rate: int, pair=None, mt: str = "nmt") -> dict:
    """src="auto": detect es/en/zh; pair=(a, b): target = the other one (conversation mode). Same as cachy."""
    t0 = time.perf_counter()
    try:
        audio, sr = engines.read_wav(data)
    except Exception as e:  # noqa: BLE001
        raise HTTPException(400, f"not a PCM WAV: {type(e).__name__}") from e
    detected = ""
    if src == "auto":
        src_text, src = E["asr"].auto(audio, sr, pair if pair else ("es", "en", "zh"))
        detected = src
        if pair and src:
            tgt = pair[1] if src == pair[0] else pair[0]
    else:
        src_text = E["asr"](audio, sr, src)
    src_text = v2.fix_terms(src_text)
    t1 = time.perf_counter()
    base = {"src": src, "tgt": tgt, "detected": detected, "mt": mt, "audio_in_s": round(len(audio) / sr, 2)}
    if src == tgt or not src:   # same language as the target -> echo, no translation (cachy behaviour)
        return {**base, "src_text": src_text, "tgt_text": src_text, "wav": b"",
                "timings": {"asr": round((t1 - t0) * 1000), "nmt": 0, "tts": 0, "total": round((t1 - t0) * 1000)}}
    tgt_text = _mt(src_text, src, tgt, mt) if src_text else ""
    t2 = time.perf_counter()
    pcm = E["tts"](tgt_text, tgt, rate) if tgt_text else np.zeros(0, np.float32)
    t3 = time.perf_counter()
    ms = lambda a, b: round((b - a) * 1000)  # noqa: E731
    return {**base, "src_text": src_text, "tgt_text": tgt_text, "wav": engines.to_wav(pcm, rate) if len(pcm) else b"",
            "timings": {"asr": ms(t0, t1), "nmt": ms(t1, t2), "tts": ms(t2, t3), "total": ms(t0, t3)}}


@app.post("/translate", dependencies=[Depends(auth)])
async def translate(request: Request, src: str | None = None, tgt: str | None = None,
                    format: str = "json", rate: int = 16000, pair: str | None = None, mt: str | None = None,
                    nocache: int = 0):
    data, fields = await _read_audio(request)
    # cachy defaults pair= to mt=llm. On the brain, Qwen3-8B with the interpreter prompt was not better and took 2x the
    # time (11-09, bench_v2 --ab-only on z16-1..4: "comes a China" vs "visitas China", MT 1.7 s vs 0.8 s median),
    # so the brain keeps the plain translation prompt unless the client asks for mt=llm explicitly.
    mt = (mt or "nmt").lower()
    src = (src or fields.get("src") or "").lower()
    tgt = (tgt or fields.get("tgt") or "").lower()
    pr = None
    if pair:
        pr = tuple(x.strip().lower() for x in pair.split(","))
        if len(pr) != 2 or pr[0] == pr[1] or any(x not in LANGS for x in pr):
            raise HTTPException(400, "pair must be two different of es,en,zh e.g. pair=es,zh")
        src = "auto"
        tgt = tgt or pr[1]
    _check_lang(src, tgt)
    if not 8000 <= rate <= 44100:
        raise HTTPException(400, "rate must be 8000..44100")
    r = await run_in_threadpool(_run, data, src, tgt, rate, pr, mt)
    return _reply(r["src"], r["tgt"], r["src_text"], r["tgt_text"], r["wav"], r["timings"], E["asr"].name,
                  format, rate, r["audio_in_s"], r["detected"], r["mt"])


@app.post("/translate-text", dependencies=[Depends(auth)])
async def translate_text(request: Request, src: str, tgt: str, format: str = "json", rate: int = 16000):
    text = ((await request.json()) or {}).get("text", "").strip()
    _check_lang(src, tgt)
    if not text or len(text) > 2000:
        raise HTTPException(400, "need JSON {text} (1..2000 chars)")
    detected = ""
    if src == "auto":
        src = detected = lid.detect(text)
        if src == tgt:
            return _reply(src, tgt, text, text, b"", {"asr": 0, "nmt": 0, "tts": 0, "total": 0}, "text",
                          format, rate, None, detected)
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
                  timings, "text", format, rate, None, detected)


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


# ---------------------------------------------------------------- /summarize (v2, local LLM)
MAX_NOTE_BYTES = 10 * 60 * 16000 * 2 + 1024   # 10 min of 16 kHz PCM16


def _sum_llm(msgs: list[dict]) -> dict:
    return v2.parse_json(E["mt"].chat(msgs, 900))


@app.post("/summarize", dependencies=[Depends(auth)])
async def summarize(request: Request, lang: str = "auto", out: str = "es"):
    """Same contract as cachy: WAV (raw or multipart `audio`, <= 10 min) or JSON {text} ->
    {lang, out, audio_s, transcript, title, summary, bullets[], actions[], model, markdown, timings_ms}."""
    if out not in LANGS or (lang != "auto" and lang not in LANGS):
        raise HTTPException(400, "lang in auto|es|en|zh, out in es|en|zh")
    if not hasattr(E["mt"], "chat"):
        raise HTTPException(404, "summaries need an LLM MT engine (PAPP_MT=qwen3|qwen25)")
    t0 = time.perf_counter()
    ctype = request.headers.get("content-type", "")
    if ctype.startswith("application/json"):
        text = ((await request.json()) or {}).get("text", "").strip()
        if not text or len(text) > 20000:
            raise HTTPException(400, "need JSON {text} (1..20000 chars)")
        det, audio_s = (lid.detect(text) if lang == "auto" else lang), 0.0
    else:
        if ctype.startswith("multipart/form-data"):
            form = await request.form()
            up = form.get("audio")
            data = await up.read() if up is not None and not isinstance(up, str) else b""
        else:
            data = await request.body()
        if not data:
            raise HTTPException(400, "empty audio")
        if len(data) > MAX_NOTE_BYTES:
            raise HTTPException(413, "audio longer than ~10 min")
        try:
            audio, sr = engines.read_wav(data)
        except Exception as e:  # noqa: BLE001
            raise HTTPException(400, f"not a PCM WAV: {type(e).__name__}") from e
        audio_s = len(audio) / sr
        text, det = await run_in_threadpool(E["asr"].long, audio, sr, lang)
        text = v2.fix_terms(text)
    t1 = time.perf_counter()
    if not text:
        return {"lang": "", "transcript": "", "title": "", "summary": "", "bullets": [], "actions": [],
                "model": "", "timings_ms": {"asr": round((t1 - t0) * 1000), "llm": 0}}
    if len(text.strip()) < 40:   # cachy 11-08: too short to summarise
        t = text.strip()
        return {"lang": det, "out": out, "audio_s": round(audio_s, 1), "transcript": t, "title": t[:60],
                "summary": t, "bullets": [], "actions": [], "model": "", "markdown": f"# {t[:60]}\n\n{t}\n",
                "timings_ms": {"asr": round((t1 - t0) * 1000), "llm": 0, "total": round((t1 - t0) * 1000)}}
    msgs = [{"role": "system", "content": v2.SUM_PROMPT[out] + v2.SUM_GLOSS + v2.SUM_JSON},
            {"role": "user", "content": f"Transcript (summarise it in {v2.SUM_LANG[out]}):\n{text}"}]
    model = E["mt"].name
    try:
        j = await run_in_threadpool(_sum_llm, msgs)
        why = v2.wrong_lang(v2.flat_fields(j), out)
        if why:   # one retry with the language stated again; keep it only if it is clean (cachy rule)
            log.warning("summarize: output not in %s (%s), retrying once", out, why)
            msgs2 = msgs + [{"role": "assistant", "content": json.dumps(j, ensure_ascii=False)},
                            {"role": "user", "content": f"Some fields are not in {v2.SUM_LANG[out]} ({why}). Rewrite "
                             f"the same JSON with EVERY field in {v2.SUM_LANG[out]}."}]
            j2 = await run_in_threadpool(_sum_llm, msgs2)
            if not v2.wrong_lang(v2.flat_fields(j2), out):
                j, model = j2, model + " (retry)"
    except Exception as e:  # noqa: BLE001
        raise HTTPException(502, f"summary LLM: {type(e).__name__}") from e
    t2 = time.perf_counter()
    res = {"lang": det, "out": out, "audio_s": round(audio_s, 1), "transcript": text,
           "title": v2.flat(j.get("title", "")), "summary": v2.flat(j.get("summary", "")),
           "bullets": [v2.flat(b) for b in (j.get("bullets") or [])],
           "actions": [c for c in (v2.clean_action(v2.flat(x), text) for x in (j.get("actions") or [])) if c],
           "model": model,
           "timings_ms": {"asr": round((t1 - t0) * 1000), "llm": round((t2 - t1) * 1000),
                          "total": round((t2 - t0) * 1000)}}
    res["markdown"] = (f"# {res['title']}\n\n{res['summary']}\n\n" + "".join(f"- {b}\n" for b in res["bullets"])
                       + ("\n## " + {"es": "Acciones", "en": "Actions", "zh": "待办"}[out] + "\n"
                          + "".join(f"- [ ] {a}\n" for a in res["actions"]) if res["actions"] else "")
                       + f"\n---\n{text}\n")
    log.info("summarize lang=%s audio=%.1fs chars=%d t=%s", det, audio_s, len(text), res["timings_ms"])
    return res


# ---------------------------------------------------------------- SD sync (same contract as cachy)
# PAPP_SD_ROOT = a local copy of cachy's phrasebook pack (`papp/` stripped); start.sh refreshes it while online.
_SD_ROOT = os.environ.get("PAPP_SD_ROOT", str(STATE / "sdpack"))
_SD_IDX: dict = {}


def _sd_index() -> list:
    import hashlib
    root = os.path.realpath(_SD_ROOT)
    if not os.path.isdir(root):
        raise HTTPException(404, "no SD pack on this brain")
    stamp = max((os.stat(os.path.join(d, f)).st_mtime for d, _, fs in os.walk(root) for f in fs), default=0)
    if _SD_IDX.get("stamp") != stamp:
        items = []
        for d, _, fs in os.walk(root):
            for f in sorted(fs):
                p = os.path.join(d, f)
                h = hashlib.sha256(open(p, "rb").read()).hexdigest()
                items.append({"path": os.path.relpath(p, root), "size": os.path.getsize(p), "sha256": h})
        _SD_IDX.update(stamp=stamp, items=sorted(items, key=lambda x: x["path"]))
    return _SD_IDX["items"]


@app.get("/sd/index", dependencies=[Depends(auth)])
async def sd_index() -> dict:
    items = await run_in_threadpool(_sd_index)
    return {"root": "papp", "count": len(items), "bytes": sum(i["size"] for i in items), "items": items}


@app.get("/sd/file", dependencies=[Depends(auth)])
async def sd_file(path: str) -> Response:
    root = os.path.realpath(_SD_ROOT)
    p = os.path.realpath(os.path.join(root, path))
    if not p.startswith(root + os.sep) or not os.path.isfile(p):
        raise HTTPException(404, "not in the SD pack")
    from fastapi.responses import FileResponse
    return FileResponse(p, media_type="application/octet-stream")


# ---------------------------------------------------------------- not on the brain yet -> 404 (Tab5 falls back)
@app.api_route("/ocr-translate", methods=["GET", "POST"])
@app.api_route("/stream/{rest:path}", methods=["GET", "POST"])
async def not_here(rest: str = "") -> None:
    raise HTTPException(404, "not available on the offline brain — use the next endpoint")
