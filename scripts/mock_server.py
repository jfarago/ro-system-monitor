#!/usr/bin/env python3
"""Serve ui/ with a mock device API for developing the dashboard without hardware.

    python3 scripts/mock_server.py   # http://localhost:8080
"""
import json
import os
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

UI = os.path.join(os.path.dirname(__file__), "..", "ui")
START = time.time()
state = {
    "volume": {"today": [4.9, 10.9], "week": [33.5, 76.5], "month": [141.2, 318.9], "all": [1271.3, 2915.7]},
    "filters": {
        "prefilter": {"installDate": "2026-06-02", "ratingGal": 1500, "usedGal": 912, "counts": "total"},
        "membrane": {"installDate": "2026-01-15", "ratingGal": 3000, "usedGal": 1046, "counts": "total"},
    },
}


def status():
    v = {k: {"clean": c, "waste": w} for k, (c, w) in state["volume"].items()}
    return {
        "time": time.strftime("%Y-%m-%dT%H:%M:%S-04:00"), "timeValid": True,
        "tds": {"product": 12, "feed": 180, "rejection": 93.3},
        "volume": v, "countingSince": "2026-01-15", "filters": state["filters"],
        "flowing": False, "uptime": int(time.time() - START), "rssi": -61,
    }


class H(BaseHTTPRequestHandler):
    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/status":
            return self._json(200, status())
        name = "index.html" if self.path in ("/", "") else self.path.lstrip("/")
        path = os.path.normpath(os.path.join(UI, name))
        if not path.startswith(os.path.normpath(UI)) or not os.path.isfile(path):
            return self._json(404, {"error": "not found"})
        ctype = {"html": "text/html", "css": "text/css", "js": "text/javascript"}.get(name.rsplit(".", 1)[-1], "text/plain")
        data = open(path, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(n) or b"{}")
        if self.path == "/api/reset-counters":
            state["volume"] = {k: [0, 0] for k in state["volume"]}
        elif self.path == "/api/settings":
            for key, new in body.items():
                cur = state["filters"][key]
                if new.get("installDate") and new["installDate"] != cur["installDate"]:
                    cur["installDate"], cur["usedGal"] = new["installDate"], 0
                if new.get("ratingGal"):
                    cur["ratingGal"] = new["ratingGal"]
        else:
            return self._json(404, {"error": "not found"})
        self._json(200, {"ok": True})


HTTPServer(("", 8080), H).serve_forever()
