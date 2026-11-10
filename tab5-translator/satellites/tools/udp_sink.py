#!/usr/bin/env python3
"""PAPP remotes — reference receiver (stand-in Tab5) for protocol v1. See ../PROTOCOL.md.

Listens on UDP 47800, verifies the HMAC tag, keeps the per-device session (boot / nonce / last seq),
executes each event once, acks with a fake Tab5 state, prints one JSON line per datagram.
The key is read from a 0600 file into memory; it is never printed (only len + sha256 prefix).

  python3 udp_sink.py --auto-pair                 # accept every device that proves the key
  python3 udp_sink.py --allow stick-353c,dial-944c
  python3 udp_sink.py --auto-pair --log ~/t5/remotes/sink.jsonl --summary ~/t5/remotes/sink-summary.json
"""
import argparse, hashlib, hmac, json, os, secrets, signal, socket, sys, time

PAIRS = ["es-zh", "es-en", "en-zh", "de-zh"]


def load_key(path):
    path = os.path.expanduser(path)
    mode = os.stat(path).st_mode & 0o777
    if mode & 0o077:
        sys.exit(f"key file mode is {oct(mode)}; must be 0600")
    with open(path) as f:
        k = bytes.fromhex(f.read().strip())
    if len(k) != 16:
        sys.exit("key must be 16 bytes (32 hex chars)")
    return k


def tag(key, body: bytes) -> bytes:
    return hmac.new(key, body, hashlib.sha256).hexdigest()[:16].encode()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key-file", default="~/.config/zenilify/t5-remote-key")
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=47800)
    ap.add_argument("--auto-pair", action="store_true")
    ap.add_argument("--allow", default="", help="comma-separated device ids already paired")
    ap.add_argument("--log", default="")
    ap.add_argument("--summary", default="", help="write a JSON summary here on exit and every 5 s")
    a = ap.parse_args()

    key = load_key(a.key_file)
    print(json.dumps({"sink": "up", "port": a.port, "key_len": len(key), "key_sha": hashlib.sha256(key).hexdigest()[:8]}), flush=True)
    allow = set(x for x in a.allow.split(",") if x)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind((a.bind, a.port))
    s.settimeout(1.0)
    logf = open(os.path.expanduser(a.log), "a") if a.log else None

    tab5 = {"st": "idle", "pair_i": 0, "vol": 70, "side": 0}
    devs = {}       # dev -> {boot, n, last, addr}
    summ = {}       # dev -> counters
    stop = []
    signal.signal(signal.SIGTERM, lambda *_: stop.append(1))
    signal.signal(signal.SIGINT, lambda *_: stop.append(1))

    def state():
        return {"st": tab5["st"], "pair": PAIRS[tab5["pair_i"]], "vol": tab5["vol"], "side": tab5["side"]}

    def out(rec):
        line = json.dumps(rec, ensure_ascii=False)
        print(line, flush=True)
        if logf:
            logf.write(line + "\n"); logf.flush()

    def write_summary():
        if not a.summary:
            return
        res = {}
        for d, c in summ.items():
            lr = sorted(c["lr"])
            res[d] = {k: v for k, v in c.items() if k != "lr"}
            if lr:
                res[d]["rtt_ms"] = {"n": len(lr), "min": round(lr[0] / 1000, 2), "median": round(lr[len(lr) // 2] / 1000, 2),
                                     "p95": round(lr[int(len(lr) * 0.95) - 1 if len(lr) >= 20 else -1] / 1000, 2), "max": round(lr[-1] / 1000, 2)}
        tmp = os.path.expanduser(a.summary) + ".tmp"
        with open(tmp, "w") as f:
            json.dump(res, f, indent=1)
        os.replace(tmp, os.path.expanduser(a.summary))

    last_sum = time.time()
    while not stop:
        if time.time() - last_sum > 5:
            write_summary(); last_sum = time.time()
        try:
            data, addr = s.recvfrom(2048)
        except socket.timeout:
            continue
        t = time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}"
        i = data.rfind(b',"m":"')
        if i < 0 or len(data) - i != 6 + 16 + 2 or not data.endswith(b'"}'):
            out({"t": t, "from": addr[0], "drop": "format", "len": len(data)}); continue
        if not hmac.compare_digest(tag(key, data[:i]), data[i + 6:i + 22]):
            out({"t": t, "from": addr[0], "drop": "auth", "len": len(data)}); continue
        try:
            m = json.loads(data[:i] + b"}")
            dev, seq, ev, arg = m["dev"], int(m["seq"]), m["ev"], m.get("arg", {})
        except Exception as e:
            out({"t": t, "from": addr[0], "drop": "json:" + type(e).__name__}); continue
        c = summ.setdefault(dev, {"datagrams": 0, "executed": 0, "dups": 0, "presses_down": 0, "presses_up": 0, "keepalive": 0,
                                  "cancel": 0, "replay": 0, "pair": 0, "vol": 0, "battery": 0, "hello": 0, "retx": 0, "seq_gaps": 0, "lr": []})
        c["datagrams"] += 1
        if m.get("try"):
            c["retx"] += 1

        def ack(ok=True, err=None, extra=None):
            r = {"v": 1, "dev": dev, "seq": seq, "ev": "ack", "ok": ok}
            if err:
                r["err"] = err
            if ok:
                r["state"] = state()
            if extra:
                r.update(extra)
            body = json.dumps(r, separators=(",", ":"))[:-1].encode()
            s.sendto(body + b',"m":"' + tag(key, body) + b'"}', addr)

        d = devs.get(dev)
        if ev == "hello":
            c["hello"] += 1
            if dev not in allow:
                if a.auto_pair:
                    allow.add(dev)
                    out({"t": t, "from": addr[0], "dev": dev, "PAIR": "accepted", "name": arg.get("name"), "kind": arg.get("kind"), "fw": arg.get("fw")})
                else:
                    out({"t": t, "from": addr[0], "dev": dev, "PAIR": "request (not accepted)", "name": arg.get("name")})
                    ack(False, "pair"); continue
            if d is None or d["boot"] != arg.get("boot"):
                d = devs[dev] = {"boot": arg.get("boot"), "n": secrets.token_hex(4), "last": 0, "addr": addr}
                out({"t": t, "from": addr[0], "dev": dev, "seq": seq, "ev": "hello", "arg": arg, "session": "new"})
            d["addr"] = addr
            d["last"] = max(d["last"], seq)
            ack(True, extra={"n": d["n"], "boot": d["boot"]}); continue
        if dev not in allow:
            ack(False, "pair"); continue
        if d is None or m.get("n") != d["n"]:
            out({"t": t, "from": addr[0], "dev": dev, "seq": seq, "ev": ev, "nack": "nonce"})
            ack(False, "nonce"); continue
        if seq <= d["last"]:
            c["dups"] += 1
            ack(True); continue                      # duplicate: re-ack, do not execute
        if seq != d["last"] + 1:
            c["seq_gaps"] += seq - d["last"] - 1
        d["last"] = seq
        if "lr" in m:
            c["lr"].append(int(m["lr"]))
        ok, err = True, None
        if ev == "ptt_down":
            if arg.get("rep"):
                c["keepalive"] += 1
            else:
                c["presses_down"] += 1
            tab5["st"], tab5["side"] = "listen", int(arg.get("side", 0))
        elif ev == "ptt_up":
            c["presses_up"] += 1
            c["cancel"] += 1 if arg.get("cancel") else 0
            tab5["st"] = "idle"
        elif ev == "replay":
            c["replay"] += 1
        elif ev in ("pair_next", "pair_prev"):
            c["pair"] += 1
            tab5["pair_i"] = (tab5["pair_i"] + (1 if ev == "pair_next" else -1)) % len(PAIRS)
        elif ev in ("vol_up", "vol_down"):
            c["vol"] += 1
            step = int(arg.get("step", 5))
            tab5["vol"] = max(0, min(100, tab5["vol"] + (step if ev == "vol_up" else -step)))
        elif ev == "battery":
            c["battery"] += 1
        else:
            ok, err = False, "unknown"
        if ok:
            c["executed"] += 1
        ack(ok, err)
        rec = {"t": t, "from": addr[0], "dev": dev, "seq": seq, "ev": ev, "arg": arg}
        for k in ("lr", "try"):
            if k in m:
                rec[k] = m[k]
        out(rec)
    write_summary()
    print(json.dumps({"sink": "down"}), flush=True)


if __name__ == "__main__":
    main()
