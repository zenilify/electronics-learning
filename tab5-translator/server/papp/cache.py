"""Disk cache for TTS audio and NMT text (keyed by sha256 of the request).

cachy: PAPP_CACHE_DIR=/cache (bind mount ~/papp/cache, survives restarts; the phrasebook
pre-render warms it). Cloud Run: /tmp/papp-cache (in-memory tmpfs, per instance).
Small in-process LRU in front. Files are written atomically (tmp + rename).
"""
from __future__ import annotations

import hashlib
import json
import os
import threading
from collections import OrderedDict

DIR = os.environ.get("PAPP_CACHE_DIR", "/tmp/papp-cache")
_MEM_MAX = 256
_mem: "OrderedDict[str, bytes]" = OrderedDict()
_lock = threading.Lock()
stats = {"tts_hit": 0, "tts_miss": 0, "nmt_hit": 0, "nmt_miss": 0}
# per-thread read bypass for benchmarks (?nocache=1): reads miss, writes still happen
local = threading.local()


def key(*parts: object) -> str:
    return hashlib.sha256("\x1f".join(str(p) for p in parts).encode()).hexdigest()


def _path(kind: str, k: str) -> str:
    return os.path.join(DIR, kind, k[:2], k + (".wav" if kind == "tts" else ".json"))


def get(kind: str, k: str) -> bytes | None:
    if getattr(local, "bypass", False):
        return None
    with _lock:
        if (kind, k).__str__() in _mem:
            _mem.move_to_end(str((kind, k)))
            stats[kind + "_hit"] += 1
            return _mem[str((kind, k))]
    p = _path(kind, k)
    try:
        with open(p, "rb") as fh:
            data = fh.read()
    except OSError:
        stats[kind + "_miss"] += 1
        return None
    _remember(kind, k, data)
    stats[kind + "_hit"] += 1
    return data


def put(kind: str, k: str, data: bytes) -> None:
    _remember(kind, k, data)
    p = _path(kind, k)
    try:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        tmp = p + ".tmp%d" % os.getpid()
        with open(tmp, "wb") as fh:
            fh.write(data)
        os.replace(tmp, p)
    except OSError:
        pass  # read-only or full disk: memory cache still works


def _remember(kind: str, k: str, data: bytes) -> None:
    with _lock:
        _mem[str((kind, k))] = data
        _mem.move_to_end(str((kind, k)))
        while len(_mem) > _MEM_MAX:
            _mem.popitem(last=False)


def get_json(kind: str, k: str):
    b = get(kind, k)
    return json.loads(b) if b else None


def put_json(kind: str, k: str, obj) -> None:
    put(kind, k, json.dumps(obj, ensure_ascii=False).encode())


def info() -> dict:
    n = 0
    for _root, _d, files in os.walk(DIR):
        n += len(files)
    return {"dir": DIR, "files": n, **stats}
