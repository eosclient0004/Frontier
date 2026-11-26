#!/usr/bin/env python3
"""Static preview server for Experimental/LightHierarchyStudio/ (repo-root anchored so shared Fonts resolve cleanly)."""

import http.server
import os
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8082
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
os.chdir(REPO_ROOT)


class LightStudioHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/Experimental/LightHierarchyStudio/")
            self.end_headers()
            return
        return super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    http.server.ThreadingHTTPServer.allow_reuse_address = True
    server = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), LightStudioHandler)
    print(f"[LightHierarchyStudio] Serving {REPO_ROOT} at http://0.0.0.0:{PORT}/Experimental/LightHierarchyStudio/", flush=True)
    server.serve_forever()
