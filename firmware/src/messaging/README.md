# Messaging

Member 1's host message bus copies fixed-size typed items into static bounded
queues: four queues, depth up to eight, and up to 64 bytes per item. It supports
drop-oldest and reject-newest overflow policies, normal or latest-item reads,
and depth/high-water/drop statistics. An empty read returns an error immediately.
The portable bus itself has no kernel waiting or timeout scheduling and requires
serialized access. Target `shared` wrappers provide the guard; the configuration
binder maps validated queue names and sample types during checked activation.
Queue replacement requires quiescent workers. Rollback does not replay old items.

Queues have a single consuming owner. Monitoring and OLED rendering use the
shared latest-sample cache, so they do not drain producer queues. Overflow health
checks are explicit. A display or network service consumes declared sample types;
core messaging is independent of the prototype's device combination. See
[the runtime guide](../runtime/README.md) for lifecycle and synchronization limits.
