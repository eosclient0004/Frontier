#!/usr/bin/env python3
"""Serve Frontier Botanical Workbench on 0.0.0.0:8081 (redirecting root directly to /Experimental/BotanicalWorkbench/)."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys


class BotanicalWorkbenchHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/Experimental/BotanicalWorkbench/index.html")
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8081
    repo_root = Path(__file__).resolve().parents[2]
    handler = partial(BotanicalWorkbenchHandler, directory=str(repo_root))
    server = ThreadingHTTPServer(("0.0.0.0", port), handler)
    print(f"Frontier Botanical Workbench serving on http://0.0.0.0:{port}/Experimental/BotanicalWorkbench/index.html", flush=True)
    server.serve_forever()
