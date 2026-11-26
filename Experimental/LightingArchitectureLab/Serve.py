#!/usr/bin/env python3
"""Serve Frontier Unified Lighting Architecture Lab on 0.0.0.0:8081."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys


class LightingLabRequestRouter(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/Experimental/LightingArchitectureLab/")
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8081
    repo_root = Path(__file__).resolve().parents[2]
    requestFactory = partial(LightingLabRequestRouter, directory=str(repo_root))
    server = ThreadingHTTPServer(("0.0.0.0", port), requestFactory)
    print(
        f"Lighting Architecture Lab serving {repo_root} on http://0.0.0.0:{port}/Experimental/LightingArchitectureLab/",
        flush=True,
    )
    server.serve_forever()
