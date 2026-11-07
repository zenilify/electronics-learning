"""Probe NVCF: list active speech functions and (with --voices) Magpie TTS voices.

Reads the NVIDIA key in-process from an env file (never argv, never printed).
Usage: python tools/probe_nvcf.py [--voices]   (key file: $NVIDIA_KEY_FILE or ~/.config/zenilify/nvidia_build.env)
"""
import os
import sys

import requests

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from papp.config import load_env_file  # noqa: E402

env = load_env_file(os.environ.get("NVIDIA_KEY_FILE", os.path.expanduser("~/.config/zenilify/nvidia_build.env")))
key = env["API_KEY"]
r = requests.get(
    "https://api.nvcf.nvidia.com/v2/nvcf/functions?visibility=public,authorized",
    headers={"Authorization": f"Bearer {key}"}, timeout=30)
r.raise_for_status()
pat = ("parakeet", "translate", "nmt", "magpie", "chatterbox", "whisper", "canary")
for f in r.json().get("functions", []):
    n = f.get("name", "")
    if any(p in n.lower() for p in pat):
        print(f.get("status"), n, f.get("id"))

if "--voices" in sys.argv:
    import riva.client
    from riva.client.proto import riva_tts_pb2
    from papp.nvcf import resolve
    fid = resolve(key, "magpie-tts-multilingual")
    auth = riva.client.Auth(uri="grpc.nvcf.nvidia.com:443", use_ssl=True,
                            metadata_args=[["function-id", fid], ["authorization", f"Bearer {key}"]])
    tts = riva.client.SpeechSynthesisService(auth)
    cfg = tts.stub.GetRivaSynthesisConfig(riva_tts_pb2.RivaSynthesisConfigRequest(),
                                          metadata=auth.get_auth_metadata())
    for m in cfg.model_config:
        p = dict(m.parameters)
        print(m.model_name, p.get("language_code"), p.get("voice_name"), (p.get("subvoices") or "")[:600])
