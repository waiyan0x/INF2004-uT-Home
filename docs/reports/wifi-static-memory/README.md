# Wi-Fi static memory and resource assessment

## Current network resource assessment

**The transaction image passes fixed-linker budgets but does not satisfy the
whole-system no-application-heap requirement yet.** On 17 November 2026 the user
authorized removal of the ESP HTTPS service, replacing the earlier requirement
to retain it alongside MQTT. The maintained image now has one MQTT TLS client;
full-profile setup/recovery uses USB. The host Desk and Home Assistant web
interfaces remain available. No Wi-Fi-driver exception automatically exempts
TLS, TCP/IP or ESP-TLS contexts.

### Imported TLS derivation key

The MQTT TLS 1.2 client reserves a separate 66-byte store for the volatile
key it imports before deriving handshake material. PSA holds that key until
the derivation finishes. Destruction and failed-import cleanup erase all 66
bytes. The allocator also reserves one byte to track whether the store is in
use. Other key types and policies retain the SDK's allocation behavior.

The compiled client matched 80 reference derivations and two known answers
across SHA-256 and SHA-384. The preceding image made one heap request per warm
derivation in the tested path. The new image made zero. A deliberately
inconsistent key length exercised the SDK's failure path after it had copied
the bytes. The probe observed those bytes entering cleanup, verified their
erasure and successfully retried. HMAC and platform services were doubled.
[Compiled probe instructions](../../../tools/target/README.md#tls-derivation-input-probe)

| Shared PSA key tables in the compiled probe | Heap request | Lifetime |
|---|---:|---|
| Persistent and built-in key cache | 1,408 bytes | Created at key-store initialization |
| First volatile key-slot table | 704 bytes | Created on first import, reused by later derivations |

Both tables remained allocated through the repeated derivations and were
released at key-store shutdown. A failed first allocation of the volatile
table returned an error and the next import succeeded. Inner certificate,
crypto and TCP/IP storage and whole-system runtime memory measurements remain
open.

An RV32 object comparison measured the SDK's native fixed table at 40 bytes
per slot plus four bytes for the aligned initialization flag. The current
dynamic table manager occupies 184 fixed bytes. Replacing it would add 1,020
bytes for 30 slots, 1,060 for 31, or 1,100 for 32. The linked SDK/kernel group
has 1,042 bytes free. If its other usage holds, 30 slots leave 22 bytes and
31 slots exceed the group budget by 18 bytes. The
[30-slot diagnostic build](#offline-fixed-table-build) measures the full-image cost.

To reproduce the comparison, use the slot-management entry in the qualified
build's `compile_commands.json`. Keep the ESP user configuration, undefine
`MBEDTLS_PSA_KEY_STORE_DYNAMIC` after it, and compile one object per
`MBEDTLS_PSA_KEY_SLOT_COUNT`. Read the size of `global_data` with the target
`nm -S`. This comparison measures table metadata only.

On 19 November the ESP32-C3 running firmware `2b3f703` obtained a Wi-Fi
address on its normal profile and completed MQTT over verified TLS on two
separate boots. Each control returned a 96-byte transaction status reply.
The complete original flash and saved profile were verified after both runs.
A preliminary trace counted 74 reservations and 74 wipes, with a peak of one
occupied slot while the station was waiting for an address. A subsequent trace covered association, authenticated
MQTT status and a broker restart on one boot.

| Board phase | Reservations | Wipes | Peak occupied slots | Occupied at phase end |
|---|---:|---:|---:|---:|
| Wi-Fi association with the broker offline | 43 | 43 | 1 | 0 |
| Broker startup through the first verified MQTT status | 13 | 11 | 3 | 2 |
| Broker restart through the next verified MQTT status | 13 | 13 | 3 | 2 |

Both traced status replies were 96 bytes and carried the same ESP boot ID and
generation. All 136 events reconciled from an empty key store. A separate
status check after debugger detachment timed out. The broker recorded nine
distinct requests without matching replies. Its cause remains open. The
original flash and profile were verified after restoration. The observed
peak was three slots for this workload. Wi-Fi rekey and TLS renegotiation
still need overlap measurements to select a production limit.
[USB debugger procedure](../../../tools/target/README.md#measuring-shared-psa-key-slots)

| Fixed memory on this image | Linked bytes | Budget | Headroom |
|---|---:|---:|---:|
| Application | 143,319 | 147,456 | 4,137 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,254 | 55,296 | 1,042 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 272,458 | 278,528 | 6,070 |
| RTC, separate from DRAM | 60 | 128 | 68 |

`c3tout` uses 25,532 of its 25,536 reserved bytes. The 102 affected host
tests, eight compiled probes, 334-file BARR-C gate, manual review and target
build passed. The 822,048-byte application's SHA-256 is
`b2e956a199a4f1dc7d7367059acf030d767c59b04e5aad4c894d8d0e020ccdd6`.

#### Offline fixed-table build

An isolated ESP32-C3 Wi-Fi/MQTT build linked the native PSA store with 30
slots. The SDK/kernel group grew by 1,024 bytes to 55,278 of 55,296 bytes,
leaving **18 bytes**. Total fixed DRAM reached 273,482 of 278,528 bytes,
leaving 5,046. Application storage, task pool, RAM code and RTC usage matched
the baseline above. The 821,616-byte diagnostic application's SHA-256 is
`9b1d8267d5f5784d3b74cbd832a7c3ae0b60c45d78431bdd1f274956a6587f33`.
The production build still uses the dynamic table.

To reproduce the comparison, use an isolated copy of the Wi-Fi/MQTT build.
Copy the pinned TF-PSA-Crypto `include/psa/crypto_config.h`, disable
`MBEDTLS_PSA_KEY_STORE_DYNAMIC` there and set `MBEDTLS_PSA_KEY_SLOT_COUNT`
to 30. Before importing the ESP-IDF project, set `TF_PSA_CRYPTO_CONFIG_FILE`
in the CMake cache and as a project-wide compile definition to the quoted
absolute path of that copy. Keep the ESP user configuration and the existing
transaction memory budget. All 1,050 application-build C/C++ compiler commands
selected the comparison header. The pinned SDK and kernel revisions,
`sdkconfig` and the other 254 staged project inputs matched the baseline.

The RV32 emulator recorded zero table allocations at startup and first key
derivation on this image. The dynamic baseline requested 1,408 and 704 bytes
at those points. Both images matched 80 SHA-256/SHA-384 derivations and two
known answers. With 30 distinct native volatile keys held, the 31st import
returned `PSA_ERROR_INSUFFICIENT_MEMORY` and an empty handle. The TLS PRF
returned its SDK error and left its output untouched. After releasing one
slot, both SHA-256 and SHA-384 derivations succeeded and all 29 remaining
keys retained their contents until destruction. The native test keys still
needed heap storage for their material. The probe supplies HMAC, heap, ROM
byte operations and task services. [Probe instructions](../../../tools/target/README.md#tls-derivation-input-probe)

#### Authenticated renegotiation on the laptop

A host build of the pinned TLS client used the same 30-slot base configuration,
ticket policy and 16,384/4,096-byte record limits. Its debugger counted every
occupied slot after each successful reservation.

| TLS 1.2 phase | Reservations | Peak occupied slots |
|---|---:|---:|
| Initial authenticated handshake | 11 | 3 |
| Renegotiation requested by the client | 10 | 5 |
| Renegotiation requested by the server | 10 | 5 |

Each direction ran twice with the same counts. The independent peer recorded
two server Certificate messages and four Finished messages on each connection.
The server-requested runs also echoed 96 encrypted application bytes after
renegotiation. The wrong-hostname control failed certificate verification in
the first handshake. The laptop used software crypto and host memory. The ESP
still needs a measurement during simultaneous TLS renegotiation and Wi-Fi rekey.
[Host trace procedure](../../../tools/target/README.md#tracing-tls-renegotiation-on-the-host)

#### Server-requested renegotiation over MQTT on the ESP

A separate loopback check exercised the MQTT bench endpoint with a simulated
device and the authenticated broker. The device answered two distinct status
queries on one connection, one before and one after the server requested a
second full TLS 1.2 handshake. The endpoint counted two server Certificate
messages and four Finished messages.

On the ESP, an attempt to repeat this check with the debugger active from
startup ended during Wi-Fi association. The station reported disconnect
reason 15 and had no IP address after 60 seconds. The debugger counted 68
reservations and 68 wipes, with a peak of one occupied slot. MQTT and TLS
had not started. The full 4 MiB flash and saved profile were restored and
verified.

In the next run, the same ESP image joined Wi-Fi and returned an authenticated
96-byte MQTT status before debugger attachment. The server then sent one TLS
1.2 HelloRequest. On that single TCP connection it observed two full handshakes
with two server Certificate messages and four Finished messages in total. A
second status query returned a fresh 96-byte reply with the original ESP boot
ID and generation. The full flash and saved profile were restored and verified.

OpenOCD could not identify the running target, so this run yielded no slot
count. Wi-Fi group-key renewal was also unobserved. Verify debugger attachment
to the running device before repeating the measurement.

### Key-derived TLS secret

On 18 November 2026 the single MQTT TLS client gained one fixed slot for the
secret that PSA copies when it derives handshake keys. The slot fits every
enabled curve's premaster secret and the 48-byte master secret. It costs 66
bytes plus one lease flag. Each completed or aborted derivation erases the
slot. The SDK still allocates the temporary imported key separately.

The ESP32-C3 image and its predecessor each matched 80 reference derivations
and two known-answer vectors across SHA-256 and SHA-384. After the first use
of the key store, the preceding image made two heap requests per derivation
and the new image made one. Both recovered from a failed key import. The new
image also rejected a second user of an occupied slot and preserved the first
user's bytes. These counts cover the key import and the PRF inputs in the
emulator. The probe executes the target instructions for key import,
lookup, PRF output, abort and destruction. It supplies HMAC and host doubles
for the heap, ROM byte operations and task services. The
[probe instructions](../../../tools/target/README.md#tls-derivation-input-probe)
describe how to repeat the comparison.

| Fixed memory on the final image | Linked bytes | Budget | Headroom |
|---|---:|---:|---:|
| Application | 143,252 | 147,456 | 4,204 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,257 | 55,296 | 1,039 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 272,394 | 278,528 | 6,134 |
| RTC, separate from DRAM | 60 | 128 | 68 |

`c3tout` uses 25,465 of its 25,472 reserved bytes. The application and total
DRAM caps are unchanged. The final 821,824-byte application's SHA-256 is
`765231610ac94ca68e0c2e19ba321a057ff550ef7aaaa96a77d5d34e0dae0c1f`.
The 99 affected host tests, eight compiled probes, 334-file BARR-C gate,
manual C review and source-snapshot check passed for this software build.

The [subsequent imported-key change](#imported-tls-derivation-key) covers the
remaining copy seen in this image.

### MQTT session tickets disabled

On 17 November 2026 the user approved disabling TLS session tickets. The
application does not reuse saved sessions across reconnects. It continues to
make full authenticated TLS handshakes, with the same certificate checks,
ciphers, renegotiation and 16384/4096 record-content limits. This resolves the
ticket-capacity decision below by removing that optional feature; it does not
add a smaller ticket limit or a heap exception.

Owned defaults disable both mbedTLS ticket options. CMake and the C feature
guard reject accidental re-enablement. Qualification checks the compiled SDK
options and rejects linked ticket helpers, including inlined-code cases detected
through configuration. Native session/handshake ticket fields are absent from the
ELF. Each session is now 128 bytes rather than 144; the two typed session stores
save 32 bytes. See the [policy and C review](../../../firmware/hal/ports/esp32c3/network/README.md#mqtt-session-ticket-policy).

All **93 affected tests**, **29 final session-fixture tests**, the full
**334-file pinned C gate**, manual BARR-C review, fresh warnings-as-errors target
build, source snapshot and fixed memory budgets passed. All 47 qualification
tests passed again after the final SDK-option check; it also rejected the
previous ticket-enabled image and accepted the current image. The compiled buffer,
input, hostname, state, certificate, PRF and connect-retry probes passed on the
new ELF. Their existing crypto/transport doubles and acceptance limits remain.

The pinned host SDK passed **15 encrypted TLS compatibility cases** across
three record profiles: MQTT round trip, fresh reconnect, wrong CA, wrong name
and a longer valid chain. All six MQTT exchanges returned 96 bytes. The smallest
record profile still rejects the longer chain as expected; the target retains
full record capacity. Every ClientHello omitted ticket extension 35. A fresh
ticket-enabled build of the same C fixture passed MQTT and included extension
35, validating the detector. The host fixture uses software crypto and heap;
this establishes neither ESP authentication nor whole-system static memory.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 143,185 | 147,456 | 4,271 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,252 | 55,296 | 1,044 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 272,322 | 278,528 | 6,206 |
| RTC, separate from DRAM | 60 | 128 | 68 |

`c3tout` uses **25,398/25,440 bytes**. Application fixed memory decreased by
36 bytes overall; removing a possible large ticket allocation is not a measured
16 KB runtime saving. Inner certificate/key, secret and TCP/IP allocations,
runtime peaks and physical acceptance remain open. The user also lifted the
earlier deferral of operator-dependent work; installation still requires the
reviewed image/backup procedure and explicit approval.

The software-qualified image is at
`C:/Users/kkak4/esp/uthome-tls-no-tickets-20261117/`.
Application SHA-256:
`d645cbddde5b5a650354f5e6fab71035d5d1bc41825e724dc9122869f22358dc`.
ELF SHA-256:
`b7f15bcb1b298dd284c6eff92cb09a81710661528872e4c241585d946cc3e10a`.
`evidence.json` SHA-256:
`860c615ec43b994aaadd94dbcc1b5488b5ab705b27c6f39657b6d7bc6ae3eff0`.
Private software reports and logs are under `build/tls-no-tickets-20261117/`.
The subsequent approved
[board comparison](../../../tools/transaction/README.md#backed-up-tls-board-comparison)
installed and verified this image temporarily. It answered ten initial and ten
post-broker-restart status queries, then timed out on five normal-profile queries.
The readiness guard prevented idle Apply. The original full flash and profile
were restored and verified. This establishes encrypted board exchanges, but
does not isolate ticket removal, qualify sustained reliability or close the
runtime allocation/peak-memory gates. The separate old HA VM issue is unchanged.
The later [USB observation build and board trial](../../../tools/transaction/README.md#usb-mqtt-observation-board-trial)
preserved every fixed-memory category, captured normal traffic and broker
recovery, and restored the original flash. Its short successful post-startup
windows do not close runtime resource or sustained-reliability qualification.

### Fixed TLS derivation inputs

The 17 November derivation change reserves the TLS 1.2 PRF's 64-byte seed and
22-byte label copies. Native PSA still validates, copies and releases the inputs.
Selection is limited to the serialized TLS task, using its native kernel ID and
existing interrupt exclusion. Wi-Fi's concurrent PSA allocations keep the SDK
path. The secret copy and key storage remain heap-backed. See the
[contract and manual C review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-tls-derivation-inputs).

All **93 affected tests**, the full **333-file pinned C gate**, manual BARR-C
review, fresh warnings-as-errors target build, exact source snapshot and fixed
memory budgets passed. The final source-binding check also rejects duplicate
copies of one required SDK unit; its qualification suite was rerun, and that
check passed against the compiled commands and symbols of this same image.
No production input changed after the build.

The [compiled PSA input probe](../../../tools/target/README.md#tls-derivation-input-probe)
passed **200 SHA-256/SHA-384 input lifecycles**, **25 length combinations**, five
error cases, both store-exhaustion/recovery cases and two foreign-task allocation
cases. Input bytes survive overwriting the caller's buffer; cleanup erases all
fixed bytes and releases leases. The fixed path made **zero seed/label heap
requests**, compared with **400** in the unwrapped reference. Both retain 200
secret allocations and matching native error outcomes. The previous ELF is
rejected as fixed storage; disabled assertions and report overwrite reject too.
Existing buffer, input, hostname, state, certificate and connect-retry probes
passed on the final ELF.

The probes double heap, ROM byte operations, hash setup/abort and native
task/interrupt helpers. They test input ownership and cleanup, not PRF output,
authentication, encrypted handshakes, physical preemption or runtime peaks.
The selected exclusive-buffer PSA path and absence of arbitrary PRF/exporter
callers are checked. Cipher admission, record capacities, certificate verification,
dependency revisions and aggregate budgets are unchanged. Inner certificate/key,
secret, ticket and TCP/IP allocations still prevent whole-system qualification.
The ticket policy was deferred at this checkpoint; the approved change is above.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 143,221 | 147,456 | 4,235 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,256 | 55,296 | 1,040 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 272,362 | 278,528 | 6,166 |
| RTC, separate from DRAM | 60 | 128 | 68 |

`c3tout` uses **25,430/25,440 bytes**. This step adds 86 payload bytes and
12 scope/lease bytes within the existing application cap. The image is
**unflashed** at `C:/Users/kkak4/esp/uthome-tls-prf-owner-fixed-20261117/`.
Application SHA-256:
`82b85736fcb116f3fe887133a151f09377198064a7dcdaf50ab45353204d7fef`.
ELF SHA-256:
`27fe0a5287e28cc6a01a08e58c737f23104a6ae1df3702f858ee8dc2791f1327`.
`evidence.json` SHA-256:
`f9e816f7d030d3a52eb603f53d76f26aaa08676f5b52b7eed3065a04422eff40`.
Final memory evidence is `build/tls-prf-fixed-20261117/memory-final.json`, beside
the private tests, probe reports and verification. No board, serial port,
hotspot or live Home Assistant service was accessed. Physical MQTT timeout
causation and the separate old HA VM instability remain unresolved.

### Fixed peer certificate records

The 17 November peer-certificate change reserves one typed 952-byte temporary peer-chain
head and one 32-byte retained peer-certificate digest in `c3tout`. A scoped wrapper
preserves the native parser and its failure semantics. The old digest coexists
with the head during renegotiation; after a successful identity comparison the
SDK frees the old digest before creating its replacement. One digest slot is
sufficient, while the head requires separate storage. Exact-pointer release
erases both stores. Exhaustion fails without a fallback allocation. See the
[contract and manual C review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-peer-certificate-records).

All **92 affected tests**, the full **333-file pinned C gate**, manual BARR-C
review, fresh warnings-as-errors ESP build, exact source snapshot and all memory
reservations passed. The 25 adapter tests were rerun after replacing a literal
test-loop bound with the named digest length; the full gate passed again.
The production image did not change during that fixture correction.

The [compiled certificate-record probe](../../../tools/target/README.md#tls-certificate-record-probe)
passed initial parsing and **100 renegotiations**, **seven error cases** including
a recovered read retry, and exhaustion of each store. The unwrapped native
reference on the same ELF made **202 outer-record allocations**; the fixed
wrapper made **zero** across the successful lifecycle. Both produced matching
error outcomes and retained/cleared digest states. A changed certificate retains
the old digest; errors after the identity match may leave it cleared, preserving
the SDK contract. Released stores are zero and reusable. The previous allocating
ELF is rejected as a fixed-storage image. Disabled assertions and report overwrite
also reject. Existing state, buffer, hostname, receive/read and connect-retry
probes passed on this exact new ELF.

The probe executes native framing, identity decisions, promotion and cleanup,
using one synthetic certificate. DER parsing, trust verification, digest/key
operations, record reading, alerts, heap and ROM/hash helpers are doubled.
It does not establish authentication, multi-certificate chain behavior, encrypted
handshakes, runtime heap/stack peaks, radio or hardware acceptance. Inner chain
nodes, names, sequences, CA/key material, tickets and TCP/IP remain open. Full
peer-certificate retention, restartable ECP, another default digest length and
linked saved-session copy/get/set/load paths are rejected until separately reviewed.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 143,123 | 147,456 | 4,333 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,250 | 55,296 | 1,046 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 272,258 | 278,528 | 6,270 |
| RTC, separate from DRAM | 60 | 128 | 68 |

`c3tout` uses **25,332/25,344 bytes**. The new stores add 984 payload bytes plus
10 bytes of scope/lease metadata. The module reservation uses existing application
space; aggregate caps, dependency revisions, TLS record limits and certificate
checks are unchanged. The image remains **unflashed** at
`C:/Users/kkak4/esp/uthome-tls-certificate-fixed-20261117/`.
Application SHA-256:
`01b0bb333904a1df2baa33dda5922e1dfb455972ef7a2c6607432ed13c38e85d`.
ELF SHA-256:
`82fb66c0aabc6637301d0201fad73cb6e803c9dfd167b641d6c47cb86881a060`.
`evidence.json` SHA-256:
`501100eb7c3daa992cc72fcbfae5b1c9ddc6fbc1a17976192a10925829685331`.
Private build/test/probe evidence is under `build/tls-certificate-fixed-20261117/`.
No board, serial port, hotspot or live Home Assistant service was accessed.

**Earlier ticket-capacity finding, before the approved removal above.** The selected SDK enabled internal
TLS 1.2 tickets even though ESP-TLS saved-session support is disabled. The native
ticket parser allocates in `ssl_tls12_client.c`, outside the current `ssl_tls.c`
allocation boundary. A private software probe on the unchanged preceding ELF
(`d058e63ba2b153a0e813f71b5764f07f73f0f5c1c4a87e3358542352f41c7983`)
confirmed accepted tickets of 32, 5,328 and **16,374 bytes**, plus zero-length,
malformed-message, replacement and allocation-failure behavior. A 16,374-byte
ticket fits one full plaintext record and already exceeds even that image's
5,327-byte headroom by 11,047 bytes, before other crypto/TCP/IP storage. This is
a demonstrated accepted size, not a claimed maximum or runtime peak.

The ticket proof injects plaintext and doubles record reading, alerts, handshake,
hash/ROM helpers and heap. Session-copy/get/set and application saved-session
APIs are absent from that ELF. Silently choosing a smaller ticket slot would
reduce that image's accepted behavior. Ticket support/admission was left
unchanged at that checkpoint. Whole-system resource qualification
and the physical MQTT timeout investigation are still open.

### Fixed TLS handshake state

The 17 November implementation now gives `c3tout` two typed transforms, two
typed sessions and one typed handshake record. The SDK retains its original
initialization, renegotiation, promotion and cleanup algorithms. Both entry
wrappers are checked, and an ordered initialization scope prevents later
equal-sized requests from taking a state slot. Recognized exhaustion returns
NULL without heap fallback; exact-pointer release erases the full record.
See the [contract and manual C review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-tls-handshake-state).

All **87 affected tests**, the full **333-file pinned C gate**, manual BARR-C
review, fresh warnings-as-errors target build, exact source snapshot and all
memory reservations passed. Host tests include all eight missing-state masks,
both renegotiation entries, retries, exhaustion, erasure/reuse and unrelated
requests before, during and after initialization/promotion. No exception,
dependency revision, TLS record limit or aggregate memory cap changed.

The [compiled state probe](../../../tools/target/README.md#tls-handshake-state-lifetime-probe)
passed **200 cycles, 200 retries, eight reinitializations and nine exhaustion
cases** against the actual typed arrays and lease code. It observed **zero
state-record heap requests** in those lifecycle cases. Active pointers/bytes
survive failure; promotion releases old records, and all free records are zero.
Selection is closed before native hash setup and doubled handshake progress.
Equal-sized out-of-scope requests still reach the SDK allocator. The previous
heap-state ELF is rejected. The current ELF also passes record-buffer setup/free,
all 12 receive/read cases, all 12 connect-retry cases and the hostname probe's
100 reconnect cycles plus three occupied-state setup failures. Probe CLI guards
reject disabled assertions, report overwrite and mismatched ELF/memory hashes.

The probes double heap, ROM byte operations, PSA hash calls and handshake
progress. Empty crypto state advances synthetically to native wrapup; removed
empty records and occupied leases inject reinitialization/exhaustion conditions.
No authentication, cipher handover, radio, board timing or runtime peak is
qualified. Client-only TLS 1.2 with renegotiation is enforced; DTLS, TLS 1.3,
server profiles and linked session-reset APIs are outside the reviewed scope.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 142,129 | 147,456 | 5,327 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,252 | 55,296 | 1,044 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 271,266 | 278,528 | 7,262 |
| RTC, separate from DRAM | 60 | 128 | 68 |

`c3tout` uses **24,338/24,352 bytes**. Typed state adds 2,904 bytes and scope/lease
metadata adds 13 application bytes. Its module reservation remains within the
existing application cap. Certificate/key, internal ticket and TCP/IP allocations
remain open; this is not whole-system static-memory qualification or closure of
the observed physical MQTT timeout cause.

The fresh image is **unflashed** at
`C:/Users/kkak4/esp/uthome-tls-state-fixed-20261117/`.
Application SHA-256:
`97a6c1c21c034dabcf089f45fee1fe59660e8d3151489632de6320d45a7fa6ab`.
ELF SHA-256:
`d058e63ba2b153a0e813f71b5764f07f73f0f5c1c4a87e3358542352f41c7983`.
`evidence.json` SHA-256:
`0774c0a55d5049b8de05a03e4acc12ea4009e946f7a2eaaeeaebdd9b51d0ad00`.
Private reports and logs are under `build/tls-state-fixed-20261117/`. No board,
serial port, hotspot or live Home Assistant service was accessed.

### Handshake-state lifetime proof

The preceding compiled SDK investigation confirmed the proposed outer-state
capacity: **two 84-byte transforms, two 144-byte sessions and one 2,448-byte
handshake record**, totaling **2,904 typed bytes**. The allocator count limits
in that experiment were simulated; fixed state storage was not implemented yet.
Against the then-existing 8,244-byte application headroom, that payload left
5,340 bytes before lease/scope metadata, other crypto and TCP/IP storage.

The probe at that checkpoint (since extended for the fixed stores above)
passed 100 cycles and 100 allocation-free in-progress retries through each of
the two linked renegotiation entries. Active and negotiating pointers stay
distinct; active contents and ownership survive initialization. Native wrapup
promotes the new records and releases the old ones. All six injected allocation
failures preserve the old active pointers/bytes, release negotiating records and
allow complete cleanup. Independent negative controls show that reducing either
the transform or session count to one fails initialization.

This reuses the unchanged, unflashed hostname-build ELF identified below
(`33f4eba9591f32ad6ce9a3b99a77e0ddc77fbe35907c9932752d12052f598204`).
Fresh reports/logs are private under `build/tls-state-lifetime-20261117/`.
Only the Python probe and existing guides changed; no fresh firmware build or
C qualification is claimed for this investigation. Heap, ROM byte operations,
PSA hash operations and handshake progress are doubled. Empty crypto state is
advanced synthetically to native wrapup; authentication, cipher handover,
handshake peaks and hardware behavior remain untested. Session-reset APIs are
not linked in this ELF and are not covered.

The implementation boundary needs the existing setup scope **and both**
`mbedtls_ssl_start_renegotiation` and `mbedtls_ssl_renegotiate`. `ssl_msg.c`
calls them for peer HelloRequest and record-counter limits respectively.
The latter also calls the former within `ssl_tls.c`; a linker wrapper on the
former alone cannot intercept that internal call. Global allocation-size
matching is unsuitable because this same SDK unit also allocates variable-length
ticket and copied-session data. The implemented adapter above selects typed init
requests within a borrowed-context scope and ends selection after initialization;
wrapup clears negotiating pointers again, so missing-pointer tests alone are
not a sufficient general classifier. Test equal-sized unrelated requests before,
during and after that phase. Preserve the existing renegotiation, certificate
and record-size behavior, vendor pins and aggregate caps.

### Fixed retained MQTT hostname

The preceding 17 November step extended `c3tout`'s existing `ssl_tls.c` boundary with
one 254-byte hostname slot, retaining the current 253-character MQTT admission
limit. A checked wrapper calls the unchanged SDK setter, so the SDK still owns
copying, context flags, replacement and NULL clearing. Oversized input leaves
the old name unchanged; exhaustion fails without a hostname heap fallback.
The entire slot is erased before reuse. It is separate from `c3tls`'s temporary
name scratch, which overlaps the setter call. See the
[contract and C review](../../../firmware/hal/ports/esp32c3/network/README.md#tls-output-c-review).

All **114 affected tests**, the full **333-file pinned C gate**, manual BARR-C
review, fresh warnings-as-errors target build, exact source snapshot and every
memory reservation passed. The compiled SDK name probe passed boundary/replacement
and clear cases, three length rejections, exhaustion/reuse, three setup failures
and **100 setup/free cycles**. It observed **zero wrapped hostname heap requests**;
the original setter reference made four allocations and produced matching copied
values/flags. The previous heap-hostname ELF is rejected as a negative control.
Record-buffer setup/cleanup, all 12 receive/read cases and all 12 connect-retry
cases also passed on this exact new ELF.

The first name-probe run exposed obsolete error values in the host fixture/probe.
They now match the pinned SDK's PSA errors (-135/-141); affected tests and the C
gate passed again. Firmware already used the SDK symbols and needed no correction.
Heap, ROM string/byte functions and hash calls are doubled. These checks are not
encrypted authentication, hardware, timing or runtime peak measurements.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 139,212 | 147,456 | 8,244 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,241 | 55,296 | 1,055 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 268,338 | 278,528 | 10,190 |
| RTC, separate from DRAM | 60 | 128 | 68 |

`c3tout` uses 21,421/21,424 bytes. The added name and selection state use 263
application bytes plus one linker-alignment byte; aggregate caps are unchanged.
The image is **unflashed** at `C:/Users/kkak4/esp/uthome-tls-name-fixed-20261117/`.
Application SHA-256:
`96ec9e59182186e0f02ef830d9251595c9942efc4d225a8fafbad70c2cf026d2`.
`evidence.json` SHA-256:
`b66666f63b0933a386a98c1112b0ad289052b5689b5211c064cc68dc501c9836`.
Private logs and memory/name/input/output/connect reports are under
`build/tls-name-fixed-20261117/`. No board, serial port or live network was used.

The setup-state work must account for **renegotiation**, which the selected SDK
client configuration explicitly enables. In the pinned `ssl_tls.c`,
`mbedtls_ssl_start_renegotiation` initializes new negotiating state while the
active transform/session remain alive; wrapup releases the old state and promotes
the new one. One transform/session slot each is therefore insufficient for that
existing behavior. Two transforms, two sessions and one handshake record alone
need 2,904 typed bytes before lease metadata and other crypto allocations.
That hostname-step result was a source/type-based capacity estimate, before the
state implementation above, and not a measured handshake peak. TLS record sizes
and renegotiation behavior are unchanged.
The ESP-TLS saved-session build guard also does not exclude mbedTLS's internal
ticket parsing/allocation. Certificates, keys, ticket state and TCP/IP pools remain
open; the hostname change does not complete static-memory qualification.

### Fixed MQTT receive buffer

The preceding 17 November follow-up extended the existing `c3tout` adapter with one full
16,717-byte input buffer in an eight-byte-aligned 16,720-byte slot. Both MQTT
record buffers remain owned through SDK retries and are fully erased on failure
or normal cleanup. Exhaustion fails setup without a record-buffer heap fallback.
The SDK, TLS limits, certificate checks and application owner are unchanged.
See the [contract and manual C review](../../../firmware/hal/ports/esp32c3/network/README.md#tls-output-c-review).

All **105 affected tests** passed with warnings-as-errors C fixtures. The full
**333-file pinned C gate**, manual BARR-C review, fresh target build, source
snapshot and every memory reservation passed. Compiled RV32 probes passed all
12 receive/read cases and 12 connect-retry cases. Actual SDK setup/cleanup also
passed one-pair exhaustion, erasure/reuse and failure at each of the three
remaining setup allocations (seven setup and six cleanup calls). ELF/DWARF
checks identify both buffers and their exact capacities; no input or output
buffer request reaches the doubled heap. The verifier rejects the preceding
heap-input image as a negative control.

These probes double heap, transport, ROM byte copies and hash setup/abort.
Read cases inject already-decrypted bytes. They do not qualify encryption,
radio behavior, stack/heap peaks or physical recovery. The image is **unflashed**.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 138,949 | 147,456 | 8,507 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,240 | 55,296 | 1,056 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 268,074 | 278,528 | 10,454 |
| RTC, separate from DRAM | 60 | 128 | 68 |

`c3tout` uses 21,158/21,168 bytes. Its increased module reservation accounts for
the input slot and lease within the existing application cap. Against the
MQTT-only baseline below, application storage grows by 16,721 bytes and
SDK/linker alignment by seven; task storage and RAM code are unchanged.
The three remaining setup requests are a transform (84 bytes), session (144)
and handshake parameters (2,448), confirmed against this ELF's SDK types.
They do not include subsequent certificates, keys, retained hostname or TCP/IP
allocations. Those lifetimes/capacities are the next resource work; fixed record
buffers alone do not complete TLS or whole-system static-memory qualification.

The fresh build is `C:/Users/kkak4/esp/uthome-tls-input-fixed-20261117/`.
Application SHA-256:
`30c6497b7a0ccc5d06d9051e59cf6f75e7c057e6d2963cdcad407b4f7609d329`.
`evidence.json` SHA-256:
`8ba85bfc4072b2ae1fbb311622697c0c1d68121de04e985de758fd7130104f04`.
Private logs, memory report and input/output/connect reports are under
`build/tls-input-fixed-20261117/`; they retain their exact ELF/report hashes.
No board, serial port or live network was accessed in this step.

### MQTT-only baseline after HTTPS removal

The [removal check](../../../firmware/src/network/README.md#esp-https-removal)
passed 509 target-facing tests, the full 333-file pinned C gate/manual review,
warnings-as-errors target build, exact source snapshot and all memory budgets.
The build remains unflashed at `C:/Users/kkak4/esp/uthome-mqtt-only-20261117/`.
Private logs, reports and preserved build settings are under
`build/remove-https-20261117/`. Application SHA-256:
`bef502e50e545a6b3613a6c9fdb2c9c8359e251fcaf3ba8113b2639c3d500f62`.
`evidence.json` SHA-256:
`be7a1c899ddd074762af0d8abdc393dff4db6ce68ba59ae25d35c11abb4861a6`.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 122,228 | 147,456 | 25,228 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,233 | 55,296 | 1,063 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 251,346 | 278,528 | 27,182 |
| RTC, separate from DRAM | 60 | 128 | 68 |

This saves **18,032 fixed DRAM bytes** from the preceding MQTT retry image.
Removing the server/parser/upload owner and selecting TLS client-only mode also
removes two connection/output slots. `c3tls` uses 3,172/3,200 bytes: one 2,896-byte
SDK context, one 20-byte error record, name scratch and lease flags. `c3tout` uses
4,437/4,448 bytes for one aligned output buffer, its lease and setup pointer.
`c3app` falls to 37,568 bytes and `c3net` to 67. The SDK/kernel group falls by
five bytes; task-pool and RAM-code usage are unchanged. Queue, socket/netconn and
datagram capacities still serve shared SDK traffic and are not inferred from the
TLS session count. No dependency revision or aggregate cap changes.

That compiled input probe passed 12 retention/read cases for one client.
It observed a 16,717-byte heap input buffer. An aligned fixed slot would
need **16,720 bytes**, leaving **8,508 application bytes** before bookkeeping and
the remaining crypto/TCP/IP work; projected total DRAM is 268,066/278,528.
This was a capacity projection, not implemented fixed input storage or measured
free heap. The output probe passes exhaustion, erasure/reuse and failure cleanup;
the connect probe retains the corrected retry behavior. TLS input, certificates,
keys, retained names and packet/control-block storage still need bounded design.

All 12 refreshed encrypted MQTT compatibility cases matched expectations.
The 16,384/4,096 content limits remain in firmware. A 2,048-byte input still rejects
the longer valid broker chain; removing HTTPS does not justify weaker certificate
checks or arbitrary record limits. Partial-header retention remains mandatory,
and the rejected dynamic-buffer option stays disabled. The next step from that
baseline was the full-size single-client reservation now verified above.
Remaining crypto/TCP/IP allocations, hardware timing, stack/heap peaks,
reconnect and combined ESP/HA acceptance are still open.

### Earlier dual-service baseline

The following implementation measurements describe the earlier one-MQTT/two-HTTPS
image and its unchanged historical evidence. Current capacity is the single-client
table above; the old overlap requirement and three-input-buffer projections are
superseded by the user's removal decision.

The earlier build uses pinned ESP-IDF 6.1
`fff9895c82d744c7237be8847347bdd1b07c6643`, kernel
`ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2` and the maintained owned patches.
The 15 November build with fixed TLS output buffers used the existing private
bench settings; no board was flashed. Local generated evidence is in
`build/tls-output/` and the isolated build directory
`C:/Users/kkak4/esp/uthome-tls-output-20261115b/`.
The target's `evidence.json` SHA-256 is
`c14034bf733a0899f7c80b243694a98a63934a6d6e3808ab3f35b2892bc0c3de`.
The application image SHA-256 is
`33e9ef3f16d37a6ade28cc3c38cd8bbe1fbafb571d24c23f1342b31720ce15fa`.
Source-snapshot verification passes against the original build inputs.

The 17 November [MQTT connect retry build](../../../firmware/src/network/README.md#mqtt-tls-connect-retry)
also passes source-snapshot verification and every fixed-memory reservation.
Its group usage and caps match the table below. The new image is unflashed,
and its separate build identities and private evidence are linked in that guide.

| Fixed-memory group | Linked bytes | Unchanged cap | Headroom |
|---|---:|---:|---:|
| Application | 140,255 | 147,456 | 7,201 |
| Task pool | 18,499 | 18,560 | 61 |
| SDK/kernel | 54,238 | 55,296 | 1,058 |
| RAM code | 56,386 | 57,152 | 766 |
| Total DRAM | 269,378 | 278,528 | 9,150 |
| RTC, separate from DRAM | 60 | 128 | 68 |

These are linker measurements, **not runtime free heap, task stack margins or
ESP timing**. The published mailbox baseline (`eede615`) used 275,930 fixed DRAM
bytes. Compact configuration views saved 24,600 bytes and enabled-only driver
workspaces save another 4,672; fixed ESP-TLS/lwIP records and TLS output buffers
reserve part of that reclaimed capacity. That image is **6,552 bytes smaller than that
baseline**. All aggregate caps are unchanged.
That image used MQTT 1,816/1,824 bytes and HTTPS 3,098/3,104; future network storage needs
an explicit module reservation within the application cap.

### Reclaimed configuration storage

`cfgjson` now reads the caller's existing configuration bytes through 512 typed,
eight-byte views instead of retaining a copied cJSON tree/text. The target module
uses 4,117 bytes including bookkeeping, with its transaction reservation reduced
to 4,128. The unlinked cJSON mutable-storage rows are removed from current budget
policies; the pinned vendor source remains unchanged. The profile schema and
admission limits remain intact. Input is borrowed until close; production
plan/discovery callers finish reading and copy scalar settings before returning.
See the [contract and manual review](../../../firmware/src/config/README.md#admission-c-review).

Reader tests include 500 generated nested documents and 2,000 grammar
mutations checked against an independent Python decoder, plus explicit numeric,
string, capacity, depth and lifetime cases. Actual owned C is compiled with
warnings as errors; allocator interception verifies no reader allocation calls.
They remain part of the full target-facing regression suite.

### Driver workspaces for enabled entries

`cfgbind` now reserves four typed driver workspaces, matching the existing
four-worker limit, while retaining eight configuration entries. Each target
workspace occupies 1,168 bytes; removing four saves **4,672 fixed DRAM bytes**.
The containing `c3app` module falls from 42,400 to **37,728 bytes**, with its
transaction reservation reduced to the latter value. No group cap increases.
Disabled entries still reserve names and wiring. Enabled entries retain their
original sample source indices and reuse storage only after checked teardown.
See the [binding contract and manual review](../../../firmware/src/config/README.md#activation-and-recovery).

For the driver-workspace change, the **524-test target-facing suite** and
**333-file C gate** passed, as did
the manual review, warnings-as-errors target build and fixed-memory budgets.
New binding scenarios exercise late/interleaved positions, actual raw/environment
sample identities, repeated changes of driver type, five-worker rejection,
rollback, retained cleanup failure and an all-disabled plan. Both normal and
light-diagnostic host variants run them. These are software/build results;
the smaller image has not been installed or tested with physical peripherals.

That preceding build remains in `build/binding-memory/` and
`C:/Users/kkak4/esp/uthome-binding-memory-20261115/`. Its unchanged
`evidence.json` SHA-256 is
`945f0057acf8d1703cfac6c57dbb19f5e94510913bf13f98b64e4f0e4ddb8a42`;
its application image SHA-256 is
`2d212019df4245afe49d611a182de295f51c5fae880c09937e9b5ef8bccec21b`.

### Fixed TLS output buffers

The earlier three-session `c3tout` reserved **13,303 fixed bytes** within a 13,312-byte module budget:
three 4,429-byte output buffers, each in an eight-byte-aligned 4,432-byte slot,
three lease flags and the current setup pointer. This preserved one MQTT and
two HTTPS connections; current one-session storage is reported above. Total linked DRAM grew by 13,304 bytes from the preceding
driver-workspace build, including one additional SDK/linker byte. It moves
storage into the fixed budget; it does not reduce TLS's total memory need.

The original record boundary binds `ssl_tls.c` allocator/release calls. The
later PRF input boundary also binds PSA cleanup, as described above. A wrapper
identifies the output-buffer request during the original synchronous SDK setup;
other allocations retain the SDK heap path. Exhausting the three output slots
fails setup without an output-buffer heap fallback. SDK cleanup erases the full
slot before reuse. The SDK retains record offsets, encryption, partial-send
state and cleanup order. Calls belong to the existing serialized application
owner. No task, lock, certificate change or vendor-source edit is added. See the
[contract and manual review](../../../firmware/hal/ports/esp32c3/network/README.md#tls-output-c-review).

The **531-test target-facing suite passed**, followed by all **59 affected
TLS/qualification tests** after correcting the SDK bindings. These include
three-slot capacity/alignment, exhaustion, setup-failure cleanup, zeroization,
same-sized unrelated allocation forwarding and 1,000 reuse cycles. The full
**337-file C gate**, manual review, warnings-as-errors target build, original
source-snapshot verification and unchanged fixed-memory caps pass. The first
target build exposed ineffective allocator substitutions and a recursive native
setup alias; its failed evidence remains in the unsuffixed local build folder.
The final `b` build corrects both; neither image was flashed.

The [SDK instruction probe](../../../tools/target/README.md#tls-output-allocation-probe)
also passes on the final ELF. It executes 12 original SDK setup calls and 11
cleanup calls, exercising a client plus two servers, fourth-session rejection,
failure at each remaining setup allocation, erasure and reuse. No output-buffer
request reaches the heap double. SDK heap, ROM byte-copy and PSA hash calls are
doubled; this proves the compiled setup/cleanup path, **not a TLS handshake,
network exchange, board timing or runtime memory peak**. Its report is
`build/tls-output/sdk-output-proof.json`, bound to ELF SHA-256
`50bd77870ef7d81ddc18ea26882c96c33eee200d167051637184f9e0b26d337a`.
Live MQTT reconnect and failure recovery remain open for this allocation
path. The earlier overlapping HTTPS load is no longer a requirement.

### Fixed ESP-TLS records

The earlier three-session `c3tls` used 9,054 fixed bytes within a 9,056-byte module reservation: three
typed ESP-TLS contexts (8,736 bytes), three error records (60), temporary hostname
storage (254) and four lease flags. This preserved one MQTT plus two HTTPS
connections; current one-session storage is reported above. The total DRAM increase is 9,056 bytes including linker padding.
These bytes are now budgeted before runtime; this is a change in allocation
strategy, not a claim that secure connections need less total memory.

Only the two selected ESP-TLS compilation units bind calloc/free/strndup to the
owned stores. Four error-tracker functions use fixed records with the SDK's
public error layout. Unsupported requests or exhausted stores fail without
heap fallback. The target gate verifies exact source scope, all linked aliases,
absence of the original allocating error tracker, flash placement and continued
use of SDK connection setup/cleanup. Vendor source and certificate checks remain
unchanged. See the [contract and manual review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-esp-tls-records).

Six direct adapter tests cover zeroing/alignment, independent error categories,
temporary-name capacity, exhaustion, initialization-failure cleanup and 1,000
full lease cycles with allocator interception. A qualification regression attacks
missing, duplicated and escaped bindings and invalid linked symbols. These tests
exercise records and build enforcement; they do not perform a TLS handshake.

Earlier ESP-TLS record implementation verification: **490 target-facing tests and 56 C-policy tests passed**.
The full 329-file pinned C gate, manual review, warnings-as-errors target build,
exact-source snapshot verification and every fixed-memory budget also pass.
Those dummy-settings build reports are in `build/tls-records/` and
`C:/Users/kkak4/esp/uthome-tls-records-20261114/`; that build's unchanged
`evidence.json` SHA-256 is
`08b8fdbe07b0dd4944aa2ac1edc1b2aa00080cd240d56cc0821dc746c6a81e67`.
Previous foundation
and runtime results remain in `build/compact-json/`; those unaffected suites were
not rerun for this SDK-record change.

### Fixed lwIP mailboxes

`c3lbox` replaces the allocating/freeing SDK mailbox functions and uses the
existing eight shared queue slots. Target-link checks confirm both aliases and
absence of the original `sys_mbox_new`/`sys_mbox_free`. Eight distinct typed
identities use 672 flash bytes, plus a 32-byte pointer table. The adapter adds
**zero mutable storage** and no extra native objects. Its entry points and
queue-binding helpers are flash code. The number of live mailboxes has not been
measured, so this does not quantify runtime heap savings.

Focused tests cover 1,000 create/free cycles, shared exhaustion, exact-capacity
and invalid requests, zero-valued messages, ISR posting and nonempty-close
preservation. See the
[adapter contract and manual review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-lwip-mailboxes).
This closes mailbox backing for selected Wi-Fi service builds; broader client
overlap and the new image's live-network validation still require the board.

### Fixed lwIP connection and datagram records

`c3lrec` uses **352 bytes** within its own 352-byte application reservation:
six 52-byte `struct netconn` objects, two 16-byte `struct netbuf` objects and
eight one-byte lease flags. The fixed-record change added 352 bytes of writable
object storage relative to the earlier working Wi-Fi image. Its linked DRAM
grew by 360 bytes; RAM-code and task-pool usage were unchanged. Aggregate caps
are unchanged.

Source-scoped replacements cover all NETCONN/NETBUF allocations and releases in
the pinned `api_lib.c`, `api_msg.c` and `netbuf.c`. SDK protocol and cleanup code
remain intact. The adapter returns NULL at exhaustion and uses the existing
native interrupt boundary for shared access. Counts include listeners and UDP
sockets, not just secure clients. There is no added task, mutex or heap fallback.
See the [contract and manual review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-lwip-records).

**524 target-facing tests passed**, including four direct record tests for
independent capacity, zeroing/alignment, invalid releases, interrupt restoration
and 1,000 reuse cycles with allocator interception. The full **333-file C gate**,
manual review, warnings-as-errors target build, original-source verification and
every module/group budget pass. The first target link exposed archive ordering;
the final build explicitly retains the adapter and native exclusion functions.
All **46 qualification tests** passed again after that correction. Target
qualification checks the three exact substitution scopes, linked flash helpers
and SDK lifecycle; the generic allocator remains for other lwIP pools.

The preceding fixed-record evidence remains in `build/lwip-records/` and
`C:/Users/kkak4/esp/uthome-lwip-records-20261115b/`. That build's unchanged
`evidence.json` SHA-256 is
`02106a968b0cc9771a2117cd7fd91774120bce1a458b05b1fb04bb9bb5b5d547`;
its application image SHA-256 is
`19eefd77cb0a39abf4112bf3134245eddc41c9ff5ef095399aa61971ce39f897`.

The new image is **unflashed**. The earlier working image remains on the ESP;
the configured hotspot was not visible during this step. Live reconnect,
pool-exhaustion recovery and interrupt latency
remain unverified for this change. Earlier [Wi-Fi/MQTT hardware results](../../../firmware/src/network/README.md#live-mqtt-check)
do not qualify this new allocation path. Packet buffers, protocol control blocks
and TLS state still allocate from the heap.

### Reachable allocation paths

The following paths were checked in the pinned SDK source and selected compiler
commands. The ELF still links `esp_tls_init`, `mbedtls_ssl_setup`,
`esp_mbedtls_mem_calloc`, `netconn_alloc` and `memp_malloc`. Scoped substitutions
cover two ESP-TLS units, three lwIP API units and `ssl_tls.c`; the TLS record
adapter forwards all other requests to the SDK. ESP-TLS records/names, TLS
input/output buffers, lwIP connection/datagram records and mailbox backing use owned
stores. Linking an allocator alone
would not prove reachability;
the owned entry paths below establish application use.

| Area | Source path and behavior | Assessment |
|---|---|---|
| Decision, codec and MQTT packet storage | `txjob`, `txport`, `c3tx`, `netmqtt`, `netbroker`, `c3mqtt`: fixed owned state and bounded copied buffers | No application allocator/object path; reviewed and software-tested |
| Native task stacks/notifications | `c3task` and the owned compatibility patch reserve/seal startup slots | Software and target-link checks pass; runtime stack/creation counters need the board |
| Native queues/semaphores/mutexes/event groups | `c3nq`/`c3event` pre-create native objects; SDK create/delete APIs lease/release fixed records | Bounded exhaustion/reuse; does not cover surrounding SDK heap storage |
| ESP-TLS contexts, error records and temporary names | `c3mqtt` calls the SDK lifecycle; two SDK units bind allocation to `c3tls`, and error trackers use linked fixed replacements | Context/name allocation is bounded for the selected single-client path; live handshake/reconnect acceptance remains |
| MQTT encryption state | SDK setup uses fixed outer state, PRF inputs and temporary imported derive-key bytes. | Shared PSA key tables and inner certificate/crypto storage still allocate. Session tickets are disabled. |
| Retained connection hostname | Scoped original SDK setter leases `c3tout`'s separate 254-byte string slot | Fixed storage for the existing 253-character MQTT limit; target software proof passes, live acceptance pending |
| TLS output buffers | `ssl_tls.c` setup leases the complete `c3tout` buffer; SDK failure/normal cleanup release it | Fixed 4,429-byte buffers for 4,096-byte content; software and compiled SDK setup/cleanup proof pass, live acceptance pending |
| TLS input buffer | `ssl_tls.c` setup leases the full `c3tout` input slot; SDK failure/normal cleanup release it | Fixed 16,717-byte buffer for 16,384-byte content; dynamic-buffer option remains off; live acceptance pending |
| lwIP connection/datagram records | Three API units bind NETCONN/NETBUF allocation/free to typed `c3lrec` stores | Six connections/two envelopes; software and target proof pass, live acceptance pending |
| TCP/IP packet and control-block pools | `lwipopts.h` still sets `MEM_LIBC_MALLOC=1`, `MEMP_MEM_MALLOC=1`; other units keep SDK allocators | Application network activity still reaches general heap-backed pools |
| lwIP mailbox backing | Wi-Fi builds link `c3lbox`; messages use existing `c3nq` payloads | Bounded software/target-link proof passes; shared capacity needs live acceptance |
| Wi-Fi/SDK startup and events | `c3wsta` creates the network interface/event loop and registers callbacks; radio code uses SDK allocations | Inventory and justify any narrow driver exception; do not exempt all SDK networking by implication |

### TLS capacity constraint

Before HTTPS removal, a sizing object compiled with the dual-service RV32
configuration reported the following. It was not linked into firmware or run on the board.
Local probe source, compiler arguments and results are in `build/compact-json/`.

| Selected target type or macro | Bytes |
|---|---:|
| `sizeof(esp_tls_t)` | 2,912 |
| `sizeof(mbedtls_ssl_context)`; already included above | 264 |
| `sizeof(mbedtls_ssl_handshake_params)` | 2,448 |
| `MBEDTLS_SSL_IN_BUFFER_LEN` | 16,717 |
| `MBEDTLS_SSL_OUT_BUFFER_LEN` | 4,429 |

Contexts were already counted in `c3tls`, and output buffers in `c3tout`; do not
reserve them twice. Each then-remaining input buffer needed **16,717 bytes**, before
certificates, keys, transforms and handshake allocations. One exceeded the
former 7,201-byte application headroom; three needed 50,151 bytes. The current
single-client implementation above now includes one full input slot and bookkeeping.
The earlier compiled setup probe requested 84-, 152-, 2,448- and 16,717-byte SDK allocations per setup;
it does not run the handshake or establish a whole-connection peak. These are
capacity observations with the current buffer lifetimes, not measured peaks.

The remaining design must account for handshake lifetimes as well as
bounded allocation. The SDK's dynamic-record-buffer mode is unsuitable as-is
for the nonblocking application, as the probe below demonstrates. Fixed input
storage preserves the SDK's record and connection lifetimes.

The pinned mbedTLS `ssl.h` contract for `mbedtls_ssl_conf_max_frag_len` also
rules out treating a smaller receive buffer as a transparent fix. The MQTT
client must explicitly request negotiation. For TLS, the documented setting
affects application data, not handshake messages.
The selected image uses TLS 1.2; TLS 1.3 and dynamic buffers remain disabled.
Small MQTT packets therefore do not prove a small TLS input
requirement. Any replacement must retain partial headers, unread plaintext and
record/cipher state across nonblocking retries, with a defined capacity policy
for the single MQTT connection. Certificate validation and the existing
dynamic-buffer rejection gates must remain intact.

### TLS input lifetime and capacity decision

The receive investigation uses the same qualified output-buffer ELF identified
above; it changes no firmware, SDK setting or memory limit. Its reproducible
[input probe](../../../tools/target/README.md#tls-input-lifetime-and-capacity-probe)
passed 36 receive/read cases across one client and two server contexts before
HTTPS removal: partial
headers, partial bodies with 16 KiB content, oversize rejection before transport,
EOF with partial data, and unread plaintext retained between small reads.
The returned stack is overwritten between calls. Actual SDK fetch/read/cleanup
instructions run; transport, heap, ROM byte copies and PSA hash calls are doubled.
Plaintext is injected after the decryption boundary. This is not handshake,
decryption, radio, timing or hardware acceptance.

An additional negative control deliberately gives two contexts the same input
buffer. The second fetch overwrites the first context's pending header despite
serialized calls. The normal SDK uses separate allocations and passes the
retention checks. Therefore one application owner is not sufficient justification
for a shared receive scratch buffer: ownership persists beyond a single call.

Each observed input allocation is 16,717 bytes. Rounding to the same eight-byte
alignment used by the fixed output pool gives the following **projections**.
They do not include pool bookkeeping, extra link padding or crypto/TCP/IP state.

| Independent input buffers | Buffer storage | Additional application capacity needed | Projected total DRAM | Above unchanged total cap |
|---|---:|---:|---:|---:|
| One | 16,720 | 9,519 | 286,098 | 7,570 |
| Two | 33,440 | 26,239 | 302,818 | 24,290 |
| Three | 50,160 | 42,959 | 319,538 | 41,010 |

That earlier linker exposes 321,296 bytes in its DRAM accounting. Three added
buffers alone would leave only 1,758 unassigned bytes before bookkeeping and
the remaining allocations. This is a size projection, not measured free heap,
but raising a policy cap alone does not resolve the physical capacity problem.
The generated report is local-only at `build/tls-input/sdk-input-final.json` and
checks the ELF hash against `build/tls-output/memory-final.json`.

**Earlier decision:** a direct three-buffer replacement did not fit that image.
The old projection is retained as evidence, not a current three-session design.
The HTTPS removal permitted the full-size input reservation now implemented
within the existing application cap, as shown above. Remaining crypto/TCP/IP
costs and live acceptance are outstanding.

### TLS record compatibility decision

**Decision: smaller record limits are not a transparent replacement.** The
[host probe](../../../tools/target/README.md#tls-record-compatibility-probe)
ran 24 encrypted cases on 15 November 2026. All matched their expected outcomes,
including compatibility failures. The same pinned SDK TLS code uses real host
software crypto, TLS 1.2 and ECDHE-ECDSA/RSA AES-128-GCM with SHA-256. It talks
to the authenticated AMQTT fixture and the then-maintained Python HTTPS uploader.
No firmware limits, clients, connection capacity or installed image changed.

| Input / output content limit | 96-byte MQTT round trip, short EC certificate | Maintained client, 8192-byte upload | Test client, same upload in 1024-byte writes | Longer broker chain | Longer HTTPS server chain |
|---|---|---|---|---|---|
| 16384 / 4096 | Pass | Pass | Pass | Pass | Pass |
| 4096 / 2048 | Pass | Fails before body delivery | Pass | Pass | Handshake fails |
| 2048 / 1024 | Pass | Fails before body delivery | Pass | Handshake fails | Handshake fails |

All three also deliver a 512-byte upload with the short certificate and reject
both a wrong CA and a wrong server name. The larger valid chain contains an
RSA-2048 leaf and two intermediates: 2,482 DER bytes, 3,526 PEM bytes. Its CA and
name are valid; the full-size control verifies it in both roles. The chain is
within the firmware's existing 8192-byte certificate-file bound, demonstrating
that a PEM file limit alone does not establish a safe TLS handshake limit.

The maintained uploader sends its 8192-byte body as one TLS application record
with 8,216 encrypted payload bytes in this AES-GCM run. Both reduced receive
profiles reject it with SDK bad-input/PSA -135 before delivering body bytes.
Sending the same complete body in 1024-byte writes works in the experimental
client; this does not reduce the HTTP body limit. The longer chain fails with
bad-input/-135 at the smallest MQTT receiver and buffer-too-small/PSA -138 at
both reduced HTTPS senders. Splitting application writes does not solve those
handshake failures. Certificate/name verification remains required throughout.

The report is local-only at `build/tls-profile/verified/report.json`, SHA-256
`9933bcd07330f4b10c11023f9ffeac432b26410a909b49e52aa246773f65e6de`.
It records SDK commit `fff9895c82d744c7237be8847347bdd1b07c6643`, nested mbedTLS
commit `a2b32072ea898afc1ed5b6caf6931e36028c91d6`, tested source/library/compiler-
command hashes and exact per-case outcomes. The host used GCC 15.2, Python 3.14.2
and OpenSSL 3.0.18. Three warnings-as-errors native builds, the full 340-file
pinned C gate/manual review and 49 relevant existing regressions passed.

This is sequential host transport/crypto verification. The SDK's host heap and
two host ciphertext queues are not ESP allocations. The upload fixture checks
exact synthetic body bytes and deliberately returns an application rejection;
it never applies a profile or writes flash. No fresh ESP build, radio test,
three-session peak, stack margin or complete static-allocation claim is made.

After HTTPS removal, the current probe repeats the four MQTT cases per profile
and keeps all expected certificate rejections. Full-upload and server-chain cases
remain facts about the old dual-service design. They no longer constrain this
client-only image. The longer broker-chain failure still matters: retain full
record/certificate compatibility while completing the single-client storage
design and measuring its remaining crypto/TCP/IP costs.

### Dynamic TLS buffer assessment

An isolated experiment based on `23f1f267d6afeff25c53ce370c8fd78c17ccd5c2`
enabled only `CONFIG_MBEDTLS_DYNAMIC_BUFFER` in the network defaults. The real
RV32 image built successfully but **failed the fragmented-header software probe**.
The working application keeps the option off. Its CMake guard now rejects
enabling it; linked-symbol qualification independently rejects the dynamic
receive helper. SDK source and pins remain unchanged.
The experimental map also exceeds the existing `c3tls` module reservation
(9,066/9,056 bytes); that failed budget was retained without changing the cap.

In the pinned `components/mbedtls/port/dynamic/esp_mbedtls_dynamic_impl.c`,
`esp_mbedtls_add_rx_buffer` points `in_hdr` at a five-byte local stack array.
`mbedtls_ssl_fetch_input` can receive part of the header and return `WANT_READ`.
The byte count survives, but the array's lifetime ends. When the owner returns
later, the SDK reads only the missing bytes into a new local array. The read
wrapper returns this retry status directly; it does not save the partial header.

The [reproducer](../../../tools/target/tls_fragment_probe.py) executes both actual
compiled RV32 functions. Transport, allocation and ROM byte-copy operations are
simulated; the returned stack is overwritten between calls to represent valid
intervening owner work. Field offsets come from the ELF's DWARF, not host type
sizes. It exercises a five-byte header `16 03 03 00 06`:

| Delivery | Observed header after retry | Allocation requested |
|---|---|---:|
| All five bytes together, control case | `16 03 03 00 06` | 347 bytes |
| Split after byte 1, 2 or 3 | Earlier bytes replaced by `A5` | 347 bytes |
| Split after byte 4 | `A5 A5 A5 A5 06` | 42,587 bytes |

All four split cases reproduce loss; the last also shows that corrupted length
bytes reach allocation before later TLS validation. This is an instruction-level
software result, **not a live TLS handshake, board observation, security exploit
claim or timing measurement**. It is sufficient to reject this option for the
current owner loop. It does not establish behavior for every SDK release.

Local experiment: `C:/Users/kkak4/esp/uthome-tls-dynamic-probe-20261114/`.
Its ELF SHA-256 is
`4b540ae35991bff7e306b8c7c3d8cb82bc129cf15566400dff3f6f07ac73cffc`.
The report and logs are under ignored `build/tls-capacity/`; no image was flashed.
Use the [target tool procedure](../../../tools/target/README.md#tls-fragment-reproducer)
to repeat the probe. The tool's successful exit means the defect was reproduced,
not that TLS acceptance passed.

For the build guard, **51 affected qualification/TLS-record tests passed**, including
execution of the CMake rejection and linked-symbol rejection paths. The pinned
329-file C gate passes. A fresh normal target build with warnings as errors and
snapshot verification passes at the unchanged **260,386/278,528 DRAM bytes**;
every module and aggregate budget passes. No project C or header was changed.
That build is `C:/Users/kkak4/esp/uthome-tls-buffer-guard-20261114/`, with
`evidence.json` SHA-256
`1e6e49853fcc6f10628fa0a63eea0e8a80837d9fec1fd5c709a8cf2f99c2cb41`.
Logs, memory report and the five-case reproducer report are in
`build/tls-capacity/`. This is software/build validation, not live acceptance.

A safe smaller-buffer design must retain partial headers and records across
retries, validate lengths before requesting storage, and preserve unread data
for each live connection. One shared scratch array cannot replace storage still
owned by another connection. Capacity must cover admitted handshake/record
overlap with deterministic exhaustion and cleanup; it must fit the unchanged
budgets alongside certificates, keys and TCP/IP storage. Default record buffers
exceeded the application headroom in that earlier three-session image. The current
single-client build reserves both full buffers; the rejected SDK option does not
resolve the remaining network allocation work.

### Required closure before whole-system acceptance

1. Provide bounded storage for the remaining mbedTLS and reachable lwIP
   allocations, including shared PSA key tables and inner certificate/crypto state. Establish lifetime,
   concurrency, exhaustion, alignment and cleanup behavior before assigning
   capacities. Preserve vendor pins and all aggregate budget caps.
2. Preserve MQTT alongside USB recovery and permitted peripheral/configuration
   traffic. Account for overlapping active and negotiating TLS state within the
   existing single connection. Keep certificate and server-name checks intact.
3. Test exhaustion and reconnect against the adapted real SDK paths, then rebuild
   and check every module and aggregate reservation. A fixed arena with unbounded
   fallback is not acceptable.
4. On the selected board, capture `c3audit`/task counters and stack headroom at
   startup, first TLS connection, repeated reconnect and concurrent permitted
   peripheral/configuration traffic. Verify no post-startup native object
   creation and classify every remaining heap event under an explicit policy.

This is not completed resource acceptance. Earlier live MQTT/BMP388 checks and
allocation-event counts are recorded in the network guide; they neither measure
heap peak nor qualify the new lwIP-record image. Runtime stack headroom, wider
secure-connection overlap and Home Assistant acceptance remain open. The
persistent host controller is already implemented and software-tested.

For reproduction, use the [target build guide](../../../tools/target/README.md)
with `--runtime-port --configuration-app --static-net-queues --network-services wifi`
and private build settings, then the
[transaction policy](../../../tools/target/transaction-budget.json):

```powershell
python tools/target/memory_budget.py --build ABSOLUTE_FRESH_WORK_DIR --budget tools/target/transaction-budget.json --output build/tls-records/memory-new.json
python tools/target/qualify_kernel.py verify-snapshot --project-dir ABSOLUTE_FRESH_WORK_DIR/project
python tools/c_standard/check.py
python -B -m unittest discover -s tests/target -v
python -B -m unittest discover -s tests/c_standard -v
python -B -m unittest discover -s tests/foundation -v
python -B -m unittest discover -s tests/runtime -v
```

The selected toolchain, test dependencies and pinned formatter must be active.
Keep generated reports and private provisioning inputs under ignored `build/`;
leave the original source snapshot unchanged when verifying evidence.

## Existing task-stack and lifecycle implementation

The sections below describe the earlier isolated task-pool work. Their numbers
belong to those original profiles; the current transaction-image table above is
the applicable integration budget.

## Outcome and scope

The isolated ESP32-C3 Wi-Fi and BLE diagnostics create shim-managed native tasks
from bounded, caller-owned stacks. The staged compatibility patch contains no
task-stack `heap_caps_malloc`/`heap_caps_free` path, uses generation-tagged opaque
handles, and defers unsafe cleanup to one permanent low-priority reaper. Native
task and notification-semaphore creation is now a startup-only operation: each
reservation is committed after both native objects exist, and the radio probe
seals the pool after its first successful scan or advertising checkpoint. Later
creation fails before either native creation call. The pinned
`firmware/third_party/esp32c3-mtk3` checkout remains unchanged.

The production patch also implements the selected one-entry FreeRTOS task
notification contract. Notification value and pending state are authoritative;
the bounded native semaphore is only a wake hint. Set-bits, increment, overwrite,
no-overwrite, no-action, clear-on-entry/exit, blocking/timed waits, take,
state/value clear, ISR variants, stale handles and invalid indexes have direct
tests against the patched source.

This closes the software implementation, host-test and target-build portion of
the Buddy 1 task/notification gap. No board was flashed. On-device stack margins,
radio CPU/timing results, cleanup fault injection and whole-system static
compliance remain unqualified.

## Design

`c3task` owns mutually exclusive fixed profiles selected at build time:

| Profile | Native task stack reservations | Pool total |
|---|---|---:|
| Wi-Fi | event 2,816; TCP/IP 3,584; ESP timer 4,096; Wi-Fi 6,656 bytes | 17,152 bytes |
| BLE | ESP timer 4,096; NimBLE host 4,096; controller 4,096 bytes | 12,288 bytes |

All slots are 16-byte aligned. A bounded best-fit search rejects zero, oversize
and exhausted requests without heap fallback. Ownership includes the handle
generation; release requires the matching base pointer, capacity and generation.
The pinned native port already reserves a separate 4 KiB interrupt stack, so no
per-task interrupt margin is duplicated.

The shim has four lifecycle records, matching the largest profile. Its ordered
states are reserved, running, stopping, termination pending, waiting dormant,
task deleted, semaphore deleted and stack released. Creation rollback enters the
same cleanup sequence. Self-delete only publishes termination; external and
concurrent deletion requests converge on the same record. Stale generation handles
cannot name a reused slot.

Cleanup runs in a permanent 1,024-byte reaper task at native priority 31, below
radio work and immediately above idle priority 32. It wakes every 100 ms and
attempts at most four records per pass. Failed task deletion, semaphore deletion,
stack release or observer release retains the record at the failed state for a
later retry. A stack is never returned while its task may still execute.

`c3task_seal()` succeeds only after the reaper is ready and every profile slot is
occupied and committed. The acquire/seal decision is protected by the existing
single-core interrupt guard. A direct post-seal acquisition test verifies that
pool occupancy is unchanged and the rejection counter advances. Current Wi-Fi
stop/start and BLE advertising stop/restart retain their native task and
notification objects, so their required diagnostic recovery remains supported.
Full `esp_wifi_deinit()`/reinitialization and NimBLE host/controller teardown
followed by reinitialization would recreate native tasks and are intentionally
unsupported after the seal. A future service needing another native task must
reserve it before sealing or revise the reviewed profile and budget.

## Verification

- The actual `c3task.c` allocator passed alignment, best-fit, exhaustion,
  ownership/stale-release and 10,000-cycle tests under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- The real staged shim patch passed creation rollback, pool exhaustion, task/
  semaphore/stack/observer cleanup failure, self/concurrent/stale deletion and
  1,000 reuse cycles under the same sanitizers. Leak detection was disabled
  because the macOS sanitizer runtime does not implement it.
- The notification harness applies the production patch to a pristine pinned
  shim and compiles the extracted production implementation with AddressSanitizer
  and UndefinedBehaviorSanitizer. It passed set-bits, increment, overwrite,
  no-overwrite, pending-state, entry/exit clear, state/value clear, finite timeout,
  stale wake hint, ISR, invalid-index and stale-handle cases. Final Wi-Fi and BLE
  ELFs link `vTaskGenericNotifyGiveFromISR`, `ulTaskNotifyTake` and
  `ulTaskGenericNotifyTake`; the remaining implemented entry points are discarded
  from these two selected links but remain patch-level tested.
- Legacy reservation, failure, self-delete and concurrent-delete regressions,
  seven fixed-pool unit tests and the 1,000-cycle model stress test passed.
- The complete BARR-C formatter/lexical gate passed 285 project C files with the
  pinned clang-format 20.1.8. The 54 checker unit tests passed. Manual review found
  no new deviation: fixed widths and bounds are explicit; shared lifecycle state
  uses the existing single-core interrupt guard; no ISR blocks; pointer values are
  compared only; and the 32-bit generation token is confined to the target handle
  ABI. Both real target builds compile owned code with warnings as errors.
- The staged patch applies cleanly to the pinned vendor source. Its SHA-256 is
  `402dc6cb3325fc1352be7e9b6fe8fa2557edc3be308498195bf5dc8c7ebb69c2`.

Final unflashed build evidence:

| Build | Evidence | Result |
|---|---|---|
| Wi-Fi scan/restart | `/private/tmp/uthome_buddy1_accept7b_wifi/evidence.json`; `memory-budget.json` | build and budget passed; commit/seal/snapshot/static-stack/reaper symbols linked |
| BLE advertising | `/private/tmp/uthome_buddy1_accept7b_ble/evidence.json`; `memory-budget.json` | build and budget passed; commit/seal/snapshot/static-stack/reaper symbols linked |

The qualification tool verifies the patched source, rejects task-stack heap calls,
checks the static APIs and lifecycle tokens, and confirms the final ELF contains
the allocator, reaper stack/task and shim cleanup function. These local build
folders are evidence inputs, not installed firmware or hardware observations.

## Memory impact

The fixed stacks move radio task memory into the ELF. Historical pre-change maps
excluded dynamically allocated task stacks and therefore are not equivalent
runtime-peak measurements.

| Profile | Historical ELF DRAM | Previous qualified DRAM | Current final DRAM | Change from historical | Final cap headroom |
|---|---:|---:|---:|---:|---:|
| Wi-Fi | 262,106 | 278,402 | 278,354 | +16,248 | 174 bytes |
| BLE | 247,703 | 259,127 | 259,063 | +11,360 | 19,465 bytes |

For Wi-Fi, the current group attribution is 168,046 bytes application fixed,
53,170 SDK/kernel fixed and 57,138 RAM code. Its subgroup margins are 146, 14 and
14 bytes. For BLE the current attribution is 163,181, 39,544 and 56,338 bytes.
`network-budget.json` retains the 278,528-byte total and the 168,192 application,
53,184 SDK/kernel and 57,152 RAM-code limits. The previously reported 126-byte
margin came directly from the preceding qualified Wi-Fi map:
`278,528 - 278,402 = 126`; it was aggregate ELF headroom, not measured runtime
free heap. The current 174-byte aggregate fit is still not comfortable growth
capacity. The detailed stack table is in
[wireless_stack_budget.csv](wireless_stack_budget.csv).

## Hardware work still required

On a deliberately selected board and only after backup/identity checks, build and
flash fresh diagnostic images separately. For each profile:

1. Confirm every requested task maps to its expected slot and every used slot is
   committed; `unused` must be nonzero for every active radio task and the reaper.
2. Require `post_seal_task_creates=0`,
   `post_seal_notification_sem_creates=0`, `sealed=1`, and a post-seal rejection
   increment without an occupancy change.
3. Capture the radio window's elapsed, idle, busy and busy-permille values. Confirm
   scan/advertising completes, stop/restart completes, and the same checks pass
   after reset. These measurements must be recorded; no host value substitutes.
4. Record task observer creation/deletion/peak data and cleanup request/completion,
   latency, retry/failure and reaper-call/headroom data. Normal radio restart may
   legitimately have zero cleanup requests because it reuses the existing tasks.
5. Use a separately reviewed fault-injection fixture before claiming on-board
   rollback, deletion-retry or reclaimed-stack recovery; the current board image
   observes those counters but does not deliberately corrupt a live radio task.
6. Repeat under combined peripheral/radio traffic, Wi-Fi/BLE coexistence and
   flash-save stress before any integrated or whole-system claim. Continue the
   SDK allocation inventory; this task-stack result grants no heap exemption.

Exact build, flash, monitor and log acceptance commands are in the
[runtime fixture guide](../../../tests/target/runtime_port_board/README.md#buddy-1-radio-resource-acceptance).
The 174-byte Wi-Fi aggregate margin, and especially the 14-byte SDK/RAM-code
subgroup margins, are the primary build-time risk. Any source, SDK configuration
or toolchain change requires a fresh map and budget record.
