#!/usr/bin/env python3
"""Aggregate out/*/status.json into manifest.json + manifest.md (sha256, size, toolchains).

  manifest.py <out_dir> <sources.tsv> <build_id> <gcs_prefix>
Exit code 0 always; the verdict is in manifest.json["summary"].
"""
import hashlib, json, os, pathlib, sys, datetime

out, sources, build_id, prefix = sys.argv[1:5]
out = pathlib.Path(out)
ART = {".bin", ".uf2", ".hex", ".elf"}

def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

targets = []
for st in sorted(out.glob("*/status.json")):
    s = json.load(open(st))
    s["artifacts"] = [
        {"file": p.name, "bytes": p.stat().st_size, "sha256": sha(p)}
        for p in sorted(st.parent.iterdir()) if p.suffix in ART]
    pins = st.parent / "pins.txt"
    if pins.exists():
        s["pins"] = pins.read_text().strip().splitlines()[:40]
    targets.append(s)

src = [dict(zip(["repo", "commit", "dirty_files_in_scope", "origin"], l.split("\t")))
       for l in open(sources).read().splitlines()[1:] if l.strip()]
ok = sum(t["status"] == "ok" for t in targets)
m = {"build_id": build_id, "generated_utc": datetime.datetime.utcnow().isoformat(timespec="seconds") + "Z",
     "gcs": prefix, "sources": src,
     "summary": {"targets": len(targets), "ok": ok, "failed": len(targets) - ok},
     "note": "Built with PLACEHOLDER secrets (foundry/scripts/dummy_secrets.py). "
             "These images cannot join any real network; do not flash them as-is.",
     "targets": targets}
json.dump(m, open(out / "manifest.json", "w"), indent=1)

md = [f"# Kaizen Firmware Foundry — build {build_id}", "",
      f"{ok}/{len(targets)} OK · {m['generated_utc']} · `{prefix}`", "",
      "| target | kind | status | s | artifact (bytes, sha256/12) | first error |", "|---|---|---|---|---|---|"]
for t in targets:
    arts = "<br>".join(f"{a['file']} ({a['bytes']}, {a['sha256'][:12]})" for a in t["artifacts"]) or "—"
    md.append(f"| {t['name']} | {t['kind']} | {'✅' if t['status']=='ok' else '❌'} | {t['seconds']} | {arts} | "
              f"{(t.get('first_error') or '').replace('|','/')[:160]} |")
md += ["", "## Sources", ""] + [f"- {s['repo']} `{s['commit'][:12]}` dirty={s['dirty_files_in_scope']}" for s in src]
md += ["", "## Toolchains", ""] + sorted({f"- {t['kind']}: {t['toolchain']}" for t in targets})
(out / "manifest.md").write_text("\n".join(md) + "\n")
print("\n".join(md))
