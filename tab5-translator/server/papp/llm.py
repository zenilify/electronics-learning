"""Text/vision LLM calls: NVIDIA integrate (OpenAI-compatible, same build key) with a model
chain, and Gemini on Vertex (global endpoint) when running with Google credentials
(Cloud Run's attached service account — no key file anywhere).

Measured 2026-11-07 from cachy: nemotron-3.5-lightning-30b-a3b 0.66 s, nemotron-3.5-bolt-e4b
0.97 s for a 3-bullet Spanish summary (thinking off); nemotron-nano-3 / nemotron-3-super /
mistral-nemo / gemma-3-12b answer 404 "Function not found for account"; gemma-4-31b and
gpt-oss-20b timed out. Gemini 3.5 Flash only answers on locations/global (us-central1 = 404).
"""
from __future__ import annotations

import json
import os
import time

import requests

NV_URL = "https://integrate.api.nvidia.com/v1/chat/completions"
TEXT_MODELS = os.environ.get("PAPP_LLM_MODELS",
                             "nvidia/nemotron-3.5-lightning-30b-a3b,nvidia/nemotron-3.5-bolt-e4b").split(",")
GEMINI_MODEL = os.environ.get("PAPP_GEMINI_MODEL", "gemini-3.5-flash")
GEMINI_PROJECT = os.environ.get("PAPP_GEMINI_PROJECT", "")   # set on Cloud Run only
COOLDOWN_S = 300.0
_down: dict[str, float] = {}


class LLMError(RuntimeError):
    pass


def _ok(name: str) -> bool:
    return time.monotonic() - _down.get(name, -1e9) > COOLDOWN_S


def nv_chat(key: str, messages: list, max_tokens: int = 700, timeout: float = 30,
            models: list[str] | None = None, json_mode: bool = False) -> tuple[str, str]:
    """Return (text, model). Tries the chain; a failing model cools down for 5 min."""
    errs = []
    chain = models or TEXT_MODELS
    for m in [x for x in chain if _ok(x)] or chain:
        body = {"model": m, "messages": messages, "max_tokens": max_tokens, "temperature": 0.2,
                "chat_template_kwargs": {"enable_thinking": False}}
        if json_mode:
            body["response_format"] = {"type": "json_object"}
        try:
            r = requests.post(NV_URL, headers={"Authorization": f"Bearer {key}"}, json=body, timeout=timeout)
            if r.status_code == 400 and json_mode:      # model without json mode: retry plain
                body.pop("response_format")
                r = requests.post(NV_URL, headers={"Authorization": f"Bearer {key}"}, json=body, timeout=timeout)
            r.raise_for_status()
            txt = (r.json()["choices"][0]["message"].get("content") or "").strip()
            if txt:
                return txt, m
            errs.append(f"{m}: empty")
        except Exception as e:  # noqa: BLE001
            errs.append(f"{m}: {type(e).__name__}")
        _down[m] = time.monotonic()
    raise LLMError("; ".join(errs))


def gemini_available() -> bool:
    return bool(GEMINI_PROJECT)


def _gcp_token() -> str:
    # Cloud Run metadata server: the attached SA's token (never a key file).
    r = requests.get("http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token",
                     headers={"Metadata-Flavor": "Google"}, timeout=3)
    r.raise_for_status()
    return r.json()["access_token"]


def gemini(parts: list, json_mode: bool = True, timeout: float = 40) -> str:
    if not GEMINI_PROJECT:
        raise LLMError("gemini not configured here")
    url = (f"https://aiplatform.googleapis.com/v1/projects/{GEMINI_PROJECT}/locations/global"
           f"/publishers/google/models/{GEMINI_MODEL}:generateContent")
    cfg = {"temperature": 0.1}
    if json_mode:
        cfg["responseMimeType"] = "application/json"
    try:
        r = requests.post(url, headers={"Authorization": f"Bearer {_gcp_token()}"}, timeout=timeout, json={
            "contents": [{"role": "user", "parts": parts}], "generationConfig": cfg,
            "labels": {"lane": "t5-s", "owner": "erick", "app": "papp"}})
        r.raise_for_status()
        return r.json()["candidates"][0]["content"]["parts"][0]["text"]
    except Exception as e:  # noqa: BLE001
        raise LLMError(f"gemini: {type(e).__name__}") from e


def parse_json(txt: str):
    """Tolerant JSON extraction (models sometimes wrap it in ``` fences)."""
    t = txt.strip()
    if t.startswith("```"):
        t = t.split("\n", 1)[1] if "\n" in t else t
        t = t.rsplit("```", 1)[0]
    a, b = t.find("{"), t.rfind("}")
    if a >= 0 and b > a:
        t = t[a:b + 1]
    return json.loads(t)
