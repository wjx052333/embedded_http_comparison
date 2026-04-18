#!/usr/bin/env python3
"""Minimal HTTP server for mg_http_client tests. Uses stdlib only."""
import os
import ssl
import sys
import shutil
from http.server import HTTPServer, BaseHTTPRequestHandler

UPLOAD_DIR = "/tmp/mhc_test"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # silence access log

    def do_POST(self):
        if self.path.startswith("/upload/"):
            name = os.path.basename(self.path[8:])
            os.makedirs(UPLOAD_DIR, exist_ok=True)
            dest = os.path.join(UPLOAD_DIR, name)
            length = int(self.headers.get("Content-Length", 0))
            with open(dest, "wb") as f:
                remaining = length
                while remaining > 0:
                    chunk = self.rfile.read(min(65536, remaining))
                    if not chunk:
                        break
                    f.write(chunk)
                    remaining -= len(chunk)
            self.send_response(200)
            self.end_headers()
            self.wfile.write(f"{length} ok\n".encode())

        elif self.path == "/echo":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        else:
            self.send_response(404)
            self.end_headers()

    def do_GET(self):
        if self.path.startswith("/download/"):
            name = os.path.basename(self.path[10:])
            filepath = os.path.join(UPLOAD_DIR, name)
            if os.path.exists(filepath):
                size = os.path.getsize(filepath)
                self.send_response(200)
                self.send_header("Content-Length", str(size))
                self.send_header("Content-Type", "application/octet-stream")
                self.end_headers()
                with open(filepath, "rb") as f:
                    shutil.copyfileobj(f, self.wfile, 65536)
            else:
                self.send_response(404)
                self.end_headers()
        else:
            self.send_response(404)
            self.end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    cert = sys.argv[2] if len(sys.argv) > 2 else None
    key  = sys.argv[3] if len(sys.argv) > 3 else None
    os.makedirs(UPLOAD_DIR, exist_ok=True)
    server = HTTPServer(("127.0.0.1", port), Handler)
    if cert and key:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(cert, key)
        server.socket = ctx.wrap_socket(server.socket, server_side=True)
    print(f"listening on {port}", flush=True)
    server.serve_forever()
