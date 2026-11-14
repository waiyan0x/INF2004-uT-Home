# ESP32-C3 network adapters

This folder contains the SDK/kernel boundaries for the experimental network
compatibility layer. Portable queue, event and wait policy lives in
[`firmware/src/network`](../../../../src/network/README.md), which owns the
resource limits, build instructions, manual reviews and qualification evidence.

| Modules | Responsibility |
|---|---|
| `c3boot` | Bounded native startup pool for selected static-network builds |
| `c3lbox` | lwIP mailbox identities bound to the existing fixed queue payloads |
| `c3tls` | Fixed ESP-TLS contexts, error trackers and temporary hostname storage |
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

## Fixed ESP-TLS records

Selected Wi-Fi services with static network queues bind `calloc`, `free` and
`strndup` only in the pinned `esp_tls.c` and `esp_tls_mbedtls.c` units to `c3tls`.
Three actual `esp_tls_t` objects support the existing one MQTT plus two HTTPS
connections. Three separate error records retain the public SDK error type;
four error-tracker entry points are linked to owned implementations. The
original tracker implementation must not remain linked. Vendor files and pins
are unchanged, and the SDK still owns connection setup and TLS/socket cleanup.

Only `calloc(1, sizeof(esp_tls_t))` is admitted. Unsupported shapes and exhausted
stores return NULL; there is no general allocator or heap fallback. One
254-byte temporary name buffer covers the existing 253-character server-name
limit and numeric IPv4 address. The SDK copies/uses and frees that scratch name
before returning; persistent mbedTLS hostname storage remains a separate open
allocation path. Session-ticket storage is excluded by a build guard, matching
the selected configuration. Global CA-store and saved-session APIs are outside
this adapter contract; selected callers supply their own CA bytes and no saved
session. Do not add another ESP-TLS user without reviewing its allocation shapes.

All operations run on the single application owner, in task context with cache
enabled. There is no new task, kernel object or cross-task lock. Cleanup must
finish before a context/error lease is returned. Released storage is cleared,
and every handle expires on release; a stale pointer cannot identify a later
lease at the same address. NULL and foreign releases leave the pools unchanged.
Acquisition/release examines at most three entries; name copying is bounded by
253 bytes. These are operation bounds, not ESP timing or stack measurements.

### TLS record C review

Reviewed against the BARR-C checklist: C99, paired public headers, SDK-compatible
prototypes, handle naming, initialization, bounded loops, signed errors and
deterministic exhaustion. Contexts have their actual SDK type and alignment;
error handles point to real `esp_tls_last_error_t` subobjects. Pointer equality
selects records without pointer subtraction, type punning or foreign-pointer
dereferences. Zeroing preserves the SDK's calloc initialization semantics on
the selected target. Get-and-clear changes only the requested error category;
invalid requests preserve output and other error fields. The enum-to-uint32
conversion makes negative/unknown categories unsupported. No new deviation,
formatter change or owned GNU syntax is introduced.

The target gate verifies the two allocator substitution scopes, all four error
aliases, absence of original error functions, flash placement and retained SDK
client/server cleanup. Host tests compile the actual owned module with SDK type
declarations doubled; they check alignment, zeroing, independent error fields,
hostname limits, exhaustion, failed-initialization cleanup and 1,000 complete
lease cycles under allocator interception. They do not perform TLS handshakes.
The real target build uses the full pinned SDK declarations.

This closes ESP-TLS record/name allocation only. Encryption buffers,
certificates/keys, retained TLS hostnames and lwIP connection/packet storage still
need bounded allocation and live acceptance. See the
[current resource assessment](../../../../../docs/reports/wifi-static-memory/README.md#current-network-resource-assessment).
