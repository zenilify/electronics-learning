"""Configuration and secret loading for PAPP.

Secrets are read from 0600 env-style files (KEY=VALUE per line), never from argv
and never logged. On Cloud Run the single Secret Manager secret is mounted as a
file holding both values:

    API_KEY=<nvidia build key>
    PAPP_TOKEN=<bearer token the Tab5 sends>
"""
from __future__ import annotations

import os
from dataclasses import dataclass


def load_env_file(path: str) -> dict[str, str]:
    out: dict[str, str] = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("//") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip().strip('"').strip("'")
    return out


@dataclass
class Settings:
    nvidia_api_key: str
    bearer_token: str
    grpc_server: str = "grpc.nvcf.nvidia.com:443"


def load_settings() -> Settings:
    """Resolution order (first hit wins), files only:
    PAPP_SECRETS_FILE -> one file with API_KEY + PAPP_TOKEN (Cloud Run mount);
    else NVIDIA_KEY_FILE (API_KEY) + PAPP_TOKEN_FILE (single line token).
    """
    combined = os.environ.get("PAPP_SECRETS_FILE")
    if combined and os.path.exists(combined):
        env = load_env_file(combined)
        return Settings(env["API_KEY"], env["PAPP_TOKEN"],
                        os.environ.get("PAPP_GRPC_SERVER", Settings.grpc_server))
    key_file = os.environ.get("NVIDIA_KEY_FILE", "/run/secrets/nvidia_build.env")
    tok_file = os.environ.get("PAPP_TOKEN_FILE", "/run/secrets/papp_token")
    key = load_env_file(key_file)["API_KEY"]
    with open(tok_file, encoding="utf-8") as fh:
        token = fh.read().strip()
    return Settings(key, token, os.environ.get("PAPP_GRPC_SERVER", Settings.grpc_server))
