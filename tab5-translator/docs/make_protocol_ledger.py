#!/usr/bin/env python3
"""Append/refresh the commit ledger (section 6) of WORK-PROTOCOL.md from git history.
Run from the repo root of electronics-learning:  TZ=America/Mexico_City python3 tab5-translator/docs/make_protocol_ledger.py
Lane = mapped from the Claude-Session trailer (sessions listed in LANES below; unknown sessions print their id)."""
import os, subprocess

LANES = {"01UigXmJvhZB": "L4 / tower 01UigXmJ (incl. N4)", "01UigXmM6nLq": "T5 (claude-3d)",
         "01UigXmKqhDZ": "T5-S (claude-98, claude-45)", "01UigXmLnm84": "T5-B (claude-1b)",
         "01UigXmJC9yG": "T5-R (claude-ab)", "01UigXmLkAbz": "T5-I (claude-e4)"}
DOC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "WORK-PROTOCOL.md")
fmt = "@@%h\x1f%ad\x1f%an\x1f%s\x1f%(trailers:key=Claude-Session,valueonly,separator=;)\x1f%(trailers:key=Co-Authored-By,valueonly,separator=;)"
out = subprocess.run(["git", "log", "--reverse", "--date=format-local:%Y-%m-%d %H:%M", f"--format={fmt}", "--name-only",
                      "--", "tab5-translator"], capture_output=True, text=True, check=True).stdout
rows, cur = [], None
for l in out.splitlines():
    if l.startswith("@@"):
        cur = l[2:].split("\x1f") + [set()]
        rows.append(cur)
    elif l and cur:
        p = l.split("/")
        cur[-1].add("/".join(p[1:3]) if len(p) > 3 else "/".join(p[1:]))
lines = ["## 6 · Commit ledger (electronics-learning `tab5-translator/`, generated)", "",
         f"{len(rows)} commits. Author field = git identity of the machine; Co-author = the AI model; Lane = from the "
         "`Claude-Session` trailer.", "",
         "| # | Commit | Date (CST) | Lane | Co-author | Areas | Subject |", "|---|---|---|---|---|---|---|"]
for i, (h, d, a, s, sess, co, areas) in enumerate(rows, 1):
    sid = sess.split("session_")[-1][:12] if sess else ""
    lane = LANES.get(sid, sid or "—")
    model = co.split("<")[0].strip() or "—"
    ar = ", ".join(sorted(areas))
    ar = ar if len(ar) <= 70 else ar[:67] + "…"
    lines.append(f"| {i} | `{h}` | {d} | {lane} | {model} | {ar} | {s.replace('|', '/')} |")
lines += ["", "Regenerate: `TZ=America/Mexico_City python3 tab5-translator/docs/make_protocol_ledger.py` (rewrites this section).", ""]
doc = open(DOC, encoding="utf-8").read()
cut = doc.find("## 6 · Commit ledger")
doc = (doc[:cut] if cut >= 0 else doc.rstrip("\n") + "\n\n") + "\n".join(lines)
open(DOC, "w", encoding="utf-8").write(doc)
print(f"ledger: {len(rows)} commits")
