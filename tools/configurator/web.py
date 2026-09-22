"""Small localhost-only review API and web application."""

from __future__ import annotations

import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit

from .monitoring import MonitoringError, validate_snapshot
from .validator import ROOT, validate_profile
from .workflow import WorkflowError, new_candidate, review_candidate


STATIC = Path(__file__).with_name("static")

# Only these extensions are ever served, and only from inside STATIC.
CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".svg": "image/svg+xml",
    ".woff2": "font/woff2",
}


def resolve_static(request_path: str) -> tuple[Path, str] | None:
    """Map a URL path to a file inside STATIC, or None if it is not servable."""
    relative = urlsplit(request_path).path.lstrip("/") or "index.html"
    if relative.endswith("/"):
        relative += "index.html"

    root = STATIC.resolve()
    try:
        candidate = (root / relative).resolve()
    except OSError:
        return None

    if not candidate.is_relative_to(root) or not candidate.is_file():
        return None

    content_type = CONTENT_TYPES.get(candidate.suffix)
    return (candidate, content_type) if content_type else None


class ReviewHandler(BaseHTTPRequestHandler):
    server_version = "uTHomeConfigurator/1.0"

    def _send(self, status: int, content: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(content)

    def _json(self, status: int, value: object) -> None:
        self._send(status, json.dumps(value, indent=2).encode(), "application/json; charset=utf-8")

    def do_GET(self) -> None:
        if self.path == "/api/sample/profile":
            self._json(200, json.loads((ROOT / "configs/nodes/first-prototype.json").read_text()))
        elif self.path == "/api/sample/health":
            self._json(200, json.loads((ROOT / "configs/health/first-prototype.json").read_text()))
        elif (resolved := resolve_static(self.path)) is not None:
            path, content_type = resolved
            self._send(200, path.read_bytes(), content_type)
        else:
            self._json(404, {"error": "Not found."})

    def do_POST(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 1_000_000:
                raise ValueError("Request body must be between 1 and 1000000 bytes.")
            body: Any = json.loads(self.rfile.read(length))
            if self.path == "/api/validate":
                self._json(200, validate_profile(body).to_dict())
            elif self.path == "/api/candidate":
                result = validate_profile(body)
                if not result.valid:
                    self._json(422, result.to_dict())
                else:
                    self._json(200, new_candidate(body, "web"))
            elif self.path in {"/api/approve", "/api/reject"}:
                decision = "approved" if self.path.endswith("approve") else "rejected"
                reviewed = review_candidate(body["candidate"], decision, body.get("reviewer", ""), body.get("note", ""))
                self._json(200, reviewed)
            elif self.path == "/api/health":
                self._json(200, validate_snapshot(body))
            else:
                self._json(404, {"error": "Not found."})
        except (json.JSONDecodeError, KeyError, TypeError, ValueError, WorkflowError, MonitoringError) as error:
            self._json(400, {"error": str(error)})

    def log_message(self, format: str, *args: object) -> None:
        return


def make_server(host: str = "127.0.0.1", port: int = 8080) -> ThreadingHTTPServer:
    """Build a localhost review server; caller owns its lifecycle."""
    return ThreadingHTTPServer((host, port), ReviewHandler)


def serve(host: str = "127.0.0.1", port: int = 8080) -> None:
    """Run the review UI until interrupted."""
    if host not in {"127.0.0.1", "localhost", "::1"}:
        raise ValueError("The review UI may only bind to localhost.")
    server = make_server(host, port)
    print(f"μT-Home configurator: http://{host}:{server.server_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
