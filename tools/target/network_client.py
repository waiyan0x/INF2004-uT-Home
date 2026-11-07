"""Read-only HTTPS client and bounded BLE decoder; never a PC gateway.

Examples:
  python tools/target/network_client.py https://esp.local status --ca device-ca.pem
  python tools/target/network_client.py https://esp.local telemetry --source 2 --ca device-ca.pem

Set UTHOME_TOKEN in the environment; do not put secrets in command history.
HTTPS certificate/hostname verification is mandatory. This client does not
provision credentials, apply configuration, or start an ESP network service.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
from pathlib import Path
import ssl
import struct
import time
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import HTTPSHandler, HTTPRedirectHandler, build_opener

FRAME_BYTES = 112


def decode_frame(data: bytes) -> dict:
    """Strictly decode one HTTP sensor body or reassembled BLE v1 frame."""
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


class NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError('redirect refused to protect Authorization credential')


def fetch(base: str, operation: str, token: str, source: int = 0,
          ca: str | None = None) -> dict:
    """Bounded verified HTTPS read; no redirects and no mutation retries."""
    parts = urlsplit(base)
    if (parts.scheme != 'https' or not parts.hostname or parts.username or
            parts.password or parts.query or parts.fragment or parts.path not in ('', '/')):
        raise ValueError('use an HTTPS origin without credentials or path')
    if (not 16 <= len(token) <= 64 or
            any(ord(char) < 0x21 or ord(char) > 0x7e for char in token)):
        raise ValueError('invalid token')
    if operation not in ('identity', 'status', 'telemetry') or not 0 <= source <= 7:
        raise ValueError('invalid operation/source')
    context = ssl.create_default_context(cafile=ca)
    opener = build_opener(HTTPSHandler(context=context), NoRedirects())
    path = f'/v1/{operation}'
    if operation == 'telemetry':
        path += f'?source={source}'
    from urllib.request import Request
    req = Request(base.rstrip('/') + path,
                  headers={'Authorization': 'Bearer ' + token, 'Connection': 'close'})
    with opener.open(req, timeout=10) as response:
        body = response.read(2049)
        if len(body) > 2048:
            raise ValueError('response exceeds bound')
        if operation == 'telemetry':
            if response.headers.get_content_type() != 'application/octet-stream':
                raise ValueError('unexpected telemetry content type')
            return decode_frame(body)
        if response.headers.get_content_type() != 'application/json':
            raise ValueError('unexpected status content type')
        return json.loads(body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('origin')
    parser.add_argument('operation', choices=['identity', 'status', 'telemetry'])
    parser.add_argument('--source', type=int, choices=range(8), default=0)
    parser.add_argument('--ca', type=str)
    parser.add_argument('--token-env', default='UTHOME_TOKEN')
    args = parser.parse_args()
    token = os.environ.get(args.token_env, '')
    try:
        result = fetch(args.origin, args.operation, token, args.source, args.ca)
    except HTTPError as error:
        # Do not print request headers or server-provided exception bodies.
        parser.exit(1, f'HTTP request failed: {error.code}\n')
    except (ValueError, OSError) as error:
        parser.exit(1, f'{type(error).__name__}: request failed\n')
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
