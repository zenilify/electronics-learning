#!/usr/bin/env python3
"""mock_line.py — stand-in for N4's Traceability Cell API (lane T5-I, satellites/line/tools).

Implements exactly the contract in mission-control claude/handoffs/2026-11-08-n4.md §3
(AGREEd by T5 2026-11-09 00:15) so the satellites can be built before N4 ships it in server/:

  POST /scan            {code, station, operator?, kind:"qr"|"rfid", result:"ok"|"scrap"|"rework", reason?, lot?, ts?}
  POST /line/event      {station, type:"run"|"stop"|"fault"|"andon"|"andon_clear"|"login"|"logout"|"kanban", operator?, reason?, text?}
  GET  /line/kpi        ?station=&window=shift|hour|<iso>/<iso>
  GET  /line/trace      ?code=
  GET|POST /line/config {stations[], ideal_cycle_s, shift{start,end,breaks[]}, reasons{code:{es,zh,en}}}
  POST /line/report     ?out=es
  POST /line/reset      {confirm:"demo"}

It is a MOCK: every reply carries "mock": true, /health says service "papp-line-mock", and
`text` is not translated (echoed in es/zh/en). Never point the Tab5 at it without the word
"demo" on screen. Stdlib only. The bearer is read from a 0600 file, never from argv.

  python3 mock_line.py                       # 0.0.0.0:8788, token ~/.config/zenilify/papp_token
  MOCK_LINE_PORT=8788 MOCK_LINE_DB=~/t5/line-mock.db python3 mock_line.py
"""
import hmac, json, os, re, sqlite3, threading, time
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

PORT = int(os.environ.get("MOCK_LINE_PORT", "8788"))
DB = os.path.expanduser(os.environ.get("MOCK_LINE_DB", ":memory:"))
TOKEN_FILE = os.path.expanduser(os.environ.get("PAPP_TOKEN_FILE", "~/.config/zenilify/papp_token"))
TOKEN = open(TOKEN_FILE).read().strip()

RESULTS = {"ok", "scrap", "rework"}
KINDS = {"qr", "rfid"}
EVENTS = {"run", "stop", "fault", "andon", "andon_clear", "login", "logout", "kanban"}

# Only the four codes N4 named in §3 are real; the other eight defaults are N4's to publish.
CONFIG = {
    "stations": ["EOL-1", "BADGE-1", "SENSOR-1"],
    "ideal_cycle_s": 6,
    "shift": {"start": "06:00", "end": "22:00", "breaks": []},
    "reasons": {
        "S01": {"es": "Defecto de material", "zh": "材料缺陷", "en": "Material defect"},
        "D01": {"es": "Falta de material", "zh": "缺料", "en": "No material"},
        "D02": {"es": "Cambio de modelo", "zh": "换型", "en": "Changeover"},
        "D03": {"es": "Falla de equipo", "zh": "设备故障", "en": "Breakdown"},
    },
}

lock = threading.Lock()
db = sqlite3.connect(DB, check_same_thread=False)
db.executescript("""
CREATE TABLE IF NOT EXISTS scans(id INTEGER PRIMARY KEY, ts REAL, code TEXT, station TEXT, operator TEXT,
  kind TEXT, result TEXT, reason TEXT, lot TEXT);
CREATE TABLE IF NOT EXISTS events(id INTEGER PRIMARY KEY, ts REAL, station TEXT, type TEXT, operator TEXT,
  reason TEXT, text TEXT, value REAL);
""")


def reason_obj(code, text=None):
    r = CONFIG["reasons"].get(code or "", {})
    base = text or code or ""
    return {"code": code, "es": r.get("es", base), "zh": r.get("zh", base), "en": r.get("en", base)}


def lot_of(code, lot):
    if lot:
        return lot
    m = re.match(r"(?:LOT[:=-]?)?([A-Za-z0-9]+)[-_/|]", code or "")
    return m.group(1) if m else (code or "")[:8]


def window(q):
    now = time.time()
    w = (q.get("window") or ["shift"])[0]
    if w == "hour":
        return w, now - 3600, now
    if "/" in w:
        a, b = w.split("/", 1)
        return w, datetime.fromisoformat(a).timestamp(), datetime.fromisoformat(b).timestamp()
    d = datetime.now()
    h, m = map(int, CONFIG["shift"]["start"].split(":"))
    start = d.replace(hour=h, minute=m, second=0, microsecond=0)
    if start > d:
        start -= timedelta(days=1)
    return "shift", start.timestamp(), now


def andon_open(station=None):
    out = {}
    for ts, stn, typ, op, reason, text in db.execute(
            "SELECT ts,station,type,operator,reason,text FROM events WHERE type IN('andon','andon_clear') ORDER BY id"):
        if typ == "andon":
            o = reason_obj(reason, text)
            out[stn] = {"station": stn, "reason": reason or text or "", "es": o["es"], "zh": o["zh"], "en": o["en"],
                        "operator": op, "ts": ts}
        else:
            out.pop(stn, None)
    return [v for k, v in out.items() if not station or k == station]


def state_of(station):
    r = db.execute("SELECT type FROM events WHERE station=? AND type IN('run','stop','fault') ORDER BY id DESC LIMIT 1",
                   (station,)).fetchone()
    return r[0] if r else "idle"


def kpi(q):
    name, t0, t1 = window(q)
    stn = (q.get("station") or [None])[0]
    f, a = ("AND station=?", (stn,)) if stn else ("", ())
    total, good, scrap = db.execute(
        f"SELECT COUNT(*), COALESCE(SUM(result='ok'),0), COALESCE(SUM(result='scrap'),0) FROM scans WHERE ts BETWEEN ? AND ? {f}",
        (t0, t1) + a).fetchone()
    # run/stop seconds: walk the state events; a station is running from 'run' until 'stop'/'fault'.
    run_s, cur = 0.0, {}
    for ts, s, typ in db.execute(
            f"SELECT ts,station,type FROM events WHERE type IN('run','stop','fault') AND ts<=? {f} ORDER BY id", (t1,) + a):
        if typ == "run":
            cur.setdefault(s, max(ts, t0))
        elif s in cur:
            run_s += max(0.0, ts - max(cur.pop(s), t0))
    run_s += sum(t1 - max(v, t0) for v in cur.values())
    planned = max(1.0, t1 - t0)
    ideal = CONFIG["ideal_cycle_s"]
    A = min(1.0, run_s / planned)
    P = min(1.0, ideal * total / run_s) if run_s > 0 else 0.0
    Q = good / total if total else 0.0
    top = [dict(reason_obj(c), count=n) for c, n in db.execute(
        f"SELECT reason, COUNT(*) FROM (SELECT reason,station,ts FROM scans WHERE reason IS NOT NULL AND reason<>'' "
        f"UNION ALL SELECT reason,station,ts FROM events WHERE type IN('andon','stop','fault') AND reason IS NOT NULL AND reason<>'') "
        f"WHERE ts BETWEEN ? AND ? {f} GROUP BY reason ORDER BY 2 DESC LIMIT 5", (t0, t1) + a)]
    last = [dict(zip(("ts", "code", "station", "operator", "result", "lot"), r)) for r in db.execute(
        f"SELECT ts,code,station,operator,result,lot FROM scans WHERE 1=1 {f} ORDER BY id DESC LIMIT 10", a)]
    return {"window": name, "planned_s": round(planned), "run_s": round(run_s), "stop_s": round(planned - run_s),
            "ideal_cycle_s": ideal, "total": total, "good": good, "scrap": scrap,
            "availability": round(A, 4), "performance": round(P, 4), "quality": round(Q, 4), "oee": round(A * P * Q, 4),
            "andon_open": andon_open(stn), "top_reasons": top, "last_scans": last}


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):  # never log headers
        print(time.strftime("%H:%M:%S"), self.command, self.path.split("?")[0], a[1] if len(a) > 1 else "", flush=True)

    def send(self, code, obj):
        if isinstance(obj, dict):
            obj["mock"] = True
        b = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def auth(self):
        got = self.headers.get("Authorization", "")
        if hmac.compare_digest(got, "Bearer " + TOKEN):
            return True
        self.send(401, {"detail": "Unauthorized"})
        return False

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return None

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/health":
            return self.send(200, {"ok": True, "service": "papp-line-mock", "features": ["scan", "line"]})
        if not self.auth():
            return
        with lock:
            if u.path == "/line/kpi":
                return self.send(200, kpi(q))
            if u.path == "/line/config":
                return self.send(200, dict(CONFIG))
            if u.path == "/line/trace":
                code = (q.get("code") or [""])[0]
                scans = [dict(zip(("ts", "station", "operator", "result", "lot"), r)) for r in db.execute(
                    "SELECT ts,station,operator,result,lot FROM scans WHERE code=? ORDER BY id", (code,))]
                lot = scans[0]["lot"] if scans else lot_of(code, None)
                t0 = scans[0]["ts"] - 3600 if scans else 0
                ev = [dict(zip(("ts", "station", "type", "operator", "reason"), r)) for r in db.execute(
                    "SELECT ts,station,type,operator,reason FROM events WHERE ts>=? ORDER BY id LIMIT 50", (t0,))]
                return self.send(200, {"code": code, "lot": lot, "scans": scans, "events": ev if scans else []})
        self.send(404, {"detail": "Not Found"})

    def do_POST(self):
        u = urlparse(self.path)
        if not self.auth():
            return
        j = self.body()
        if j is None:
            return self.send(422, {"detail": "invalid JSON"})
        now = time.time()
        with lock:
            if u.path == "/scan":
                code, stn = j.get("code"), j.get("station")
                kind, res = j.get("kind", "qr"), j.get("result", "ok")
                if not code or not stn or kind not in KINDS or res not in RESULTS:
                    return self.send(422, {"detail": "need code, station, kind qr|rfid, result ok|scrap|rework"})
                lot = lot_of(code, j.get("lot"))
                dup = db.execute("SELECT 1 FROM scans WHERE code=? AND station=?", (code, stn)).fetchone() is not None
                # FIFO: warn when an OLDER lot (lexically smaller) is first seen after a newer one at this station.
                newest = db.execute("SELECT MAX(lot) FROM scans WHERE station=?", (stn,)).fetchone()[0]
                seen = db.execute("SELECT 1 FROM scans WHERE station=? AND lot=?", (stn, lot)).fetchone()
                fifo = bool(newest and not seen and lot < newest)
                cur = db.execute("INSERT INTO scans(ts,code,station,operator,kind,result,reason,lot) VALUES(?,?,?,?,?,?,?,?)",
                                 (j.get("ts") or now, code, stn, j.get("operator"), kind, res, j.get("reason"), lot))
                db.commit()
                g, s, r = db.execute("SELECT COALESCE(SUM(result='ok'),0),COALESCE(SUM(result='scrap'),0),"
                                     "COALESCE(SUM(result='rework'),0) FROM scans WHERE station=?", (stn,)).fetchone()
                seq = db.execute("SELECT COUNT(*) FROM scans WHERE station=?", (stn,)).fetchone()[0]
                return self.send(200, {"id": cur.lastrowid, "seq": seq, "station": stn, "lot": lot, "duplicate": dup,
                                       "fifo_warning": fifo, "counts": {"good": g, "scrap": s, "rework": r}})
            if u.path == "/line/event":
                stn, typ = j.get("station"), j.get("type")
                if not stn or typ not in EVENTS:
                    return self.send(422, {"detail": "need station and type " + "|".join(sorted(EVENTS))})
                v = j.get("value")
                cur = db.execute("INSERT INTO events(ts,station,type,operator,reason,text,value) VALUES(?,?,?,?,?,?,?)",
                                 (now, stn, typ, j.get("operator"), j.get("reason"), j.get("text"),
                                  v if isinstance(v, (int, float)) else None))
                db.commit()
                return self.send(200, {"id": cur.lastrowid, "state": state_of(stn), "andon_open": andon_open()})
            if u.path == "/line/config":
                for k in ("stations", "ideal_cycle_s", "shift", "reasons"):
                    if k in j:
                        CONFIG[k] = j[k]
                return self.send(200, dict(CONFIG))
            if u.path == "/line/report":
                k = kpi({})
                bullets = [f"OEE {k['oee']:.0%} (A {k['availability']:.0%} · P {k['performance']:.0%} · Q {k['quality']:.0%})",
                           f"{k['total']} piezas: {k['good']} buenas, {k['scrap']} scrap",
                           f"{len(k['andon_open'])} andon abiertos"] + [f"{r['code']} {r['es']} ×{r['count']}" for r in k["top_reasons"]]
                return self.send(200, {"title": "Reporte de turno (mock)", "summary": " · ".join(bullets[:2]),
                                       "bullets": bullets, "markdown": "\n".join("- " + b for b in bullets)})
            if u.path == "/line/reset":
                if j.get("confirm") != "demo":
                    return self.send(422, {"detail": 'need {"confirm":"demo"}'})
                kept = None
                if DB != ":memory:":  # contract: keep the old DB as line.db.<ts>, no delete
                    kept = f"{DB}.{time.strftime('%Y%m%d-%H%M%S')}"
                    bk = sqlite3.connect(kept)
                    db.backup(bk)
                    bk.close()
                db.executescript("DELETE FROM scans; DELETE FROM events;")
                db.commit()
                return self.send(200, {"ok": True, "kept": kept})
        self.send(404, {"detail": "Not Found"})


if __name__ == "__main__":
    print(f"mock_line on :{PORT} db={DB} token_len={len(TOKEN)}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
