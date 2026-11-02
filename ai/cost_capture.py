"""Retain measured workload costs separately from labels and configured features."""
from collections.abc import Collection, Mapping, Sequence
from typing import Any, Literal, TypedDict, cast

from ai.measurements import WorkloadWindow, available_stats, summarize_window, validate_snapshot
from tools.config_transfer.client import Client
from tools.config_transfer.payloads import StackObservation, Unavailable, WorkloadMeasurement
from tools.config_transfer.workcost import FIELDS, WorkcostReply
from tools.config_validator.planner import ConfigurationError, require


class CaptureObservation(TypedDict):
    measurement: WorkloadMeasurement
    handles: list[tuple[int, int]]
    observer_us: int
    controller_unused: int
    idle_unused: int


class CostRecord(TypedDict):
    type: Literal['workcost']
    elapsed_ms: int
    response: dict[str, Any]


class CaptureSummary(TypedDict):
    contract: Literal['uthome.cost-capture']
    version: Literal['0.1.0']
    window: WorkloadWindow
    observations: int
    observer_max_us: int
    controller_unused_min: int
    idle_unused_min: int
    cost_cadence_ms: Literal[1000]
    model_features: Literal[False]
    scope: str


def check_audit(snapshot: Mapping[str, Any]) -> None:
    audit = snapshot.get('memory_audit')
    if (not isinstance(audit, dict) or audit.get('sealed') is not True
            or audit.get('saturated') is not False):
        raise ConfigurationError('allocation_audit_unavailable')
    for phase in ('application_startup', 'normal'):
        value = audit.get(phase)
        if not isinstance(value, dict):
            raise ConfigurationError('application_allocation_phase_missing')
        require(all(value.get(key) == 0 for key in ('allocations', 'bytes', 'frees')),
                'application_heap_activity')
        calls = value.get('kernel_calls')
        require(isinstance(calls, dict) and set(calls) == {'malloc', 'calloc', 'realloc', 'free'}
                and all(v == 0 for v in calls.values()), 'kernel_heap_activity')
    creates = audit['normal'].get('creates')
    require(isinstance(creates, dict) and len(creates) == 11
            and all(v == 0 for v in creates.values()), 'post_startup_object_creation')


def _stack_unused(stack: StackObservation | Unavailable, size: int, reserve: int) -> int:
    # Both transport variants have string keys; the unavailable variant is empty.
    unused = cast(Mapping[str, object], stack).get('unused_min')
    if (stack != dict(bytes=size, unused_min=unused, measured=True, empirical=True)
            or type(unused) is not int or not reserve < unused <= size):
        raise ConfigurationError('controller_or_idle_stack_margin_unverified')
    return unused


def validate_reply(reply: WorkcostReply, *, identity: Mapping[str, Any],
                   profile_hash: str, expected: Sequence[int], generation: int,
                   sources: Collection[int]) -> CaptureObservation:
    """Bind a decoded live reply to the verified capture context."""
    require(reply.result == 0 and reply.generation == generation
            and (reply.active_revision, reply.active_crc) == (expected[0], expected[2]),
            'workcost_configuration_changed')
    require(type(reply.capture_us) is int and 0 <= reply.capture_us <= 0xffffffff,
            'invalid_observer_duration')
    measurement = validate_snapshot(reply.measurement(
        firmware_elf_sha256=identity['build']['elf_sha256'], profile_sha256=profile_hash))
    require({r['source_index'] for r in measurement['workers']} == set(sources), 'workcost_source_mapping_changed')
    require(all(r['available'] and r['stats'] is not None and r['stats']['flags'] == 0
                for r in measurement['workers']),
            'incomplete_or_saturated_workcost')
    controller_unused = _stack_unused(reply.controller_stack, 8448, 512)
    idle_unused = _stack_unused(reply.idle_stack, 1024, 128)
    handles: list[tuple[int, int]] = []
    for worker in reply.workers:
        if worker['bound']:
            handle = worker['task_handle']
            if type(handle) is not int or not 0 < handle <= 0x7fffffff:
                raise ConfigurationError('invalid_workcost_binding_handle')
            handles.append((worker['slot'], handle))
    require(len({handle for _, handle in handles}) == len(handles), 'invalid_workcost_binding_handle')
    return dict(measurement=measurement, handles=handles, observer_us=reply.capture_us,
                controller_unused=controller_unused, idle_unused=idle_unused)


def decode_record(response: Mapping[str, Any], *, identity: Mapping[str, Any],
                  profile_hash: str, expected: Sequence[int], generation: int,
                  sources: Collection[int]) -> CaptureObservation:
    """Reconstruct a retained JSON reply and apply the live capture checks."""
    return validate_reply(WorkcostReply(**response), identity=identity, profile_hash=profile_hash,
                          expected=expected, generation=generation, sources=sources)


def observe(client: Client, *, identity: Mapping[str, Any], profile_hash: str,
            expected: Sequence[int], generation: int, sources: Collection[int]) -> WorkcostReply:
    """Read one cost snapshot using complete, explicitly named capture context."""
    reply = client.workcost()
    validate_reply(reply, identity=identity, profile_hash=profile_hash,
                   expected=expected, generation=generation, sources=sources)
    return reply


def summarize(records: Sequence[CostRecord], *, identity: Mapping[str, Any],
              profile_hash: str, expected: Sequence[int], generation: int,
              sources: Collection[int]) -> CaptureSummary:
    """Validate retained observations against one capture before summarizing."""
    require(len(records) >= 2, 'insufficient_workcost_observations')
    values = [decode_record(row['response'], identity=identity, profile_hash=profile_hash,
                            expected=expected, generation=generation, sources=sources)
              for row in records]
    for before, after in zip(values, values[1:]):
        require(before['handles'] == after['handles'], 'workcost_binding_changed')
        # A window may contain no slow worker completion at an intermediate
        # endpoint. Require monotonic counters here; the full window needs steps.
        earlier, later = before['measurement'], after['measurement']
        require(later['timestamp_us'] > earlier['timestamp_us'], 'workcost_clock_reversed')
        require([(r['slot'], r['source_index']) for r in earlier['workers']] ==
                [(r['slot'], r['source_index']) for r in later['workers']], 'workcost_slots_changed')
        for before_worker, after_worker in zip(earlier['workers'], later['workers']):
            before_stats, after_stats = available_stats(before_worker), available_stats(after_worker)
            require(all(after_stats[key] >= before_stats[key] for key in FIELDS), 'workcost_counter_reset')
    window = summarize_window(values[0]['measurement'], values[-1]['measurement'])
    return dict(contract='uthome.cost-capture', version='0.1.0', window=window,
                observations=len(values), observer_max_us=max(v['observer_us'] for v in values),
                controller_unused_min=min(v['controller_unused'] for v in values),
                idle_unused_min=min(v['idle_unused'] for v in values),
                cost_cadence_ms=1000, model_features=False,
                scope='USB core only; excludes radio traffic and cross-device accuracy')
