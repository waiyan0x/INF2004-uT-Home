# Wi-Fi static memory and resource assessment

## Current network resource assessment

The single-broker BMP388/LD2450 demo has measured AAI13 coverage under a
specifically justified Wi-Fi driver exemption. It is not a claim of zero heap
use throughout the ESP or of arbitrary broker/workload support. The release
retains certificate-verified MQTT/TLS, USB configuration and recovery.

## Week 13 evidence for AAI13

The final diagnostic run covered one ESP boot: Wi-Fi association, authenticated
MQTT status, broker restart, TLS renegotiation, rejected USB Apply, valid idle
Apply, BMP388 and LD2450 reads, and eight seconds after broker closure. It
recorded **410 successful general allocations, 118,202 cumulative requested
bytes, and zero SDK allocation-failure notifications**. Both recorder overflow
counters were zero. Independent audit counts and bytes reconciled; all six USB
samples reported zero normal-service kernel object creations.

The [final call-site inventory](station-disconnect-owners.csv) attributes the
observed allocations to identified station-driver paths. The diagnostic image
SHA-256 is `4a9fbf7cf5a2034756d512a6bbcc57356e706ebd8072c9a4cdb0de727c2fa414`.
The release image SHA-256 is
`63eb1aaec4e682ce2fa64e2a41cbfdea43a2b43f07de36ebae8ec4f208a05e37`.
These identities belong to the recorded measurements; a new build needs its own
identity and qualification.

## Fixed RAM budget

| Fixed storage | Linked bytes | Budget bytes | Margin bytes |
|---|---:|---:|---:|
| Application modules | 147,057 | 147,456 | 399 |
| Project task pool | 18,499 | 18,560 | 61 |
| Kernel and SDK fixed data | 53,708 | 55,296 | 1,588 |
| Code resident in RAM | 55,500 | 57,152 | 1,652 |
| **Total DRAM** | **274,764** | **278,528** | **3,764** |
| RTC memory, separate | 60 | 128 | 68 |

All 41 modules in the [module budget table](transaction-module-budget.csv) fit.
Unused aggregate space does not increase an individual module's allowance.
Fixed linked storage excludes variable Wi-Fi heap payload; live payload
measurements exclude allocator metadata. Cumulative requested bytes count reuse
and are not a simultaneous RAM requirement. The diagnostic recorder is excluded
from the release budget; its instrumented build exceeds the application sub-cap.

Measured release ELF SHA-256:
`c0664c3536b4ddf4d63e7892bd6c9b3565655986e96a97515d2707cac4cd3fa6`.
Measured map SHA-256:
`55f9ee593665ebaccd3f6f0cba4e3f5bf78445de1dadb61e6fd478bcb66af424`.
With the pinned ESP-IDF tools active, reproduce the check on the selected build:

```text
python tools/target/memory_budget.py --build <station-project-directory> --budget tools/target/transaction-budget.json --output <report.json>
```

## Fixed storage design

Tasks, stacks, control blocks, queues, parser slots, TCP packet/message records
and timer records reserve bounded storage. Peripheral workers are created at
startup and reused; transaction execution adds no worker. The single broker
session uses fixed TLS record buffers. Certificate processing borrows the USB
parser workspace only while the owner has released its views. MQTT session
tickets are disabled; reconnect performs a full verified handshake.

Current implementation contracts and manual reviews are maintained with the
[network adapters](../../../firmware/hal/ports/esp32c3/network/README.md),
[runtime](../../../firmware/src/runtime/README.md),
[configuration/storage](../../../firmware/src/config/README.md), and
[network service](../../../firmware/src/network/README.md).
[Budget policy and capacities](../../../tools/target/transaction-budget.json)
remain authoritative for builds; the summary does not widen them.

## Runtime ownership and startup

The Wi-Fi exemption is restricted to identified driver instances and call paths:

- MAC packet buffers, receive rings/descriptors, aggregation and station state.
- PHY calibration/clock tracking, radio timers and coexistence state.
- WPA association/authentication, including EAPOL-triggered shared crypto work.
  Traced WPA HMAC/AES imports do not grant MQTT TLS a heap exemption.
- The measured driver-owned per-thread semaphore list heads and values. Generic
  SDK clients do not inherit that exemption.

The station reserves one socket key and two generic pthread registry keys in
fixed application RAM. Native destructors remain intact. Fixed libc environment
storage avoids the previous empty-environment allocation. Remaining driver
per-thread values follow the scoped ownership above.

Before project application entry, SDK interrupt bookkeeping and logging made
15 successful requests totalling **209 bytes**. These are reported separately
from application heap use and the Wi-Fi exception. Boot ROM and the separate
bootloader precede this measurement.

The supporting [startup](station-startup-owners.csv),
[network](station-network-owners.csv), and [demo](station-demo-owners.csv)
inventories retain their original measurements. They describe separate runs;
do not sum their totals or substitute them for the final disconnect inventory.

## Exhaustion and recovery verification

The parser admits an object containing 510 Booleans, using all 512 slots;
one additional Boolean is rejected. Host tests repeat admission, rejection and
successful reuse 20 times with heap interception enabled:

```text
python -B -m unittest discover -s tests/target -p test_admission.py -v
```

On the ESP, a 1,033-byte Apply containing 513 array elements was rejected.
Active and saved revision 943 and CRC 3,623,560,383 were preserved. The next
valid idle Apply and both peripheral reads succeeded. Rollback and retained
cleanup failure paths are exercised by the [fault-injection tests](../../../tests/README.md).
The [TCP packet probe](../../../tools/target/tcp_packet_probe.py) executes linked
ESP instructions to check segment splitting, capacity and slot reuse; it is
separate from physical radio acceptance.

The [allocation recorder](../../../tools/target/heap_census.S) and
[unwind-rule generator](../../../tools/target/station_census_rules.py) retain
reproducible instrumentation. Bind the diagnostic ELF and unwind rules by hash;
require zero dropped sites/pointers and reconciliation with the independent audit.
[Target tools](../../../tools/target/README.md) describe the maintained probes.

## Qualification limits

The measured scope is one authenticated broker with the reviewed certificate
profile, scheduling model and BMP388/LD2450 workload. The physical run included
broker restart, TLS renewal and both peripheral reads. It does not qualify:

- arbitrary certificate chains, additional SDK clients or simultaneous brokers;
- forced concurrent USB parsing and certificate verification, which can reach
  the general allocator;
- all WPA security events, BLE/coexistence workloads or indefinite reliability;
- optional actuator loads, visible OLED operation after idle transition, or
  unmeasured electrical and cache-off timing.

Changes to certificates, scheduling, drivers, SDK pins or supported workloads
require new measurements. Use the [hardware procedure](../../../HARDWARE_ACCEPTANCE.md)
and [physical demo summary](../../../tools/configurator/README.md#physical-verification).
Existing source hashes and inventories are evidence identities, not values to
rewrite when source files change.
