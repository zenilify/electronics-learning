#!/usr/bin/env python3
"""Synthesise EN + DE test voice notes for eval_summarize.py (Magpie, 16 kHz) -> ~/t5/notes-t5s/{en,de}.wav.
Run on cachy from server/. Lists the DE voices it finds; never prints secrets."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import riva.client
from riva.client.proto import riva_tts_pb2
from papp.config import load_env_file
from papp.pipeline import Pipeline, TTS_FN, to_wav

key = load_env_file(os.path.expanduser("~/.config/zenilify/nvidia_build.env"))["API_KEY"]
p = Pipeline(key)
svc = riva.client.SpeechSynthesisService(p._auth_for(TTS_FN))
cfg = svc.stub.GetRivaSynthesisConfig(riva_tts_pb2.RivaSynthesisConfigRequest(), metadata=p._md(TTS_FN))
voices = []
for m in cfg.model_config:
    v = m.parameters.get("voice_name", ""); l = m.parameters.get("language_code", "")
    voices += [x for x in m.parameters.get("subvoices", "").split(",") if x] if not v else [v]
names = sorted({v.split(":")[0] for v in voices})
de = [v for v in names if "DE" in v.upper()]
print("voices:", len(names), "DE:", de[:6])
NOTES = {
    "en": ("en-US", "Magpie-Multilingual.EN-US.Aria", [
        "Quick voice note after the call with the M5Stack team.",
        "They liked the Tab5 translator demo, especially the camera translation.",
        "Erick has to send the technical document before the fifth of December.",
        "Their engineers will ship two new Tab5 units and one LLM module for testing.",
        "Next meeting is on the twelfth of December at three pm Beijing time.",
        "Open question: battery life should reach four hours of continuous use."]),
    "de": ("de-DE", os.environ.get("DE_VOICE", de[0] if de else ""), [
        "Kurze Sprachnotiz nach dem Gespräch mit dem Kunden in Zürich.",
        "Sie wollen die OEE-Auswertung für drei Linien bis Ende Januar.",
        "Erick schickt das Angebot bis Freitag, den vierzehnten November.",
        "Das LLM-Modul soll lokal laufen, ohne Cloud.",
        "Nächster Termin ist am zweiten Dezember um zehn Uhr.",
        "Offener Punkt: Wer übernimmt die Schulung der Schichtleiter?"]),
}
if os.environ.get("TRY_DE"):
    for v in os.environ["TRY_DE"].split(","):
        req = riva_tts_pb2.SynthesizeSpeechRequest(text="Hallo Erick, guten Tag.", voice_name=v, language_code="de-DE",
              sample_rate_hz=16000, encoding=riva.client.AudioEncoding.LINEAR_PCM)
        try:
            a = svc.stub.Synthesize(req, metadata=p._md(TTS_FN), timeout=20).audio; print("DE OK", v, len(a))
        except Exception as e:  # noqa: BLE001
            print("DE no", v, str(getattr(e, "details", lambda: "")())[:90])
    sys.exit(0)
out_dir = os.path.expanduser("~/t5/notes-t5s"); os.makedirs(out_dir, exist_ok=True)
for lang, (code, voice, lines) in NOTES.items():
    if not voice:
        print(lang, "no voice"); continue
    pcm = b""
    for ln in lines:
        req = riva_tts_pb2.SynthesizeSpeechRequest(text=ln, voice_name=voice, language_code=code, sample_rate_hz=16000,
                                                   encoding=riva.client.AudioEncoding.LINEAR_PCM)
        try:
            pcm += svc.stub.Synthesize(req, metadata=p._md(TTS_FN), timeout=20).audio + b"\0" * 16000
        except Exception as e:  # noqa: BLE001
            print(lang, "FAIL", type(e).__name__, getattr(e, "details", lambda: "")()[:80]); pcm = b""; break
    if pcm:
        open(f"{out_dir}/{lang}.wav", "wb").write(to_wav(pcm, 16000))
        print(lang, voice, f"{len(pcm)/32000:.1f}s ->", f"{out_dir}/{lang}.wav")
