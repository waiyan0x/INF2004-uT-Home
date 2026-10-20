"""Version 1 binary framing, independent of native C structure layout."""
from dataclasses import dataclass
from enum import IntEnum
import struct
import zlib

MAX_PROFILE_BYTES = 8192
HEADER = struct.Struct("<4sBBHIIII")


class Command(IntEnum):
    APPLY = 1
    QUERY = 2
    SAVE = 3
    RECOVER = 4
    PROBE = 5
    TELEMETRY = 6
    CAPABILITIES = 7
    DISCOVER = 8


@dataclass(frozen=True)
class Reply:
    command: Command
    request_id: int
    revision: int
    status: int
    active_revision: int
    saved_revision: int
    active_crc: int
    saved_crc: int
    flags: int


@dataclass(frozen=True)
class ProbeReply:
    command: Command
    request_id: int
    revision: int
    last_request_id: int
    active_revision: int
    saved_revision: int
    active_crc: int
    saved_crc: int
    flags: int


def encode_request(command: Command, request_id: int, revision: int = 0,
                   profile: bytes = b"") -> bytes:
    """Encode an already reviewed profile; this does not validate or send it."""
    command = Command(command)
    if type(request_id) is not int or not 1 <= request_id <= 0xFFFFFFFF:
        raise ValueError("request_id must be a nonzero uint32")
    if type(revision) is not int or not 0 <= revision <= 0xFFFFFFFF:
        raise ValueError("revision must be a uint32")
    if not isinstance(profile, bytes) or len(profile) > MAX_PROFILE_BYTES:
        raise ValueError("profile must be at most 8192 bytes")
    if command == Command.APPLY:
        if not profile or not revision:
            raise ValueError("APPLY requires profile bytes and a revision")
    elif command == Command.DISCOVER:
        if len(profile) != 24:
            raise ValueError("DISCOVER requires its 24-byte versioned request")
    elif profile or (not revision if command == Command.SAVE else revision != 0):
        raise ValueError("Unexpected payload or revision for this command")
    prefix = HEADER.pack(b"UTH1", 1, command, 0, request_id, revision, len(profile), 0)[:20]
    checksum = zlib.crc32(profile, zlib.crc32(prefix))
    return prefix + struct.pack("<I", checksum) + profile


def decode_reply(frame: bytes, *, command: Command, request_id: int,
                 revision: int) -> Reply | ProbeReply:
    """Reject corrupt, extended or mismatched responses; no implicit retry."""
    if Command(command) in (Command.TELEMETRY, Command.CAPABILITIES, Command.DISCOVER):
        raise ValueError("observation requires its versioned decoder")
    if len(frame) != 48:
        raise ValueError("reply must contain exactly 48 bytes")
    magic, version, received, reserved, rid, rev, size, checksum = HEADER.unpack(frame[:24])
    if (magic, version, reserved, size) != (b"UTH1", 1, 0, 24):
        raise ValueError("unsupported reply header")
    if (received, rid, rev) != (int(Command(command)) | 0x80, request_id, revision):
        raise ValueError("reply does not match the outstanding request")
    if checksum != zlib.crc32(frame[24:], zlib.crc32(frame[:20])):
        raise ValueError("reply checksum mismatch")
    fields = struct.unpack("<6I", frame[24:])
    if (command != Command.PROBE and fields[0] > 8) or fields[5] & ~0x7F:
        raise ValueError("unknown status or flags")
    result_type = ProbeReply if command == Command.PROBE else Reply
    return result_type(Command(command), rid, rev, *fields)
