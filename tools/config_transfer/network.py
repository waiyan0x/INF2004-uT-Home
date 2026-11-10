"""Strict USB command-10 Wi-Fi status decoding without credentials."""
from dataclasses import dataclass
import struct
import zlib

from .protocol import Command, HEADER

STAGES = ('not_attempted', 'netif_init', 'event_loop', 'default_station',
          'wifi_register', 'got_ip_register', 'lost_ip_register', 'wifi_init',
          'storage', 'mode', 'config', 'start', 'ready', 'connect', 'disconnect',
          'tx_power')
RETRY_STATES = ('waiting', 'attempt', 'online')


@dataclass(frozen=True)
class NetworkReply:
    request_id: int
    stage: str
    retry_state: str
    sdk_error: int
    disconnect_reason: int
    ipv4: str | None
    deadline_ms: int
    attempted: bool
    started: bool
    station_ready: bool
    has_ip: bool
    cancelling: bool
    fault: bool


def decode_network(frame: bytes, *, request_id: int) -> NetworkReply:
    """Decode one observation; a missing address remains unavailable.

    Old and non-Wi-Fi images may not implement this command and time out.
    The deadline is an absolute board-monotonic millisecond value, not a wait.
    """
    if len(frame) != 48:
        raise ValueError('Network reply must contain exactly 48 bytes')
    if type(request_id) is not int or not 1 <= request_id <= 0xffffffff:
        raise ValueError('request_id must be a nonzero uint32')
    magic, version, command, reserved, rid, revision, size, checksum = HEADER.unpack(frame[:24])
    if ((magic, version, command, reserved, rid, revision, size) !=
            (b'UTH1', 1, int(Command.NETWORK) | 0x80, 0, request_id, 0, 24)):
        raise ValueError('Network reply does not match request')
    if checksum != zlib.crc32(frame[24:], zlib.crc32(frame[:20])):
        raise ValueError('Network checksum mismatch')
    payload_version, flags, stage, retry, sdk_error, reason, padding, address, deadline = \
        struct.unpack('<4BiHH4sQ', frame[24:])
    if (payload_version != 1 or flags & ~0x3f or stage >= len(STAGES)
            or retry >= len(RETRY_STATES) or padding):
        raise ValueError('Unsupported network payload')
    if not flags & 8 and any(address):
        raise ValueError('Unavailable network address carries data')
    return NetworkReply(rid, STAGES[stage], RETRY_STATES[retry], sdk_error, reason,
                        '.'.join(str(octet) for octet in address) if flags & 8 else None,
                        deadline, bool(flags & 1), bool(flags & 2), bool(flags & 4),
                        bool(flags & 8), bool(flags & 16), bool(flags & 32))
