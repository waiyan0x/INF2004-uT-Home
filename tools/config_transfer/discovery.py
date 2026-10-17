"""Bounded discovery wire format: raw observations, without device guessing."""
from dataclasses import dataclass
import struct
import zlib

from .protocol import Command, HEADER

RESULTS = ("complete", "busy", "fault", "invalid", "stale", "incomplete")
OBSERVATIONS = ("responding", "not_responding", "timeout", "fault", "not_checked")


@dataclass(frozen=True)
class Discovery:
    request_id: int
    revision: int
    active_revision: int
    result: str
    preset_code: int
    session_id: int
    started_ms: int
    elapsed_ms: int
    build_sha256: str
    records: tuple


def discovery_request(session_id, *, wiring_confirmed, preset_code=1):
    if wiring_confirmed is not True:
        raise ValueError("Confirm the reviewed preset wiring before discovery")
    if type(session_id) is not int or not 0 < session_id <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError("discovery session must be a nonzero uint64")
    if type(preset_code) is not int or preset_code not in (1, 2):
        raise ValueError("unsupported discovery preset")
    return struct.pack("<IIQII", 1, preset_code, session_id, 1, 0)


def decode_discovery(frame, *, request_id, revision, payload):
    if len(payload) != 24 or not 104 <= len(frame) <= 232:
        raise ValueError("invalid discovery length")
    version, preset, session, confirmed, reserved = struct.unpack("<IIQII", payload)
    if (version, confirmed, reserved) != (1, 1, 0) or preset not in (1, 2) or not session:
        raise ValueError("unsupported discovery request")
    header = HEADER.unpack(frame[:24])
    if header[:7] != (b"UTH1", 1, int(Command.DISCOVER) | 0x80, 0,
                      request_id, revision, len(frame) - 24):
        raise ValueError("discovery reply does not match request")
    data = frame[24:]
    if header[7] != zlib.crc32(data, zlib.crc32(frame[:20])):
        raise ValueError("discovery checksum mismatch")
    version, result, received_preset, active_revision, received_session, started, elapsed, count = (
        struct.unpack_from("<IIIIQQII", data))
    expected_count, record_size = (2, 16) if preset == 1 else (1, 32)
    if (version != 1 or result >= len(RESULTS) or received_preset != preset or
        received_session != session or count not in (0, expected_count) or
        len(data) != 80 + count * record_size or any(data[72:80]) or not any(data[40:72])):
        raise ValueError("unsupported discovery fields")
    if result != 4 and active_revision != revision:
        raise ValueError("configuration revision changed during discovery")
    if ((result in (0, 5) and count != expected_count) or
        (result in (1, 3, 4) and (count or started or elapsed))):
        raise ValueError("inconsistent discovery outcome")
    records = []
    if preset == 2 and count:
        parser, received, valid, framing, discarded, errors, pending, window = struct.unpack_from('<8I', data, 80)
        if (parser != 1 or received > 128000 or valid * 30 > received or
            framing > discarded or discarded + valid * 30 + pending != received or
            errors > 1 or pending >= 30 or window not in (0, 1000) or elapsed > 1100 or
            (result == 0 and (window != 1000 or elapsed < window or errors))):
            raise ValueError('invalid UART discovery evidence')
        records.append(dict(parser_id='ld2450_reports', received_bytes=received, valid_reports=valid,
                            framing_errors=framing, discarded_bytes=discarded,
                            receive_errors=errors, pending_bytes=pending, window_ms=window))
    for index in range(count):
        if preset != 1:
            break
        address, register, status, value = struct.unpack_from("<4I", data, 80 + index * 16)
        if ((address, register) != (118 + index, 0) or
            status >= len(OBSERVATIONS) or value > 255 or
            (status != 0 and value != 0) or (result == 0 and status > 1)):
            raise ValueError("invalid or unrequested discovery observation")
        records.append(dict(address=address, register=register,
                            status=OBSERVATIONS[status], value=value if status == 0 else None))
    return Discovery(request_id, revision, active_revision, RESULTS[result], preset,
                     session, started, elapsed, data[40:72].hex(), tuple(records))
