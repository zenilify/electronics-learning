#!/usr/bin/env python3
"""OCR/VLM probe for /ocr-translate (run on cachy from server/). Images in testimages/.
Prints recognised text + latency per engine; no secret is printed."""
import base64, glob, json, os, subprocess, sys, time, requests
KEYF = os.path.expanduser("~/.config/zenilify/nvidia_build.env")
key = [l.split("=", 1)[1].strip() for l in open(KEYF) if l.startswith("API_KEY=")][0]
H = {"Authorization": f"Bearer {key}", "Accept": "application/json"}
PROMPT = ("Read every piece of Chinese text in this photo (signs, menus, labels). Reply ONLY JSON: "
          '{"items":[{"zh":"...","pinyin":"...","es":"...","en":"..."}]} ordered top to bottom.')

def b64(p): return base64.b64encode(open(p, "rb").read()).decode()
def mime(p): return "image/png" if p.endswith(".png") else "image/jpeg"

def paddle(p):
    r = requests.post("https://ai.api.nvidia.com/v1/cv/baidu/paddleocr", headers=H, timeout=60,
                      json={"input": [{"type": "image_url", "url": f"data:{mime(p)};base64,{b64(p)}"}]})
    if not r.ok: return f"HTTP {r.status_code} {r.text[:160]}"
    j = r.json(); d = (j.get("data") or [{}])[0]
    return " / ".join(t.get("text_prediction", {}).get("text", "") for t in d.get("text_detections", []))

def nvocr(p, model="nemotron-ocr-v1"):
    r = requests.post(f"https://ai.api.nvidia.com/v1/cv/nvidia/{model}", headers=H, timeout=60,
                      json={"input": [{"type": "image_url", "url": f"data:{mime(p)};base64,{b64(p)}"}]})
    if not r.ok: return f"HTTP {r.status_code} {r.text[:160]}"
    j = r.json(); d = (j.get("data") or [{}])[0]
    return " / ".join(t.get("text_prediction", {}).get("text", "") for t in d.get("text_detections", []))

def vlm(p, model):
    r = requests.post("https://integrate.api.nvidia.com/v1/chat/completions", headers=H, timeout=90, json={
        "model": model, "max_tokens": 900, "temperature": 0.1,
        "chat_template_kwargs": {"enable_thinking": False},
        "messages": [{"role": "user", "content": [
            {"type": "text", "text": PROMPT},
            {"type": "image_url", "image_url": {"url": f"data:{mime(p)};base64,{b64(p)}"}}]}]})
    if not r.ok: return f"HTTP {r.status_code} {r.text[:160]}"
    m = r.json()["choices"][0]["message"]
    return (m.get("content") or "")[:700]

def gemini(p, model, project="papps-513907", loc="global"):
    tok = subprocess.run([os.path.expanduser("~/google-cloud-sdk/bin/gcloud"), "auth", "print-access-token"],
                         capture_output=True, text=True).stdout.strip()
    host = "aiplatform.googleapis.com" if loc == "global" else f"{loc}-aiplatform.googleapis.com"
    url = (f"https://{host}/v1/projects/{project}/locations/{loc}"
           f"/publishers/google/models/{model}:generateContent")
    r = requests.post(url, headers={"Authorization": f"Bearer {tok}"}, timeout=90, json={
        "contents": [{"role": "user", "parts": [{"text": PROMPT},
                      {"inline_data": {"mime_type": mime(p), "data": b64(p)}}]}],
        "generationConfig": {"temperature": 0.1, "responseMimeType": "application/json"},
        "labels": {"lane": "t5-s", "owner": "erick"}})
    if not r.ok: return f"HTTP {r.status_code} {r.text[:200]}"
    return r.json()["candidates"][0]["content"]["parts"][0]["text"][:700]

engines = {"paddle": paddle, "nvocr2": lambda p: nvocr(p, "nemotron-ocr-v2"),
           "vl-super": lambda p: vlm(p, "nvidia/nemotron-3.5-super-vl-120b-a12b"),
           "llama90v": lambda p: vlm(p, "meta/llama-3.2-90b-vision-instruct"),
           "gemini": lambda p: gemini(p, os.environ.get("GEMINI_MODEL", "gemini-3.5-flash"))}
sel = sys.argv[1].split(",") if len(sys.argv) > 1 else list(engines)
for img in sorted(glob.glob("testimages/*")):
    for name in sel:
        t = time.perf_counter()
        try: out = engines[name](img)
        except Exception as e: out = f"FAIL {type(e).__name__}"
        print(f"### {os.path.basename(img)} · {name} · {int((time.perf_counter()-t)*1000)} ms\n{out}\n", flush=True)
