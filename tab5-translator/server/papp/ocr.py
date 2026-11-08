"""Point & Translate: photo (JPEG/PNG) -> Chinese text items -> pinyin + es + en.

Engines (probe 2026-11-07, 5 real Commons signage photos, cachy):
  vl    nvidia/nemotron-3.5-super-vl-120b-a12b, thinking off — 2.2–4.4 s, good, rare invented chars
  ocr   nvidia nemotron-ocr-v2 (0.85–0.9 s) + riva NMT per line — fastest; printed simplified only,
        fragments lines, misses traditional/stylised shop signs
  best  Gemini 3.5 Flash on Vertex (global) — 5–12 s, best (traditional HK sign, metro sign all
        right); only where Google credentials exist (Cloud Run). cachy delegates `best` to Cloud Run.
Pinyin is always recomputed with pypinyin from the recognised hanzi (the VLMs' pinyin had errors).
"""
from __future__ import annotations

import base64
import re

import requests
from pypinyin import Style, lazy_pinyin

from . import llm

_CJK = re.compile(r"[㐀-䶿一-鿿豈-﫿]")
PROMPT = ("You are a travel translator. Read every piece of Chinese text in this photo (signs, menus, "
          "labels, screens), top to bottom. Merge characters that belong to one sign line. Skip text that is "
          "only Latin letters or numbers. Reply ONLY JSON: "
          '{"items":[{"zh":"<exact characters>","es":"<Spanish meaning>","en":"<English meaning>"}],'
          '"summary_es":"<one short Spanish sentence: what this is>","summary_en":"<same in English>"}')


def pinyin(zh: str) -> str:
    return " ".join(lazy_pinyin(zh, style=Style.TONE, tone_sandhi=True, errors=lambda x: [x])).strip()


def has_cjk(s: str) -> bool:
    return bool(_CJK.search(s or ""))


def _data_uri(img: bytes, mime: str) -> str:
    return f"data:{mime};base64,{base64.b64encode(img).decode()}"


def _clean(obj: dict, engine: str) -> dict:
    items = []
    for it in obj.get("items") or []:
        zh = str(it.get("zh", "")).strip()
        if not has_cjk(zh):
            continue
        items.append({"zh": zh, "pinyin": pinyin(zh), "es": str(it.get("es", "")).strip(),
                      "en": str(it.get("en", "")).strip()})
    return {"engine": engine, "items": items, "summary_es": obj.get("summary_es", ""),
            "summary_en": obj.get("summary_en", "")}


def vl(key: str, img: bytes, mime: str) -> dict:
    txt, model = llm.nv_chat(key, [{"role": "user", "content": [
        {"type": "text", "text": PROMPT},
        {"type": "image_url", "image_url": {"url": _data_uri(img, mime)}}]}],
        max_tokens=1200, timeout=45, models=["nvidia/nemotron-3.5-super-vl-120b-a12b"])
    return _clean(llm.parse_json(txt), "vl:" + model.split("/")[-1])


def best(img: bytes, mime: str) -> dict:
    txt = llm.gemini([{"text": PROMPT}, {"inline_data": {"mime_type": mime,
                      "data": base64.b64encode(img).decode()}}])
    return _clean(llm.parse_json(txt), "best:" + llm.GEMINI_MODEL)


def ocr_lines(key: str, img: bytes, mime: str, model: str = "nemotron-ocr-v2") -> list[str]:
    r = requests.post(f"https://ai.api.nvidia.com/v1/cv/nvidia/{model}", timeout=30,
                      headers={"Authorization": f"Bearer {key}", "Accept": "application/json"},
                      json={"input": [{"type": "image_url", "url": _data_uri(img, mime)}]})
    r.raise_for_status()
    d = (r.json().get("data") or [{}])[0]
    out, seen = [], set()
    for t in d.get("text_detections", []):
        s = t.get("text_prediction", {}).get("text", "").strip()
        if has_cjk(s) and s not in seen:
            seen.add(s)
            out.append(s)
    return out


def ocr_nmt(key: str, img: bytes, mime: str, translate_many) -> dict:
    """translate_many(lines, src, tgt) -> list[str] (the pipeline's batched, cached NMT)."""
    lines = ocr_lines(key, img, mime)[:20]
    es = translate_many(lines, "zh", "es") if lines else []
    en = translate_many(lines, "zh", "en") if lines else []
    items = [{"zh": zh, "pinyin": pinyin(zh), "es": a, "en": b} for zh, a, b in zip(lines, es, en)]
    return {"engine": "ocr:nemotron-ocr-v2+riva-nmt", "items": items, "summary_es": "", "summary_en": ""}
