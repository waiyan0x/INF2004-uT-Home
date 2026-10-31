"""Retain measured workload costs separately from labels and configured features."""
from dataclasses import asdict

from ai.measurements import summarize_window, validate_snapshot
from tools.config_transfer.workcost import WorkcostReply
from tools.config_validator.planner import require


def check_audit(snapshot):
    audit = snapshot.get('memory_audit')
    require(isinstance(audit, dict) and audit.get('sealed') is True
            and audit.get('saturated') is False, 'allocation_audit_unavailable')
    for phase in ('application_startup', 'normal'):
        value = audit.get(phase)
        require(isinstance(value, dict), 'application_allocation_phase_missing')
        require(all(value.get(key) == 0 for key in ('allocations', 'bytes', 'frees')),
                'application_heap_activity')
        calls = value.get('kernel_calls')
        require(isinstance(calls, dict) and set(calls) == {'malloc', 'calloc', 'realloc', 'free'}
                and all(v == 0 for v in calls.values()), 'kernel_heap_activity')
    creates = audit['normal'].get('creates')
    require(isinstance(creates, dict) and len(creates) == 11
            and all(v == 0 for v in creates.values()), 'post_startup_object_creation')


def decode_record(response, *, identity, profile_hash, expected, generation, sources):
    reply = WorkcostReply(**response)
    require(reply.result == 0 and reply.generation == generation
            and (reply.active_revision, reply.active_crc) == (expected[0], expected[2]),
            'workcost_configuration_changed')
    require(type(reply.capture_us) is int and 0 <= reply.capture_us <= 0xffffffff,
            'invalid_observer_duration')
    measurement = validate_snapshot(reply.measurement(
        firmware_elf_sha256=identity['build']['elf_sha256'], profile_sha256=profile_hash))
    require({r['source_index'] for r in measurement['workers']} == set(sources), 'workcost_source_mapping_changed')
    require(all(r['available'] and r['stats']['flags'] == 0 for r in measurement['workers']),
            'incomplete_or_saturated_workcost')
    for stack, size, reserve in ((reply.controller_stack, 8448, 512), (reply.idle_stack, 1024, 128)):
        require(stack == dict(bytes=size, unused_min=stack.get('unused_min'), measured=True, empirical=True)
                and type(stack['unused_min']) is int and reserve < stack['unused_min'] <= size,
                'controller_or_idle_stack_margin_unverified')
    handles = [(r['slot'], r['task_handle']) for r in reply.workers if r['bound']]
    require(all(type(h) is int and 0 < h <= 0x7fffffff for _, h in handles)
            and len(set(h for _, h in handles)) == len(handles), 'invalid_workcost_binding_handle')
    return dict(measurement=measurement, handles=handles, observer_us=reply.capture_us,
                controller_unused=reply.controller_stack['unused_min'], idle_unused=reply.idle_stack['unused_min'])


def observe(client, **context):
    response = asdict(client.workcost())
    decode_record(response, **context)
    return response


def summarize(records, **context):
    require(len(records) >= 2, 'insufficient_workcost_observations')
    values = [decode_record(row['response'], **context) for row in records]
    for before, after in zip(values, values[1:]):
        require(before['handles'] == after['handles'], 'workcost_binding_changed')
        # A window may contain no slow worker completion at an intermediate
        # endpoint. Require monotonic counters here; the full window needs steps.
        a, b = before['measurement'], after['measurement']
        require(b['timestamp_us'] > a['timestamp_us'], 'workcost_clock_reversed')
        require([(r['slot'], r['source_index']) for r in a['workers']] ==
                [(r['slot'], r['source_index']) for r in b['workers']], 'workcost_slots_changed')
        require(all(all(y['stats'][k] >= x['stats'][k] for k in x['stats'])
                    for x, y in zip(a['workers'], b['workers'])), 'workcost_counter_reset')
    window = summarize_window(values[0]['measurement'], values[-1]['measurement'])
    return dict(contract='uthome.cost-capture', version='0.1.0', window=window,
                observations=len(values), observer_max_us=max(v['observer_us'] for v in values),
                controller_unused_min=min(v['controller_unused'] for v in values),
                idle_unused_min=min(v['idle_unused'] for v in values),
                cost_cadence_ms=1000, model_features=False,
                scope='USB core only; excludes radio traffic and cross-device accuracy')
