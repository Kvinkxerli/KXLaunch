#!/usr/bin/env python3
"""sLaunch online counter. Stdlib only.

  POST /ping   id=<32 hex>&v=<version>   -> 204   (sent by every menu, opt-out)
  GET  /stats?key=<SLC_KEY>              -> JSON  (owner only)

Only the random install id, the version and the last-seen time are kept.
IP addresses are used for rate limiting in memory and never written to disk.

  SLC_KEY=<secret> SLC_PORT=8787 SLC_HOST=127.0.0.1 SLC_DB=counts.json ./server.py
"""
import hmac, json, os, re, threading, time
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

KEY  = os.environ.get("SLC_KEY", "")
PORT = int(os.environ.get("SLC_PORT", "8787"))
DB   = os.environ.get("SLC_DB", "counts.json")
ONLINE_S   = 15 * 60          # "online" = pinged in the last 15 min (menu pings every 5)
MAX_IDS    = 1_000_000
NEW_PER_IP = 10               # new ids one IP may introduce per hour

ID_RE = re.compile(r"^[0-9a-f]{32}$")
V_RE  = re.compile(r"^[0-9A-Za-z.\-]{1,16}$")

lock = threading.Lock()
seen = {}                     # id -> [last_seen, version, first_seen]
new_by_ip = {}                # ip -> [window_start, count]   (memory only)

def load():
    try:
        with open(DB) as f:
            seen.update(json.load(f))
    except (OSError, ValueError):
        pass

def save():
    with lock:
        data = json.dumps(seen)
    tmp = DB + ".tmp"
    with open(tmp, "w") as f:
        f.write(data)
    os.replace(tmp, DB)

def saver():
    while True:
        time.sleep(60)
        save()

def ping(ip, id_, v, now):
    if not ID_RE.match(id_) or not V_RE.match(v):
        return False
    with lock:
        if id_ in seen:
            seen[id_][0] = now
            seen[id_][1] = v
            return True
        w = new_by_ip.get(ip)
        if not w or now - w[0] > 3600:
            w = new_by_ip[ip] = [now, 0]
        if w[1] >= NEW_PER_IP or len(seen) >= MAX_IDS:
            return False
        w[1] += 1
        seen[id_] = [now, v, now]
        return True

def stats(now):
    with lock:
        rows = list(seen.values())
    online = [r for r in rows if now - r[0] < ONLINE_S]
    return {
        "online":   len(online),
        "today":    sum(now - r[0] < 86400 for r in rows),
        "week":     sum(now - r[0] < 7 * 86400 for r in rows),
        "total":    len(rows),
        "new_today": sum(now - r[2] < 86400 for r in rows),
        "versions": dict(Counter(r[1] for r in online).most_common(5)),
    }

class H(BaseHTTPRequestHandler):
    def ip(self):
        # Behind a local tunnel (Tailscale Funnel, cloudflared) every request
        # comes from loopback; the proxy puts the real client first in
        # X-Forwarded-For. Trusted only from loopback, where it can't be forged.
        peer = self.client_address[0]
        if peer in ("127.0.0.1", "::1"):
            fwd = self.headers.get("X-Forwarded-For", "").split(",")[0].strip()
            return fwd or self.headers.get("CF-Connecting-IP") or peer
        return peer

    def reply(self, code, body=b"", ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if urlparse(self.path).path != "/ping":
            return self.reply(404)
        n = min(int(self.headers.get("Content-Length") or 0), 256)
        q = parse_qs(self.rfile.read(n).decode("ascii", "replace"))
        ok = ping(self.ip(), q.get("id", [""])[0], q.get("v", [""])[0], int(time.time()))
        self.reply(204 if ok else 400)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path != "/stats":
            return self.reply(404)
        key = parse_qs(u.query).get("key", [""])[0]
        if not KEY or not hmac.compare_digest(key, KEY):
            return self.reply(403)
        self.reply(200, json.dumps(stats(int(time.time()))).encode())

    def log_message(self, *a):   # no access log: it would record IPs
        pass

def selftest():
    global seen, new_by_ip
    seen, new_by_ip = {}, {}
    a, b = "a" * 32, "b" * 32
    assert ping("1.1.1.1", a, "v1.5.5", 1000)
    assert ping("1.1.1.1", a, "v1.5.5", 1100)          # repeat ping, same console
    assert not ping("1.1.1.1", "zz", "v1", 1000)        # bad id
    assert not ping("1.1.1.1", b, "v1 5", 1000)         # bad version
    assert ping("2.2.2.2", b, "v1.5.4", 1000)
    s = stats(1200)
    assert s["online"] == 2 and s["total"] == 2, s
    s = stats(1099 + ONLINE_S)                          # b went quiet
    assert s["online"] == 1 and s["today"] == 2, s
    for i in range(NEW_PER_IP):                         # flood guard
        ping("3.3.3.3", "%032x" % (i + 10), "v1", 5000)
    assert not ping("3.3.3.3", "%032x" % 999, "v1", 5000)
    seen, new_by_ip = {}, {}
    print("selftest ok")

if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv:
        selftest(); sys.exit(0)
    if not KEY:
        sys.exit("set SLC_KEY")
    load()
    threading.Thread(target=saver, daemon=True).start()
    try:
        ThreadingHTTPServer((os.environ.get("SLC_HOST", "127.0.0.1"), PORT), H).serve_forever()
    finally:
        save()
