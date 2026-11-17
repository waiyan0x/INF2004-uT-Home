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
| `c3tout` | Fixed MQTT buffers, hostname, outer TLS/certificate records and PRF seed/label |
| `c3lrec` | Fixed typed lwIP connection and datagram-envelope records |
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
One actual `esp_tls_t` object supports the single MQTT connection. One
separate error record retains the public SDK error type;
four error-tracker entry points are linked to owned implementations. The
original tracker implementation must not remain linked. Vendor files and pins
are unchanged, and the SDK still owns connection setup and TLS/socket cleanup.

Only `calloc(1, sizeof(esp_tls_t))` is admitted. Unsupported shapes and exhausted
stores return NULL; there is no general allocator or heap fallback. One
254-byte temporary name buffer covers the existing 253-character server-name
limit and numeric IPv4 address. The SDK copies/uses and frees that scratch name
before returning; `c3tout` below owns a separate persistent connection hostname.
The build guard excludes ESP-TLS saved-session storage. mbedTLS ticket parsing
can still allocate internally; it is not covered by that guard.
Global CA-store and saved-session APIs are outside
this adapter contract; selected callers supply their own CA bytes and no saved
session. Do not add another ESP-TLS user without reviewing its allocation shapes.

All operations run on the single application owner, in task context with cache
enabled. There is no new task, kernel object or cross-task lock. Cleanup must
finish before a context/error lease is returned. Released storage is cleared,
and every handle expires on release; a stale pointer cannot identify a later
lease at the same address. NULL and foreign releases leave the pools unchanged.
Acquisition/release examines one entry; name copying is bounded by
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
client cleanup and absence of the removed HTTPS server. Host tests compile the actual owned module with SDK type
declarations doubled; they check alignment, zeroing, independent error fields,
hostname limits, exhaustion, failed-initialization cleanup and 1,000 complete
lease cycles under allocator interception. They do not perform TLS handshakes.
The real target build uses the full pinned SDK declarations.

The selected build rejects `CONFIG_MBEDTLS_DYNAMIC_BUFFER`; the pinned SDK's
partial-header failure is documented in the resource assessment below. The
qualifier also rejects a linked `esp_mbedtls_add_rx_buffer` implementation.

This closes ESP-TLS record/name allocation only. The adapter below owns TLS
record buffers and the connection hostname; certificates/keys, ticket state and lwIP packet/control-block storage still
need bounded allocation and live acceptance. See the
[current resource assessment](../../../../../docs/reports/wifi-static-memory/README.md#current-network-resource-assessment).

## Fixed TLS record buffers

`c3tout` reserves one input and one output buffer for MQTT. Each has the pinned
SDK's full capacity: `MBEDTLS_SSL_IN_BUFFER_LEN` is 16,717 bytes for 16,384-byte
content; `MBEDTLS_SSL_OUT_BUFFER_LEN` is 4,429 bytes for 4,096-byte content.
Eight-byte-aligned slots occupy 16,720 and 4,432 bytes respectively.
Both remain attached through handshake, partial reads/writes and retries until SDK
cleanup. Exhaustion returns NULL to the SDK's existing setup failure path;
there is no heap fallback for either record buffer. TLS record limits and
certificate checks remain unchanged; HTTPS capacity has been removed.

The setup wrapper calls the original SDK function through a checked linker
alias. A borrowed setup-context pointer lives only for that synchronous call.
`ssl_tls.c` binds its allocator/free/wipe calls to the record adapter; the
separate PSA input boundary is described below. A record request selects storage only
during setup: input is selected before either buffer exists, and output after
input exists but before output is assigned. Setup requires an initialized,
unused context, as the SDK does. A same-sized later allocation retains the SDK path. Other
allocations deliberately remain with the SDK; this does not close whole-TLS
heap use or replace the encryption implementation.

Both setup-error free and normal zeroizing cleanup recognize exact input/output
buffer identities and erase the complete buffer before releasing its lease.
Other valid SDK pointers follow their original free/wipe functions. This is an
SDK boundary, not an arbitrary-pointer allocator API. Buffer resizing and equal
input/output sizes are rejected at compile time; the existing dynamic-buffer
guard remains. All calls belong to the single serialized application owner in
task context with cache enabled. Adding another TLS owner or enabling another
SDK TLS consumer requires review of capacity and serialization.

The hostname wrapper calls the original `mbedtls_ssl_set_hostname` through a
separate checked alias. One 254-byte character array retains the name used for
certificate matching and SNI, distinct from `c3tls`'s temporary copy. The wrapper
checks at most 254 bytes and rejects names longer than the existing MQTT limit
of 253 characters before changing the old name. During that synchronous call,
only its exact `calloc(1, length + 1)` request with a cleared SDK hostname selects
the fixed slot. Same-sized allocations outside the setter keep the SDK path.
Exhaustion returns the SDK allocation error without a hostname heap fallback.

The original setter retains copying, the hostname-set flag, replacement and
NULL-clear semantics. It requires an initialized context and borrowed immutable
input that does not alias the retained SDK name. A NULL or empty name is supported
at this SDK boundary; MQTT admission still requires a nonempty reviewed name.
Cleanup/replace/clear erase the full 254-byte slot before reuse, including bytes
beyond a shorter replacement name. All error returns clear the temporary selection
scope. This changes storage, not certificate verification or SNI behavior.

### TLS output C review

The original output adapter now also owns input, hostname and typed state storage;
its existing public prefix is retained.

Reviewed `c3tout.c/.h` and `touttest.c/.h` against the BARR-C checklist: C99,
paired modules, named prototypes, fixed-width counters, initialized state,
bounded one-entry searches, explicit failure results and no new casts.
Alignment-only unions supply real character-array storage; neither is ever
interpreted as a different object type. SDK context pointers have the actual
pinned type and remain valid for the synchronous setup call. The private SDK
layout/length macros are confined to this adapter; the target build checks its
ABI and effective compiler profile. No naming, language or formatter exception
is added, and vendor sources remain unchanged.

The original SDK owns buffer pointer updates, retries and cleanup order. The
adapter never releases a buffer at WANT_READ/WANT_WRITE or uses stack-backed storage for
partial data. It uses the SDK's explicit zeroization operation before making
storage reusable. The two record requests and scoped hostname request have a fixed-pool guarantee;
other SDK allocations are visible remaining work. Host tests intercept direct
allocator calls in the owned module and separately count intentional SDK
forwarding. They cover independent exhaustion, alignment, complete clearing on
both cleanup paths, failure before/between/after buffer acquisition, output
exhaustion returning the new input lease, same-sized later allocations,
unmatched request shapes and 1,000 complete reuse cycles. No new task, lock,
cast, exception, aggregate cap or SDK modification is introduced.

The compiled SDK probes bind both slots to this module's DWARF and ELF symbols,
then check actual setup/failure cleanup, absence of record-buffer heap requests,
partial headers/bodies, unread plaintext and erasure. Transport, heap, ROM byte
copies and hash setup/abort are doubled; plaintext is injected after decryption.
These are software checks, not encrypted-handshake or hardware acceptance.

The hostname extension adds ordinary C99 state and matching prototypes, without
casts, type punning, locks or hardware access. Character-array alignment suffices
for the SDK's string operations. The bounded length scan keeps `length + 1` within
254 bytes; `size_t` matches allocator sizes. The native SDK exclusively updates
the context's name pointer and flags; exact-pointer release never dereferences a
foreign pointer. Invalid length preserves existing storage and flags. The slot
lease persists beyond setter return and ends only through SDK cleanup/replace.
Host checks cover boundary lengths, copied lifetime, rejection, exhaustion,
both erasure paths, unrelated requests and 1,000 connection/failure cycles.
The compiled name probe compares the original setter with the fixed-storage
wrapper, then exercises native setup failures and cleanup. ROM `strlen` is doubled;
the SDK setter and its flag/copy/error paths execute their real instructions.

## Fixed TLS handshake state

`c3tout` also reserves two actual `mbedtls_ssl_transform` objects, two actual
`mbedtls_ssl_session` objects and one `mbedtls_ssl_handshake_params` object.
The active transform/session remain alive while renegotiation prepares their
replacements. The SDK owns initialization, promotion, interior crypto cleanup
and release order; exact-pointer release erases the full outer object before
its lease becomes reusable. Exhaustion returns NULL without a state-record
heap fallback. Certificates, keys and ticket contents still use SDK allocation.

Setup and both renegotiation entries call the unchanged SDK through checked
aliases. Both `mbedtls_ssl_start_renegotiation` and `mbedtls_ssl_renegotiate`
must be wrapped: the latter's same-source call to the former bypasses a linker
wrapper. `c3tout_renegotiate` arms selection only for a new, valid renegotiation;
an in-progress retry does not. `c3tout_start` always scopes initialization,
including reuse of existing outer records. A borrowed context and pending bits
identify missing records in the
pinned SDK's transform/session/handshake order, after both record buffers exist.
Each attempted allocation consumes its bit, including exhaustion. Selection
ends before checksum setup and handshake progression, so later equal-sized
ticket or copied-session requests retain the SDK path even after promotion
clears negotiating pointers. Existing outer records are reused by the SDK.

This depends on the pinned SDK's uninterrupted initialization sequence, not
general allocation-size classification. Calls belong to one serialized owner;
callbacks must not reenter these wrappers. The build rejects server, DTLS and
TLS 1.3 profiles, requires TLS 1.2 client renegotiation, and rejects linked
session-reset APIs until their initialization paths receive separate review.
Neither record limits nor certificate verification are changed.

### State storage C review

The 17 November review covers the added `c3tout` and `touttest` code against the
full project BARR-C checklist. Ordinary C99, paired headers, public prefixes,
named prototypes and SDK function signatures are retained. Typed arrays supply
native alignment and lifetime without casts or byte-buffer type punning.
`size_t` carries sizes/indices; the pending mask uses unsigned `uint32_t`
operations. Searches are bounded by two records, and the handshake store has
one entry. State is initialized before use and scopes clear at every return.
Exact-pointer matching does not dereference foreign pointers. SDK interior
cleanup precedes erasure and release of the outer record. No ISR, MMIO, lock,
task, language/naming exception or dependency change is added.

Host doubles retain the borrowed configuration for the context lifetime and
reject disabled assertions. They cover all eight missing-record combinations,
active/negotiating coexistence, setup and renegotiation exhaustion, retries,
both release paths, zeroing/reuse and out-of-phase equal-sized allocations.
Their opaque typed records do not simulate crypto cleanup. Compiled SDK probes
separately exercise native lifecycle instructions against the real typed stores.
Fresh checks, build identity and limits are in the
[resource assessment](../../../../../docs/reports/wifi-static-memory/README.md#fixed-tls-handshake-state).

## Fixed peer certificate records

The selected parser uses one actual `mbedtls_x509_crt` chain head (952 bytes)
temporarily and retains one 32-byte peer-certificate digest. Both now belong to
`c3tout`. The head and old digest can coexist while parsing a renegotiation
message, so they have separate stores. The unchanged SDK checks that the peer's
certificate matches its old digest, frees that digest after a match, then later
creates the replacement. One digest slot therefore suffices for the selected
client lifecycle. A mismatch preserves the old digest; a later parse or verify
failure may leave it cleared, exactly as the SDK does.

`c3tout_certificate` wraps the unchanged `mbedtls_ssl_parse_certificate` call.
Its borrowed context and phases select the exact typed head request first;
only release of that head permits selection of the next digest request. Each
request consumes its phase even on exhaustion, and all returns clear the scope.
Unrelated request shapes and requests during head use or after digest selection
retain the SDK allocator. Selected exhaustion returns NULL without fallback.
Exact-pointer cleanup erases the complete store before publishing its free lease.
The pinned parser's direct allocation/free sequence and serialized, nonrecursive
callback contract remain required; this is not general size-based allocation.

The SDK still performs certificate validation and identity comparison. This
change does not alter trust roots, server-name checks, TLS record limits or
ticket behavior. The DER input remains borrowed from the existing receive buffer.
Additional chain nodes, parsed names, sequences and keys still allocate through
their original SDK units. The build rejects full peer-certificate retention,
restartable ECP and a different default digest length. It also rejects linked
session copy/get/set/load or context-load APIs: those require separate lifetimes.

### Certificate storage C review

Reviewed the added `c3tout` and `touttest` code against the project's full BARR-C
checklist on 17 November. The code remains C99 with matching public headers and
SDK-compatible declarations. The head has its native type, alignment and lifetime;
the digest is a byte array. No casts, type punning, new task, lock, ISR, MMIO or
exception is introduced. The unsigned phase has four documented states and
no unbounded operations. SDK interior cleanup precedes complete outer erasure;
foreign pointers are compared without dereference. All wrapper returns clear
the borrowed scope. Host doubles keep configuration pointers alive, reject
disabled assertions, use named loop bounds and keep their opaque record sizes
separate from the real target ABI proof.

Host checks cover repeated reuse, old/new ownership, retry and failure paths,
both stores' exhaustion, both free paths and unrelated allocations across phases.
The compiled probe separately executes native message framing, identity decisions,
promotion and cleanup. DER parsing, trust verification, digest/key operations,
record reading and alerts are doubled; it is not authentication or hardware proof.
See the [fresh evidence and remaining work](../../../../../docs/reports/wifi-static-memory/README.md#fixed-peer-certificate-records).

## Fixed TLS derivation inputs

The TLS 1.2 pseudorandom function (PRF) copies a seed and label into PSA-owned
storage. `ssl_tls.c` alone routes its byte-input calls through `c3tout_prf_input`.
One 64-byte seed and one 22-byte label cover the selected handshake's random
pair (64 bytes), extended-master-secret hash (32/48 bytes), finished hash and
fixed labels. The longest label is `extended master secret` (22 bytes).
They coexist until the native PSA abort releases them, including error aborts.
The adapter retains input validation, copying, state transitions and cleanup;
it does not implement the PRF or change certificate verification.

Only `psa_crypto.c` binds its allocator to `c3tout_prf_alloc` and its free/wipe
calls to the existing exact-pointer release. Its matching `calloc(1, size)`
attempt consumes the scope even on exhaustion. An occupied/oversized selected
store returns NULL without heap fallback. Other allocations, including secret
copies and key material, retain the SDK allocator. Zero-length inputs allocate
nothing. Cleanup erases each entire fixed store before reuse.

Wi-Fi also uses PSA, so allocation selection requires the native task ID of the
serialized TLS owner. `c3bport_task` uses the kernel ID; the FreeRTOS shim handle
can be NULL for the application controller. Existing interrupt exclusion protects
scope publication, matching and consumption. The native PSA call and unrelated
heap allocations run outside exclusion. A competing task cannot consume or clear
the TLS scope. Fixed pointers remain private to that owner; this does not make
arbitrary simultaneous TLS owners supported.

The selected SDK assumes exclusive input buffers. A compile guard rejects its
copy-input mode, which would add another allocation before the intended copy.
Qualification rejects linked public PRF/exporter APIs with arbitrary seed/label
lengths. The current application uses none; this is not a new restriction on
its TLS handshake. TLS 1.2, record capacities and cipher admission are unchanged.

### Derivation storage C review

Reviewed `c3tout`, `c3bport` and `touttest` against the complete BARR-C checklist:
C99, paired headers, SDK-compatible PSA types, native ID returned as `int`,
initialized private state, unsigned sizes, bounded 64/22-byte clearing and no
new casts. The existing single-core exclusion restores the exact saved mask;
no SDK call, heap call or wait occurs while held. Only task identity is queried
before exclusion. The scope is cleared on all native returns. Fixed leases
outlive the wrapper and end at native cleanup; only the owner can receive them.
No new task, lock, ISR/MMIO access, deviation, dependency change or aggregate cap
increase. The module reservation uses existing application headroom.

Host tests check coexisting copies, exhaustion, bounds, zero-length inputs,
whole-store erasure, repeated reuse, failures and identical allocations from a
different task, with assertions and compiler warnings enabled. The compiled
[PSA input probe](../../../../../tools/target/README.md#tls-derivation-input-probe)
checks actual input/abort instructions. Hash support, native task identity,
interrupt exclusion and heap are doubled; PRF output, authentication, hardware
preemption and whole-connection peak memory are not qualified by these checks.

## Fixed lwIP records

Selected Wi-Fi services with static network queues replace `memp_malloc` and
`memp_free` only in the pinned lwIP `api_lib.c`, `api_msg.c` and `netbuf.c`.
These are the complete allocation/free sites for `MEMP_NETCONN` and
`MEMP_NETBUF` in the pinned SDK. `c3lrec` stores actual `struct netconn` and
`struct netbuf` objects. Capacity follows `MEMP_NUM_NETCONN` and `MEMP_NUM_NETBUF`:
six connection records and two datagram envelopes in the selected build.

Those are shared limits, including listeners, accepted connections and UDP
sockets; they do not promise six simultaneous TLS sessions. The old heap-backed
SDK pools did not enforce these record counts. Exhaustion now returns NULL
through the SDK's existing error/packet-drop paths. No heap fallback is allowed.
One MQTT TLS session remains configured after HTTPS removal. Queue capacity,
packet storage and TLS allocations can still independently limit operation.

SDK code retains initialization, protocol handling and cleanup. In particular,
it must release PCBs, packet buffers and mailboxes and exclude all other users
before returning a record. The adapter never frees pointees. A returned pointer
expires on release; a stale pointer cannot identify a later lease at the same
address. Unsupported kinds and NULL, foreign, interior or wrong-kind releases
preserve active leases. Vendor sources, revisions and unrelated pool paths stay
unchanged.

### lwIP record C review

Reviewed against the BARR-C checklist: C99, paired headers, minimal SDK includes,
native `memp_t` ABI, actual typed/aligned objects, initialized state and bounded
loops. No casts, pointer arithmetic, type punning or foreign-pointer dereferences
are used. Zeroing matches the selected target's null/zero representations.
Functions remain below 100 lines. Names, Boolean prefixes, const/static use,
braces, equality order, integer ranges and deterministic errors were reviewed.

The TCP/IP task and socket callers can share the pool. Each operation uses
`c3bport_enter`/`leave` to preserve the exact previous native interrupt mask on
the single-core C3. This covers selection, clearing and lease publication without
adding a mutex, task or lazy kernel allocation. Calls require task context with
cache enabled; the critical section contains no blocking SDK calls. It examines
at most six or two entries and clears at most one typed record. These bounds
are not measured interrupt latency, stack usage or hardware acceptance.

Host tests compile the actual adapter with typed SDK declarations doubled. They
check independent exhaustion, alignment, zeroing, isolation, invalid releases,
nested interrupt-mask restoration and 1,000 full reuse cycles with heap calls
intercepted. Assertions and warnings-as-errors stay enabled. They do not simulate
TCP/IP or prove multicore behavior. The real build uses pinned SDK types; its
qualification checks all three source substitutions, helper placement and
retention of SDK cleanup and unrelated `memp_malloc`/`memp_free` paths.

Run `python -B -m unittest discover -s tests/target -p test_lwip_records.py -v`
with GCC and objcopy available. See the resource assessment for the fresh target
build and budget result. TLS, packet payloads, protocol control blocks and other
lwIP pools still need bounded storage and live acceptance.
