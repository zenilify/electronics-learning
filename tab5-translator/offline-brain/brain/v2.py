"""v2 contract helpers, copied (not imported) from server/papp @3e51ffe so the brain answers like cachy.

Sources: pipeline.py (HOTWORDS, ASR_FIXES/fix_terms, _mt_sane, translate_llm prompt) and app.py
(_flat, _SUM_LANG/_SUM_PROMPT/_SUM_GLOSS, _STOP/_wrong_lang). Lane T5-S owns the originals.
"""
from __future__ import annotations

import json
import os
import re

HOTWORDS = os.environ.get("PAPP_HOTWORDS", "开发板 (dev board / placa de desarrollo), M5Stack, 明栈 (M5Stack), Tab5, "
                          "Erick (ASR writes Eric/Erik), LLM (ASR writes LM), ESP32, ESP32-P4, 深圳 (Shenzhen), "
                          "华强北 (Huaqiangbei), NVIDIA, Zenilify")
ASR_FIXES = [tuple(x.split("=", 1)) for x in os.environ.get("PAPP_ASR_FIXES", "Eric=Erick,Erik=Erick,LM=LLM").split(",")
             if "=" in x]
_CJK_RE = re.compile(r"[㐀-鿿豈-﫿]")
NAMES = {"es": "Spanish", "en": "English", "zh": "Simplified Chinese"}


def fix_terms(text: str) -> str:
    for a, b in ASR_FIXES:
        text = re.sub(rf"(?<![A-Za-z]){re.escape(a)}(?![A-Za-z])", b, text)
    return text


def mt_sane(out: str, src_text: str, tgt: str) -> bool:
    """Reject LLM replies that are not a bare translation (notes, echoed prompt, wrong script)."""
    if not out or len(out) > 3 * len(src_text) + 80:
        return False
    low = out.lower()
    if "asr a" in low or "asr b" in low or "\n\n" in out or "traducción:" in low or "translation:" in low:
        return False
    if tgt != "zh" and _CJK_RE.search(out):
        return False
    return True


def llm_mt_system(src: str, tgt: str) -> str:
    """cachy's speech-aware interpreter prompt (translate_llm, single-transcript variant) + glossary."""
    return ("You are the interpreter in a face-to-face conversation between a foreign visitor and local "
            f"people (shops, electronics markets, taxis, business meetings). Translate spoken {NAMES[src]} "
            f"into natural spoken {NAMES[tgt]}. The input is a speech-recognition transcript and often has "
            "recognition errors: homophones (板/版), wrong tones (买 buy / 卖 sell — pick what fits who is "
            "speaking), missing punctuation. Silently repair the transcript from context, then translate the "
            "meaning faithfully, keeping numbers, prices and names. Reply with ONLY the translation."
            f" Glossary (names/products that may appear): {HOTWORDS}.")


# ---- /summarize -------------------------------------------------------------------------------
def flat(x) -> str:
    if isinstance(x, dict):
        who, what = x.get("owner") or x.get("who") or "", x.get("action") or x.get("task") or x.get("text") or ""
        return f"{who}: {what}" if who and what else (what or who or ", ".join(f"{k}: {v}" for k, v in x.items()))
    return str(x)


SUM_LANG = {"es": "Spanish (español)", "en": "English", "zh": "Simplified Chinese (简体中文)"}
SUM_PROMPT = {k: (f"You summarise voice notes and meeting recordings. The transcript may be in any language "
                  f"(Spanish, English, German, Chinese...). Write EVERY field — title, summary, bullets, actions — "
                  f"in {v}, translating as needed. Never answer in the transcript's language unless it is {v}.")
              for k, v in SUM_LANG.items()}
SUM_GLOSS = (f" Names and terms that may appear (spell them exactly like this even if the transcript misspells "
             f"them, e.g. Eric -> Erick): {HOTWORDS}, OEE. Start each action with its owner's name when one was "
             f"said (\"Erick: …\").")
# Local deviation from cachy (measured 11-09, Qwen3-8B): the cachy example "Owner: task — before 5 December"
# was copied literally into every action. The local prompt gives no copyable example; clean_action() strips leftovers.
SUM_JSON = (' Reply ONLY JSON: {"title": "<= 8 words", "summary": "2-3 sentences", "bullets": ["key points, max 6"], '
            '"actions": ["one plain string per task that was actually agreed or requested: the person\'s real name '
            'and a colon if a name was said, then the task, then the deadline only if one was said; never invent '
            'owners, deadlines or years"]} — use [] for actions when no task was agreed.')


def clean_action(a: str, transcript: str) -> str:
    """Strip template leftovers the local LLM copies: a literal 'Owner:' and an example deadline not in the transcript."""
    a = re.sub(r"^\s*(owner|responsable|dueño|负责人)\s*:\s*", "", a, flags=re.I)
    m = re.search(r"\s*[—-]\s*(before|antes del?)\s+5\s+(december|diciembre)\s*$", a, flags=re.I)
    if m and not re.search(r"5\s*(de\s*)?(dic|dec)|12月5", transcript, flags=re.I):
        a = a[:m.start()]
    return a.strip()
_STOP = {"de": set("der die das und ist nicht wir ich sie ein eine mit für auf zu den dem bis wird werden soll".split()),
         "en": set("the and is are we will should must to of for with by this that next have be".split()),
         "es": set("el la los las de y que en un una para con por se del al es son debe hasta".split())}


def wrong_lang(fields: list[str], out: str) -> str:
    for v in fields:
        if out != "zh" and re.search(r"[㐀-鿿]", v):
            return "CJK"
        if out == "zh" and not re.search(r"[㐀-鿿]", v):
            return "no CJK"
        w = re.findall(r"[a-záéíóúüñäöß]+", v.lower())
        if out in ("es", "en") and len(w) >= 4:
            sc = {k: sum(x in st for x in w) for k, st in _STOP.items()}
            sc["de"] += 2 * len(re.findall(r"[äöüß]", v.lower()))
            sc["es"] += 2 * len(re.findall(r"[áéíóúñ¿¡]", v.lower()))
            top = max(sc, key=sc.get)
            if top != out and sc[top] >= 2 + sc[out]:
                return top
    return ""


def parse_json(txt: str):
    t = txt.strip()
    if t.startswith("```"):
        t = t.split("\n", 1)[1] if "\n" in t else t
        t = t.rsplit("```", 1)[0]
    a, b = t.find("{"), t.rfind("}")
    if a >= 0 and b > a:
        t = t[a:b + 1]
    return json.loads(t)


def flat_fields(j: dict) -> list[str]:
    return [flat(j.get("title", "")), flat(j.get("summary", ""))] + [flat(x) for x in (j.get("bullets") or [])] \
        + [flat(x) for x in (j.get("actions") or [])]
