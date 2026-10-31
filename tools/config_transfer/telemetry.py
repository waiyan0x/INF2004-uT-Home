"""Strict decoding of version 1/2/3 typed observations (TELEMETRY.md)."""
from dataclasses import dataclass
import math
import re
import struct
import zlib

from .protocol import Command, HEADER


@dataclass(frozen=True)
class Snapshot:
    request_id: int
    result: int
    timestamp_ms: int
    configuration: dict
    runtime: dict
    health: dict
    queues: tuple
    samples: tuple
    memory_audit: dict | None = None


def signed(value, bits=32):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def decode_telemetry(frame: bytes, *, request_id: int) -> Snapshot:
    if not 184 <= len(frame) <= 2040:
        raise ValueError("invalid telemetry length")
    magic, version, command, reserved, rid, rev, size, crc = HEADER.unpack(frame[:24])
    if ((magic, version, command, reserved, rid, rev, size) !=
        (b"UTH1", 1, int(Command.TELEMETRY) | 0x80, 0, request_id, 0, len(frame) - 24)):
        raise ValueError("invalid telemetry header")
    if crc != zlib.crc32(frame[24:], zlib.crc32(frame[:20])):
        raise ValueError("telemetry checksum mismatch")
    data = frame[24:]
    h = struct.unpack_from("<40I", data)
    if h[0] not in (1, 2, 3) or h[1] > 2:
        raise ValueError("unsupported telemetry version/result")
    header_size = {1: 160, 2: 320, 3: 352}[h[0]]
    if len(data) < header_size:
        raise ValueError("truncated telemetry header")
    if h[1]:
        if len(data) != header_size or any(data[8:]):
            raise ValueError("invalid failed capture")
        return Snapshot(rid, h[1], 0, {}, {}, {}, (), ())
    samples, queues = h[14:16]
    if (samples > 12 or queues > 4 or h[21] != 8 or h[18] > 1 or h[19] >= 8 or
        h[8] & ~0x70 or h[9] > 2 or h[11] > 8 or h[12] > 8 or
        len(data) != header_size + queues * 32 + samples * 128):
        raise ValueError("invalid snapshot counts/state")
    audit = _decode_audit(data[160:header_size]) if h[0] >= 2 else None
    queue_rows = []
    offset = header_size
    for index in range(queues):
        q = struct.unpack_from("<8I", data, offset)
        if q[0] != index or not 1 <= q[1] <= 4 or q[5] > q[6] or q[6] > 8 or q[7]:
            raise ValueError("invalid queue row")
        queue_rows.append(dict(zip(("index", "type", "published", "consumed", "dropped",
                                    "depth", "high_water"), q[:7])))
        offset += 32
    sample_rows = []
    seen = set()
    for _ in range(samples):
        row = data[offset:offset + 128]
        s = struct.unpack_from("<10I", row)
        queue, kind, source, quality, low, high, sequence, status, stale, age = s
        identity = (source, queue if source == 65535 else None)
        if (queue >= queues or kind != queue_rows[queue]["type"] or
            not (source < 8 or (source == 65535 and kind == 4)) or
            identity in seen or quality > 3 or status > 4 or stale == 0 or any(row[112:])):
            raise ValueError("invalid sample row")
        seen.add(identity)
        name, separator, padding = row[40:72].partition(b"\0")
        if not separator or any(padding) or not re.fullmatch(rb"[A-Za-z][A-Za-z0-9_.-]{0,30}", name):
            raise ValueError("invalid source name")
        timestamp = low | high << 32
        now = h[2] | h[3] << 32
        if quality == 0:
            if timestamp or sequence or status or age != 0xFFFFFFFF or any(row[72:112]):
                raise ValueError("invalid missing sample")
        elif quality in (1, 2):
            if (status != 1 or timestamp > now or age != min(now - timestamp, 0xFFFFFFFF) or
                (now - timestamp >= stale) != (quality == 2)):
                raise ValueError("inconsistent freshness")
        elif status == 1 and timestamp <= now:
            raise ValueError("inconsistent sample fault")
        value = _decode_value(kind, row[72:112])
        if status != 1 and any(row[72:112]):
            raise ValueError("fault values must be zero")
        sample_rows.append(dict(queue=queue, type=kind, source_index=source,
            name=name.decode("ascii"), quality=("missing", "fresh", "stale", "fault")[quality],
            timestamp_ms=timestamp, sequence=sequence, status=status,
            stale_after_ms=stale, age_ms=None if quality == 0 else age,
            value=value if status == 1 else None))
        offset += 128
    return Snapshot(rid, 0, h[2] | h[3] << 32,
        dict(zip(("active_revision", "saved_revision", "active_crc", "saved_crc", "flags"), h[4:9])),
        dict(state=h[9], generation=h[10], failed_workers=h[11], retained_instances=h[12],
             service_error=signed(h[13]), apply_error=signed(h[38]), recovery_error=signed(h[39])),
        dict(total_events=h[16], total_suppressed=h[17],
             last_event=h[19] if h[18] else None, last_event_time_ms=h[20] if h[18] else None,
             event_counts=list(h[22:30]), suppressed_counts=list(h[30:38])),
        tuple(queue_rows), tuple(sample_rows), audit)


AUDIT_KINDS = ("task", "mutex", "semaphore", "flag", "mailbox", "message_buffer",
               "fixed_pool", "variable_pool", "rendezvous", "cyclic", "alarm")


def _decode_audit(data):
    words = struct.unpack("<" + str(len(data) // 4) + "I", data)
    flags = words[0]
    if flags & ~7 or words[1] or any(words[30:32]):
        raise ValueError("invalid memory audit flags/reserved")
    if not flags & 1:
        if any(words):
            raise ValueError("unavailable memory audit must be zero")
        return None
    application = None
    if len(words) == 48:
        if words[40] > 1 or (not words[40] and (any(words[41:]) or flags & 2)):
            raise ValueError("invalid application-startup boundary")
        if words[40]:
            application = dict(allocations=words[41], bytes=words[42], frees=words[43],
                               kernel_calls=dict(zip(("malloc", "calloc", "realloc", "free"), words[44:48])))
            if (any(words[41 + index] > words[2 + index] for index in range(3)) or
                    any(words[44 + index] > words[32 + index] for index in range(4))):
                raise ValueError("application counts exceed total startup")
    if not flags & 2 and (any(words[5:8]) or any(words[19:30]) or any(words[36:40])):
        raise ValueError("unsealed memory audit has normal-operation counts")
    return dict(sealed=bool(flags & 2), saturated=bool(flags & 4), application_startup=application,
                startup=dict(allocations=words[2], bytes=words[3], frees=words[4],
                             creates=dict(zip(AUDIT_KINDS, words[8:19])),
                             kernel_calls=dict(zip(("malloc", "calloc", "realloc", "free"), words[32:36]))),
                normal=dict(allocations=words[5], bytes=words[6], frees=words[7],
                            creates=dict(zip(AUDIT_KINDS, words[19:30])),
                            kernel_calls=dict(zip(("malloc", "calloc", "realloc", "free"), words[36:40]))))


def _decode_value(kind, data):
    used = 36 if kind == 2 else 8
    if any(data[used:]):
        raise ValueError("nonzero value padding")
    if kind == 1:
        pressure, temperature = struct.unpack_from("<2f", data)
        if not math.isfinite(pressure) or not math.isfinite(temperature):
            raise ValueError("non-finite environment sample")
        return dict(pressure_pa=pressure, temperature_c=temperature)
    if kind == 3:
        distance, echo = struct.unpack_from("<2I", data)
        return dict(distance_mm=distance, echo_us=echo)
    if kind == 4:
        event, occurrences = struct.unpack_from("<2I", data)
        if event >= 8:
            raise ValueError("unknown health event")
        return dict(event=event, occurrences=occurrences)
    targets = []
    for index in range(3):
        flags, position, speed = struct.unpack_from("<3I", data, index * 12)
        if flags & 0xFFFE or speed > 65535 or (not flags & 1 and (flags or position or speed)):
            raise ValueError("invalid target slot")
        targets.append(dict(present=bool(flags & 1), resolution_mm=flags >> 16,
            x_mm=signed(position & 65535, 16), y_mm=signed(position >> 16, 16),
            speed_cm_s=signed(speed, 16)))
    return dict(targets=targets)
