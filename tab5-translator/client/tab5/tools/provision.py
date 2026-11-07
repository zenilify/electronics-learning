#!/usr/bin/env python3
"""Build the Tab5's NVS partition (namespace "papp") from secrets that already live on cachy.

  token       <- ~/.config/zenilify/papp_token            (0600, the backend bearer)
  wifi0_*     <- ~/esphome/secrets.yaml wifi_ssid/password (home, 2.4 GHz)
  wifi1_*     <- "zenilify-6E" + the same passphrase       (UTR 2.4 GHz SSID, see tab5-banco.yaml)
  pair, rot   <- defaults (es<->zh, 90 deg)

Nothing secret is printed or put in argv: the CSV goes to nvs_partition_gen through stdin and
the binary lands in ~/t5/secrets/nvs.bin (0600). tools/flash.sh writes it at 0x9000.
Output: lengths and a sha256 prefix only.
"""
import csv, hashlib, io, os, re, subprocess, sys

IMG = "espressif/idf:v6.0.3@sha256:01ab23e20744721378d1dd9d0fff4fe15e2edd8bd5cdaf02d24597f4ade05a1e"
HOME = os.path.expanduser("~")
OUTDIR = os.path.join(HOME, "t5", "secrets")

def secrets_yaml(path):
    d = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r'^(\w+):\s*(.*?)\s*$', line)
        if m:
            v = m.group(2)
            if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'": v = v[1:-1]
            d[m.group(1)] = v
    return d

def main():
    token = open(os.path.join(HOME, ".config/zenilify/papp_token")).read().strip()
    s = secrets_yaml(os.path.join(HOME, "esphome/secrets.yaml"))
    nets = [(s["wifi_ssid"], s["wifi_password"]), ("zenilify-6E", s["wifi_password"])]
    rows = [["key", "type", "encoding", "value"], ["papp", "namespace", "", ""], ["token", "data", "string", token]]
    for i, (ssid, pw) in enumerate(nets):
        rows += [[f"wifi{i}_ssid", "data", "string", ssid], [f"wifi{i}_pass", "data", "string", pw]]
    rows += [["pair", "data", "u8", "0"], ["rot", "data", "u8", "1"]]
    buf = io.StringIO(); csv.writer(buf, lineterminator="\n").writerows(rows)
    os.makedirs(OUTDIR, mode=0o700, exist_ok=True); os.chmod(OUTDIR, 0o700)
    cmd = ["docker", "run", "--rm", "-i", "-u", f"{os.getuid()}:{os.getgid()}", "-v", f"{OUTDIR}:/out", IMG,
           "bash", "-c", "cat > /tmp/nvs.csv && python $IDF_PATH/components/nvs_flash/nvs_partition_generator/"
           "nvs_partition_gen.py generate /tmp/nvs.csv /out/nvs.bin 0x6000 >/dev/null && rm -f /tmp/nvs.csv"]
    p = subprocess.run(cmd, input=buf.getvalue().encode(), capture_output=True)
    buf = None; rows = None
    if p.returncode:
        print("nvs_partition_gen failed rc", p.returncode, "(stderr suppressed: may echo values)"); return 1
    out = os.path.join(OUTDIR, "nvs.bin"); os.chmod(out, 0o600)
    b = open(out, "rb").read()
    print(f"nvs.bin {len(b)} B sha256:{hashlib.sha256(b).hexdigest()[:12]} token_len={len(token)} "
          f"wifi0_ssid_len={len(nets[0][0])} wifi_pw_len={len(nets[0][1])} wifi1_ssid=zenilify-6E")
    return 0

if __name__ == "__main__":
    sys.exit(main())
