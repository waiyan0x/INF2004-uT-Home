"""One local controller and one reviewed candidate; no automatic mutation retry."""
from dataclasses import asdict
import hashlib
import json
import secrets
from threading import Lock
from tools.config_validator.planner import Catalogue, ROOT, build_plan, load_document, parse_document, require
from tools.config_transfer.client import Client, TransferError
from tools.config_transfer.protocol import Command

STATUS = ("Active", "Active and saved", "Active; saving failed", "Rejected", "Recovery required",
          "Stale revision", "Request conflict", "Storage error", "Empty")

def open_serial(port):
    require(isinstance(port, str) and 0 < len(port) <= 128, "Choose an explicit serial port")
    import serial
    stream = serial.Serial(port=None, baudrate=115200, timeout=0.05,
                           write_timeout=0.2, rtscts=False, dsrdtr=False)
    try:
        stream.dtr = False
        stream.rts = False
        stream.port = port
        stream.open()
        return stream
    except Exception:
        stream.close()
        raise

def identity(reply):
    return (reply.active_revision, reply.saved_revision, reply.active_crc,
            reply.saved_crc, reply.flags & 0x70)

def reply_data(reply):
    return dict(asdict(reply), message=STATUS[reply.status])

class Workflow:
    def __init__(self, *, opener=open_serial, client_factory=Client):
        self.catalogue = Catalogue()
        self.opener, self.client_factory = opener, client_factory
        self.lock = Lock()
        self.stream = self.client = self.caps = self.pending = None

    def close(self):
        stream, self.stream = self.stream, None
        self.client = self.caps = self.pending = None
        if stream is not None:
            stream.close()

    def dispatch(self, action, body):
        require(self.lock.acquire(blocking=False), "Another request is in progress")
        try:
            if action == "disconnect":
                self.close()
                return {"connected": False}
            if action == "connect":
                require(self.client is None, "Disconnect the current port first")
                self.stream = self.opener(body.get("port"))
                self.client = self.client_factory(self.stream)
                self.caps = self.client.capabilities()
                require(self.caps.board == "esp32-c3-supermini" and self.caps.node_schema in ("2.0.0", "2.1.0"),
                        "Unsupported connected firmware")
                self.pending = None
                return {"connected": True, "capabilities": asdict(self.caps),
                        "state": reply_data(self.client.query())}
            if action == "review":
                return self.review(body.get("profile"))
            require(self.client is not None, "Connect to the board first")
            if action == "telemetry":
                return asdict(self.client.telemetry())
            if action == "query":
                return reply_data(self.client.query())
            if action == "apply":
                pending, self.pending = self.pending, None
                require(pending is not None and body.get("review_id") == pending[0],
                        "Review the current profile again before applying")
                _, raw, base, caps = pending
                require(base is not None, "Connect and review against the board before applying")
                current = self.client.query()
                require(identity(current) == identity(base), "Board state changed; review again")
                observed = self.client.capabilities()
                require(asdict(observed) | {"request_id": 0} == asdict(caps) | {"request_id": 0},
                        "Firmware capabilities changed; reconnect and review")
                revision = max(current.active_revision, current.saved_revision) + 1
                require(revision <= 0xFFFFFFFF and current.request_id < 0xFFFFFFFF,
                        "Revision or request sequence exhausted")
                return reply_data(self.client.exchange(Command.APPLY, current.request_id + 1, revision, raw))
            if action in ("save", "recover"):
                self.pending = None
                current = self.client.query()
                require(current.request_id < 0xFFFFFFFF, "Request sequence exhausted")
                revision = current.active_revision if action == "save" else 0
                require(action != "save" or revision > 0, "There is no active configuration to save")
                return reply_data(self.client.exchange(Command[action.upper()], current.request_id + 1, revision))
            raise ValueError("Unknown operation")
        except (TransferError, OSError):
            self.close()
            raise
        except Exception:
            if action == "connect":
                self.close()
            raise
        finally:
            self.lock.release()

    def review(self, text):
        self.pending = None
        require(isinstance(text, str), "Profile must be JSON text")
        document = parse_document(text.encode("utf-8"), 32768)
        # The target admits exact integer tokens; avoid silently rounding or
        # changing a user's decimals before review.
        def integers(value):
            require(not isinstance(value, float), "Configuration numbers must be integers")
            if isinstance(value, dict):
                for item in value.values(): integers(item)
            elif isinstance(value, list):
                for item in value: integers(item)
        integers(document)
        raw = json.dumps(document, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        manifest = load_document(ROOT / "configs/builds/host-reference.json")
        if self.caps is not None:
            manifest.update(backend="target", build_id="connected-compiled-bindings",
                            drivers=list(self.caps.drivers), board=self.caps.board)
            require(len(raw) <= self.caps.max_profile_bytes, "Profile exceeds connected firmware capacity")
            require(document.get("schema_version") in ("2.0.0", self.caps.node_schema),
                    "Connected firmware does not support this profile schema")
            if document.get("rules"):
                require(self.caps.features & 1, "Connected firmware does not support rules")
        require(len(raw) <= 8192, "Compact profile exceeds 8192 bytes")
        plan = build_plan(document, manifest, self.catalogue)
        if self.caps is not None:
            require(len(document["devices"]) <= self.caps.max_devices and
                    plan["resources"]["active_tasks"] <= self.caps.max_workers, "Connected capacity exceeded")
            # Keep transport/backend restrictions separate from device identity.
            for fields in document["interfaces"] + [d["interface"] for d in document["devices"]]:
                uart = fields.get("protocol") == "uart"
                for key, pin in fields.items():
                    if key.endswith("_gpio"):
                        require(pin in (20, 21) if uart else bool(self.caps.allowed_gpio_mask & (1 << pin)),
                                f"GPIO {pin} is unavailable in the connected backend")

        base = self.client.query() if self.client else None
        token = secrets.token_urlsafe(24)
        self.pending = (token, raw, base, self.caps)
        return {"review_id": token, "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw),
                "profile": document, "plan": plan, "connected": self.client is not None,
                "hardware_qualified": bool(self.caps and self.caps.hardware_qualified),
                "state": reply_data(base) if base else None}
