"""Decode compiled capabilities separately from hardware qualification."""
from dataclasses import dataclass
import re
import struct
import zlib
from .protocol import Command, HEADER

@dataclass(frozen=True)
class Capabilities:
    request_id: int
    hardware_qualified: bool
    max_profile_bytes: int
    allowed_gpio_mask: int
    max_devices: int
    max_workers: int
    features: int
    board: str
    node_schema: str
    drivers: tuple

def decode_capabilities(frame, *, request_id):
    if not 120 <= len(frame) <= 376:
        raise ValueError("invalid capability length")
    fields = HEADER.unpack(frame[:24])
    if fields[:7] != (b"UTH1", 1, int(Command.CAPABILITIES) | 0x80, 0,
                      request_id, 0, len(frame) - 24):
        raise ValueError("invalid capability header")
    data = frame[24:]
    if fields[7] != zlib.crc32(data, zlib.crc32(frame[:20])):
        raise ValueError("capability checksum mismatch")
    version, qualified, limit, count, pins, devices, workers, features = struct.unpack_from("<8I", data)
    if (version != 1 or qualified > 1 or not 1 <= limit <= 8192 or count > 8 or
        not 1 <= workers <= devices <= 8 or features & ~7 or len(data) != 96 + count * 32):
        raise ValueError("unsupported capability fields")
    def name(offset, pattern):
        text, separator, padding = data[offset:offset+32].partition(b"\0")
        if not separator or any(padding) or not re.fullmatch(pattern, text):
            raise ValueError("invalid capability name")
        return text.decode("ascii")
    board = name(32, rb"[A-Za-z][A-Za-z0-9_.-]{0,30}")
    schema = name(64, rb"[0-9]+\.[0-9]+\.[0-9]+")
    drivers = tuple(name(96+i*32, rb"[A-Za-z][A-Za-z0-9_.-]{0,30}") for i in range(count))
    if len(set(drivers)) != count:
        raise ValueError("duplicate capability driver")
    return Capabilities(request_id, bool(qualified), limit, pins, devices,
                        workers, features, board, schema, drivers)
