# Configuration transfer and storage core

Checkpoint 5a implements the portable framing, storage and request state machine.
It performs no automatic board I/O. Node schema 2.0.0 remains the deployment
document; neither raw C structures nor an unchecked host plan are accepted as a
replacement contract. JSON/schema validation and target binding are the next
checkpoint, followed by the USB pump and physical storage adapter.

## Small modules and ownership

| Module | Responsibility |
|---|---|
| `cfgbytes` | Explicit little-endian uint32 fields and CRC-32/ISO-HDLC |
| `cfgwire` | One bounded byte-stream request with a whole-frame deadline |
| `cfgstore` | Two independent slots, readback, commit marker and recovery |
| `cfgctrl` | Validation/activation/save ordering, request identity and status |
| `tools.config_transfer.protocol` | Matching Python request encoder/reply decoder |

One long-lived controller task owns all calls and callback execution. Contexts
must be zero-initialized and live at least as long as their borrowers. Reentry is
rejected; the Boolean guards are not locks and do not make concurrent tasks safe.
Use static/application storage, not a peripheral worker's small stack. The core
does not allocate heap memory, create tasks, choose pins or name drivers.

`cfgwire_request` borrows the complete payload until `cfgwire_reset`. Call
`cfgctrl_handle` before resetting the receiver; request bytes must remain stable
through the synchronous call. Request/reply buffers must not overlap controller
storage or the store scratch buffer. Copy the six reply fields through
`cfgwire_reply` before reusing the response buffer. A physical transport must
retain unsent response bytes across partial writes.

## Wire version 1

Every frame has a 24-byte header, followed by exactly `length` payload bytes.
Multibyte integers are unsigned little-endian, independent of native alignment.

| Offset | Bytes | Meaning |
|---|---|---|
| 0 | 4 | ASCII `UTH1` |
| 4 | 1 | Version = 1 |
| 5 | 1 | Command, or command OR 0x80 for a response |
| 6 | 2 | Reserved = zero |
| 8 | 4 | Nonzero request ID |
| 12 | 4 | Requested revision, echoed in the response |
| 16 | 4 | Payload length |
| 20 | 4 | CRC over bytes 0..19 followed by payload; CRC field excluded |

CRC parameters: reflected polynomial 0xEDB88320, initial state 0xFFFFFFFF,
final complement. `123456789` produces 0xCBF43926. CRC detects accidental damage;
it is not authentication, encryption or an adversarial integrity guarantee.

| Command | Value | Revision | Request payload |
|---|---|---|---|
| APPLY | 1 | Nonzero | 1..8192 exact profile bytes |
| QUERY | 2 | Zero | Empty |
| SAVE | 3 | Current active revision | Empty; retry persistence only |
| RECOVER | 4 | Zero | Empty; release retained resources to empty state |

The receiver scans for magic and rejects unsupported headers and oversized
lengths before payload writes. A complete frame stays retained until reset;
additional bytes cannot silently overwrite it. A future transport pump must
hold unread bytes, or process/reset each completed request before feeding more.
Malformed frames do not reach a validation or activation callback.

A **2000 ms total deadline** starts with the first magic byte. Progress never
extends it. Call `cfgwire_expire` even when no bytes arrive. Timeout, a backwards
clock, bad CRC or bad header discards the incomplete frame; a sender must retry
the entire frame. Noise is not acknowledged. This core sets no baud rate and
does not claim USB timing qualification.

Controller/probe replies are 48 bytes: the same header with response bit set, length 24,
then six uint32 fields: **status, active revision, saved revision, active CRC,
saved CRC, flags**. Payload CRCs cover the original profile bytes, so bytewise
changes such as whitespace require a new revision. Revision zero means absent
or unknown, distinguished by the flags. The Python decoder requires matching
command, request ID and requested revision; it rejects extension bytes and CRC
errors instead of silently accepting a response for another operation.

| Status | Value | Meaning |
|---|---|---|
| OK | 0 | Query completed |
| SAVED | 1 | Requested active bytes are durably saved |
| UNSAVED | 2 | Activation succeeded, save failed or its outcome is uncertain |
| REJECTED | 3 | Validation failed, activation rejected/restored, or invalid state |
| RECOVERY | 4 | Resources/state require explicit recovery |
| STALE | 5 | Older request ID or configuration revision |
| CONFLICT | 6 | Reused ID/revision with different command or bytes |
| STORAGE | 7 | Persistent state could not be established before activation/query |
| EMPTY | 8 | Recovery released resources, or boot found no valid saved record |

Flags 1/2/4/8 record the request milestones **received/validated/activated/saved**.
Flags 16/32/64 mean **durable state known/recovery required/active configuration
known**. One final reply carries the reached milestones; this is not a stream of
intermediate acknowledgements. A configure revision being active says nothing
about current sensor health; health/telemetry remains separate.

## Admission and activation boundary

`cfgctrl_init` requires all three application callbacks and a storage object;
there is no default accepting validator. It only borrows them, without I/O.
The application must implement:

1. `validate`: independently parse/admit the entire node against the compiled
   catalogue, actual backends, wiring, queue routes and resource limits. Build a
   candidate in application-owned storage without changing hardware/active state.
2. `activate`: consume that candidate through the lifecycle owner. Return 0 only
   for a successfully activated whole plan; return 1 only for rejection with the
   previous plan fully restored. Any other result means uncertain state/recovery.
3. `recover`: return 0 only after every retained resource is released and the
   runtime is empty. This must bridge the existing activation manager, not reset
   metadata while tasks or hardware remain owned.

The existing `activate` status codes therefore need an explicit bridge; they
must not be passed through numerically. No such production bridge or parser is
installed by this checkpoint. Test callbacks are deliberately test-only.

APPLY first establishes durable state, rejects stale/conflicting revisions,
validates, activates, then saves. Rejection leaves active/saved state unchanged.
An uncertain activation clears the claim that any active revision is known and
blocks APPLY/SAVE until recovery succeeds. QUERY remains available. RECOVER
does not reapply stored settings or silently resume outputs.

After successful activation, a save failure leaves the new active revision in
RAM and reports UNSAVED with unknown durability. It does not claim rollback.
QUERY performs a synchronized read to rediscover durable state; SAVE with a new
request ID retries only the current active bytes, without repeating activation.

`cfgctrl_restore` may be attempted once before handling requests, and revalidates
saved bytes against the current firmware before activation. Stored CRC/version
checks alone cannot authorize execution. Empty or rejected storage leaves the
control path available. Boot restore does not rewrite flash. A fresh boot can
receive the already active revision and identical bytes without reactivating it.

## Request identity

IDs increase strictly within one controller boot. The most recent request is
cached with exact command, revision and payload bytes, including failures. An
identical retry returns that exact reply with no callback or storage operation.
The same ID with different content conflicts; older IDs are stale. QUERY and
SAVE use new IDs. ID exhaustion requires a new controller boot; no wraparound
ordering is implemented. Revisions also never wrap.

Reconnect within one boot preserves this history. Reboot clears the request
cache; durable revision/content checks still prevent an older revision from
being applied. This is not a persistent exactly-once transaction log. The host
client must track IDs and handle a stale response; a serial reconnect/boot
handshake and automatic retry policy are deliberately left to the transport
integration. Callers must not replace a rejected cached request's bytes and
reuse its ID.

## Two-slot record format and failure boundary

The backend provides exact reads, bounded writes, independently erasable slots
and a durability `sync`. Each slot needs at least **8224 bytes**; actual flash
must round each slot up to its erase geometry and isolate it from every other
partition. No flash address or existing NVS partition is selected here.

| Offset | Bytes | Meaning |
|---|---|---|
| 0 | 4 | ASCII `UTHC` |
| 4 | 4 | Record format = 1 |
| 8 | 4 | Nonzero monotonically increasing revision |
| 12 | 4 | Payload size, 1..8192 |
| 16 | 4 | Payload CRC |
| 20 | 4 | CRC over the first 20 header bytes |
| 24 | 4 | Commit marker 0xC35A3CC3, written last |
| 28 | 4 | Reserved erased value 0xFFFFFFFF |
| 32 | length | Exact admitted profile bytes, without an added terminator |

A save syncs existing operations, finds the newest valid record, and erases only
the inactive slot. It writes header/payload, syncs, reads both back and compares
them byte-for-byte, then writes/syncs the separate commit marker. Final readback
checks the committed record. Identical revision/content retries perform no
erase/write; lower or conflicting revisions fail. Equal committed revisions in
both slots are treated as a storage conflict, not arbitrarily selected.

Loading syncs pending work and checks header bounds, both CRCs and the marker
before copying output. Partial/corrupt records are ignored in favor of the other
valid record; if neither is valid, EMPTY is explicit. A read/sync failure returns
an error rather than assuming an unreadable slot is older. Failure after the
commit may leave the new record durable despite an error response; QUERY/SAVE
resolve this without repeating activation.

These guarantees depend on the backend's slot isolation and durability contract.
Host tests model immediate NOR-style writes and injected failures/torn prefixes;
they do not prove a particular flash controller, cache or power-loss behavior.
The actual target adapter and power-interruption acceptance remain outstanding.

## Verification and manual C review

`tests/target/cfg_main.c` checks bounds, CRC vectors, frame timeout/noise,
maximum payload, stale/conflicting IDs, exact retries, no save after rejection,
reentrant callbacks, active/saved divergence, recovery and boot revalidation.
It injects failure at every storage operation and byte prefixes 0..32, covering
every prefix of its test header/payload/marker writes and partial erases. After
each simulated interruption, loading returns the intact previous or complete
new record; the previously current slot remains byte-for-byte unchanged.

`test_config_transfer.py` independently encodes real example-profile frames in
Python and decodes C replies, including corruption, truncation, trailing bytes,
command/ID mismatch and unsigned bounds. The C fixture uses C99 with enabled
warnings treated as errors and refuses NDEBUG. All four existing regression
suites remain required. The real target compiles the same four modules as C99;
nine added no-I/O board checks give **552 expected checks**, not a board capture.

Manual BARR-C review covers paired modules/prefixes, fixed-width arithmetic,
unsigned shifts, narrowing only after bounds checks, byte serialization,
borrowed lifetimes, stable callback input, reentry, bounded loops, zeroed metadata
and preservation of active/old saved data on failure. No raw C struct is sent or
stored. Scratch/input aliasing is prohibited. Every backend operation and
activation result is checked. Existing C11/GNU99 boundaries, dependency pins,
policy, formatter, deviations and approval are unchanged.

See [fresh software/build evidence](../../qualification/esp32c3-config-core-20260928.json).
No board was flashed, reset or accessed. Target capabilities remain empty.
Next: bounded managed-cJSON parsing, independent semantic/resource admission,
and concrete driver/activation binding, then the real USB/storage adapters.
These need no new teammate specification; wiring and physical tests remain
reserved for final acceptance. AI remains last.

## Measured C3 context sizes

The final linked ELF's DWARF records `cfgwire_t` = 8,256 bytes,
`cfgstore_t` = 8,208 bytes and `cfgctrl_t` = 16,464
bytes: **32,928 bytes** for one of each. The default board fixture
does not allocate these instances. Allocate application-owned static storage and
account for it once when integrating the controller. This total excludes JSON
parser/candidate storage, backend buffers, task stacks and transient stack use;
it does not establish remaining RAM for AI or prove stack headroom.

## Checkpoint 5b implementation update

The required application callbacks now have a concrete sensor implementation
in cfgbind. See [admission and binding](ADMISSION.md). activate_clear supplies
safe-empty RECOVER. USB/storage backends remain pending. Wire/storage layouts
and retry semantics from checkpoint 5a are unchanged.

## Checkpoint 6a observation extension

Read-only command 6 has a separate versioned, variable-length payload; it bypasses
the controller's sequence and retry cache. Controller semantics above are
unchanged. See [exact framing and observation layout](../services/TELEMETRY.md).
