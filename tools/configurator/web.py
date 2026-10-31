"""Small localhost configurator, adapted from Member 5's stdlib server design."""
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import secrets
from tools.config_validator.planner import ROOT, load_document, parse_document
from tools.config_transfer.client import TransferError
from .workflow import Workflow
from .discovery import connection_presets

STATIC = Path(__file__).with_name("static")
ASSETS = {"/": ("index.html", "text/html"), "/app.js": ("app.js", "text/javascript"),
          "/style.css": ("style.css", "text/css"), "/dashboard.js": ("dashboard.js", "text/javascript"),
          "/discovery.js": ("discovery.js", "text/javascript")}
# Explicit assets only: restoring the design must not expose the source tree.
STYLE_FILES = (
    "main.css", "tokens.css", "fonts.css", "base.css",
    "layout/appbar.css", "layout/page.css",
    "components/alert.css", "components/badge.css", "components/button.css",
    "components/card.css", "components/empty.css", "components/error-list.css",
    "components/field.css", "components/icon-badge.css", "components/meter.css",
    "components/pill.css", "components/tile.css",
)
ASSETS.update({"/styles/" + name: ("styles/" + name, "text/css") for name in STYLE_FILES})
ASSETS.update({"/assets/fonts/" + name: ("assets/fonts/" + name, "font/woff2")
               for name in ("roboto-latin.woff2", "roboto-greek.woff2")})

class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(3)

    def local_request(self):
        expected = f"127.0.0.1:{self.server.server_port}"
        return (self.headers.get("Host") == expected and
                self.headers.get("Origin", "http://" + expected) == "http://" + expected)

    def send(self, status, data, kind="application/json"):
        content = json.dumps(data, allow_nan=False).encode() if kind == "application/json" else data
        self.send_response(status)
        self.send_header("Content-Type", kind if kind == "font/woff2" else kind + "; charset=utf-8")
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'none'")
        self.end_headers()
        self.wfile.write(content)

    def do_GET(self):
        if not self.local_request():
            self.send(403, {"error": "Local origin required"})
        elif self.path in ASSETS:
            name, kind = ASSETS[self.path]
            content = (STATIC / name).read_bytes()
            if kind == "text/html":
                content = content.replace(b"{{TOKEN}}", self.server.token.encode())
            self.send(200, content, kind)
        elif self.path == "/api/catalogue":
            self.send(200, {"examples": {p.stem: load_document(p) for p in sorted((ROOT / "configs/nodes").glob("*.json"))},
                            "descriptors": list(self.server.workflow.catalogue.descriptors.values()),
                            "connections": connection_presets()})
        else:
            self.send(404, {"error": "Not found"})

    def do_POST(self):
        authorized = (self.local_request() and
                      secrets.compare_digest(self.headers.get("X-Session", "").encode("utf-8"),
                                             self.server.token.encode("ascii")))
        try:
            # Consume a valid bounded body before closing even when access is
            # denied. Unread TCP data can otherwise reset the socket on Windows
            # before the client receives our rejection. Never parse or dispatch
            # an unauthorized body; the existing socket timeout still applies.
            raw = self.read_body()
            if not authorized:
                self.send(403, {"error": "Local session required"})
                return
            if self.headers.get("Content-Type") != "application/json":
                raise ValueError("Send a bounded JSON request")
            body = parse_document(raw)
            if not self.path.startswith("/api/"):
                raise ValueError("Unknown operation")
            result = self.server.workflow.dispatch(self.path[5:], body)
            self.send(200, result)
        except (ImportError, OSError, ValueError, TransferError, RecursionError) as error:
            if authorized:
                self.send(400, {"error": str(error), "connected": self.server.workflow.client is not None})
            else:
                self.send(403, {"error": "Local session required"})

    def read_body(self):
        lengths = self.headers.get_all("Content-Length", [])
        if (self.headers.get("Transfer-Encoding") or len(lengths) != 1 or
            not lengths[0].isascii() or not lengths[0].isdecimal()):
            raise ValueError("Send one bounded Content-Length")
        length = int(lengths[0])
        if not 0 < length <= 65536:
            raise ValueError("Request must be between 1 and 65536 bytes")
        raw = self.rfile.read(length)
        if len(raw) != length:
            raise ValueError("Incomplete request")
        return raw

    def log_message(self, format, *args):
        pass

class LocalServer(ThreadingHTTPServer):
    # A cold browser requests the existing CSS imports/fonts concurrently.
    # Windows can refuse those connections with HTTPServer's backlog of five.
    request_queue_size = 32


def make_server(port=8080, workflow=None):
    server = LocalServer(("127.0.0.1", port), Handler)
    server.workflow = workflow or Workflow()
    server.token = secrets.token_urlsafe(32)
    return server

def serve(port=8080):
    server = make_server(port)
    print(f"uT-Home: http://127.0.0.1:{server.server_port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        server.workflow.close()
