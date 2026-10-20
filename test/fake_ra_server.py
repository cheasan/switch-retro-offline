#!/usr/bin/env python3
"""
fake_ra_server.py - stand-in for retroachievements.org on the laptop.

Records every POST body to $FAKE_LOG (one per line) and replies like the
real dorequest.php. With FAKE_FAIL_FIRST=N set, the first N requests get
{"Success":false} to exercise the retry-keeps-queue path. With
FAKE_ALREADY=1, every request gets the real server's "User already has"
rejection, which the module must count as synced.
"""
import http.server
import json
import os

LOG = os.environ.get("FAKE_LOG", "/tmp/opencode/fake_ra.log")
FAIL_FIRST = int(os.environ.get("FAKE_FAIL_FIRST", "0"))
ALREADY = os.environ.get("FAKE_ALREADY") == "1"
count = 0


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        global count
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n).decode("utf-8", "replace")
        with open(LOG, "a") as f:
            f.write(body + "\n")
        count += 1
        ok = count > FAIL_FIRST and not ALREADY
        reply = {"Success": ok, "Score": 1234, "AchievementsRemaining": 0}
        if ALREADY:
            reply["Error"] = ("User already has hardcore and regular "
                              "achievements awarded.")
        resp = json.dumps(reply).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(resp)))
        self.end_headers()
        self.wfile.write(resp)

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    port = int(os.environ.get("FAKE_PORT", "9090"))
    http.server.HTTPServer(("127.0.0.1", port), Handler).serve_forever()
