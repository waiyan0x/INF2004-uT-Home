"""Strict command-9 decoder; unavailable timings never become zero features."""
from dataclasses import dataclass
import struct
import zlib

from .protocol import HEADER, Command

FIELDS = ('steps', 'elapsed_us', 'max_elapsed_us', 'max_lateness_us',
          'bus_wait_us', 'bus_active_us', 'transfer_bytes', 'transfers',
          'timeouts', 'errors', 'flags')


@dataclass(frozen=True)
class WorkcostReply:
    result: int
    generation: int
    active_revision: int
    active_crc: int
    timestamp_us: int
    capture_us: int
    controller_stack: dict
    idle_stack: dict
    workers: tuple
    scheduler: dict | None = None

    def scheduler_window(self, previous):
        """Derive sampled non-idle time only for a short coherent same-profile window.

        Callers must bind both observations to the same verified connection and
        image. Tick attribution includes preemption/interrupts in the sampled
        task and does not isolate ISR cost or measure CPU cycles.
        """
        if (self.result or previous.result or not self.scheduler or not previous.scheduler
                or (self.generation, self.active_revision, self.active_crc) !=
                   (previous.generation, previous.active_revision, previous.active_crc)
                or self.timestamp_us <= previous.timestamp_us):
            raise ValueError('Scheduler window has unavailable or changed context')
        current, earlier = self.scheduler, previous.scheduler
        elapsed = current['elapsed_ms'] - earlier['elapsed_ms']
        tick = current['tick_ms']
        idle = (current['idle_ms'] - earlier['idle_ms']) & 0xffffffff
        if (tick != earlier['tick_ms'] or not max(100, 100 * tick) <= elapsed <= 60000
                or idle > elapsed):
            raise ValueError('Scheduler window is short, reset, wrapped or inconsistent')
        return dict(duration_ms=elapsed, idle_ms=idle, non_idle_ms=elapsed-idle,
                    sampled_non_idle_percent=100 * (elapsed-idle) / elapsed,
                    tick_ms=tick, method='native_tick_attribution', cpu_cycles_measured=False)

    def measurement(self, *, firmware_elf_sha256, profile_sha256):
        """Bind separately verified image/profile identities, without guessing."""
        if self.result:
            raise ValueError('Workload capture unavailable')
        return dict(contract='uthome.workload-measurement', version='0.1.0',
                    firmware_elf_sha256=firmware_elf_sha256,
                    profile_sha256=profile_sha256, generation=self.generation,
                    timestamp_us=self.timestamp_us,
                    workers=[{key: row[key] for key in ('slot', 'source_index', 'available', 'stats')}
                             for row in self.workers if row['bound']])


def _stack(words, expected):
    size, unused, available = words
    if size != expected or available not in (0, 1) or unused > size or (not available and unused):
        raise ValueError('Invalid stack observation')
    return dict(bytes=size, unused_min=unused if available else None,
                measured=bool(available), empirical=True)


def decode_workcost(frame, *, request_id):
    if len(frame) not in (120, 408):
        raise ValueError('Invalid workload frame length')
    magic, version, command, reserved, rid, revision, size, checksum = HEADER.unpack(frame[:24])
    if ((magic, version, command, reserved, rid, revision) !=
            (b'UTH1', 1, int(Command.WORKCOST) | 0x80, 0, request_id, 0)
            or size != len(frame) - 24):
        raise ValueError('Workload reply does not match request')
    if checksum != zlib.crc32(frame[24:], zlib.crc32(frame[:20])):
        raise ValueError('Workload checksum mismatch')
    payload = frame[24:]
    header = struct.unpack_from('<24I', payload)
    if header[0] not in (1, 2) or header[1] not in (0, 1, 2):
        raise ValueError('Unsupported workload payload')
    if header[1]:
        if size != 96 or any(header[2:]):
            raise ValueError('Failed workload capture carries data')
        return WorkcostReply(header[1], 0, 0, 0, 0, 0, {}, {}, ())
    if size != 384 or header[5] != 4 or any(header[20:]):
        raise ValueError('Invalid workload header')
    scheduler = None
    if header[0] == 1:
        if any(header[15:20]):
            raise ValueError('Legacy workload header carries scheduler data')
    elif header[15] not in (0, 1) or (not header[15] and any(header[16:20])):
        raise ValueError('Invalid scheduler availability')
    elif header[15]:
        if not 1 <= header[19] <= 1000:
            raise ValueError('Invalid native tick period')
        scheduler = dict(elapsed_ms=header[16] | (header[17] << 32), idle_ms=header[18],
                         tick_ms=header[19], method='native_tick_attribution')
    controller = _stack(header[8:11], 8448)
    idle = _stack(header[11:14], 1024)
    rows, sources, handles = [], set(), set()
    for index in range(4):
        words = struct.unpack_from('<18I', payload, 96 + index * 72)
        slot, source, handle, flags, stack_bytes, unused = words[:6]
        bound, available, measured = bool(flags & 1), bool(flags & 2), bool(flags & 4)
        if slot != index or flags & ~7 or words[17] or stack_bytes != 8192:
            raise ValueError('Invalid workload slot')
        stack = _stack((stack_bytes, unused, int(measured)), 8192)
        if bound:
            if source >= 8 or source in sources or not 0 < handle <= 0x7fffffff or handle in handles:
                raise ValueError('Duplicate/invalid worker binding')
            sources.add(source)
            handles.add(handle)
        elif source or handle or available:
            raise ValueError('Unbound slot carries a binding')
        stats = dict(zip(FIELDS, words[6:17])) if available else None
        if stats is None:
            if any(words[6:17]):
                raise ValueError('Unavailable costs carry counters')
        elif (not stats['steps'] or stats['flags'] not in (0, 1)
              or stats['timeouts'] > stats['errors']
              or stats['max_elapsed_us'] > stats['elapsed_us']
              or (not stats['flags'] and
                  stats['bus_wait_us'] + stats['bus_active_us'] > stats['elapsed_us'])):
            raise ValueError('Inconsistent workload counters')
        rows.append(dict(slot=slot, source_index=source if bound else None,
                         task_handle=handle if bound else None, bound=bound,
                         available=available, stats=stats, stack=stack))
    return WorkcostReply(0, header[2], header[3], header[4],
                         header[6] | (header[7] << 32), header[14], controller, idle, tuple(rows), scheduler)
