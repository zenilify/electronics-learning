#!/usr/bin/env python3
"""PAPP remotes — fake remote (protocol v1) for testing a receiver without hardware. See ../PROTOCOL.md.

Speaks exactly what the StickS3/Dial firmware speaks. Aim it at the Tab5 (or at udp_sink.py):

  python3 fake_remote.py --host 192.168.1.50 --presses 20      # 20 x (ptt_down, 300 ms, ptt_up), prints ack RTTs
  python3 fake_remote.py --broadcast --presses 3                # discovery by broadcast, as the real remotes do
  python3 fake_remote.py --host 192.168.1.50 --selftest         # negative cases: bad tag, wrong nonce, duplicate, replayed hello

Key from ~/.config/zenilify/t5-remote-key (0600), never printed.
"""
import argparse, hashlib, hmac, json, os, secrets, socket, sys, time

PORT = 47800


class Remote:
    def __init__(self, key, dev="fake-0001", host=None):
        self.key, self.dev, self.host = key, dev, host
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.s.bind(("0.0.0.0", 0))
        self.seq, self.n, self.boot, self.peer = 0, None, secrets.randbits(32) | 1, None
        self.state = {}

    def pack(self, ev, arg, seq, n=None, key=None):
        m = {"v": 1, "dev": self.dev, "seq": seq, "ev": ev, "arg": arg}
        if n:
            m["n"] = n
        body = json.dumps(m, separators=(",", ":"))[:-1].encode()
        t = hmac.new(key or self.key, body, hashlib.sha256).hexdigest()[:16].encode()
        return body + b',"m":"' + t + b'"}'

    def recv_ack(self, seq, timeout):
        t_end = time.time() + timeout
        while time.time() < t_end:
            self.s.settimeout(max(0.001, t_end - time.time()))
            try:
                data, addr = self.s.recvfrom(2048)
            except socket.timeout:
                return None, None
            i = data.rfind(b',"m":"')
            if i < 0 or not hmac.compare_digest(hmac.new(self.key, data[:i], hashlib.sha256).hexdigest()[:16].encode(), data[i + 6:i + 22]):
                continue
            m = json.loads(data[:i] + b"}")
            if m.get("ev") == "ack" and m.get("dev") == self.dev and m.get("seq") == seq:
                return m, addr
        return None, None

    def raw(self, pkt, seq, tries=5, to=None):
        """send with the firmware's retry rule (60 ms); returns (ack, rtt_ms, tries_used)"""
        t0 = time.perf_counter()
        for i in range(tries):
            self.s.sendto(pkt, to or self.peer)
            ack, addr = self.recv_ack(seq, 0.06)
            if ack:
                return ack, (time.perf_counter() - t0) * 1000, i + 1, addr
        return None, None, tries, None

    def hello(self, boot=None):
        self.seq += 1
        arg = {"kind": "stick", "name": "Fake remote", "fw": "0.1.0", "boot": boot or self.boot}
        to = (self.host, PORT) if self.host else ("255.255.255.255", PORT)
        ack, rtt, tr, addr = self.raw(self.pack("hello", arg, self.seq), self.seq, tries=20, to=to)
        if ack and ack.get("ok"):
            self.n, self.peer, self.state = ack.get("n"), addr, ack.get("state", {})
        return ack

    def event(self, ev, arg=None, tries=5):
        self.seq += 1
        return self.raw(self.pack(ev, arg or {}, self.seq, self.n), self.seq, tries)[:3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host")
    ap.add_argument("--broadcast", action="store_true")
    ap.add_argument("--presses", type=int, default=0)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--dev", default="fake-0001")
    ap.add_argument("--key-file", default="~/.config/zenilify/t5-remote-key")
    a = ap.parse_args()
    if not a.host and not a.broadcast:
        sys.exit("--host <ip> or --broadcast")
    key = bytes.fromhex(open(os.path.expanduser(a.key_file)).read().strip())
    r = Remote(key, a.dev, a.host)
    h = r.hello()
    if not h:
        sys.exit("no ack to hello (receiver down, wrong key, firewall, or client isolation)")
    if not h.get("ok"):
        sys.exit(f"hello refused: {h.get('err')} — accept {a.dev} on the receiver and run again")
    print(json.dumps({"linked": r.peer[0], "state": r.state}))
    if a.presses:
        rtts, lost = [], 0
        for _ in range(a.presses):
            for ev, arg, tries in (("ptt_down", {"side": 0, "syn": 1}, 5), ("ptt_up", {"side": 0, "ms": 300, "syn": 1}, 20)):
                ack, rtt, tr = r.event(ev, arg, tries)
                if ack and ack.get("ok"):
                    rtts.append(rtt)
                else:
                    lost += 1
                if ev == "ptt_down":
                    time.sleep(0.3)
            time.sleep(0.1)
        rtts.sort()
        print(json.dumps({"events": a.presses * 2, "acked": len(rtts), "lost": lost,
                          "rtt_ms": {"min": round(rtts[0], 2), "median": round(rtts[len(rtts) // 2], 2), "max": round(rtts[-1], 2)} if rtts else None}))
    if a.selftest:
        res = {}
        # 1 bad tag -> silence (or err auth)
        r.seq += 1
        ack = r.raw(r.pack("replay", {}, r.seq, r.n, key=b"\x00" * 16), r.seq, tries=2)[0]
        res["bad_tag_ignored"] = ack is None
        # 2 valid event executes
        ack, _, _ = r.event("pair_next")
        res["event_ok"] = bool(ack and ack.get("ok"))
        pair1 = (ack or {}).get("state", {}).get("pair")
        # 3 duplicate seq: re-acked, not executed twice
        ack2 = r.raw(r.pack("pair_next", {}, r.seq, r.n), r.seq)[0]
        res["duplicate_reacked_not_executed"] = bool(ack2 and ack2.get("ok") and ack2.get("state", {}).get("pair") == pair1)
        # 4 wrong nonce -> err nonce
        r.seq += 1
        ack = r.raw(r.pack("replay", {}, r.seq, "00000000"), r.seq)[0]
        res["wrong_nonce_rejected"] = bool(ack and not ack.get("ok") and ack.get("err") == "nonce")
        # 5 hello with another boot id -> new nonce; an event with the OLD nonce is then rejected (replay protection)
        old = r.n
        r.hello(boot=secrets.randbits(32) | 1)
        res["new_boot_new_nonce"] = r.n != old
        r.seq += 1
        ack = r.raw(r.pack("replay", {}, r.seq, old), r.seq)[0]
        res["old_session_event_rejected"] = bool(ack and ack.get("err") == "nonce")
        # 6 unknown event
        ack, _, _ = r.event("selfdestruct")
        res["unknown_event_rejected"] = bool(ack and not ack.get("ok") and ack.get("err") == "unknown")
        res["PASS"] = all(res.values())
        print(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
