"""One-time model fetch for the PAPP offline brain (run while online)."""
import sys, time
from huggingface_hub import snapshot_download, hf_hub_download

REPOS = [
    ("mlx-community/whisper-large-v3-turbo", None),
    ("facebook/nllb-200-distilled-600M", ["*.json", "*.model", "pytorch_model.bin", "*.txt"]),
    ("mlx-community/Qwen2.5-7B-Instruct-4bit", None),
    ("mlx-community/Qwen3-8B-4bit", None),
]
PIPER = ["zh/zh_CN/huayan/medium/zh_CN-huayan-medium.onnx",
         "es/es_MX/claude/high/es_MX-claude-high.onnx",
         "es/es_MX/ald/medium/es_MX-ald-medium.onnx",
         "en/en_US/lessac/medium/en_US-lessac-medium.onnx"]
only = sys.argv[1:]
for repo, pats in REPOS:
    if only and not any(o in repo for o in only):
        continue
    t = time.time()
    p = snapshot_download(repo, allow_patterns=pats)
    print("OK", repo, p, f"{time.time()-t:.0f}s", flush=True)
if not only or "piper" in only:
    for f in PIPER:
        for ext in ("", ".json"):
            p = hf_hub_download("rhasspy/piper-voices", f + ext)
        print("OK piper", f, flush=True)
print("FETCH_DONE")
