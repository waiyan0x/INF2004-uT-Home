"""One local controller and one reviewed candidate; no automatic mutation retry."""
from dataclasses import asdict
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
import secrets
from threading import Lock
from time import monotonic
from tools.config_validator.planner import Catalogue, ROOT, build_plan, load_document, parse_document, require
from tools.config_transfer.client import Client, TransferError
from tools.config_transfer.protocol import Command
from tools.device_discovery import identify_i2c, identify_uart, identity_result, confirm_identity
from .discovery import connection_preset, recommend, add_to_draft, source_queues

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
    data = reply if isinstance(reply, dict) else asdict(reply)
    return tuple(data[key] for key in ("active_revision", "saved_revision", "active_crc", "saved_crc")) + (data["flags"] & 0x70,)

def reply_data(reply):
    return dict(asdict(reply), message=STATUS[reply.status])

class Workflow:
    def __init__(self, *, opener=open_serial, client_factory=Client, clock=monotonic):
        self.catalogue = Catalogue()
        self.opener, self.client_factory = opener, client_factory
        self.lock = Lock()
        self.stream = self.client = self.caps = self.pending = None
        self.discovery = self.last_timestamp = None
        self.clock = clock

    def close(self):
        stream, self.stream = self.stream, None
        self.client = self.caps = self.pending = None
        self.discovery = self.last_timestamp = None
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
                self.discovery = None
                return self.review(body.get("profile"))
            if action == "datasheet_match":
                return self.datasheet_match(body)
            require(self.client is not None, "Connect to the board first")
            if action == "datasheet_prepare_i2c":
                return self.datasheet_prepare_i2c(body)
            if action == "discover":
                return self.discover(body)
            if action == "add_discovered":
                return self.add_discovered(body)
            if action == 'confirm_discovered':
                return self.confirm_discovered(body)
            if action == "telemetry":
                snapshot = self.observe()
                return dict(asdict(snapshot), discovery_id=self.discovery["id"] if self.discovery else None)
            if action == "query":
                current = self.client.query()
                if self.discovery and identity(current) != self.discovery["base"]:
                    self.discovery = None
                return reply_data(current)
            if action == "apply":
                self.discovery = None
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
                self.pending = self.discovery = None
                current = self.client.query()
                require(action != "recover" or current.flags & 32,
                        "Recovery is only available when the board reports recovery required")
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

    def datasheet_match(self, body):
        """Suggest exact catalogue identities; AI names are not device proof."""
        name = body.get("device_name")
        require(isinstance(name, str) and 0 < len(name) <= 160,
                "Enter a device name from the draft")
        observed = self.client.capabilities() if self.client is not None else None
        matches = []
        for (identifier, version), descriptor in self.catalogue.descriptors.items():
            if identifier.casefold() == name.strip().casefold():
                driver = descriptor["driver_id"]
                matches.append({"descriptor_id": identifier, "descriptor_version": version,
                                "driver_id": driver,
                                "driver_available": None if observed is None else driver in observed.drivers})
        return {"matches": matches, "connected": observed is not None,
                "identity_verified": False,
                "notice": "A name match is a candidate only. Confirm the actual module and wiring."}

    def datasheet_prepare_i2c(self, body):
        """Build a draft only from a reviewed periodic-I2C catalogue entry."""
        self.pending = self.discovery = None
        require(body.get("wiring_confirmed") is True and body.get("identity_confirmed") is True,
                "Confirm the module and physical wiring")
        descriptor_id = body.get("descriptor_id")
        version = body.get("descriptor_version")
        require(isinstance(descriptor_id, str) and isinstance(version, str), "Choose a reviewed device")
        descriptor = self.catalogue.descriptors.get((descriptor_id, version))
        require(descriptor is not None and descriptor["protocol"] == "i2c" and
                descriptor["execution"]["kind"] == "periodic" and
                descriptor["defaults"]["behaviour"].get("profile") == "periodic_sensor",
                "No reviewed periodic I2C driver for this device")
        current_caps = self.client.capabilities()
        require(asdict(current_caps) | {"request_id": 0} == asdict(self.caps) | {"request_id": 0},
                "Firmware capabilities changed; reconnect")
        require(descriptor["driver_id"] in current_caps.drivers, "Connected firmware has no compatible driver")
        before = self.client.query()
        document, _, _ = self.validate(body.get("profile"))
        name = body.get("device_id")
        address = body.get("address")
        sda, scl = body.get("sda_gpio"), body.get("scl_gpio")
        require(isinstance(name, str) and 1 <= len(name) <= 22 and
                isinstance(address, str) and type(sda) is int and type(scl) is int and
                0 <= sda <= 21 and 0 <= scl <= 21 and sda != scl,
                "Choose a device name, address and distinct GPIO pins")
        require(all(device["device_id"] != name for device in document["devices"]),
                "This device name is already in the profile")
        interface_schema = descriptor["instance_schema"]["properties"]["interface"]
        needs_enable = "enable_gpio" in interface_schema.get("required", [])
        enable = body.get("enable_gpio")
        if needs_enable:
            require(type(enable) is int and 0 <= enable <= 21 and enable not in (sda, scl),
                    "Choose a distinct enable GPIO")
        else:
            require(enable is None, "This reviewed device does not use an enable GPIO")
        buses = [bus for bus in document["interfaces"] if bus["protocol"] == "i2c" and bus["port"] == 0]
        if buses:
            bus = buses[0]
            require((bus["sda_gpio"], bus["scl_gpio"]) == (sda, scl),
                    "The existing I2C controller uses different pins")
        else:
            bus = dict(id="i2c0", protocol="i2c", port=0, sda_gpio=sda, scl_gpio=scl, clock_hz=100000)
            document["interfaces"].append(bus)
        defaults = descriptor["defaults"]
        behaviour = deepcopy(defaults["behaviour"])
        behaviour["destination"] = "readings." + name
        connection = dict(ref=bus["id"], address=address)
        if needs_enable:
            connection["enable_gpio"] = enable
        document["devices"].append(dict(device_id=name, driver=descriptor["driver_id"],
            descriptor=dict(id=descriptor_id, version=version), enabled=True,
            interface=connection, behaviour=behaviour,
            task=dict(name="read_" + name, priority=defaults["priority"],
                      stack_size_bytes=defaults["stack_size_bytes"])))
        document["queues"].append(dict(id=behaviour["destination"],
            item_type=descriptor["routing"]["output_type"], depth=document["runtime"]["queue_depth"],
            overflow_policy="drop_oldest"))
        self.validate(json.dumps(document))
        require(identity(self.client.query()) == identity(before), "Board state changed; prepare again")
        return {"profile": document, "identity_verified": False,
                "notice": "Draft only. Validate and review before applying."}

    def validate(self, text):
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

        return document, raw, plan

    def review(self, text):
        self.pending = None
        document, raw, plan = self.validate(text)
        base = self.client.query() if self.client else None
        token = secrets.token_urlsafe(24)
        self.pending = (token, raw, base, self.caps)
        return {"review_id": token, "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw),
                "profile": document, "plan": plan, "connected": self.client is not None,
                "hardware_qualified": bool(self.caps and self.caps.hardware_qualified),
                "state": reply_data(base) if base else None}

    def observe(self):
        snapshot = self.client.telemetry()
        restarted = not snapshot.result and self.last_timestamp is not None and snapshot.timestamp_ms < self.last_timestamp
        if restarted:
            self.pending = None
        if self.discovery and (snapshot.result or restarted or
                self.clock() >= self.discovery["expires"] or
                identity(snapshot.configuration) != self.discovery["base"]):
            self.discovery = None
        if not snapshot.result:
            self.last_timestamp = snapshot.timestamp_ms
        return snapshot

    def discover(self, body):
        self.pending = self.discovery = None
        preset = connection_preset(body.get('preset_id'))
        require(body.get("wiring_confirmed") is True, "Confirm the connection wiring first")
        text = body.get("profile")
        require(isinstance(text, str) and len(text.encode("utf-8")) <= 32768, "Draft must be bounded JSON text")
        base = self.client.query()
        capture = None
        if preset['method'] == 'manual':
            caps = self.client.capabilities()
            report = self.manual_report(preset, text, base)
        else:
            feature = 1 << preset['wire_code']
            require(self.caps.features & feature, 'This firmware does not support discovery for this connection; update it before detecting')
            capture, caps = (self.client.discover_i2c if preset['wire_code'] == 1 else self.client.discover_uart)(wiring_confirmed=True)
            report = (identify_i2c(capture, caps, catalogue=self.catalogue) if preset['wire_code'] == 1 else
                      identify_uart(capture, caps, preset, catalogue=self.catalogue))
        require(asdict(caps) | {"request_id": 0} == asdict(self.caps) | {"request_id": 0},
                "Firmware capabilities changed; reconnect and detect again")
        report = recommend(report, self.catalogue, preset)
        current = self.client.query()
        require(identity(base) == identity(current) and (capture is None or capture.active_revision == current.active_revision),
                "Board state changed; detect again")
        report.update(discovery_id=None, expires_in_ms=60000, state=reply_data(current))
        if capture is None or capture.result == "complete":
            token = secrets.token_urlsafe(24)
            self.discovery = dict(id=token, report=report, preset=preset, profile=text,
                                  base=identity(current), expires=self.clock() + 60)
            if capture:
                self.last_timestamp = capture.started_ms + capture.elapsed_ms
            report["discovery_id"] = token
        return report

    def manual_report(self, preset, text, base):
        snapshot = self.observe()
        require(not snapshot.result and identity(snapshot.configuration) == identity(base), 'Board state unavailable; reconnect and prepare again')
        document, _, _ = self.validate(text)
        policy = load_document(ROOT / 'catalogue/discovery.json')
        ref = preset['descriptor']
        entry = next(row for row in policy['entries'] if row['descriptor'] == ref)
        require(entry['identification']['method'] == 'manual', 'Connection is not a manual selection')
        descriptor = self.catalogue.descriptors[(ref['id'], ref['version'])]
        device = identity_result(dict(method='manual', status='not_observed', protocol=descriptor['protocol']), [ref], 'unknown', self.caps)
        device['confirmation_text'] = entry['identification']['confirmation']
        device['cautions'] = entry['cautions']
        if descriptor['execution']['kind'] == 'queue_consumer':
            device['source_queues'] = source_queues(document, descriptor)
        return dict(contract_version='0.1.0', catalogue_version=policy['catalogue_version'],
                    board=deepcopy(preset['board']), preset_id=preset['preset_id'], preset_version=preset['preset_version'],
                    capture=dict(result='manual', active_revision=base.active_revision), devices=[device])

    def checked_discovery(self, body):
        self.pending = None
        self.observe()
        pending = self.discovery
        require(pending is not None and body.get('discovery_id') == pending['id'], 'Detection expired or changed; detect again')
        require(body.get('profile') == pending['profile'], 'Draft changed; detect again before adding')
        require(body.get('preset_id') == pending['preset']['preset_id'] and body.get('wiring_confirmed') is True,
                'Connection confirmation changed; detect again')
        caps = self.client.capabilities()
        require(asdict(caps) | {'request_id': 0} == asdict(self.caps) | {'request_id': 0},
                'Firmware capabilities changed; reconnect and detect again')
        require(identity(self.client.query()) == pending['base'], 'Board state changed; detect again')
        index = body.get('index')
        require(type(index) is int and 0 <= index < len(pending['report']['devices']), 'Choose a detected result')
        return pending, index

    def confirm_discovered(self, body):
        pending, index = self.checked_discovery(body)
        require(body.get('identity_confirmed') is True, 'Confirm the module and wiring explicitly')
        report = deepcopy(pending['report'])
        device = report['devices'][index]
        confirm_identity(device, self.caps, self.catalogue)
        document, _, _ = self.validate(body['profile'])
        recommend(report, self.catalogue, pending['preset'], document=document, source_queue=body.get('source_queue'))
        device['identity']['confirmation'] = dict(source='user', at_utc=datetime.now(timezone.utc).isoformat(),
                                                preset_version=pending['preset']['preset_version'])
        # Keep the original expiry; confirmation cannot renew old evidence.
        report['expires_in_ms'] = max(0, int((pending['expires'] - self.clock()) * 1000))
        require(report['expires_in_ms'] > 0, 'Detection expired; detect again')
        report['discovery_id'] = secrets.token_urlsafe(24)
        pending.update(id=report['discovery_id'], report=report)
        return report

    def add_discovered(self, body):
        try:
            pending, index = self.checked_discovery(body)
        finally:
            self.discovery = None  # Single-use even when an attempted add fails.
        document, _, _ = self.validate(body["profile"])
        draft = add_to_draft(document, pending["report"]["devices"][index], pending["preset"])
        self.validate(json.dumps(draft))
        return {"profile": draft}  # No review token and no APPLY/SAVE/RECOVER command.
