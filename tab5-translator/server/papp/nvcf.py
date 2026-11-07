"""Resolve NVCF function ids at runtime (never hardcode; NVIDIA rotates them)."""
from __future__ import annotations

import time

import requests

_FUNCS_URL = "https://api.nvcf.nvidia.com/v2/nvcf/functions?visibility=public,authorized"
_cache: dict[str, object] = {"t": 0.0, "funcs": []}


def _functions(key: str, ttl: float = 3600) -> list[dict]:
    if time.time() - float(_cache["t"]) > ttl or not _cache["funcs"]:
        r = requests.get(_FUNCS_URL, headers={"Authorization": f"Bearer {key}"}, timeout=30)
        r.raise_for_status()
        _cache["funcs"] = r.json().get("functions", [])
        _cache["t"] = time.time()
    return _cache["funcs"]  # type: ignore[return-value]


def resolve(key: str, name_fragment: str) -> str:
    """Return the id of the ACTIVE function whose name ends with / contains the fragment.
    Exact suffix match (e.g. 'ai-parakeet-ctc-0_6b-es' endswith 'parakeet-ctc-0_6b-es') wins."""
    frag = name_fragment.lower()
    active = [f for f in _functions(key) if f.get("status") == "ACTIVE"]
    exact = [f for f in active if f.get("name", "").lower().endswith(frag)]
    loose = [f for f in active if frag in f.get("name", "").lower()]
    hits = exact or loose
    if not hits:
        raise LookupError(f"no ACTIVE NVCF function matching {name_fragment!r}")
    return hits[0]["id"]
