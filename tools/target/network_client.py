"""Bounded MQTT telemetry decoder and BLE frame reassembly."""
from __future__ import annotations

import math
import re
import struct
import time

FRAME_BYTES = 112


def decode_frame(data: bytes) -> dict:
    """Strictly decode one MQTT sensor payload or reassembled BLE v1 frame."""
    if len(data) != FRAME_BYTES:
        raise ValueError('sensor frame must be exactly 112 bytes')
    version, kind, quality, status, source = struct.unpack_from('<BBBBH', data)
    if version != 1 or kind not in (1, 2, 3, 4, 5) or quality > 3 or status > 4:
        raise ValueError('unsupported sensor header')
    if data[6:8] != b'\0\0' or source not in (*range(8), 65535):
        raise ValueError('invalid source or reserved fields')
    if source == 65535 and kind != 4:
        raise ValueError('only health may use the global source')
    name, separator, padding = data[44:76].partition(b'\0')
    if not separator or any(padding):
        raise ValueError('invalid source name padding')
    try:
        name = name.decode('ascii')
    except UnicodeError as error:
        raise ValueError('invalid source name') from error
    if not name or any(ord(char) < 32 or ord(char) > 126 for char in name):
        raise ValueError('invalid source name')
    sequence, revision, generation, boot, timestamp, age, stale = struct.unpack_from(
        '<IIIQQII', data, 8)
    values = None
    raw = data[76:]
    if quality in (1, 2):
        if status != 1:
            raise ValueError('usable measurement requires OK status')
        if kind == 1:
            pressure, temperature = struct.unpack_from('<ff', raw)
            if not math.isfinite(pressure) or not math.isfinite(temperature):
                raise ValueError('non-finite sensor value')
            values = dict(pressure_pa=pressure, temperature_c=temperature)
        elif kind == 3:
            distance, echo = struct.unpack_from('<II', raw)
            values = dict(distance_mm=distance, echo_us=echo)
        elif kind == 5:
            millilux, = struct.unpack_from('<I', raw)
            if millilux > 200_000_000:
                raise ValueError('light measurement exceeds contract')
            values = dict(millilux=millilux)
        elif kind == 4:
            event, occurrences = struct.unpack_from('<II', raw)
            if event >= 8:
                raise ValueError('unknown health event')
            values = dict(event=event, occurrences=occurrences)
        else:
            targets = []
            for offset in range(0, 36, 12):
                flags, x, y, speed, reserved = struct.unpack_from('<IhhhH', raw, offset)
                if flags & 0xFFFE or reserved:
                    raise ValueError('reserved target bits')
                present = bool(flags & 1)
                if not present and any(raw[offset:offset + 12]):
                    raise ValueError('absent target contains measurements')
                targets.append(dict(present=present, x_mm=x, y_mm=y,
                                    speed_cm_s=speed, resolution_mm=flags >> 16))
            values = dict(targets=targets)
        unused = 4 if kind == 5 else 8
        if kind != 2 and any(raw[unused:]):
            raise ValueError('nonzero unused value bytes')
    elif any(raw):
        raise ValueError('missing/fault measurement contains values')
    if quality == 0 and (status != 0 or timestamp != 0 or sequence != 0 or age != 0xFFFFFFFF):
        raise ValueError('invalid missing measurement')
    return dict(version=version, type=kind, source_index=source, device_id=name,
                boot_id=f'{boot:016x}', active_revision=revision,
                runtime_generation=generation, sequence=sequence,
                timestamp_ms=timestamp, age_ms=None if age == 0xFFFFFFFF else age,
                stale_ms=stale, quality=('missing', 'fresh', 'stale', 'fault')[quality],
                status=status, values=values)


def decode_mqtt(topic: str, payload: bytes) -> dict:
    """Decode publisher topics without treating availability as sensor health."""
    if not isinstance(topic,str) or len(topic)>128:
        raise ValueError('invalid MQTT topic')
    parts=topic.split('/')
    if (len(parts) not in (4,5) or parts[:2]!=['uthome','v1'] or
            not re.fullmatch(r'[A-Za-z0-9_-]{1,31}',parts[2])):
        raise ValueError('invalid MQTT node/topic')
    if len(parts)==4 and parts[3]=='availability':
        if payload not in (b'online',b'offline'):
            raise ValueError('invalid availability payload')
        return dict(node_id=parts[2],online=payload==b'online')
    if len(parts)!=5 or parts[3]!='telemetry':
        raise ValueError('unsupported MQTT topic')
    decoded=decode_frame(payload)
    if parts[4]!=decoded['device_id']:
        raise ValueError('MQTT device identity mismatch')
    return dict(node_id=parts[2],**decoded)


class BleReassembler:
    """Single fixed-size frame; ordered fragments, absolute two-second expiry.

    Call reset on disconnect/boot change. Incomplete expired frames are discarded;
    they never become a successful zero reading. A new index-zero fragment
    replaces an incomplete frame. No history or retransmission queue is retained.
    """
    def __init__(self, mtu: int = 23):
        if not 23 <= mtu <= 517:
            raise ValueError('invalid ATT MTU')
        self.payload = mtu - 11
        self.reset()

    def reset(self):
        self.buffer = bytearray()
        self.sequence = None
        self.next_index = 0
        self.started = None
        self.last = None

    def feed(self, data: bytes, now: float | None = None) -> dict | None:
        now = time.monotonic() if now is None else now
        if not math.isfinite(now):
            self.reset()
            raise ValueError('invalid clock')
        if self.last is not None and now < self.last:
            self.reset()
            raise ValueError('clock regression')
        if self.started is not None and now - self.started >= 2.0:
            self.reset()
        if len(data) < 8:
            self.reset()
            raise ValueError('short fragment')
        version, kind, sequence, index, count, total = struct.unpack_from('<BBHBBH', data)
        expected_count = (FRAME_BYTES + self.payload - 1) // self.payload
        if (version != 1 or kind != 1 or total != FRAME_BYTES or
                count != expected_count or index >= count):
            self.reset()
            raise ValueError('invalid fragment header')
        expected_length = min(self.payload, total - index * self.payload)
        if len(data) != 8 + expected_length:
            self.reset()
            raise ValueError('invalid fragment length')
        if index == 0:
            self.reset()
            self.sequence = sequence
            self.started = now
        if sequence != self.sequence or index != self.next_index:
            self.reset()
            raise ValueError('fragment sequence gap')
        self.last = now
        self.buffer.extend(data[8:])
        self.next_index += 1
        if self.next_index == count:
            frame = bytes(self.buffer)
            self.reset()
            return decode_frame(frame)
        return None
