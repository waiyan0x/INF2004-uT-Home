# ESP32-C3 network adapters

This folder contains the SDK/kernel boundaries for the experimental network
compatibility layer. Portable queue, event and wait policy lives in
[`firmware/src/network`](../../../../src/network/README.md), which owns the
resource limits, build instructions, manual reviews and qualification evidence.

| Modules | Responsibility |
|---|---|
| `c3boot` | Bounded native startup pool for selected static-network builds |
| `c3lbox` | lwIP mailbox identities bound to the existing fixed queue payloads |
| `c3nq`, `c3event` | Native queue/event synchronization and fixed resource bindings |
| `c3ewait` | Stateless native notification, clock and context boundary for events |
| `c3wifi` | Isolated Wi-Fi scan diagnostic |
| `c3ble`, `c3gatt` | Isolated BLE advertising and read-only GATT diagnostics |
| `c3bport` | Typed kernel boundary that keeps kernel headers out of NimBLE units |

The target qualifier stages these files into its existing flat HAL layout and
keeps the separate radio component's include namespace. Peripheral, USB and
configuration-storage adapters remain in the [parent port](../README.md).
These sources do not establish combined wireless acceptance or static-memory
qualification. Historical evidence retains its original source paths and hashes.

## Fixed lwIP mailboxes

Wi-Fi service builds with static network queues replace only the pinned SDK's
`sys_mbox_new` and `sys_mbox_free` at link time. `c3lbox` binds a mailbox to
`c3nq`'s existing eight shared queue slots and 8,192 bytes of payload storage.
There is no new mutable pool, heap fallback or runtime native-object creation.
Mailbox depths must fit an available partition; exhaustion returns `ERR_MEM`.
Ordinary SDK queues share the same slots, so eight is a total upper bound,
not eight extra mailboxes or a promise of eight simultaneous sockets.

The SDK mailbox struct has a flexible-array member. Eight separate, typed
constant objects provide permanent identities in flash; their control fields
are never queue storage. The existing post/fetch/ISR SDK functions only take the
address of `os_mbox`, which the owned queue adapter treats as an opaque key.
Do not send these identities to native FreeRTOS queue functions. The target
qualifier checks both replacement aliases, absence of the original allocating
functions, eight distinct flash identities and flash placement of the new
creation/release helpers. Vendor source and pinned revisions remain unchanged.

Create/free run in task context with cache enabled. The caller must stop all
users and drain messages before closing, as lwIP requires. Closing a foreign,
nonempty or in-use mailbox preserves its lease. Message pointers are copied;
the mailbox never owns or frees their pointees. Null message values are valid.
Startup before pool initialization, invalid sizes and pool exhaustion fail
without allocating. Reuse is bounded by eight identity attempts, each using the
existing fixed-slot search; this is an operation bound, not an ESP timing result.

### Manual C review

Reviewed `c3lbox.c/.h` and the two new `c3nq` entry points against the project's
BARR-C checklist: ordinary C99, paired headers, bounded loops, initialization,
error paths, signed-size validation, existing interrupt-protected pool leases,
and task/cache context. The explicit foreign struct pointer declarations retain
exact compatibility with `sys_mbox_t *`; the SDK `err_t` and `int` ABI is retained.
The positive `int` to `uint32_t` cast is valid on the supported RV32/host ABIs.
The documented const-removing cast is limited to the foreign opaque-handle ABI:
the objects have the actual SDK type, alignment and permanent lifetime, and no
reachable selected queue operation reads or writes their control contents.
No raw-byte-buffer type punning, new naming exception or language deviation is
introduced. The real target build and pinned source/member-access review are
required when changing this boundary or its SDK dependency.

Tests execute the actual adapter and queue pool with only the SDK declarations
and kernel/clock calls doubled. They cover shared exhaustion, exact partition
capacity, invalid/oversized requests, null messages, full queues, ISR posting,
nonempty/foreign close, initialization order and 1,000 reuse cycles without
additional native objects. The existing pool suite covers waiter/release races.
For fresh target results and the remaining TLS/TCP/IP allocation gaps, see the
[current resource assessment](../../../../../docs/reports/wifi-static-memory/README.md#current-network-resource-assessment).
No live broker, board timing or whole-system heap acceptance follows from these
software checks.
