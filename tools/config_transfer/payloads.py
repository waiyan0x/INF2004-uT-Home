"""JSON-compatible observation shapes after strict wire validation.

Unavailable captures retain the existing empty-object representation. Nullable
fields distinguish missing measurements from genuine zero values. These are
static contracts; the wire decoders remain the runtime validation boundary.
"""
from typing import Literal, Never, TypedDict


Unavailable = dict[Never, Never]
Quality = Literal["missing", "fresh", "stale", "fault"]
DiscoveryResult = Literal["complete", "busy", "fault", "invalid", "stale", "incomplete"]
ObservationStatus = Literal["responding", "not_responding", "timeout", "fault", "not_checked"]


class ConfigurationState(TypedDict):
    active_revision: int
    saved_revision: int
    active_crc: int
    saved_crc: int
    flags: int


class RuntimeState(TypedDict):
    state: int
    generation: int
    failed_workers: int
    retained_instances: int
    service_error: int
    apply_error: int
    recovery_error: int


class HealthState(TypedDict):
    total_events: int
    total_suppressed: int
    last_event: int | None
    last_event_time_ms: int | None
    event_counts: list[int]
    suppressed_counts: list[int]


class QueueObservation(TypedDict):
    index: int
    type: int
    published: int
    consumed: int
    dropped: int
    depth: int
    high_water: int


class EnvironmentValue(TypedDict):
    pressure_pa: float
    temperature_c: float


class DistanceValue(TypedDict):
    distance_mm: int
    echo_us: int


class HealthValue(TypedDict):
    event: int
    occurrences: int


class IlluminanceValue(TypedDict):
    millilux: int


class RadarTarget(TypedDict):
    present: bool
    resolution_mm: int
    x_mm: int
    y_mm: int
    speed_cm_s: int


class RadarValue(TypedDict):
    targets: list[RadarTarget]


SampleValue = EnvironmentValue | DistanceValue | HealthValue | IlluminanceValue | RadarValue


class SampleObservation(TypedDict):
    queue: int
    type: int
    source_index: int
    name: str
    quality: Quality
    timestamp_ms: int
    sequence: int
    status: int
    stale_after_ms: int
    age_ms: int | None
    value: SampleValue | None


class AllocationCounts(TypedDict):
    allocations: int
    bytes: int
    frees: int
    kernel_calls: dict[str, int]


class LifecycleCounts(AllocationCounts):
    creates: dict[str, int]


class MemoryAudit(TypedDict):
    sealed: bool
    saturated: bool
    application_startup: AllocationCounts | None
    startup: LifecycleCounts
    normal: LifecycleCounts


class StackObservation(TypedDict):
    bytes: int
    unused_min: int | None
    measured: bool
    empirical: Literal[True]


class WorkStats(TypedDict):
    steps: int
    elapsed_us: int
    max_elapsed_us: int
    max_lateness_us: int
    bus_wait_us: int
    bus_active_us: int
    transfer_bytes: int
    transfers: int
    timeouts: int
    errors: int
    flags: int


class WorkerMeasurement(TypedDict):
    slot: int
    source_index: int | None
    available: bool
    stats: WorkStats | None


class WorkerObservation(WorkerMeasurement):
    task_handle: int | None
    bound: bool
    stack: StackObservation


class SchedulerObservation(TypedDict):
    elapsed_ms: int
    idle_ms: int
    tick_ms: int
    method: Literal["native_tick_attribution"]


class SchedulerWindow(TypedDict):
    duration_ms: int
    idle_ms: int
    non_idle_ms: int
    sampled_non_idle_percent: float
    tick_ms: int
    method: Literal["native_tick_attribution"]
    cpu_cycles_measured: Literal[False]


class WorkloadMeasurement(TypedDict):
    contract: Literal["uthome.workload-measurement"]
    version: Literal["0.1.0"]
    firmware_elf_sha256: str
    profile_sha256: str
    generation: int
    timestamp_us: int
    workers: list[WorkerMeasurement]


class I2cObservation(TypedDict):
    address: int
    register: int
    status: ObservationStatus
    value: int | None


class UartObservation(TypedDict):
    parser_id: Literal["ld2450_reports"]
    received_bytes: int
    valid_reports: int
    framing_errors: int
    discarded_bytes: int
    receive_errors: int
    pending_bytes: int
    window_ms: int


DiscoveryObservation = I2cObservation | UartObservation
