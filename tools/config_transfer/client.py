"""One outstanding USB request, bounded framing and no automatic deployment retry."""
import secrets
import time

from .protocol import Command, HEADER, decode_reply, encode_request
from .telemetry import decode_telemetry
from .capabilities import decode_capabilities
from .discovery import decode_discovery, discovery_request
from .workcost import decode_workcost


class TransferError(RuntimeError):
    """The operation may have completed; reconnect and query before deciding."""


class Client:
    """Exclusive borrowed serial stream. Caller configures finite read/write waits.

    No threads, reset, input purge, automatic retry, or retained port ownership.
    Use a new client after disconnect. Sequence discovery never activates/saves.
    """
    def __init__(self, stream, *, clock=time.monotonic, timeout=10.0):
        if not 0 < timeout <= 60:
            raise ValueError("timeout must be in (0, 60] seconds")
        self.stream = stream
        self.clock = clock
        self.timeout = timeout
        self.discovery_session = secrets.randbits(64) or 1

    def exchange(self, command, request_id, revision=0, profile=b""):
        frame = encode_request(command, request_id, revision, profile)
        deadline = self.clock() + self.timeout
        sent = 0
        # Every partial write advances only by the reported count. A stream
        # exception is ambiguous, so never automatically resend a mutation.
        while sent < len(frame):
            if self.clock() >= deadline:
                raise TransferError("send timed out; outcome unknown, reconnect and query")
            try:
                count = self.stream.write(frame[sent:sent + 64])
            except OSError as error:
                raise TransferError("send failed; outcome unknown, reconnect and query") from error
            if type(count) is not int or not 0 < count <= min(64, len(frame) - sent):
                raise TransferError("invalid/zero write count; outcome unknown, reconnect and query")
            sent += count
        pending = bytearray()
        while self.clock() < deadline:
            try:
                chunk = self.stream.read(64)
            except OSError as error:
                raise TransferError("receive failed; outcome unknown, reconnect and query") from error
            if not isinstance(chunk, bytes) or len(chunk) > 64:
                raise TransferError("invalid bounded read result")
            pending.extend(chunk)
            # Bound retained bytes by the validated header, then decode CRC and
            # payload. Never allocate from an unchecked wire length.
            while len(pending) >= HEADER.size:
                magic, version, received, reserved, rid, rev, size, _ = HEADER.unpack(pending[:24])
                telemetry = command == Command.TELEMETRY
                capabilities = command == Command.CAPABILITIES
                discovery = command == Command.DISCOVER
                workcost = command == Command.WORKCOST
                valid_size = ((160 <= size <= 2016 and size % 32 == 0) if telemetry else
                              (96 <= size <= 352 and size % 32 == 0) if capabilities else
                              (80 <= size <= 208 and size % 16 == 0) if discovery else
                              size in (96, 384) if workcost else size == 24)
                if ((magic, version, received, reserved, rid, rev) !=
                    (b"UTH1", 1, int(command) | 0x80, 0, request_id, revision) or not valid_size):
                    del pending[0]
                    continue
                length = HEADER.size + size
                if len(pending) < length:
                    break
                try:
                    if telemetry:
                        reply = decode_telemetry(bytes(pending[:length]), request_id=request_id)
                    elif capabilities:
                        reply = decode_capabilities(bytes(pending[:length]), request_id=request_id)
                    elif discovery:
                        reply = decode_discovery(bytes(pending[:length]), request_id=request_id,
                                                 revision=revision, payload=profile)
                    elif workcost:
                        reply = decode_workcost(bytes(pending[:length]), request_id=request_id)
                    else:
                        reply = decode_reply(bytes(pending[:length]), command=command,
                                             request_id=request_id, revision=revision)
                except ValueError:
                    del pending[0]
                else:
                    return reply
        raise TransferError("reply timed out; outcome unknown, reconnect and query")

    def telemetry(self):
        """Read observations without consuming IDs or evicting a mutation retry."""
        return self.exchange(Command.TELEMETRY, secrets.randbelow(0xFFFFFFFF) + 1)

    def workcost(self):
        """Read bounded costs and stack observations without changing a profile."""
        return self.exchange(Command.WORKCOST, secrets.randbelow(0xFFFFFFFF) + 1)

    def capabilities(self):
        """Discover compiled bindings without changing the mutation sequence."""
        return self.exchange(Command.CAPABILITIES, secrets.randbelow(0xFFFFFFFF) + 1)

    def query(self):
        """Discover sequence state, then refresh durable and active state."""
        probe = self.exchange(Command.PROBE, secrets.randbelow(0xFFFFFFFF) + 1)
        if probe.last_request_id == 0xFFFFFFFF:
            raise TransferError("request IDs exhausted; PROBE remains available until reboot")
        reply = self.exchange(Command.QUERY, probe.last_request_id + 1)
        if reply.status in (5, 6):
            raise TransferError("another controller changed request IDs; use one exclusive client")
        return reply

    def discover_i2c(self, *, wiring_confirmed=False):
        """Observe one reviewed idle connection; never stop or reconfigure it."""
        return self.discover(1, wiring_confirmed=wiring_confirmed)

    def discover_uart(self, *, wiring_confirmed=False):
        return self.discover(2, wiring_confirmed=wiring_confirmed)

    def discover(self, preset_code, *, wiring_confirmed=False):
        payload = discovery_request(self.discovery_session, wiring_confirmed=wiring_confirmed, preset_code=preset_code)
        capabilities = self.capabilities()
        if capabilities.board != "esp32-c3-supermini" or not capabilities.features & (1 << preset_code):
            raise TransferError("connected firmware does not support this discovery preset")
        current = self.query()
        if current.flags & 32:
            raise TransferError("recovery is required before discovery; no probe sent")
        return self.exchange(Command.DISCOVER, secrets.randbelow(0xFFFFFFFF) + 1,
                             current.active_revision, payload), capabilities

