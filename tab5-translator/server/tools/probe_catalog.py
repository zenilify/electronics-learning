#!/usr/bin/env python3
"""List hosted NVIDIA functions/models relevant to PAPP v2 (no secrets printed).
Usage on cachy: ~/papp/.venv/bin/python tools/probe_catalog.py [keyword ...]"""
import os, sys, requests
KEYF = os.path.expanduser("~/.config/zenilify/nvidia_build.env")
key = [l.split("=", 1)[1].strip() for l in open(KEYF) if l.startswith("API_KEY=")][0]
H = {"Authorization": f"Bearer {key}"}
kw = [k.lower() for k in sys.argv[1:]] or ["whisper", "parakeet", "canary", "ocr", "vl", "vision",
      "paddle", "translate", "magpie", "lang", "nemotron-nano", "gemma", "qwen", "llama-3.3", "mistral"]
try:
    r = requests.get("https://api.nvcf.nvidia.com/v2/nvcf/functions?visibility=public,authorized", headers=H, timeout=30)
    print("NVCF functions HTTP", r.status_code)
    for f in sorted(r.json().get("functions", []), key=lambda f: f.get("name", "")):
        n = f.get("name", "").lower()
        if any(k in n for k in kw):
            print(f"  {f.get('status'):8} {f.get('name')}")
except Exception as e:
    print("NVCF list failed:", type(e).__name__)
try:
    r = requests.get("https://integrate.api.nvidia.com/v1/models", headers=H, timeout=30)
    print("integrate /v1/models HTTP", r.status_code)
    for m in sorted(x["id"] for x in r.json().get("data", [])):
        if any(k in m.lower() for k in kw):
            print("  ", m)
except Exception as e:
    print("models list failed:", type(e).__name__)
