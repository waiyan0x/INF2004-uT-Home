"""Protocol workload features from reviewed configuration, never run outcomes.

Rates describe configured work requests, not measured CPU time, wire utilization
or completed display frames. Unknown execution/protocol shapes are unsupported.
"""
from copy import deepcopy

from ai.records import FEATURES as BASE_FEATURES, FEATURE_LIMITS as BASE_LIMITS, features as base_features
from ai.timing import digest, export_profile
from tools.config_validator.planner import Catalogue, ConfigurationError, require


VERSION = "0.1.0"
FEATURES = BASE_FEATURES + [
    "target_priority", "higher_priority_workers", "i2c_workers", "i2c_min_clock_hz",
    "i2c_requests_millihz", "spi_workers", "spi_min_clock_hz", "spi_refresh_millihz",
    "uart_workers", "uart_min_baud", "uart_declared_bytes_per_second", "uart_min_rx_buffer_bytes",
]
LIMITS = BASE_LIMITS + [[5, 19], [0, 8], [0, 8], [0, 1000000], [0, 8000000],
                      [0, 8], [0, 16000000], [0, 8000000], [0, 8], [0, 4000000],
                      [0, 16000000], [0, 65536]]
SHAPES = {("i2c", "periodic"), ("spi", "queue_consumer"), ("uart", "stream")}


def describe(profile, target_id, catalogue=None):
    """Called after whole-profile admission. No names or device IDs enter inputs."""
    catalogue = catalogue or Catalogue()
    buses = {b["id"]: b for b in profile["interfaces"]}
    active = [d for d in profile["devices"] if d["enabled"]]
    target = next(d for d in active if d["device_id"] == target_id)
    priority = target["task"]["priority"]
    values = {p: dict(count=0, speeds=[], rate=0, buffers=[]) for p in ("i2c", "spi", "uart")}
    for device in active:
        ref = device["descriptor"]
        descriptor = catalogue.descriptors[(ref["id"], ref["version"])]
        protocol, execution = descriptor["protocol"], descriptor["execution"]
        require((protocol, execution["kind"]) in SHAPES, "Unsupported protocol/execution workload")
        bus = buses[device["interface"]["ref"]]
        row = values[protocol]
        row["count"] += 1
        row["speeds"].append(bus["baud_rate" if protocol == "uart" else "clock_hz"])
        if protocol == "uart":
            frame = device["behaviour"].get("frame", {})
            require(all(type(frame.get(k)) is int and frame[k] > 0 for k in ("size_bytes", "report_rate_hz")),
                    "UART workload requires declared frame size and report rate")
            row["rate"] += frame["size_bytes"] * frame["report_rate_hz"]
            row["buffers"].append(bus["rx_buffer_bytes"])
        else:
            period = device["behaviour"][execution["period_field"]]
            row["rate"] += (1000000 + period - 1) // period
    minimum = lambda items: min(items, default=0)
    i2c, spi, uart = (values[p] for p in ("i2c", "spi", "uart"))
    return [priority, sum(d["task"]["priority"] < priority for d in active),
            i2c["count"], minimum(i2c["speeds"]), i2c["rate"],
            spi["count"], minimum(spi["speeds"]), spi["rate"],
            uart["count"], minimum(uart["speeds"]), uart["rate"], minimum(uart["buffers"])]


def features(experiment, interval, catalogue=None):
    catalogue = catalogue or Catalogue()
    profile = export_profile(experiment, interval, catalogue)
    # The initial protocol study measures producer freshness under mixed load.
    require(experiment["target"]["execution_kind"] == "periodic"
            and experiment["scenario"]["goal"]["metric"] == "observed_sample_age_ms",
            "Protocol study currently measures periodic producer freshness")
    result = base_features(experiment, interval, catalogue) + describe(profile, experiment["target"]["instance_id"], catalogue)
    require(all(type(v) is int and low <= v <= high for v, (low, high) in zip(result, LIMITS)),
            "Workload features exceed the reviewed contract")
    return result


def family_key(experiment):
    """Keep repeated sessions, goal variants and candidate periods together."""
    profile = deepcopy(experiment["profile"])
    target = experiment["target"]
    profile["devices"] = [d for d in profile["devices"] if d["enabled"]]
    device = next(d for d in profile["devices"] if d["device_id"] == target["instance_id"])
    device["behaviour"][target["period_field"]] = 0
    # Remove labels only; retain wiring, routes and all workload settings.
    profile.pop("node_id", None)
    for item in profile["devices"]:
        item["task"].pop("name", None)
    used_buses = {d["interface"]["ref"] for d in profile["devices"]}
    used_queues = {q for d in profile["devices"] for q in
                   ([d["behaviour"]["destination"]] if "destination" in d["behaviour"] else d["behaviour"].get("sources", []))}
    profile["interfaces"] = [b for b in profile["interfaces"] if b["id"] in used_buses]
    profile["queues"] = [q for q in profile["queues"] if q["id"] in used_queues]
    bus_names = {b["id"]: f"bus-{i}" for i, b in enumerate(profile["interfaces"])}
    queue_names = {q["id"]: f"queue-{i}" for i, q in enumerate(profile["queues"])}
    for bus in profile["interfaces"]:
        bus["id"] = bus_names[bus["id"]]
    for queue in profile["queues"]:
        queue["id"] = queue_names[queue["id"]]
    for index, item in enumerate(profile["devices"]):
        item["device_id"] = f"device-{index}"
        item["interface"]["ref"] = bus_names[item["interface"]["ref"]]
        behaviour = item["behaviour"]
        if "destination" in behaviour:
            behaviour["destination"] = queue_names[behaviour["destination"]]
        if "sources" in behaviour:
            behaviour["sources"] = [queue_names[q] for q in behaviour["sources"]]
    return digest(profile)


def fallback(experiment, reason):
    """No automatic Apply; preserve the validated current draft as the fallback."""
    current = next(t["interval_ms"] for t in experiment["trials"] if "current_profile" in t["roles"])
    return dict(status="abstain", reason=reason, deployment_allowed=False,
                fallback=dict(action="keep_current_profile", interval_ms=current,
                              goal_guaranteed=False, automatic_apply=False))


def compatibility(experiment):
    """Keep omitted characteristics guarded while allowing studied rate/speed changes.

    Device/build provenance remains mandatory. Payload geometry, queue depths,
    priorities, timeouts and topology cannot silently change behind the features.
    """
    from ai.model import context
    masked = deepcopy(experiment)
    for bus in masked["profile"]["interfaces"]:
        if bus["protocol"] in ("i2c", "spi"):
            bus["clock_hz"] = 0
    for device in masked["profile"]["devices"]:
        behaviour = device["behaviour"]
        if "refresh_interval_ms" in behaviour:
            behaviour["refresh_interval_ms"] = 0
    return digest(dict(provenance=context(experiment), fixed_workload=family_key(masked)))


def coverage(experiment, interval, catalogue=None):
    try:
        return dict(status="supported", feature_names=FEATURES, features=features(experiment, interval, catalogue))
    except ConfigurationError as error:
        return fallback(experiment, str(error))
