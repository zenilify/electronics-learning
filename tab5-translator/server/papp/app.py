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
  GET|POST /fx  bearer auth; {"base": "USD", "date", "rates": {...}, "pairs": {"CNY>MXN": ..}} cached 6 h,
        persisted to the cache dir so a restart without internet still answers (converter app)
v2 (lane T5-S, 2026-11-07) — full contract in server/API.md:
  /translate?src=auto[&pair=es,zh]   language ID (whisper multi + text LID); pair = conversation mode
  /translate-text?src=auto           text LID
  /tts                               disk-cached (X-Papp-Cache: hit|miss); phrasebook pre-render warms it
  POST /summarize?lang=auto&out=es   voice note (WAV ≤ 10 min) or JSON {text} -> title/summary/bullets/actions
  POST /ocr-translate?engine=vl|ocr|best  JPEG/PNG -> items [{zh, pinyin, es, en}] (+ optional TTS)
  POST /stream/start|chunk|stop      live subtitles over HTTP chunks (papp/stream.py)
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

from . import cache, lid, line, llm, ocr, stream
from .config import load_settings
from .pipeline import HOTWORDS, LANGS, fix_terms, BadAudio, PcmAudio, Pipeline, UpstreamError, read_wav, to_wav

log = logging.getLogger("papp")
logging.basicConfig(level=os.environ.get("PAPP_LOG_LEVEL", "INFO"))

settings = load_settings()
pipeline = Pipeline(settings.nvidia_api_key, settings.grpc_server)
app = FastAPI(title="PAPP translator", version="0.2.4", docs_url=None, redoc_url=None)
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
    return {"ok": True, "service": "papp", "version": app.version, "commit": os.environ.get("PAPP_COMMIT", "dev"),
            "langs": sorted(LANGS), "cooling_down": pipeline.status(),
            "features": ["translate", "auto", "translate-text", "tts-cache", "summarize", "ocr-translate",
                         "fx", "stream", "mt-llm", "asr-alt", "scan", "line"],
            "ocr_engines": ["vl", "ocr"] + (["best"] if llm.gemini_available() or PEER else []),
            "cache": {k: v for k, v in cache.stats.items()}}


PEER = os.environ.get("PAPP_PEER_URL", "").rstrip("/")   # cachy -> Cloud Run for engine=best


def _check_lang(src: str, tgt: str) -> None:
    if src == "auto" and tgt in LANGS:
        return
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
                    format: str = "json", rate: int = 16000, pair: str | None = None, mt: str | None = None,
                    nocache: int = 0):
    data, fields = await _read_audio(request)
    # mt=llm|nmt; conversation mode (pair=) defaults to the speech-aware LLM translation (T5, 11-08)
    mt = (mt or ("llm" if pair else "nmt")).lower()
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
    try:
        res = await run_in_threadpool(pipeline.run, data, src, tgt, rate, pr, mt, bool(nocache))
    except BadAudio as e:
        raise HTTPException(400, str(e)) from e
    except UpstreamError as e:
        raise HTTPException(502, str(e)) from e
    wav = res.pop("wav")
    log.info("translate %s>%s mt=%s in=%.2fs asr=%s t=%s", src, tgt, mt, res["audio_in_s"],
             res["asr_model"], res["timings_ms"])
    if format == "wav":
        q = urllib.parse.quote
        return Response(wav, media_type="audio/wav", headers={
            "X-Papp-Src-Text": q(res["src_text"]), "X-Papp-Tgt-Text": q(res["tgt_text"]),
            "X-Papp-Timings": json.dumps(res["timings_ms"]), "X-Papp-Asr-Model": res["asr_model"],
            "X-Papp-Src": res["src"], "X-Papp-Tgt": res["tgt"], "X-Papp-Detected": res.get("detected", ""),
            "X-Papp-Mt": res.get("mt", ""), "X-Papp-Asr-Alt": q(res.get("asr_alt", ""))})
    res["audio_b64"] = base64.b64encode(wav).decode()
    res["audio_rate"] = rate
    return res


@app.post("/tts", dependencies=[Depends(auth)])
async def tts(request: Request, lang: str, rate: int = 16000):
    body = await request.json()
    text = (body or {}).get("text", "").strip()
    if lang not in LANGS or not text:
        raise HTTPException(400, "need ?lang in es|en|zh and JSON {text}")
    if len(text) > 1000:
        raise HTTPException(400, "text longer than 1000 chars")
    t0 = time.perf_counter()
    try:
        pcm = await run_in_threadpool(pipeline.tts, text, lang, rate)
    except UpstreamError as e:
        raise HTTPException(502, str(e)) from e
    ms = round((time.perf_counter() - t0) * 1000)
    hit = getattr(pipeline, "last_tts_cached", False)
    log.info("tts %s chars=%d %s %d ms", lang, len(text), "hit" if hit else "miss", ms)
    return Response(to_wav(pcm, rate), media_type="audio/wav",
                    headers={"X-Papp-Cache": "hit" if hit else "miss", "X-Papp-Ms": str(ms)})


@app.post("/translate-text", dependencies=[Depends(auth)])
async def translate_text(request: Request, src: str, tgt: str, format: str = "json", rate: int = 16000):
    body = await request.json()
    text = (body or {}).get("text", "").strip()
    _check_lang(src, tgt)
    if not text or len(text) > 2000:
        raise HTTPException(400, "need JSON {text} (1..2000 chars)")
    detected = ""
    if src == "auto":
        src = detected = lid.detect(text)
        if src == tgt:
            return {"src": src, "tgt": tgt, "detected": detected, "src_text": text, "tgt_text": text,
                    "timings_ms": {"asr": 0, "nmt": 0, "tts": 0, "total": 0}, "audio_b64": "", "audio_rate": rate}
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
    return {"src": src, "tgt": tgt, "detected": detected, "src_text": text, "tgt_text": tgt_text, "timings_ms": timings,
            "audio_b64": base64.b64encode(wav).decode(), "audio_rate": rate}


_FX: dict = {}
_FX_TTL_S = 6 * 3600
_FX_KEEP = ("USD", "CNY", "MXN", "EUR", "CHF", "HKD", "JPY")
_FX_PAIRS = (("CNY", "MXN"), ("MXN", "CNY"), ("USD", "CNY"), ("CNY", "USD"), ("USD", "MXN"),
             ("MXN", "USD"), ("EUR", "CNY"), ("EUR", "MXN"))
_FX_FILE = os.path.join(cache.DIR, "fx.json")


def _fx_fetch() -> dict:
    r = requests.get("https://open.er-api.com/v6/latest/USD", timeout=10)
    r.raise_for_status()
    j = r.json()
    rates = {k: j["rates"][k] for k in _FX_KEEP if k in j.get("rates", {})}
    pairs = {f"{a}>{b}": round(rates[b] / rates[a], 6) for a, b in _FX_PAIRS if a in rates and b in rates}
    return {"base": "USD", "date": j.get("time_last_update_utc", ""), "source": "open.er-api.com",
            "rates": rates, "pairs": pairs, "fetched_at": int(time.time())}


def _fx_load_disk() -> None:
    try:
        with open(_FX_FILE, encoding="utf-8") as fh:
            d = json.load(fh)
        _FX.update(d, _t=d.get("fetched_at", 0))
    except (OSError, ValueError):
        pass


@app.api_route("/fx", methods=["GET", "POST"], dependencies=[Depends(auth)])
async def fx() -> dict:
    now = time.time()
    if not _FX:
        _fx_load_disk()
    if not _FX or now - _FX.get("_t", 0) > _FX_TTL_S:
        try:
            d = await run_in_threadpool(_fx_fetch)
            _FX.clear(); _FX.update(d, _t=now)
            try:
                os.makedirs(cache.DIR, exist_ok=True)
                with open(_FX_FILE, "w", encoding="utf-8") as fh:
                    json.dump(d, fh)
            except OSError:
                pass
        except Exception as e:  # noqa: BLE001
            if not _FX:
                raise HTTPException(502, f"fx upstream: {type(e).__name__}") from e
            log.warning("fx refresh failed, serving cached: %s", type(e).__name__)
    out = {k: v for k, v in _FX.items() if k != "_t"}
    out["stale"] = now - _FX.get("_t", 0) > _FX_TTL_S
    return out


# ---------------------------------------------------------------- /summarize
MAX_NOTE_BYTES = 10 * 60 * 16000 * 2 + 1024   # 10 min of 16 kHz PCM16
# 11-08 (T5): a Chinese meeting note with out=es came back in Chinese, a German one in German — the model followed
# the transcript language. The output language is now stated in English, twice, and checked below.
def _flat(x) -> str:
    """LLMs sometimes return {"owner", "action", "date"} objects instead of strings (11-08) — render them as text.
    The model's own `date` field is dropped: it invents years; dates that were said stay inside the action text."""
    if isinstance(x, dict):
        who, what = x.get("owner") or x.get("who") or "", x.get("action") or x.get("task") or x.get("text") or ""
        return f"{who}: {what}" if who and what else (what or who or ", ".join(f"{k}: {v}" for k, v in x.items()))
    return str(x)


_SUM_LANG = {"es": "Spanish (español)", "en": "English", "zh": "Simplified Chinese (简体中文)"}
_SUM_PROMPT = {k: (f"You summarise voice notes and meeting recordings. The transcript may be in any language "
                   f"(Spanish, English, German, Chinese...). Write EVERY field — title, summary, bullets, actions — "
                   f"in {v}, translating as needed. Never answer in the transcript's language unless it is {v}.")
               for k, v in _SUM_LANG.items()}
# T5-S v2.1 (11-08 eval_summarize.py baseline): ASR writes "Eric", the summary then dropped the name or wrote
# "LM"/"UE" for LLM/OEE -> give the model the glossary and ask for owners in actions.
_SUM_GLOSS = (f" Names and terms that may appear (spell them exactly like this even if the transcript misspells "
              f"them, e.g. Eric -> Erick): {HOTWORDS}, OEE. Start each action with its owner's name when one was "
              f"said (\"Erick: …\").")
# 11-08 probe_sum.py on m16/en/de: super-vl followed the glossary (Erick, OEE) 2/3 PASS (m16: transcript says "LM"
# -> glossary hint added); lightning ignored it 0/3 (Eric/Erik, German title). Override with PAPP_SUM_MODELS.
SUM_MODELS = os.environ.get("PAPP_SUM_MODELS", "nvidia/nemotron-3.5-super-vl-120b-a12b,"
                                               "nvidia/nemotron-3.5-lightning-30b-a3b").split(",")
_STOP = {"de": set("der die das und ist nicht wir ich sie ein eine mit für auf zu den dem bis wird werden soll".split()),
         "en": set("the and is are we will should must to of for with by this that next have be".split()),
         "es": set("el la los las de y que en un una para con por se del al es son debe hasta".split())}


def _wrong_lang(fields: list[str], out: str) -> str:
    """'' when every field is in `out`; else a short reason (CJK in es/en, or a German/English sentence in es)."""
    import re as _re
    for v in fields:
        if out != "zh" and _re.search(r"[\u3400-\u9fff]", v):
            return "CJK"
        if out == "zh" and not _re.search(r"[\u3400-\u9fff]", v):
            return "no CJK"
        w = _re.findall(r"[a-záéíóúüñäöß]+", v.lower())
        if out in ("es", "en") and len(w) >= 4:
            sc = {k: sum(x in st for x in w) for k, st in _STOP.items()}
            sc["de"] += 2 * len(_re.findall(r"[äöüß]", v.lower()))
            sc["es"] += 2 * len(_re.findall(r"[áéíóúñ¿¡]", v.lower()))
            if max(sc, key=sc.get) != out and sc[max(sc, key=sc.get)] >= 2 + sc[out]:
                return max(sc, key=sc.get)
    return ""


@app.post("/summarize", dependencies=[Depends(auth)])
async def summarize(request: Request, lang: str = "auto", out: str = "es"):
    """Voice note -> transcript + title + summary + bullets + action items (JSON).
    Body: WAV (raw audio/wav or multipart `audio`, mono PCM16, <= 10 min) or JSON {"text": ...}."""
    if out not in LANGS or (lang != "auto" and lang not in LANGS):
        raise HTTPException(400, "lang in auto|es|en|zh, out in es|en|zh")
    t0 = time.perf_counter()
    ctype = request.headers.get("content-type", "")
    if ctype.startswith("application/json"):
        text = ((await request.json()) or {}).get("text", "").strip()
        if not text or len(text) > 20000:
            raise HTTPException(400, "need JSON {text} (1..20000 chars)")
        det = lid.detect(text) if lang == "auto" else lang
        audio_s = 0.0
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
            audio = read_wav(data)
        except BadAudio as e:
            raise HTTPException(400, str(e)) from e
        audio_s = len(audio.pcm) / 2 / audio.rate
        try:
            text, det = await run_in_threadpool(pipeline.asr_long, audio, lang)
        except UpstreamError as e:
            raise HTTPException(502, str(e)) from e
        text = fix_terms(text)
    t1 = time.perf_counter()
    if not text:
        return {"lang": "", "transcript": "", "title": "", "summary": "", "bullets": [], "actions": [],
                "model": "", "timings_ms": {"asr": round((t1 - t0) * 1000), "llm": 0}}
    if len(text.strip()) < 40:   # 11-08: "Okay." made the LLM summarise its own instructions — too short to summarise
        t = text.strip()
        return {"lang": det, "out": out, "audio_s": round(audio_s, 1), "transcript": t, "title": t[:60],
                "summary": t, "bullets": [], "actions": [], "model": "", "markdown": f"# {t[:60]}\n\n{t}\n",
                "timings_ms": {"asr": round((t1 - t0) * 1000), "llm": 0, "total": round((t1 - t0) * 1000)}}
    msgs = [{"role": "system", "content": _SUM_PROMPT[out] + _SUM_GLOSS + ' Reply ONLY JSON: {"title": "<= 8 words", '
             '"summary": "2-3 sentences", "bullets": ["key points, max 6"], "actions": ["plain strings like '
             '\'Owner: task — deadline as said (e.g. before 5 December)\'; never invent a year; [] if none"]}'},
            {"role": "user", "content": f"Transcript (summarise it in {_SUM_LANG[out]}):\n{text}"}]
    try:
        raw, model = await run_in_threadpool(lambda: llm.nv_chat(settings.nvidia_api_key, msgs, 900, 40,
                                                                 models=SUM_MODELS, json_mode=True))
        j = llm.parse_json(raw)
        flat = [_flat(j.get("title", "")), _flat(j.get("summary", ""))] + [_flat(x) for x in (j.get("bullets") or [])] \
            + [_flat(x) for x in (j.get("actions") or [])]
        why = _wrong_lang(flat, out)
        if why:   # one retry with the language stated again; keep the retry only if it is clean
            log.warning("summarize: output not in %s (%s), retrying once", out, why)
            msgs2 = msgs + [{"role": "assistant", "content": raw},
                            {"role": "user", "content": f"Some fields are not in {_SUM_LANG[out]} ({why}). Rewrite the "
                             f"same JSON with EVERY field in {_SUM_LANG[out]}."}]
            raw2, model2 = await run_in_threadpool(lambda: llm.nv_chat(settings.nvidia_api_key, msgs2, 900, 40,
                                                                       models=SUM_MODELS, json_mode=True))
            j2 = llm.parse_json(raw2)
            flat2 = [_flat(j2.get("title", "")), _flat(j2.get("summary", ""))] + \
                [_flat(x) for x in (j2.get("bullets") or [])] + [_flat(x) for x in (j2.get("actions") or [])]
            if not _wrong_lang(flat2, out):
                j, model = j2, model2 + " (retry)"
    except Exception as e:  # noqa: BLE001
        raise HTTPException(502, f"summary LLM: {type(e).__name__}") from e
    t2 = time.perf_counter()
    res = {"lang": det, "out": out, "audio_s": round(audio_s, 1), "transcript": text,
           "title": _flat(j.get("title", "")), "summary": _flat(j.get("summary", "")),
           "bullets": [_flat(b) for b in (j.get("bullets") or [])], "actions": [_flat(x) for x in (j.get("actions") or [])],
           "model": model,
           "timings_ms": {"asr": round((t1 - t0) * 1000), "llm": round((t2 - t1) * 1000),
                          "total": round((t2 - t0) * 1000)}}
    res["markdown"] = (f"# {res['title']}\n\n{res['summary']}\n\n" + "".join(f"- {b}\n" for b in res["bullets"])
                       + ("\n## " + {"es": "Acciones", "en": "Actions", "zh": "待办"}[out] + "\n" + "".join(f"- [ ] {a}\n" for a in res["actions"]) if res["actions"] else "")
                       + f"\n---\n{text}\n")
    log.info("summarize lang=%s audio=%.1fs chars=%d model=%s t=%s", det, audio_s, len(text), model, res["timings_ms"])
    return res


# ---------------------------------------------------------------- /ocr-translate
MAX_IMG_BYTES = 6 * 1024 * 1024


@app.post("/ocr-translate", dependencies=[Depends(auth)])
async def ocr_translate(request: Request, engine: str = "vl", tts: str | None = None, rate: int = 16000):
    """Camera JPEG/PNG -> {"items": [{zh, pinyin, es, en}], summary_es, engine, timings_ms}.
    engine: vl (default, NVIDIA VLM 2–4 s) | ocr (fastest, NVIDIA OCR + NMT) | best (Gemini; cachy
    forwards it to Cloud Run). Falls back vl -> ocr on failure. tts=es|en|zh adds audio_b64 of the
    joined translation (zh = the hanzi read aloud)."""
    ctype = request.headers.get("content-type", "")
    if ctype.startswith("multipart/form-data"):
        form = await request.form()
        up = form.get("image")
        img = await up.read() if up is not None and not isinstance(up, str) else b""
        mime = getattr(up, "content_type", "") or "image/jpeg"
    else:
        img = await request.body()
        mime = ctype.split(";")[0] or "image/jpeg"
    if not img:
        raise HTTPException(400, "empty image (send image/jpeg or image/png)")
    if len(img) > MAX_IMG_BYTES:
        raise HTTPException(413, "image larger than 6 MB")
    if img[:3] == b"\xff\xd8\xff":
        mime = "image/jpeg"
    elif img[:8] == b"\x89PNG\r\n\x1a\n":
        mime = "image/png"
    else:
        raise HTTPException(400, "not a JPEG/PNG")
    if engine not in ("vl", "ocr", "best"):
        raise HTTPException(400, "engine in vl|ocr|best")
    if tts and tts not in LANGS:
        raise HTTPException(400, "tts in es|en|zh")
    t0 = time.perf_counter()
    order = {"best": ["best", "vl", "ocr"], "vl": ["vl", "ocr"], "ocr": ["ocr", "vl"]}[engine]
    res, errs = None, []
    for eng in order:
        try:
            if eng == "best" and llm.gemini_available():
                res = await run_in_threadpool(ocr.best, img, mime)
            elif eng == "best" and PEER:
                res = await run_in_threadpool(_peer_ocr, img, mime)
            elif eng == "best":
                errs.append("best: not available here"); continue
            elif eng == "vl":
                res = await run_in_threadpool(ocr.vl, settings.nvidia_api_key, img, mime)
            else:
                res = await run_in_threadpool(ocr.ocr_nmt, settings.nvidia_api_key, img, mime, pipeline.translate_many)
            break
        except Exception as e:  # noqa: BLE001
            errs.append(f"{eng}: {type(e).__name__}")
    if res is None:
        raise HTTPException(502, "ocr engines failed: " + "; ".join(errs))
    t1 = time.perf_counter()
    if tts and res["items"]:
        field = {"zh": "zh", "es": "es", "en": "en"}[tts]
        sep = "，" if tts == "zh" else ". "
        say = sep.join(i[field] for i in res["items"] if i.get(field))[:600]
        try:
            pcm = await run_in_threadpool(pipeline.tts, say, tts, rate)
            res["audio_b64"] = base64.b64encode(to_wav(pcm, rate)).decode()
            res["audio_rate"] = rate
        except UpstreamError as e:
            res["tts_error"] = str(e)
    res["fallbacks"] = errs
    res["timings_ms"] = {"ocr": round((t1 - t0) * 1000), "tts": round((time.perf_counter() - t1) * 1000),
                         "total": round((time.perf_counter() - t0) * 1000)}
    log.info("ocr-translate engine=%s items=%d t=%s fb=%s", res["engine"], len(res["items"]), res["timings_ms"], errs)
    return res


def _peer_ocr(img: bytes, mime: str) -> dict:
    r = requests.post(f"{PEER}/ocr-translate?engine=best", data=img, timeout=45,
                      headers={"Authorization": f"Bearer {settings.bearer_token}", "Content-Type": mime})
    r.raise_for_status()
    j = r.json()
    if not j.get("engine", "").startswith("best"):
        raise RuntimeError("peer fell back")
    j["engine"] = "peer:" + j["engine"]
    return j


# ---------------------------------------------------------------- /stream (live subtitles)
@app.post("/stream/start", dependencies=[Depends(auth)])
async def stream_start(lang: str = "auto", tgt: str = ""):
    if lang != "auto" and lang not in LANGS:
        raise HTTPException(400, "lang in auto|es|en|zh")
    if tgt and tgt not in LANGS:
        raise HTTPException(400, "tgt in es|en|zh or empty")
    log.info("stream start lang=%s tgt=%s", lang, tgt or "-")
    return {"sid": stream.start(lang, tgt), "rate": stream.RATE, "chunk_ms": 1000,
            "format": "raw PCM16 mono 16 kHz (or WAV)", "idle_timeout_s": stream.IDLE_S}


async def _stream_feed(request: Request, sid: str, flush: bool) -> dict:
    s = stream.get(sid)
    if s is None:
        raise HTTPException(404, "unknown or expired sid")
    data = await request.body()
    if len(data) > 10 * 16000 * 2 + 1024:
        raise HTTPException(413, "chunk longer than 10 s")
    if data[:4] == b"RIFF":
        try:
            a = read_wav(data)
        except BadAudio as e:
            raise HTTPException(400, str(e)) from e
        if a.rate != stream.RATE:
            raise HTTPException(400, "stream audio must be 16 kHz")
        data = a.pcm
    t0 = time.perf_counter()
    out = await run_in_threadpool(stream.feed, s, data, pipeline.recognise, pipeline.translate, flush)
    out["ms"] = round((time.perf_counter() - t0) * 1000)
    # One line per recognised segment (sizes and timings only, never the words). Until 2026-11-11 the stream
    # routes logged nothing, so "the Tab5 shows no subtitles" could not be told apart from "it never called".
    for f in out["final"]:
        log.info("stream final lang=%s dur=%.1fs voiced=%.1fs chars=%d tgt_chars=%d asr+mt=%d ms p90=%d floor=%d bg=%d dom=%d",
                 f.get("lang"), f.get("dur_s", 0), f.get("voiced_s", 0), len(f.get("text", "")), len(f.get("tgt_text", "")),
                 f.get("ms", 0), f.get("p90", 0), out["noise"], out["bg"], out["dom"])
    if out["empty"]:
        log.info("stream segment without text x%d level=%d floor=%d bg=%d", out["empty"], out["level"], out["noise"], out["bg"])
    if flush:
        log.info("stream stop chunks=%d finals=%d dropped_short=%d flat=%d repeats=%d floor=%d bg=%d",
                 s.seq, s.finals, s.dropped, s.flat, s.repeats, out["noise"], out["bg"])
    return out


@app.post("/stream/chunk", dependencies=[Depends(auth)])
async def stream_chunk(request: Request, sid: str):
    return await _stream_feed(request, sid, False)


@app.post("/stream/stop", dependencies=[Depends(auth)])
async def stream_stop(request: Request, sid: str):
    out = await _stream_feed(request, sid, True)
    stream.drop(sid)
    return out


# ---- SD sync (T5, 11-08; asked of T5-S, built by the integrator while T5-S was closed) -------------------
# The Tab5's card stays in the device: the firmware mirrors PAPP_SD_ROOT (the phrasebook pack, card layout
# `papp/...` stripped) into /sdcard/papp/. Index is computed once per mtime of the root and cached.
_SD_ROOT = os.environ.get("PAPP_SD_ROOT", "/sdpack")
_SD_IDX: dict = {}


def _sd_index() -> list:
    import hashlib
    root = os.path.realpath(_SD_ROOT)
    if not os.path.isdir(root):
        raise HTTPException(404, "no SD pack mounted")
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


# ---- Traceability Cell (contract: mission-control handoff 2026-11-08-n4.md §3; built by T5 11-10) ---------------
# The Tab5 "Línea · 产线" board polls GET /line/kpi; the T5-I satellites (scanner, badge, sensor) POST /scan and
# /line/event. SQLite in the cache dir: persistent on cachy (~/papp/cache), ephemeral (demo-only) on Cloud Run.
LINE = line.Line(os.path.join(os.environ.get("PAPP_CACHE_DIR", "/tmp/papp-cache"), "line.db"),
                 translate=lambda t, a, b: pipeline.translate(t, a, b))


async def _json(request: Request) -> dict:
    raw = await request.body()
    try:
        j = json.loads(raw or b"{}")
    except ValueError:
        raise HTTPException(422, "invalid JSON")
    if not isinstance(j, dict):
        raise HTTPException(422, "JSON object expected")
    return j


async def _line_call(fn, *a):
    try:
        return await run_in_threadpool(fn, *a)
    except ValueError as e:
        raise HTTPException(422, str(e))


@app.post("/scan", dependencies=[Depends(auth)])
async def line_scan(request: Request) -> dict:
    return await _line_call(LINE.scan, await _json(request))


@app.post("/line/event", dependencies=[Depends(auth)])
async def line_event(request: Request) -> dict:
    return await _line_call(LINE.event, await _json(request))


@app.get("/line/kpi", dependencies=[Depends(auth)])
async def line_kpi(window: str = "shift", station: str | None = None) -> dict:
    return await _line_call(LINE.kpi, window, station)


@app.get("/line/trace", dependencies=[Depends(auth)])
async def line_trace(code: str) -> dict:
    return await _line_call(LINE.trace, code)


@app.api_route("/line/config", methods=["GET", "POST"], dependencies=[Depends(auth)])
async def line_config(request: Request) -> dict:
    j = await _json(request) if request.method == "POST" else None
    return await _line_call(LINE.config, j)


@app.post("/line/report", dependencies=[Depends(auth)])
async def line_report(out: str = "es") -> dict:
    return await _line_call(LINE.report, out)


@app.post("/line/reset", dependencies=[Depends(auth)])
async def line_reset(request: Request) -> dict:
    return await _line_call(LINE.reset, await _json(request))
