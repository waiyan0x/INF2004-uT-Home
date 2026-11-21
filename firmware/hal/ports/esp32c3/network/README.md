# ESP32-C3 network adapters

This folder contains the SDK/kernel boundaries for the experimental network
compatibility layer. Portable queue, event and wait policy lives in
[`firmware/src/network`](../../../../src/network/README.md), which owns the
resource limits, build instructions, manual reviews and qualification evidence.

| Modules | Responsibility |
|---|---|
| `c3boot` | Bounded native startup pool for selected static-network builds |
| `c3lbox` | lwIP mailbox identities bound to the existing fixed queue payloads |
| `c3tls` | Fixed ESP-TLS context, error tracking and temporary hostname storage shared with TLS key payloads |
| `c3tout` | Fixed MQTT buffers, hostname, outer TLS/certificate records and TLS 1.2 PRF inputs |
| `c3lrec`, `c3pkt`, `c3rx`, `c3dhcp` | Fixed socket, TCP, UDP, ARP and TCP/IP message storage and Wi-Fi receive references |
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

For the one MQTT client, only the socket setup in `esp_tls.c` binds
`lwip_getaddrinfo` and `lwip_freeaddrinfo` to the numeric endpoint in
`c3mqtt`. ESP-TLS supplies a null service, a stream socket and an unspecified
or IPv4 family. The endpoint parses the broker literal with the same IPv4
parser used at admission. ESP-TLS sets the port and copies the socket address
before releasing it. Its TLS certificate name still comes from `common_name`.

**C review.** The C99 adapter keeps the SDK resolver signatures. Its fixed
result points at the generic socket address member of an aligned union. The
SDK reads the IPv4 member at the same address. Each successful lookup
initializes the entire IPv4 address and result, including a zero port and
null next pointer. A live result belongs to the serialized network owner.
A second borrow returns the SDK memory error and preserves the first
address. Only the matching release clears the lease. No task, lock or
coding-standard deviation is added.

Only `calloc(1, sizeof(esp_tls_t))` is admitted. Unsupported shapes and exhausted
stores return NULL; there is no general allocator or heap fallback. One
254-byte temporary name buffer covers the existing 253-character server-name
limit and numeric IPv4 address. The SDK copies/uses and frees that scratch name
before returning; `c3tout` below owns a separate persistent connection hostname.
The three stored MQTT topic buffers use the size of each format string and
the full 31-character node limit. This pays for the socket endpoint within the
original MQTT module reservation. The maximum-name test checks availability,
the transaction subscription and the result topic through the MQTT adapter.

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

The scoped bindings cover ESP-TLS context, name and numeric endpoint storage. The adapter below owns TLS
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
heap fallback. Inner certificates and keys still use SDK allocation; session
tickets are disabled by the approved policy below.

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

On the static Wi-Fi target, the single MQTT owner lends the 512-entry JSON view
workspace to the first four peer intermediates. Each typed certificate occupies
952 bytes on the pinned ESP32-C3 SDK. The USB configuration parser borrows the
whole workspace from open through close. A live view tree rejects a peer parse,
and a live intermediate rejects a new tree. USB transport buffers and the
configured trust anchor have separate lifetimes.

The boot configuration identifies a single provisioned DER trust anchor by its
encoding, pointer and full length. ESP-TLS borrows that exact flash object
through cleanup and reconnect. The format flag occupies padding after the
broker port. The generator sets it only for a single decoded certificate.
Other inputs, including a complete PEM bundle or a different caller's DER,
use the native copying parser. Peer-chain handling, hostname verification and
signature checks remain with mbedTLS. The DER bytes in flash remain valid until the end of the boot.

The no-copy peer parser requests a node before parsing each additional
certificate. Only its owning task can claim that allocation in `x509_crt.c`.
The SDK links, verifies, zeroes and releases its own chain, including failed
appends. Exact addresses identify shared nodes on release. The fifth intermediate returns the SDK allocation error
before allocating or modifying the chain. The application loop serializes USB
parsing and MQTT, and certificate verification completes synchronously. The
typed union preserves alignment, and the build checks its size against the
original JSON workspace. These ownership, bounds, failure and task-context
checks form the manual C review for this change.

For synchronous verification of a single ECDSA-signed leaf and one P-256
issuer, the same union can hold 32 equal slots of 24 RV32 words. The demo's
previous trace used at most 27 live buffers and 18 words per buffer. Keeping
one slot size avoids size classes and fits in the existing 4 KiB. The
application task checks that USB views and peer intermediates are free before
arming the loan. Entry with a CRL, verification callback or restart context
uses native allocation. The SDK decides trust and checks the hostname.

Only the verifier's native task uses the slots in the pinned crypto
`bignum.c`. Each slot is aligned for the SDK's 32-bit limb type. A count
above 24 or a full bitmap returns NULL. Both SDK release paths clear the
entire slot before reuse. The SDK's modular inverse asks for five vectors
of 32 bytes for the P-256 issuer. A separate 40-word aligned array in the
same union supplies this exact request. The verifier resets its occupancy
flag when it acquires the union from USB or the certificate parser. Both
release paths clear all 160 bytes. A second concurrent inverse request
returns NULL. Other allocation shapes and other tasks retain
their SDK allocator and matching release. The native P-256 verification
cleans up its temporary limbs synchronously before the wrapper returns.
The parser's bounded four-node count fits in one byte. The build checks
the allocator bindings on the crypto source and excludes the Wi-Fi
supplicant's separate file of the same name. These bounds, exact-pointer
releases, task exclusion and callback signatures were reviewed for BARR-C.

The first DER certificate can also borrow one typed `mbedtls_ecp_keypair`
inside the workspace. The configured DER trust anchor and first peer use this
path when USB views, intermediate nodes and other crypto operations are clear.
The single-leaf verifier uses the same object alongside its integer slots.
While parsing, the owning task borrows integer slots only inside the selected
uncompressed P-256 public-key import. The SDK releases the temporary keypair
and its coordinates before returning on success or failure. Each lease starts
with an empty slot bitmap. The import restores the verifier's previous access
on return. Modular inversion and point tables remain verifier-only. The SDK retains subject alternative names until certificate
cleanup. Manual review covered scope, integer sizes, task isolation, bounded
slot reuse, zeroization and cleanup on error. A busy workspace keeps the
existing trust-parser and verifier behavior. A live USB tree still rejects a
peer parse, as required by its existing contract.

For the fixed peer head, the first two extra ASN.1 sequence records from
`x509.c` share the retained peer digest's 32 bytes. The first name is already
embedded in the certificate. The synchronous leaf parser selects the single
owning task and certificate. Other callers, a live digest and further names
keep the SDK allocator. Each node has its own lease bit and native alignment.
A compile-time size check distinguishes the other name type allocated in
`x509.c`. Native ASN.1 traversal reads the next link before releasing each
node. The release adapter clears fixed nodes and forwards every other pointer.
A live node prevents the digest allocator from claiming the union. On the
pinned TLS 1.2 client the old digest comparison precedes peer parsing, and
the chain destructor precedes the replacement digest. Manual C review covered
those lifetimes, bounds, pointer identity, the unsigned mask and error cleanup.
The existing RAM cap and C policy apply.

After verification the TLS client saves the public key in the handshake.
Only the `ssl_tls.c` call to `mbedtls_pk_parse_subpubkey` borrows the
certificate workspace for this copy. On P-256 the SDK writes the encoded
point into its own key context, validates it through a temporary PSA key and
destroys the temporary key before returning. Releasing the workspace on
success or failure leaves the handshake key usable. A live USB tree or
intermediate chain keeps the existing native allocation path. Manual C
review covered the SDK's cursor and end-pointer ABI, key lifetime,
task ownership, error returns and return of the lease. No new static storage
or coding-standard deviation was introduced.

Only the TLS 1.2 client source binds the server signature check to
`c3peer_signature`. A synchronous P-256 key with SHA-256 or SHA-512 can
borrow the same workspace for the duration of native ECDSA verification.
Manual C review checked the pinned seven-argument ABI, pointer and length
forwarding, curve and digest selection, exclusion of restartable calls,
task ownership and release on both success and failure. The native verifier
owns the result and all signature checks. A busy workspace takes the SDK
allocation path. There is no added permanent buffer or change to the SDK.

Only the TLS 1.2 client in `ssl_tls.c` calls `c3peer_client_step`. Its
X25519 loan requires the client-key-exchange state, ECDHE-ECDSA or ECDHE-RSA,
and the negotiated 255-bit Montgomery keypair type. The synchronous SDK step
generates the private key, exports the public key, calculates the premaster and
destroys the key before sending the handshake record. PSA owns the private key handle. Its 32-byte payload shares the
temporary ESP-TLS hostname store with the AES-128-GCM traffic keys.
Decoded key objects and arithmetic scratch borrow
typed storage during the call. Shared-secret calculation needs two decoded
objects simultaneously. Other workspace owners retain the one-key limit.

The serialized client can borrow four 32-byte slots from the 254-byte temporary
hostname. A slot can hold one AES key or one X25519 private key. A one-byte
mask excludes key allocation while the hostname is live and excludes hostname
allocation until the last key is destroyed. A full pool returns an allocation
failure through PSA. Each exact-pointer release zeroes all 32 bytes. The SDK
retains a separate hostname for broker identity verification.

Manual C review checked the task-scoped PSA call bindings, native error paths,
unsigned slot masks, bounded offsets and zeroization before reuse. The
allocation scope is consumed once on the selecting task. A DER or unrelated
PSA allocation retains its own owner. All calls run in task context.

Manual C review checked the pinned client-step ABI and state preconditions,
typed union capacity, task exclusion, native return and error propagation,
and zeroization before the workspace is released. The compiled replay covers
a valid exchange, rejection of an all-zero peer and USB contention.

Only the pinned `ecp.c` receives the two-point table allocation and free
bindings. The verifier owns an aligned array of two SDK point records.
The compile-time guard requires window size two, disabled group caching
and synchronous arithmetic. In this configuration the SDK frees both
points and their coordinates before releasing the table. The exact-pointer
release clears the array. A second simultaneous loan fails and other
tasks, sizes and certificate paths retain the SDK allocator. The union
size check covers the typed array. Manual C review checked allocation factors,
alignment, ownership, error cleanup and interrupt scope. No kernel object
or coding-standard deviation was added.

Only the pinned `psa_crypto_ecp.c` gets the key allocation and release
bindings. The owner can request one exact SDK keypair. A second simultaneous
request returns NULL. Other tasks and allocation shapes use the native heap.
The synchronous SDK import and verify paths release the key's members before
returning the outer object. Exact-pointer release clears its full size.
The build checks the typed union fits the original 4 KiB. The task boundary,
alignment, error cleanup and workspace lifetimes were reviewed for BARR-C.

A separate 65-byte array inside the same union holds the serialized
uncompressed P-256 public key during a temporary PSA import. The pinned
public-key parser and signature verifier destroy this key before returning.
Only their imports of volatile public keys of the reviewed family, format
and length can select the array on the owning task. PSA validates the key,
copies the bytes and owns the slot until destruction or failed-import cleanup.
The one-shot allocation flag is consumed by one matching request. A second
selected allocation while the copy is live returns NULL. Both release paths
clear the full array. Other PSA operations retain their existing allocator,
including TLS key derivation. The task, size, pointer identity and
synchronous release boundaries form the manual C review for this change.

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
Additional names beyond the two fixed records and other unhandled SDK
allocations retain their native lifetime. The build rejects full peer-certificate retention,
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

The TLS 1.2 pseudorandom function (PRF) keeps three inputs until PSA completes
or aborts a derivation. Only `ssl_tls.c` routes byte inputs through
`c3tout_prf_input`, key-ID inputs through `c3tout_prf_key` and imports
through `c3tout_prf_import`.
The 64-byte seed fits the random pair and the 32/48-byte handshake hashes.
The longest label is `extended master secret` (22 bytes). For a nonempty secret,
the client imports a volatile derive key. PSA checks that key's permissions,
locks its slot and copies the material into the PRF operation.
The imported key stays live through PRF output, so the two copies have
distinct stores. Each fits the larger of a 48-byte TLS master secret and
`MBEDTLS_PREMASTER_SIZE`, which is 66 bytes for the selected curves.

Only `psa_crypto.c` binds its allocator to `c3tout_prf_alloc` and its free/wipe
calls to the existing exact-pointer release. Its matching `calloc(1, size)`
attempt consumes the scope even on exhaustion. PSA supplies the copy length
after the key lookup. The pinned volatile key-input path makes one allocation
in this translation unit. An occupied or oversized store returns NULL.
Only a volatile `PSA_KEY_TYPE_DERIVE` import with derive-only usage and a
TLS 1.2 PRF algorithm selects the imported-key store. The active native task
ID and exact input length scope its allocation. The SDK releases the store
when it destroys the key or unwinds a failed import. Both paths erase the full
66 bytes. Record cipher, MAC and other PSA callers use their native allocations.
PSA's shared key tables have independent lifetimes.

Wi-Fi also uses PSA, so allocation selection requires the native task ID of the
serialized TLS owner. `c3bport_task` uses the kernel ID; the FreeRTOS shim handle
can be NULL for the application controller. Existing interrupt exclusion protects
scope publication, matching and consumption. The native PSA call and unrelated
heap allocations run outside exclusion. A competing task cannot consume or clear
the TLS scope. Fixed pointers remain private to that owner; this does not make
arbitrary simultaneous TLS owners supported.

The selected SDK assumes exclusive input buffers and dynamically backed PSA
key slots. Compile guards enforce both assumptions at this allocator boundary.
Qualification rejects linked public PRF/exporter APIs with arbitrary seed/label
lengths. The current application uses none; this is not a new restriction on
its TLS handshake. TLS 1.2, record capacities and cipher admission are unchanged.

### Derivation storage C review

Reviewed `c3tout`, `c3bport` and `touttest` against the complete BARR-C checklist:
C99, paired headers, SDK-compatible PSA types, native ID returned as `int`,
initialized private state, unsigned sizes, bounded 64/22/66-byte clearing and no
new casts. The existing single-core exclusion restores the exact saved mask;
no SDK call, heap call or wait occurs while held. Only task identity is queried
before exclusion. Allocation consumes its owner's one-shot scope. Native returns
also clear a scope when PSA rejects an input before allocating. Fixed leases
outlive the wrapper and end at native cleanup; only the owner can receive them.
No new task, lock, ISR/MMIO access, deviation, dependency change or aggregate cap
increase. The module reservation uses existing application headroom.

Host tests check coexisting copies, exhaustion, bounds, zero-length inputs,
whole-store erasure, repeated reuse, failures and identical allocations from a
different task, with assertions and compiler warnings enabled. The compiled
[PSA input probe](../../../../../tools/target/README.md#tls-derivation-input-probe)
checks actual input/abort instructions. The companion key-ID probe executes
the compiled client from key import through output and destruction. It checks
both hashes against reference output, observes populated key bytes entering
failed-import cleanup and verifies erasure and retry. Manual review covered
the SDK attribute and key-ID types, full-slot bounds, distinct overlapping
lifetimes, owner-only allocation and standard success/error cleanup. The
adapter uses C99 and the existing interrupt exclusion. The probe doubles
HMAC, heap, native task and mutex services. Broker authentication, physical
preemption and whole-connection peak memory require separate evidence.

## MQTT session-ticket policy

On 17 November 2026 the user approved disabling TLS session tickets. The app
does not save/reuse a TLS session across reconnects, so it continues to perform
a full verified handshake. Encryption, peer authentication, certificate checks,
TLS 1.2 renegotiation and record capacities are unchanged. A peer must support
an ordinary full handshake without session tickets.

Owned network defaults disable both mbedTLS ticket options. CMake rejects either
mbedTLS or ESP-TLS ticket storage being enabled; `c3tout` also rejects the native
ticket feature macro. Qualification checks the compiled SDK options and rejects
linked ticket helpers; absence of symbols alone cannot exclude inlined code. This removes
the variable-length ticket allocation instead of introducing a smaller admission
limit or a heap exception. No pinned SDK source is modified.

The final ELF has no session/handshake ticket fields. A native session is now
128 bytes, down from 144; the two session stores save 32 bytes. Transforms remain
84 bytes and the handshake remains 2,448 bytes. State probes and host doubles
use the reviewed new sizes. This does not close inner certificate/key, secret
or TCP/IP allocations, nor establish runtime peaks or hardware acceptance.

Manual BARR-C review covers the additional feature guard and the host fixture's
session-size change: C99 and existing prefixes/types remain; bounds match the
measured layout, and no runtime algorithm, cast, lifetime, synchronization,
task/ISR contract or exception changes. The host-only `tlsconf.h` uses ordinary
preprocessing to select the same ticket policy without altering vendor headers.
See the [verification and build identity](../../../../../docs/reports/wifi-static-memory/README.md#mqtt-session-tickets-disabled).

## Fixed lwIP records

The Wi-Fi station uses fixed arrays of native SDK objects. The build redirects
both allocation and release in `api_lib.c`, `api_msg.c`, `netbuf.c`, `tcpip.c`,
`tcp.c`, `tcp_out.c`, `udp.c`, `etharp.c`, `igmp.c` and `timeouts.c`. The
ARP queue covers two unresolved neighbours. Timer and multicast membership
counts cover the single-station workload. The other capacities use the
configured lwIP counts.

| Record | Slots | Use |
|---|---:|---|
| NETCONN | 6 | Socket connection handles |
| NETBUF | 2 | Datagram envelopes |
| TCP_PCB | 3 | Active and closing TCP connections share these slots |
| TCP_PCB_LISTEN | 1 | Native listener record |
| TCP_SEG | 16 | Queued transmit and receive segment metadata |
| TCPIP_MSG_INPKT | 8 | Input frames awaiting the TCP/IP thread |
| TCPIP_MSG_API | 8 | Callback messages awaiting the same thread |
| UDP_PCB | 2 | DHCP client and one concurrent UDP endpoint |
| ARP_QUEUE | 7 | Three waiting packets per neighbour and one temporary append |
| IGMP_GROUP | 1 | All-systems membership on the station interface |
| SYS_TIMEOUT | 12 | Two cyclic timers, seven on-demand timer owners and three spare records |

One MQTT client can connect while earlier connections finish closing.
The SDK retains its native TIME_WAIT reclamation on connection exhaustion.
When segment allocation fails, lwIP returns `ERR_MEM` to the TCP writer
and frees any packet buffers acquired for the unsuccessful write. The SDK
owns the packet data, mailbox and protocol teardown. It must finish using a
record before returning it to the adapter. A later lease can reuse its address.

The pinned IPv4 image schedules ARP maintenance and coarse DHCP periodically.
TCP, fine DHCP, DNS, IGMP, the DHCP address-conflict probe, gratuitous ARP and
the lost-IP notification can also schedule callbacks. lwIP removes and frees
an expired record before invoking its handler, allowing recurring callbacks to
reschedule even with every slot occupied. Native scheduling asserts on
exhaustion in this build. Changes to interfaces, protocols or timer owners
require another capacity check.

[Timer capacity and verification](../../../../../docs/reports/wifi-static-memory/README.md#fixed-tcpip-timeout-records)

### lwIP record C review

Each array uses the actual SDK element type and alignment. Its layout entry
holds the address of the complete array. Walking that object's byte
representation by the element size yields a native element boundary at every
index below the count. The immutable layout uses the same compile-time count
as the array and lease flags. The offset fits `size_t`. Release compares exact
addresses only within the requested pool. Zeroing matches the C3 SDK's null
and zero representations. The C99 code uses paired headers, private state,
bounded loops and the existing native `memp_t` ABI.

Allocation, release and optional lwIP statistics updates preserve the previous
interrupt mask on the single-core C3. The scan visits at most 16 slots under
the guard and clears at most one object. The implementation calls no blocking
operation and creates no kernel object. The caller must exclude outstanding
users before release. Interrupt latency remains unmeasured.

The multicast pool uses one native `struct igmp_group` and a Boolean occupancy
flag. Both follow the same alignment, zeroing, interrupt exclusion and exact
pointer release rules as the other records. The SDK owns the list and the
reference count. Its stop path unlinks the list and saves the next pointer
before releasing each entry. A second interface or distinct group receives
`ERR_MEM` while the station holds the slot. Home Assistant discovery runs
on the MQTT host, so the demo requires no further group on the ESP.
[Membership checks](../../../../../docs/reports/wifi-static-memory/README.md#fixed-station-multicast-membership)

The two message arrays use the native `struct tcpip_msg` layout. Each
record uses 16 bytes on RV32 plus one byte of occupancy. A failed post
returns the message to its own pool. For input frames and one-shot callbacks,
the TCP/IP thread releases the message after dispatch. Reusable callback
messages follow the SDK's explicit deletion rule. The caller remains
responsible for the lifetime of callback arguments.

The host suite fills all nine pools, rejects exhaustion, checks alignment and
zeroing and repeats release and reuse 1,000 times with heap calls trapped.
Run it with `python -B -m unittest discover -s tests/target -p test_lwip_records.py -v`.
The target build checks the eight record source bindings. The linked-instruction
test exercises both immediate failure and partial-write rollback.
[TCP record measurements](../../../../../docs/reports/wifi-static-memory/README.md#fixed-tcp-records)
[Receive and dispatch measurements](../../../../../docs/reports/wifi-static-memory/README.md#fixed-tcpip-message-handoff)

## Outgoing TCP packets

The station's copied TCP transmit path uses five aligned packet slots. Four
can carry the 2,440-byte send window. The core may use the last free slot only
while splitting its unsent head to fit a smaller peer receive window. Two
independent IP control slots can carry the TCP header, up to 40 option bytes
and the one-byte zero-window probe while all transport slots are occupied.
The native SDK still owns queuing, retransmission, reference counting and
final release. The receive path has a separate [frame pool](#wi-fi-receive-records).

The pinned single-buffer build always copies TCP writes. The only redirected
packet allocator is in `tcp_out.c`. Only the call in `tcp.c` to the native
split routine permits an extra transport lease. Both are serialized by lwIP's
TCP core. Final reference release may occur in another context. Allocation
and release use the existing interrupt exclusion and create no kernel objects.
Each slot contains a real custom pbuf followed by four-byte-aligned storage
for the selected layer's full headers and payload. lwIP skips heap trimming
and invokes the release callback for a custom buffer. The callback converts
the pbuf back to its enclosing custom header, whose first member is the pbuf.
It marks the slot free only after the last reference. All array scans are
bounded. Oversize requests and exhausted pools return NULL to lwIP. This
C99 boundary adds no coding-standard deviation.

## ARP frames and UDP endpoints

The station reserves one 64-byte slot for a 28-byte ARP request or reply.
It has a native custom pbuf and aligned link-header space for the 14-byte
Ethernet header. A retained transmission can hold the slot until its final
reference is released. During that time another ARP frame returns `ERR_MEM`.
TCP transport, TCP control and incoming Wi-Fi frames use separate storage.

The pinned SDK can queue three packets per unresolved IPv4 neighbour. It
temporarily appends a fourth before rejecting it. Seven native queue records
cover two full neighbours and this temporary append. Each entry holds a
packet reference. After resolution the native ARP code sends that neighbour's
packets in arrival order and releases them. Interface teardown drains the
remaining entries. A volatile packet chain takes the independent frame slot described below.

**C review.** The station uses the SDK's actual UDP and ARP queue element types,
their native alignment and zero initialization. Record counts, lease flags and
bounded scans share the existing interrupt guard. The ARP slot uses the fixed
packet release callback and reserves aligned storage for the selected link
layer and the exact SDK frame size. Its 44-byte backing length fits the
16-bit SDK argument. The ABI, device register access and task/ISR contracts
are unchanged. The two reviewed C99 adapters require no new deviation.

[ARP and UDP measurements](../../../../../docs/reports/wifi-static-memory/README.md#fixed-arp-and-udp-storage)

## ARP and Wi-Fi packet copies

The one-station build reserves one contiguous 1,536-byte slot. An unresolved
ARP entry can hold an independent copy of an IPv4 packet up to the station
MTU of 1,500 bytes. Wi-Fi output uses the same slot when it must join a
chain into one untagged Ethernet frame (up to 1,514 bytes). A resolved
ARP clone already consists of one pbuf and can transmit from that slot.
A second copy while the slot is held returns the native memory error.
Ordinary single-buffer sends and ARP request/reply frames use separate storage.

ICMP uses this frame to copy an echo request or build an error reply. IPv4
fragmentation leases it once per outgoing fragment, including an eight-byte
tail. The fragment payload limit is 1,480 bytes with room for the 20-byte
IPv4 header and Ethernet header. A retained transmission holds the slot. If
a later fragment cannot acquire it, lwIP returns `ERR_MEM` to its caller.
An earlier fragment may already have been sent. ICMP drops a reply when the
slot is occupied. The UDP header remains independent.

**C review.** The packet adapter keeps the native C99 allocator signature,
checked unsigned lengths and aligned static storage. It enters the existing
interrupt guard only around reservation and restores the saved state on
every exit from the guard. The native pbuf keeps its last-reference release
callback. Both the callback's lifetime and the existing queue capacities are
preserved. The change uses no additional RAM, casts or deviations.

UDP can prepend its header to a datagram with headroom. For a borrowed
payload it uses a separate 64-byte slot, including the eight UDP header bytes,
native pbuf and network headroom. Each slot stays leased through the final
pbuf release. The reserve operation is protected by the existing interrupt
guard. Copying the payload runs outside the guard, after the caller acquires
the slot. The native pbuf header remains the first member and its backing
words have four-byte alignment. Length bounds precede conversion to the
16-bit native size. These changes add no BARR-C deviation.

The network queue adapter stores one native read-semaphore or mutex handle
per slot and a second notification handle for each of the eight queues.
The non-null opaque key identifies a live slot under the same guard used
for lookup and deletion. The eight queues, sixteen semaphores, thirty-two
mutexes and 8,192 queue payload bytes retain their previous capacities.

[Packet-copy measurements](../../../../../docs/reports/wifi-static-memory/README.md#fixed-arp-and-wi-fi-frame-copies)

## DHCP client and outgoing messages

The one-station build reserves one native DHCP client and two outgoing
datagrams with aligned transport headroom. DHCP start and cleanup own the
client on the serialized lwIP core. A unicast renewal may remain queued for ARP
while the station sends another request. Each datagram stays reserved until
its final pbuf reference is released. A third simultaneous request receives
the native allocation error and uses the client's retry schedule. Timer
records and packets copied by other layers have separate ownership.

[DHCP memory checks](../../../../../docs/reports/wifi-static-memory/README.md#fixed-dhcp-client-and-messages)

## Wi-Fi receive records

The zero-copy station build reserves one native custom pbuf record per
configured dynamic Wi-Fi receive buffer. Eight records require 224 bytes on
the ESP32-C3. A record points at the driver's payload and retains the network
interface and the original buffer token. Only the station entry in
`esp_netif/lwip/netif/wlanif.c` uses this allocator.

The input caller owns the driver token until allocation succeeds. A full
pool leaves the token with that caller, which returns it to the driver.
After admission, lwIP owns the packet reference. If its input queue refuses
the packet, normal pbuf cleanup releases it. Extra references delay the
driver return until the last release.

**C review.** The SDK boundary uses its C99 prototype and a typed, initially
zeroed array. The bounded scan and ownership updates run under the port's
interrupt guard. Each slot holds a real custom header whose first member
is the native pbuf. These initial members permit the callback to recover
the enclosing slot with its original alignment. It copies the interface and
token and clears the lease before invoking the driver outside the guard.
Lengths above the SDK's 16-bit field are refused before conversion. A raw
pbuf adds zero header offset, preserving the driver's payload pointer.
No new kernel object or coding-standard deviation is required.

[Receive tests and measurements](../../../../../docs/reports/wifi-static-memory/README.md#fixed-wi-fi-receive-records)
