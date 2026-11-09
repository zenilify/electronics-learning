# Copied verbatim from server/papp/lid.py @3e51ffe (lane T5-S owns the original) — keep in sync by copy, never import.
"""Tiny text language ID for es / en / zh (after a multilingual ASR pass).

Whisper-large-v3 with language_code="multi" transcribes in the spoken language but the hosted
Riva response carries no language field (measured 2026-11-07), so the language is read off the
transcript: CJK share -> zh; otherwise Spanish vs English by accents/¿¡ñ and stop words.
"""
from __future__ import annotations

import re

_CJK = re.compile(r"[㐀-䶿一-鿿豈-﫿]")
_WORD = re.compile(r"[a-záéíóúüñ']+")
_ES = set("el la los las de del que y en un una es por para con no se lo le al como pero más muy "
          "está estoy dónde donde qué cuánto cuanto gracias hola buenos buenas sí señor favor "
          "necesito quiero tengo hay ser son mi tu su nos este esta eso aquí".split())
_EN = set("the a an of and to in is it you that for on with are this be at have i we do not "
          "where what how much thank thanks hello good please need want my your our here there "
          "can could would will".split())


def detect(text: str, allowed: tuple[str, ...] = ("es", "en", "zh")) -> str:
    t = (text or "").strip()
    if not t:
        return ""
    letters = [c for c in t if c.isalpha()]
    cjk = len(_CJK.findall(t))
    if "zh" in allowed and letters and cjk / len(letters) >= 0.3:
        return "zh"
    low = t.lower()
    words = _WORD.findall(low)
    es = sum(w in _ES for w in words) + 2 * len(re.findall(r"[áéíóúñ¿¡]", low))
    en = sum(w in _EN for w in words)
    cands = [l for l in ("es", "en") if l in allowed]
    if not cands:
        return allowed[0]
    if len(cands) == 1:
        return cands[0]
    return "es" if es >= en and es > 0 else "en"
