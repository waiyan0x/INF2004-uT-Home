# Messaging

Member 1's host message bus copies fixed-size typed items into static bounded
queues: four queues, depth up to eight, and up to 64 bytes per item. It supports
drop-oldest and reject-newest overflow policies, normal or latest-item reads,
and depth/high-water/drop statistics. An empty read returns an error immediately.
No kernel waiting, timeout scheduling or concurrent access is implemented.

The host integration tests publish from independently bound runtime instances.
The caller explicitly creates queues and provides their IDs to driver step
contexts. Runtime apply/rollback does not replace queues or restore their contents.

Target configuration must map validated queue names and sample types to actual
queue objects, enforce ownership, synchronize concurrent access, and include
creation/cleanup in checked activation. Wire overflow checks to health explicitly.
A display or network transport is an optional consumer of declared sample types;
core messaging is independent of the prototype's device combination.
