"""Traceability Cell ("Kaizen Lab line in a box") — the server side of the Tab5 "Línea · 产线" board.

Contract: mission-control claude/handoffs/2026-11-08-n4.md §3 (proposed by N4, AGREEd by T5 2026-11-09 00:15),
built by T5 on 2026-11-10 because N4 closed without shipping it. Logic ported from T5-I's §3-exact mock
(satellites/line/tools/mock_line.py) so the satellites see the same replies, minus the "mock" flag.

Store: SQLite PAPP_CACHE_DIR/line.db (the RW cache volume on cachy). On Cloud Run the cache dir is ephemeral,
so there it is demo-only, as the contract says. Additive to N4's table: /line/event accepts value/unit/count
(T5-I ask 11-10), andon text is translated into es/zh/en with the pipeline's NMT when a translate function is given.
"""
from __future__ import annotations

import json
import os
import re
import sqlite3
import threading
import time
from datetime import datetime, timedelta

RESULTS = {"ok", "scrap", "rework"}
KINDS = {"qr", "rfid"}
EVENTS = {"run", "stop", "fault", "andon", "andon_clear", "login", "logout", "kanban"}

# 12 bilingual (trilingual) default reason codes — S = scrap/quality, D = downtime, A = andon/help.
DEFAULT_REASONS = {
    "S01": {"es": "Defecto de material", "zh": "材料缺陷", "en": "Material defect"},
    "S02": {"es": "Medida fuera de tolerancia", "zh": "尺寸超差", "en": "Out of tolerance"},
    "S03": {"es": "Daño en el manejo", "zh": "搬运损坏", "en": "Handling damage"},
    "S04": {"es": "Etiqueta incorrecta", "zh": "标签错误", "en": "Wrong label"},
    "S05": {"es": "Contaminación", "zh": "污染", "en": "Contamination"},
    "D01": {"es": "Falta de material", "zh": "缺料", "en": "No material"},
    "D02": {"es": "Cambio de modelo", "zh": "换型", "en": "Changeover"},
    "D03": {"es": "Falla de equipo", "zh": "设备故障", "en": "Breakdown"},
    "D04": {"es": "Falta de operador", "zh": "缺少操作员", "en": "No operator"},
    "D05": {"es": "Mantenimiento planeado", "zh": "计划保养", "en": "Planned maintenance"},
    "A01": {"es": "Calidad, ayuda", "zh": "质量求助", "en": "Quality help"},
    "A02": {"es": "Supervisor, por favor", "zh": "请主管过来", "en": "Supervisor, please"},
}


class Line:
    def __init__(self, path: str, translate=None):
        self.path = path
        self.translate = translate            # f(text, src, tgt) -> str, or None
        self.lock = threading.Lock()
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.executescript("""
        CREATE TABLE IF NOT EXISTS scans(id INTEGER PRIMARY KEY, ts REAL, code TEXT, station TEXT, operator TEXT,
          kind TEXT, result TEXT, reason TEXT, lot TEXT);
        CREATE TABLE IF NOT EXISTS events(id INTEGER PRIMARY KEY, ts REAL, station TEXT, type TEXT, operator TEXT,
          reason TEXT, text TEXT, value REAL, unit TEXT, count INTEGER, text_es TEXT, text_zh TEXT, text_en TEXT);
        """)
        self.cfg_path = os.path.join(os.path.dirname(path) or ".", "line-config.json")
        self.cfg = {"stations": ["EOL-1", "BADGE-1", "SENSOR-1"], "ideal_cycle_s": 6,
                    "shift": {"start": "06:00", "end": "22:00", "breaks": []}, "reasons": dict(DEFAULT_REASONS)}
        try:
            self.cfg.update(json.load(open(self.cfg_path)))
        except (OSError, ValueError):
            pass

    # ---------- helpers ----------
    def reason_obj(self, code, text=None, tr=None):
        r = self.cfg["reasons"].get(code or "", {})
        base = text or code or ""
        tr = tr or {}
        return {"code": code, "es": r.get("es", tr.get("es") or base), "zh": r.get("zh", tr.get("zh") or base),
                "en": r.get("en", tr.get("en") or base)}

    @staticmethod
    def lot_of(code, lot):
        if lot:
            return lot
        m = re.match(r"([A-Za-z0-9]+)[-_/|]", code or "")    # "LOT9-004" -> lot "LOT9"
        return m.group(1) if m else (code or "")[:8]

    def window(self, w):
        now = time.time()
        w = w or "shift"
        if w == "hour":
            return w, now - 3600, now
        if "/" in w:
            a, b = w.split("/", 1)
            return w, datetime.fromisoformat(a).timestamp(), datetime.fromisoformat(b).timestamp()
        d = datetime.now()
        h, m = map(int, self.cfg["shift"]["start"].split(":"))
        start = d.replace(hour=h, minute=m, second=0, microsecond=0)
        if start > d:
            start -= timedelta(days=1)
        return "shift", start.timestamp(), now

    def andon_open(self, station=None):
        out = {}
        for ts, stn, typ, op, reason, text, es, zh, en in self.db.execute(
                "SELECT ts,station,type,operator,reason,text,text_es,text_zh,text_en FROM events "
                "WHERE type IN('andon','andon_clear') ORDER BY id"):
            if typ == "andon":
                o = self.reason_obj(reason, text, {"es": es, "zh": zh, "en": en})
                out[stn] = {"station": stn, "reason": reason or text or "", "es": o["es"], "zh": o["zh"], "en": o["en"],
                            "operator": op, "ts": ts}
            else:
                out.pop(stn, None)
        return [v for k, v in out.items() if not station or k == station]

    def state_of(self, station):
        r = self.db.execute("SELECT type FROM events WHERE station=? AND type IN('run','stop','fault') "
                            "ORDER BY id DESC LIMIT 1", (station,)).fetchone()
        return r[0] if r else "idle"

    def _translate3(self, text):
        if not text or not self.translate:
            return {}
        out = {}
        src = "zh" if re.search(r"[一-鿿]", text) else "es"
        for tgt in ("es", "zh", "en"):
            if tgt == src:
                out[tgt] = text
                continue
            try:
                out[tgt] = self.translate(text, src, tgt)
            except Exception:  # noqa: BLE001 — a translation miss never blocks the andon
                out[tgt] = text
        return out

    # ---------- API ----------
    def kpi(self, window=None, station=None):
        with self.lock:
            name, t0, t1 = self.window(window)
            f, a = ("AND station=?", (station,)) if station else ("", ())
            total, good, scrap = self.db.execute(
                f"SELECT COUNT(*), COALESCE(SUM(result='ok'),0), COALESCE(SUM(result='scrap'),0) FROM scans "
                f"WHERE ts BETWEEN ? AND ? {f}", (t0, t1) + a).fetchone()
            run_s, cur = 0.0, {}
            for ts, s, typ in self.db.execute(
                    f"SELECT ts,station,type FROM events WHERE type IN('run','stop','fault') AND ts<=? {f} ORDER BY id",
                    (t1,) + a):
                if typ == "run":
                    cur.setdefault(s, max(ts, t0))
                elif s in cur:
                    run_s += max(0.0, ts - max(cur.pop(s), t0))
            run_s += sum(t1 - max(v, t0) for v in cur.values())
            # planned time starts when the line first did something in the window (a demo cell switched on at 10:00
            # must not count 06:00-10:00 as lost availability); before that the shift had nothing planned
            first = self.db.execute(f"SELECT MIN(ts) FROM (SELECT ts,station FROM events UNION ALL SELECT ts,station FROM scans) "
                                    f"WHERE ts BETWEEN ? AND ? {f}", (t0, t1) + a).fetchone()[0]
            planned = max(1.0, t1 - max(t0, first or t1))
            ideal = self.cfg["ideal_cycle_s"]
            A = min(1.0, run_s / planned)
            P = min(1.0, ideal * total / run_s) if run_s > 0 else 0.0
            Q = good / total if total else 0.0
            top = [dict(self.reason_obj(c), count=n) for c, n in self.db.execute(
                f"SELECT reason, COUNT(*) FROM (SELECT reason,station,ts FROM scans WHERE reason IS NOT NULL AND reason<>'' "
                f"UNION ALL SELECT reason,station,ts FROM events WHERE type IN('andon','stop','fault') AND reason IS NOT NULL "
                f"AND reason<>'') WHERE ts BETWEEN ? AND ? {f} GROUP BY reason ORDER BY 2 DESC LIMIT 5", (t0, t1) + a)]
            last = [dict(zip(("ts", "code", "station", "operator", "result", "lot"), r)) for r in self.db.execute(
                f"SELECT ts,code,station,operator,result,lot FROM scans WHERE 1=1 {f} ORDER BY id DESC LIMIT 10", a)]
            sensors = {}
            for stn, v, unit, ts in self.db.execute(
                    "SELECT station, value, unit, MAX(ts) FROM events WHERE value IS NOT NULL GROUP BY station"):
                sensors[stn] = {"value": v, "unit": unit, "ts": ts}
            return {"window": name, "planned_s": round(planned), "run_s": round(run_s), "stop_s": round(planned - run_s),
                    "ideal_cycle_s": ideal, "total": total, "good": good, "scrap": scrap,
                    "availability": round(A, 4), "performance": round(P, 4), "quality": round(Q, 4),
                    "oee": round(A * P * Q, 4), "andon_open": self.andon_open(station), "top_reasons": top,
                    "last_scans": last, "sensors": sensors}

    def scan(self, j):
        code, stn = j.get("code"), j.get("station")
        kind, res = j.get("kind", "qr"), j.get("result", "ok")
        if not code or not stn or kind not in KINDS or res not in RESULTS:
            raise ValueError("need code, station, kind qr|rfid, result ok|scrap|rework")
        with self.lock:
            lot = self.lot_of(code, j.get("lot"))
            dup = self.db.execute("SELECT 1 FROM scans WHERE code=? AND station=?", (code, stn)).fetchone() is not None
            newest = self.db.execute("SELECT MAX(lot) FROM scans WHERE station=?", (stn,)).fetchone()[0]
            seen = self.db.execute("SELECT 1 FROM scans WHERE station=? AND lot=?", (stn, lot)).fetchone()
            fifo = bool(newest and not seen and lot < newest)   # an older lot first seen after a newer one
            cur = self.db.execute("INSERT INTO scans(ts,code,station,operator,kind,result,reason,lot) "
                                  "VALUES(?,?,?,?,?,?,?,?)", (j.get("ts") or time.time(), code, stn, j.get("operator"),
                                                              kind, res, j.get("reason"), lot))
            self.db.commit()
            g, s, r = self.db.execute("SELECT COALESCE(SUM(result='ok'),0),COALESCE(SUM(result='scrap'),0),"
                                      "COALESCE(SUM(result='rework'),0) FROM scans WHERE station=?", (stn,)).fetchone()
            seq = self.db.execute("SELECT COUNT(*) FROM scans WHERE station=?", (stn,)).fetchone()[0]
            return {"id": cur.lastrowid, "seq": seq, "station": stn, "lot": lot, "duplicate": dup,
                    "fifo_warning": fifo, "counts": {"good": g, "scrap": s, "rework": r}}

    def event(self, j):
        stn, typ = j.get("station"), j.get("type")
        if not stn or typ not in EVENTS:
            raise ValueError("need station and type " + "|".join(sorted(EVENTS)))
        tr = self._translate3(j.get("text")) if typ == "andon" else {}   # outside the lock: NMT is slow
        v, n = j.get("value"), j.get("count")
        with self.lock:
            cur = self.db.execute(
                "INSERT INTO events(ts,station,type,operator,reason,text,value,unit,count,text_es,text_zh,text_en) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
                (time.time(), stn, typ, j.get("operator"), j.get("reason"), j.get("text"),
                 v if isinstance(v, (int, float)) else None, j.get("unit"), n if isinstance(n, int) else None,
                 tr.get("es"), tr.get("zh"), tr.get("en")))
            self.db.commit()
            return {"id": cur.lastrowid, "state": self.state_of(stn), "andon_open": self.andon_open()}

    def trace(self, code):
        with self.lock:
            scans = [dict(zip(("ts", "station", "operator", "result", "lot"), r)) for r in self.db.execute(
                "SELECT ts,station,operator,result,lot FROM scans WHERE code=? ORDER BY id", (code,))]
            lot = scans[0]["lot"] if scans else self.lot_of(code, None)
            t0 = scans[0]["ts"] - 3600 if scans else 0
            ev = [dict(zip(("ts", "station", "type", "operator", "reason"), r)) for r in self.db.execute(
                "SELECT ts,station,type,operator,reason FROM events WHERE ts>=? ORDER BY id LIMIT 50", (t0,))]
            lot_scans = [dict(zip(("ts", "code", "station", "result"), r)) for r in self.db.execute(
                "SELECT ts,code,station,result FROM scans WHERE lot=? ORDER BY id LIMIT 50", (lot,))]
            return {"code": code, "lot": lot, "scans": scans, "lot_scans": lot_scans, "events": ev if scans else []}

    def config(self, j=None):
        with self.lock:
            if j:
                for k in ("stations", "ideal_cycle_s", "shift", "reasons"):
                    if k in j:
                        self.cfg[k] = j[k]
                try:
                    json.dump(self.cfg, open(self.cfg_path, "w"), ensure_ascii=False, indent=1)
                except OSError:
                    pass
            return dict(self.cfg)

    def report(self, out="es"):
        k = self.kpi()
        lang = out if out in ("es", "zh", "en") else "es"
        L = {"es": ("Reporte de turno", "piezas", "buenas", "andon abiertos"),
             "zh": ("班次报告", "件", "良品", "未关闭报警"),
             "en": ("Shift report", "pieces", "good", "open andons")}[lang]
        bullets = [f"OEE {k['oee']:.0%} (A {k['availability']:.0%} · P {k['performance']:.0%} · Q {k['quality']:.0%})",
                   f"{k['total']} {L[1]}: {k['good']} {L[2]}, {k['scrap']} scrap",
                   f"{len(k['andon_open'])} {L[3]}"] + [f"{r['code']} {r[lang]} ×{r['count']}" for r in k["top_reasons"]]
        return {"title": L[0], "summary": " · ".join(bullets[:2]), "bullets": bullets,
                "markdown": f"# {L[0]}\n\n" + "\n".join("- " + b for b in bullets), "kpi": k}

    def reset(self, j):
        if (j or {}).get("confirm") != "demo":
            raise ValueError('need {"confirm":"demo"}')
        with self.lock:
            kept = f"{self.path}.{time.strftime('%Y%m%d-%H%M%S')}"   # contract: keep line.db.<ts>, no delete
            bk = sqlite3.connect(kept)
            self.db.backup(bk)
            bk.close()
            self.db.executescript("DELETE FROM scans; DELETE FROM events;")
            self.db.commit()
            return {"ok": True, "kept": os.path.basename(kept)}
