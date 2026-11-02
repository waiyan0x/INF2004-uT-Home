"""One outstanding USB request, bounded framing and no automatic deployment retry."""
import secrets
import time
from collections.abc import Callable
from typing import Literal, Protocol, overload

from .protocol import Command, HEADER, ProbeReply, Reply, decode_reply, encode_request
from .telemetry import Snapshot, decode_telemetry
from .capabilities import Capabilities, decode_capabilities
from .discovery import Discovery, decode_discovery, discovery_request
from .workcost import WorkcostReply, decode_workcost


Response = Reply | ProbeReply | Snapshot | Capabilities | Discovery | WorkcostReply


class SerialStream(Protocol):
    """Borrowed byte stream with finite waits configured by its owner."""

    def read(self, size: int, /) -> bytes: ...

    def write(self, data: bytes, /) -> int: ...


class TransferError(RuntimeError):
    """The operation may have completed; reconnect and query before deciding."""


def _valid_payload_size(command: Command, size: int) -> bool:
    """Bound frame retention before interpreting a command-specific payload."""
    if command == Command.TELEMETRY:
        return 160 <= size <= 2016 and size % 32 == 0
    if command == Command.CAPABILITIES:
        return 96 <= size <= 352 and size % 32 == 0
    if command == Command.DISCOVER:
        return 80 <= size <= 208 and size % 16 == 0
    if command == Command.WORKCOST:
        return size in (96, 384)
    return size == 24


class Client:
    """Exclusive borrowed serial stream. Caller configures finite read/write waits.

    No threads, reset, input purge, automatic retry, or retained port ownership.
    Use a new client after disconnect. Sequence discovery never activates/saves.
    """
    def __init__(self, stream: SerialStream, *, clock: Callable[[], float] = time.monotonic,
                 timeout: float = 10.0) -> None:
        if not 0 < timeout <= 60:
            raise ValueError("timeout must be in (0, 60] seconds")
        self.stream = stream
        self.clock = clock
        self.timeout = timeout
        self.discovery_session = secrets.randbits(64) or 1

    @overload
    def exchange(self, command: Literal[Command.PROBE], request_id: int,
                 revision: int = 0, profile: bytes = b"") -> ProbeReply: ...

    @overload
    def exchange(self, command: Literal[Command.APPLY, Command.QUERY, Command.SAVE, Command.RECOVER],
                 request_id: int, revision: int = 0, profile: bytes = b"") -> Reply: ...

    @overload
    def exchange(self, command: Literal[Command.TELEMETRY], request_id: int,
                 revision: int = 0, profile: bytes = b"") -> Snapshot: ...

    @overload
    def exchange(self, command: Literal[Command.CAPABILITIES], request_id: int,
                 revision: int = 0, profile: bytes = b"") -> Capabilities: ...

    @overload
    def exchange(self, command: Literal[Command.DISCOVER], request_id: int,
                 revision: int = 0, profile: bytes = b"") -> Discovery: ...

    @overload
    def exchange(self, command: Literal[Command.WORKCOST], request_id: int,
                 revision: int = 0, profile: bytes = b"") -> WorkcostReply: ...

    @overload
    def exchange(self, command: Command, request_id: int, revision: int = 0,
                 profile: bytes = b"") -> Response: ...

    def exchange(self, command: Command, request_id: int, revision: int = 0,
                 profile: bytes = b"") -> Response:
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
        last_rejection = None
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
                if ((magic, version, received, reserved, rid, rev) !=
                    (b"UTH1", 1, int(command) | 0x80, 0, request_id, revision) or
                    not _valid_payload_size(command, size)):
                    del pending[0]
                    continue
                length = HEADER.size + size
                if len(pending) < length:
                    break
                reply: Response
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
                except ValueError as error:
                    last_rejection = error
                    del pending[0]
                else:
                    return reply
        if last_rejection is not None:
            raise TransferError(
                "matching reply rejected; outcome unknown, reconnect and query"
            ) from last_rejection
        raise TransferError("reply timed out; outcome unknown, reconnect and query")

    def telemetry(self) -> Snapshot:
        """Read observations without consuming IDs or evicting a mutation retry."""
        return self.exchange(Command.TELEMETRY, secrets.randbelow(0xFFFFFFFF) + 1)

    def workcost(self) -> WorkcostReply:
        """Read bounded costs and stack observations without changing a profile."""
        return self.exchange(Command.WORKCOST, secrets.randbelow(0xFFFFFFFF) + 1)

    def capabilities(self) -> Capabilities:
        """Discover compiled bindings without changing the mutation sequence."""
        return self.exchange(Command.CAPABILITIES, secrets.randbelow(0xFFFFFFFF) + 1)

    def query(self) -> Reply:
        """Discover sequence state, then refresh durable and active state."""
        probe = self.exchange(Command.PROBE, secrets.randbelow(0xFFFFFFFF) + 1)
        if probe.last_request_id == 0xFFFFFFFF:
            raise TransferError("request IDs exhausted; PROBE remains available until reboot")
        reply = self.exchange(Command.QUERY, probe.last_request_id + 1)
        if reply.status in (5, 6):
            raise TransferError("another controller changed request IDs; use one exclusive client")
        return reply

    def discover_i2c(self, *, wiring_confirmed: bool = False) -> tuple[Discovery, Capabilities]:
        """Observe one reviewed idle connection; never stop or reconfigure it."""
        return self.discover(1, wiring_confirmed=wiring_confirmed)

    def discover_uart(self, *, wiring_confirmed: bool = False) -> tuple[Discovery, Capabilities]:
        return self.discover(2, wiring_confirmed=wiring_confirmed)

    def discover(self, preset_code: int, *, wiring_confirmed: bool = False) -> tuple[Discovery, Capabilities]:
        payload = discovery_request(self.discovery_session, wiring_confirmed=wiring_confirmed, preset_code=preset_code)
        capabilities = self.capabilities()
        if capabilities.board != "esp32-c3-supermini" or not capabilities.features & (1 << preset_code):
            raise TransferError("connected firmware does not support this discovery preset")
        current = self.query()
        if current.flags & 32:
            raise TransferError("recovery is required before discovery; no probe sent")
        return self.exchange(Command.DISCOVER, secrets.randbelow(0xFFFFFFFF) + 1,
                             current.active_revision, payload), capabilities

