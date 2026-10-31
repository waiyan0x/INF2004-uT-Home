# Shared firmware contracts

`sample.h` declares the common environment, target, distance and health records,
with source identity, monotonic timestamps and explicit validity status. It is
independent of drivers and the HAL. See [the integration contract](../../CONTRACTS.md)
for ownership, routing, cleanup and deployment decisions. The records fit the
message-bus item ceiling; they are not raw USB or storage encodings.

`component.h` supplies Member 1's generic configure/read/write/publish interface.
`drvreg.h` maps compiled driver IDs to operations and binds independent
instance contexts. `runtime.h` owns generic instance scheduling/lifecycle and
bounded host configuration admission. Its optional bindings connect independently
owned driver contexts to bounded execution steps. `msgbus.h`, `health.h`,
`hlthbus.h` and `watchdog.h` expose the bounded host queue, event, queue-health
and watchdog APIs. None enumerates
sensor models. Calls use the target `shared` synchronization layer; direct
portable-module calls require serialized access.

A node names a driver and reviewed descriptor version, then supplies its own
instance ID, connection, behaviour and scheduling values. The target adapter
must validate these against its actual build and board capabilities before
calling low-level lifecycle APIs. Host code must not be mistaken for a target
kernel adapter. See [the platform baseline](../../../README.md#platform-contract).

The component interface and its unit test retain Member 1's contribution.
Shared-header changes require matching caller and target-adapter updates.

The BARR-C audit shortened public identifiers and split headers to match their
modules. The migration table and required checks are in
[the C standard](../../../C_CODING_STANDARD.md). Rebuild and update callers
when integrating teammate branches; configuration JSON keys are unchanged.
