# Wi-Fi static memory and resource assessment

## Current network resource assessment

**The transaction image passes fixed-linker budgets but does not satisfy the
whole-system no-application-heap requirement yet.** On 17 November 2026 the user
authorized removal of the ESP HTTPS service, replacing the earlier requirement
to retain it alongside MQTT. The maintained image now has one MQTT TLS client;
full-profile setup/recovery uses USB. The host Desk and Home Assistant web
interfaces remain available. No Wi-Fi-driver exception automatically exempts
TLS, TCP/IP or ESP-TLS contexts.

### Tribunal static-memory check, 20 November 2026

**The demo firmware fails the no-application-heap requirement.** The tribunal
change request covers startup and normal operation, including network
communication. It permits a justified Wi-Fi-driver exception. The MQTT client
uses TLS certificate verification and TCP/IP on behalf of the application.
Both have confirmed heap paths. The exception does not cover either path.

The image under review is the tested ESP32-C3 application from `370d09e`,
SHA-256 `4fd99ab283ee237084a1f1f83cc6b49849850973707867c9bb27ec19ab20d8d5`.
The firmware and target-build inputs in the committed checkout at `b74f4db`
match that revision. The archived sources and private provisioning inputs
verified against their build manifest. Local, uncommitted edits require their
own build before these results can be applied to them. The pinned C gate
passed on 336 committed files. All 131 selected budget, lifecycle, storage,
queue, parser, fault and exhaustion tests passed on that clean checkout.

| Tribunal check | Result for the demo image |
|---|---|
| Fixed sub-module and total budgets | **Pass for linked writable data.** All 34 modules with writable state and every group fit the limits below. Stack-local temporaries use their owning task's stack. |
| Fixed buffers, parsing, validation and exhaustion | **Verified for the tested core paths.** The C admission tests reject oversize input and recover from an exhausted parser pool with heap calls trapped. Queue and lwIP record tests fill the pools, reject the next request and reuse released records. |
| Configuration changes, supervisor and inter-task communication | Fixed configuration stores, startup-created workers, queues and locks have lifecycle and fault tests. The board switched rules through 64 reads on one boot. |
| Kernel objects created at startup only | **Pass in the measured workload.** Startup counters recorded 11 task, 41 mutex, 54 semaphore and one flag creation calls. All eleven normal-mode creation counters were zero before and after the 64 reads. The evidence covers this boot and workload. |
| Heap-free application at startup and during normal operation | **Fail.** The exact ELF calls the general allocator from TCP/IP. Its certificate verifier made 492 allocation requests in the measured verification phase. Aggregate board counts lack caller attribution. |
| Wi-Fi exception | The permitted scope is the Wi-Fi driver. A caller-level inventory and justification for any remaining driver allocations are outstanding. |

For the overflow demonstration, run these commands from the repository root
with GCC, objcopy and the project's pinned formatter available. The first
suite proves the allocation trap detects a deliberate heap call, then checks
the exact input limit, parser exhaustion and reuse. The second exercises a
full queue and its recovery. The third fills the native record slots and
checks that releasing one makes exactly that slot reusable.

```powershell
python -B -m unittest discover -s tests/target -p test_admission.py -v
python -B -m unittest discover -s tests/target -p test_network_pool.py -v
python -B -m unittest discover -s tests/target -p test_lwip_records.py -v
```

#### Fixed RAM in the tested image

The map and ELF hashes were checked against the qualified build before
recomputing these numbers. Each row uses bytes. Total DRAM includes the four
preceding DRAM categories once. RTC is a separate region.

| Region | Linked | Budget | Headroom |
|---|---:|---:|---:|
| Application writable data | 143,332 | 147,456 | 4,124 |
| Wireless task pool | 18,499 | 18,560 | 61 |
| SDK and kernel writable data | 54,257 | 55,296 | 1,039 |
| RAM-resident code | 56,386 | 57,152 | 766 |
| Total fixed DRAM | 272,474 | 278,528 | 6,054 |
| RTC | 60 | 128 | 68 |

The table below lists the fixed writable state attributed to each project
module by the map. Code-only modules borrow caller storage or use the stack.
`c3task` is charged to the wireless task pool. The other entries are charged
to application writable data. [Build policy](../../../tools/target/transaction-budget.json)

| Module | Linked bytes | Budget bytes | Module | Linked bytes | Budget bytes |
|---|---:|---:|---|---:|---:|
| `activate` | 3,955 | 3,955 | `c3task` | 18,499 | 18,504 |
| `c3app` | 37,568 | 37,728 | `c3tls` | 3,168 | 3,200 |
| `c3audit` | 184 | 184 | `c3tout` | 25,532 | 25,536 |
| `c3bind` | 16 | 16 | `c3tx` | 813 | 880 |
| `c3boot` | 12,288 | 12,288 | `c3uart` | 21 | 32 |
| `c3event` | 450 | 450 | `c3ukern` | 8 | 8 |
| `c3i2c` | 60 | 64 | `c3umem` | 1,280 | 1,280 |
| `c3lrec` | 352 | 352 | `c3usb` | 1 | 16 |
| `c3mqtt` | 1,816 | 1,824 | `c3wsta` | 51 | 64 |
| `c3net` | 67 | 96 | `cfgjson` | 25 | 32 |
| `c3nq` | 12,090 | 12,096 | `health` | 212 | 224 |
| `c3peer` | 4,105 | 4,112 | `main` | 1,032 | 1,040 |
| `c3pin` | 132 | 144 | `msgbus` | 2,240 | 2,240 |
| `c3pulse` | 59 | 64 | `sampcache` | 1,440 | 1,440 |
| `c3pwm` | 115 | 128 | `shared` | 68 | 80 |
| `c3spi` | 60 | 64 | `tkport` | 33,428 | 33,428 |
| `c3store` | 312 | 320 | `watchdog` | 384 | 384 |

#### Why network memory fails

* **Physical workload.** Two sealed snapshots on the same idle configuration
  bracketed 329.163 s. The SDK heap hooks counted 11,478 allocations, 11,482
  frees and 3,205,556 cumulative bytes requested in that window. The boot
  also recorded 102 allocations after the application entry marker and before
  sealing. These totals give neither the caller nor peak live RAM. The
  controller and idle stacks retained measured low-water margins of 5,088
  and 896 bytes. [Board results](../../../tools/configurator/README.md#repeated-bmp388-and-radar-switching)
* **TLS.** Replaying the compiled verifier against the public demo certificate
  reproduced a 5,946-byte peak in requested live payload during verification.
  Two PSA table allocations of 1,408 and 704 bytes survived cleanup. The test
  accepted the broker and rejected both the wrong hostname and an empty trust
  list. This is an instruction-level measurement with simulated allocator and
  hardware services. [Verifier scope](#physical-demo-broker-acceptance)
* **TCP/IP.** Disassembly of the same ELF shows `memp_malloc` calling
  `do_memp_malloc_pool`, then `mem_malloc` and libc `malloc`. The packet-buffer
  allocator `pbuf_alloc` also calls `mem_malloc` directly. A fixed record pool
  alone leaves this second path active.
* **Sizing.** Compiling the pinned `memp.c` for RV32 with its recorded build
  arguments and only `MEMP_MEM_MALLOC=0` after `lwip/opt.h` produced 30,425
  bytes of pool backing symbols. `PBUF_POOL` accounts for 24,195 bytes.
  Even omitting the two record pools already replaced by `c3lrec` leaves
  30,075 bytes against 6,054 bytes of total fixed headroom. This is a layout
  comparison of one object. Live occupancy, additional metadata, separate
  variable packet payloads and simultaneous TLS demand need their own accounting.

Reproduce the map check with `tools/target/memory_budget.py` in the pinned SDK
Python using `--budget tools/target/transaction-budget.json`. For the pool
comparison, compile an **isolated** `memp.c` object with that build's compiler
command and an include that selects the static-pool mode after the original
options. Sum only `memp_memory_*_base` sizes with `riscv32-esp-elf-nm -S`.
The private reports for this check are under `build/static-requirement-20261120/`.

The closure test is a complete cold boot, authenticated MQTT connection,
normal traffic, broker restart and failure recovery with fixed TLS and TCP/IP
storage. Exercise full pools, rejection and reuse during the same design
qualification. Attribute any remaining heap traffic to the documented
driver exception and verify all group limits on the resulting image.
[Static design and recovery contracts](../../../firmware/README.md#mandatory-static-memory-change)

#### Measuring TCP/IP storage for one broker

The sizing target is one authenticated MQTT broker, including a reconnect, under
the existing RAM caps. The diagnostic built from `0318bdb` uses the production
allocator with lwIP's native counters. In an isolated build, add
`CONFIG_LWIP_STATS=y` to `wifi-station.defaults` and add
`add_compile_definitions(MEM_STATS=1 MEMP_STATS=1)` before the project includes
ESP-IDF in the board's top-level CMake file. All lwIP components and the
application's record adapter must agree on the descriptor layout. Confirm the
flags in the resulting compile commands. The ordinary firmware uses its
original settings.

The diagnostic image (`aed481c7`) added 664 bytes of linked SDK/kernel data.
It used 273,138 of 278,528 total fixed DRAM bytes. All group and module checks
passed. With the matching ELF loaded in ESP GDB and the target halted, use
the debugger command below from the repository root.

```gdb
source tools/target/lwip_memory.py
uthome-lwip-memory
```

`requested_bytes` reports live and peak requested bytes through lwIP's memory
allocator. It includes direct packet allocations and heap-backed pool records.
It excludes libc overhead, the diagnostic size prefix, and allocations that
bypass lwIP. Each pool reports live and peak **record counts**, failures and
element size. The two existing application-owned record pools bypass these
counters. A pool's own peak may occur at a different time from another's.
Use the aggregate byte peak directly for simultaneous demand. Reserve storage
by type and validate exhaustion and recovery before assigning final capacities.

An RV32 instruction replay checked the counters with two live 16-byte TCP
segment records and one 97-byte raw packet. Packet alignment and its header
brought the total to 148 bytes. An injected allocator refusal advanced both
error counters and preserved live counts. Releasing all three objects returned
live bytes to zero, retained the peak and allowed a new record. The allocator
and critical sections were simulated. This verifies the measurement method.
Board traffic is needed to size the pools.

##### Physical one-broker baseline

The ESP32-C3 ran the instrumented image on its normal profile. Seven
authenticated 96-byte status replies identified the same boot across four
additional status requests, one broker restart and a full TLS renegotiation.
The table reports live bytes at each debugger stop and the highest simultaneous
requested bytes since boot. Every reported allocation error counter was zero.

| Stop | Live bytes | Peak bytes since boot |
|---|---:|---:|
| Wi-Fi address acquired, broker offline | 488 | 688 |
| First authenticated status | 3,452 | 3,496 |
| First status after broker restart | 1,924 | 3,496 |
| First status after renegotiation | 1,924 | 3,496 |

The highest record counts were one TCP connection, two TCP segments, one UDP
connection, one API message, two input messages, one ARP queue entry, one IGMP
group, six timers and one address-lookup record. This run exercised the
normal profile and one reconnect. Additional connections in TIME_WAIT and
peripheral traffic still need coverage when sizing a production pool. The
full 4 MiB flash, saved and active profile, and laptop settings verified
after restoration.

In a separate instruction replay, a single 96-byte TCP write occupied 1,696
bytes including its connection record, segment record and 1,512-byte packet
buffer. Queueing the configured 5,760-byte send capacity occupied 6,280
bytes even before adding receive traffic or TLS. These are controlled
send-path cases. On the pinned ESP port, single-packet transmission reserves
a full segment for each new packet. Segment and send-window sizing therefore
affect the storage needed even for one small MQTT status reply.

##### One-broker TCP sizing

The Wi-Fi station uses **610-byte TCP segments** and a **2,440-byte send
buffer**, enough for four full segments. The latter is the minimum allowed
by the pinned SDK. The receive window stays at 5,760 bytes. Smaller
segments require more packets for larger writes. The broker's credentials,
certificate checks and MQTT packet limits use their existing settings.

The matched diagnostic image (`c3a8f899`) passed the same seven authenticated
status replies, broker restart and TLS renegotiation on one ESP boot. Its
peak live lwIP request fell from **3,496 to 1,884 bytes**. The Wi-Fi-only
peak was 688 bytes, the first connected peak was 1,840, and reconnect
raised it to 1,884. All recorded pool and memory error counters stayed
at zero. Both diagnostics used 273,138 bytes of fixed DRAM. The board and
laptop were restored and verified after each trial.

In the compiled send-path replay, a 96-byte TCP write used 868 live bytes
including the connection, down from 1,696. A full send window used 2,968
bytes including four segment records, down from 6,280 for the former window.
These numbers cover queued writes with no incoming traffic. The measured
1,884-byte workload peak is a lower bound for fixed-pool design. Packet
buffers, TCP/IP records and TLS can still call the heap.

The ordinary image with these two station settings, SHA-256
`7162048ddb046cb499829606ca482f44607f0fc8d84647fb2e3247b583e71a56`,
used 272,474 of 278,528 fixed DRAM bytes with diagnostic counters disabled.
After an idle-profile Apply on the same boot, eight reads made two complete
cycles of BMP388 chip ID, 16 radar bytes, three pressure bytes and 32 radar
bytes. All eight broker result frames matched their controller request IDs,
byte counts and returned data. Both chip IDs were `50`. The original full
flash and profile 943 verified after restoration. This hardware check used
the direct MQTT controller. The earlier power-bank, Desk and HA acceptance
records identify their own images.

#### Fixed TCP records

The station reserves three active TCP control blocks, one listener and 16
segment records. These pools are shared by all sockets. The two existing
socket-handle and datagram pools remain independent. The TCP allocator can
reclaim a closing connection when a new connection needs its slot.
[Record ownership and C review](../../../firmware/hal/ports/esp32c3/network/README.md#fixed-lwip-records)

The linked ESP32-C3 image uses **1,168 bytes** for all five record pools and
their lease flags, up from 352 bytes. Application writable data is 144,148 of
147,456 bytes. Total fixed DRAM is 273,298 of 278,528 bytes, leaving **5,230
bytes**. The existing group caps all pass. The station reserves 816 additional
application bytes for these records.

In an instruction-level test, a 96-byte write with all 16 segment slots
held returned `ERR_MEM`. A 2,440-byte write with only three slots available
also returned `ERR_MEM`, released its temporary packets and segment records,
and preserved the available send window and empty send queue. Releasing one
slot allowed each original write to succeed. The recovered writes used one
and four fixed segment records respectively. Packet payloads still requested
684 and 2,736 heap bytes. The emulator supplied the underlying allocator,
ROM helpers and critical sections.

On the physical ESP32-C3, image
`1143df5afe8e10f873ac921d50e43ba211c70f7655d02afb7d3650f5d01012dd`
completed two cycles of BMP388 chip ID, 16 radar bytes, BMP388 raw pressure
and 32 radar bytes on the idle profile. The broker restarted between cycles.
The controller recovered the same ESP boot, generation and last completed
request ID. All eight broker frames matched the command IDs, transmitted
and received lengths, and returned bytes. Full flash and profile 943 verified
after restoration. TLS allocations and other lwIP memory still need fixed
storage for whole-system acceptance.

#### Fixed outgoing TCP packets

The Wi-Fi station keeps the 610-byte MSS and 2,440-byte send window. Moving
ring descriptors into the eight slots that actually hold queues freed 1,152
application bytes. The new TCP packet adapter uses 3,681 bytes. Five aligned
transport slots take 3,440 bytes, two control slots take 240 and one byte
tracks whether the core is splitting a segment. The final image uses
146,677 of 147,456 application bytes and 275,670 of 278,528 total fixed DRAM
bytes. Every module and group fits its cap.

In a replay of the linked ESP instructions, a full-window write with one
ordinary packet already held returned `ERR_MEM` and kept the send window
and queue intact. It also refused the write after just one of two references
to that packet was released. Once its last reference was released, the same
write queued all four segments. A simulated reduction in peer window split
the first segment into 1 and 609 bytes. All 2,440 payload bytes remained in
order. Control packets could still be allocated, and all four ordinary slots
were reusable after cleanup. The emulator trapped general heap calls and
observed zero throughout this test. ROM copies and interrupt guards were
simulated. Other protocols, receive-side allocations and TLS certificate
verification remain outside this result.

On the physical ESP32-C3, firmware
`403605498d6772f2c9357cfee6eb3fa7a0a9f8eddd96f716885a664516ffa192`
completed four BMP388/LD2450 reads before a broker restart and four after it.
All eight replies matched the broker frames on one ESP boot. Full flash and
both copies of profile 943 verified after restoration.

The same ELF still calls `mem_malloc(28)` in `esp_pbuf_allocate`. With the
station's receive-copy option disabled, this record tracks a driver-owned
incoming frame until its last lwIP reference is released. Its peak
simultaneous demand and deterministic recovery still need qualification.

Run from the repository root using the optional Unicorn and pyelftools probe
dependencies and the ESP toolchain debugger. Pass the matching qualified ELF.

```powershell
python -B -m tools.target.tcp_packet_probe --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

[Packet ownership and C review](../../../firmware/hal/ports/esp32c3/network/README.md#outgoing-tcp-packets)

#### Fixed Wi-Fi receive records

The station reserves eight 28-byte records for the configured eight dynamic
radio receive buffers. The 224 bytes of metadata belong to the application.
The driver retains ownership of the variable-size payload buffers until lwIP
returns the corresponding tokens. Payload allocation in the radio driver
still belongs in the Wi-Fi exception inventory.

This image uses 146,901 of 147,456 application bytes and 275,894 of 278,528
total fixed DRAM bytes. The source binding is confined to the station input
function. The compiled ESP32-C3 replay exercised eight live packet leases,
pool exhaustion, an extra reference, final release, input refusal, a reused
slot and an oversized length. The simulated driver received 13 distinct
tokens exactly once. No general heap call occurred in the tested path.
The replay supplies the downstream input callback and the radio buffer-return
callback. The existing full-window TCP send replay also passed on this ELF.

The intermediate firmware `147011d2` answered authenticated MQTT status on
the physical board. The idle-profile Apply timed out. The trial restored and
verified the original flash and profile 943 before any sensor read or pool
observation. This run establishes normal-profile connectivity only.

Run the linked receive test with the ESP debugger and the same optional Python
dependencies as the outgoing TCP test.

```powershell
python -B -m tools.target.wifi_receive_probe --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

[Ownership and C review](../../../firmware/hal/ports/esp32c3/network/README.md#wi-fi-receive-records)

#### Fixed TCP/IP message handoff

The station's incoming frame references and the messages posted to the TCP/IP
thread have independent ownership. The final image reserves eight messages for
input frames and eight for API callbacks. Each native message takes 16 bytes
plus a one-byte lease flag. This adds 272 bytes to the existing record module.
The application uses 147,173 of 147,456 bytes. Total fixed DRAM is 276,166 of
278,528 bytes. The existing group caps and every module limit passed. The
application and transaction adapter reservations were tightened to 37,568 and
864 bytes respectively to keep the sum of reservations inside the cap.

Firmware `d4edb5d9`, ELF `4aaa0ddb`. The instruction replay queued eight
input frames and eight callbacks together, filled both pools, dispatched the
messages, refused a mailbox post and refused a frame at the downstream input.
Across the receive and handoff tests all 23 driver buffers were returned once
and no general heap calls occurred. The mailbox post, terminal Ethernet
handler and driver return were simulated. The compiled station input, TCP/IP
enqueue, dispatch and release paths ran. The full-window send regression
preserved all 2,440 bytes on the same ELF.

```powershell
python -B -m tools.target.wifi_receive_probe --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb --handoff
```

On the board, the normal profile answered authenticated MQTT status. One live
switch saved and activated idle revision 944, but MQTT readiness then timed
out. In a separate run the board booted from that saved idle profile. On one
boot it completed chip ID, a 16-byte radar read, raw pressure and a 32-byte
radar read both before and after a broker restart. All eight controller results
matched their individual MQTT result frames. A debugger attached after the
last read found three used receive slots over the boot lifetime and zero
currently leased. The next MQTT reply had a new boot ID, so this first snapshot
could not independently identify the boot whose slots it sampled. The trial
restored and verified the full 4 MiB, both original profile revisions (943)
and the laptop network settings.

A further run attached the USB debugger before booting the saved idle
profile and repeated the four reads on each side of a broker restart.
The receive records use first-fit allocation. The release callback remains
set in every slot used on that boot, so its presence records the lifetime
peak without changing the firmware. After the eighth read, a single halt
captured three previously leased slots out of eight, zero live leases, the
transaction boot ID and last completed ID 16. MQTT confirmed the same boot
and request ID after resuming. This measures concurrent frame references in
the tested workload. It does not measure driver payload bytes or peak use of
the two TCP/IP message pools. All eight wire results matched. The full flash,
active and saved profile 943, and original network settings were restored
and verified after the measurement.

The driver is configured with six static RX buffers, up to eight dynamic RX
buffers and up to eight dynamic TX buffers. The eight fixed frame references
do not provide storage for the driver's variable-size payloads. In this ELF,
`esf_buf_alloc_dynamic` calls offset 344 of the vendor OS table. The RV32
debug type identifies this as `_malloc_internal`; the C3 adapter binds it to
`esp_coex_common_malloc_internal_wrapper`, which calls `heap_caps_malloc`.
This is one identifiable driver allocation site. Its live byte peak and the
rest of the driver's allocation sites still need attribution. Outside the
driver, the final ELF still calls the native packet allocator from
`etharp_raw` and the native record allocator from `udp_new`. The native
packet and record paths can reach libc allocation. TLS certificate verification
also remains heap-backed. Whole-system static memory qualification is open.

#### Fixed ARP and UDP storage

Firmware `89e3f994`, ELF `777261ae`. The IPv4 demo station has two fixed
40-byte UDP records for DHCP and one concurrent endpoint, down from the SDK
default setting of 16. Seven 8-byte ARP queue entries and their lease flags
support two unresolved neighbours with three waiting packets each and one
temporary append. Together these records and flags add 145 application bytes.
ARP requests and replies share a separate 64-byte frame slot. Each use sends a
42-byte Ethernet frame. The TCP transport and control reservations retain their
previous capacities.

| Fixed RAM | Linked | Limit | Available |
|---|---:|---:|---:|
| Application | 147,382 | 147,456 | 74 |
| Total DRAM | 276,374 | 278,528 | 2,154 |

All module and group checks passed. The station, transaction and configuration
module reservations now use their measured linked sizes where needed. Application
module reservations total 147,455 bytes. The application gained 209 fixed bytes
and the kernel/SDK group decreased by one byte relative to image `d4edb5d9`.

In the RV32 replay the native UDP stack allocated and bound two endpoints,
refused a third, then removed and rebound the first. The native ARP code
produced byte-exact request and reply frames. A retained reference prevented
reuse of the ARP frame. Two unresolved queues held six packets. A full queue
or exhausted pool returned `ERR_MEM` and preserved caller ownership.
Resolving one peer sent its three packets in order. Interface cleanup released
the remaining references and all seven queue records could be leased again.
The replay trapped general allocation and supplied the final link-output
callback. The existing full-window TCP and Wi-Fi receive/dispatch replays
also passed on this ELF.

```powershell
python -B -m tools.target.arp_udp_probe --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

On the physical ESP32-C3, the normal profile answered authenticated MQTT
status. After booting the saved idle configuration, the board completed a
BMP388 chip-ID read, a 16-byte LD2450 read, a BMP388 pressure read and a
32-byte LD2450 read on each side of a broker restart. All eight results
matched their individual MQTT frames. A single debugger snapshot after the
last read found the DHCP client pointing at fixed UDP slot 0 on local port
68, slot 1 free, and all seven ARP queue records free. These occupancy
figures describe the sampling instant. The same boot ID and completed request
16 were confirmed over MQTT after resume. The fresh backup and full 4 MiB
restoration verified, as did the saved and active profile 943 and host network
settings.

The same ELF calls `mem_malloc` from `dhcp_start` for a 92-byte client state
when no state has been supplied. Its `dhcp_create_msg` calls the native packet
allocator for a 309-byte message before packet metadata and headroom. Native
ARP cloning of volatile packets, the optional UDP header allocation and TLS
certificate verification also retain heap paths. The Wi-Fi driver allocation
inventory and whole-system static-memory qualification remain open.

#### Fixed DHCP client and messages

Firmware `820fb4ed`, ELF `7df94a1c`. The one-station build reserves 869 bytes
for its native 92-byte DHCP client, the client lease flag, and two 388-byte
outgoing packet slots. Each slot can carry the full 309-byte DHCP message plus
native transport headroom, alignment and a custom pbuf header. The USB
configuration pump reclaimed 2,072 bytes by reusing its request storage for
replies. On the host a discovery callback wrote the maximum 2,048-byte
response and could still read all 24 original request bytes. The pump sent
that response seven bytes at a time and handled the next queued request.

| Fixed RAM | Linked | Limit | Available |
|---|---:|---:|---:|
| Application | 146,179 | 147,456 | 1,277 |
| Total DRAM | 275,174 | 278,528 | 3,354 |

All module and group limits passed. The application gained 1,203 bytes of
headroom relative to image `89e3f994`. The linked RV32 test exercised native
discovery, unicast renewal, broadcast rebinding, release and client cleanup.
It verified both destination addresses and the encoded DHCP message types.
Four occupied TCP send slots did not block those requests. Two retained DHCP
packets kept their contents. A third renewal returned `ERR_MEM` until a
reference was released. A second interface acquired the fixed client after
the first interface released it. The old ELF triggered the same test's
general-heap trap on its first DHCP start.

The replay supplies a lease and models the clock, RNG, UDP transmission,
interrupt guard and address-change notification. It checks the client and
outgoing message lifetimes. The SDK still allocates TCP/IP timeout records
through `memp_malloc(MEMP_SYS_TIMEOUT)` and the heap-backed pool allocator.
Packet-copy paths, TLS certificate verification and the Wi-Fi driver inventory
also remain open.

```powershell
python -B -m tools.target.dhcp_probe --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

On the physical ESP32-C3, the normal profile acquired an address and answered
authenticated MQTT status. After restarting into the saved idle profile, the
board read the BMP388 chip ID and pressure bytes and two LD2450 UART samples
(16 and 32 bytes). It repeated all four operations after a broker restart.
Each result matched its MQTT frame. A debugger snapshot showed the active
network interface pointing to the fixed client, its lease flag set and the
native DHCP state `BOUND`. MQTT confirmed the same boot and completed request
16 after resume. This trial observed lease acquisition. Renewal and rebinding
were exercised in the RV32 replay. The original full 4 MiB flash verified
against the fresh backup, and the original active/saved profile 943 and
laptop network settings were restored.

#### Fixed TCP/IP timeout records

Firmware `13fe4407`, ELF `b1c5b5e6`. The station reserves twelve native
16-byte timeout records and twelve occupancy flags. The existing lwIP record
adapter grew by 204 bytes to 1,789 bytes. The scheduler's allocation, expiry
and cancellation all use this pool.

| Fixed RAM | Linked | Limit | Available |
|---|---:|---:|---:|
| Application | 146,383 | 147,456 | 1,073 |
| Total DRAM | 275,382 | 278,528 | 3,146 |

The linked image has two startup timers for periodic ARP and coarse DHCP.
The remaining linked callers serve TCP, fine DHCP, DNS, IGMP, DHCP address
conflict detection, gratuitous ARP and the lost-IP notification. The capacity
allows one record for each of these nine uses and three additional records
for the one-interface workload. A thirteenth simultaneous reservation fails.
This build's native scheduler asserts if asked to schedule with no free slot.

The RV32 execution filled all twelve slots. It checked FIFO order at equal
deadlines, first-match cancellation, TCP timer shutdown after the last
connection, and correct expiry across the 32-bit millisecond clock rollover.
Both periodic callbacks rescheduled with the list full. All twelve records
were recovered, and the tested scheduler made zero general heap calls. The
previous ELF tripped the same allocator check during timer initialization.
The DHCP replay also used the native scheduler for discovery, renewal,
rebinding and cleanup. The list was empty after the last client stopped.

```powershell
python -B -m tools.target.timeout_probe --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

On the ESP32-C3, authenticated MQTT status confirmed the initial connection.
After booting from the saved idle profile, the board read BMP388 chip ID and
pressure bytes and two LD2450 samples (16 and 32 bytes) on each side of a
broker restart. All eight payloads matched the individual MQTT wire results.
At the final read the native timeout list contained five records for TCP, ARP
maintenance, coarse DHCP, gratuitous ARP and the lost-IP notification. Every
list address matched an occupied fixed slot, and all seven other slots were
free at that instant. Peak physical occupancy over the run was not measured.
After debugger resume MQTT confirmed the same boot and completed request 16.
The full 4 MiB flash matched its fresh backup, the active and saved profiles
returned to revision 943, and the laptop hotspot and firewall cleanup passed.

#### Fixed ARP and Wi-Fi frame copies

Firmware `d4c47a74`, ELF `07420520`. An unresolved ARP entry must preserve
a volatile packet before its caller can reuse the source. A UDP send from a
borrowed payload can require a separate header. When the resulting chain goes
straight to a resolved neighbour, the Wi-Fi adapter joins it into one frame
for transmission. The station now has one 1,536-byte slot for either copy and
a separate 64-byte UDP header slot. Each slot remains reserved until its last
pbuf reference is released. A competing copy returns `ERR_MEM` and protects
the live frame.

The network queue adapter reclaimed 640 bytes. Each of its 56 slots has one
native read semaphore or mutex handle. Only the eight queue slots have a
second handle. A non-null lease key also serves as the occupied flag.
The queue counts, semaphore and mutex counts, and queue payload capacity are
unchanged. All fixed RAM limits passed.

| Fixed RAM | Linked | Limit | Available |
|---|---:|---:|---:|
| Application | 147,343 | 147,456 | 113 |
| Total DRAM | 276,446 | 278,528 | 2,082 |

The linked RV32 test sent a 1,472-byte UDP payload through native UDP, IPv4,
ARP and Wi-Fi output. It checked the outgoing 1,514-byte Ethernet frame,
addresses, ports and payload at the driver handoff. It exercised both a known
neighbour and an initially unresolved one. Releasing and overwriting the
original buffer preserved the queued bytes. While a simulated driver retained
the frame, other copies failed cleanly. All slots became reusable after final
release. Both borrowed buffers returned exactly once. The old image reached
`mem_malloc` on the same UDP send. The new image made zero general heap calls
in the tested packet path. The test supplies the Wi-Fi driver handoff.

```powershell
python -B -m tools.target.frame_copy_probe --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

On the physical ESP32-C3, four BMP388/LD2450 reads before the broker
restart and four after it matched their individual MQTT frames on the same
saved-idle boot. Two ICMP echo requests with 1,472 data bytes and the
Don't Fragment flag received full-size replies. The debugger found both copy
slots free after the pings. MQTT then confirmed the same boot and completed
request 16. The ping checks the physical MTU. The RV32 replay above covers
volatile UDP buffers and contention for the copy slots.

The original 4 MiB flash matched the fresh backup. The first USB status check
timed out after restoration. A separate read-only query then confirmed active
and saved revision 943 and both original CRCs. The laptop retained its
original uplink, and hotspot settings and firewall cleanup verified.

The linked ICMP input and IPv4 fragmentation functions still call the native
packet allocator. Certificate verification and the Wi-Fi driver allocation
inventory remain open.

#### Fixed ICMP and outbound IPv4 fragments

Firmware `efd99e48`, ELF `561430ed`. The ICMP and IPv4 fragmentation
translation units use the existing 1,536-byte frame slot. The map still
reports 147,343 of 147,456 application bytes and 276,446 of 278,528 total
fixed DRAM bytes. The 64-byte UDP header remains available independently.

The RV32 replay fed valid IPv4 packets to the linked input function. It
checked a 1,500-byte echo reply and a 56-byte port-unreachable reply,
including the quoted IP header, addresses and checksums. Holding the frame
suppressed both replies and returned each receive buffer. Releasing it
allowed the next reply. On output, the native fragmenter split a 2,968-byte
UDP datagram into IPv4 packets of 1,500, 1,500 and 28 bytes. Offsets, flags,
checksums and the reconstructed payload matched. The replay held the first
fragment at the interface. The native sender reported `ERR_MEM` and the
retained bytes stayed intact. A fresh attempt succeeded after release. A
caller can therefore see an error after one fragment has already gone out.

Both replay cases trapped general allocation on the preceding image and
completed with zero general heap calls on this image. These tests supply the
network-interface output callback. Outbound fragmentation was checked in
instruction replay. Incoming IPv4 fragment reassembly is disabled in this
configuration.

```powershell
python -B -m tools.target.ipv4_packet_probe --case icmp --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
python -B -m tools.target.ipv4_packet_probe --case fragment --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

On the USB-connected ESP32-C3, eight BMP388/LD2450 reads matched their MQTT
result frames across one broker restart on the same saved-idle boot. Both
pings with 1,472 data bytes and the Don't Fragment flag received full-size
replies. While sending these pings, the hardware breakpoint hit `0x4203b844`,
the linked echo handler's call to `c3pkt_frame`. The debugger recorded the
instruction address. Its log omitted the argument registers. Both packet
slots were free at the subsequent snapshot, and the node confirmed
completed request 16 after resuming.

A fresh 4 MiB backup verified before installation. The restored flash
matched it byte for byte. USB confirmed original active and saved profile
943 and CRC 3623560383. The laptop kept its original uplink and the temporary
hotspot and firewall rule were cleaned up.

The ELF still contains native packet allocations from DNS, IGMP, raw sockets,
netbuf and loopback output. Which of those run during the one-broker demo
requires separate evidence. Certificate verification and Wi-Fi driver
allocation attribution remain open.

#### Numeric broker lookup and multicast membership

On firmware `efd99e48` (ELF `561430ed`), the application passes a validated
numeric IPv4 address to ESP-TLS. The certificate name is supplied separately.
The linked ESP-TLS connection code calls `lwip_getaddrinfo` even for the
numeric address. The IPv4 resolver parses the literal locally, then allocates
one `MEMP_NETDB` result. The station's IGMP startup separately allocates a
`MEMP_IGMP_GROUP` record for the all-systems address, `224.0.0.1`.

| Linked-firmware replay | General heap request | Lifetime |
|---|---:|---|
| Numeric TCP endpoint lookup | 308 bytes | Released by `lwip_freeaddrinfo` after each lookup |
| Station's all-systems group | 16 bytes | Held until `igmp_stop` removes the group |

Both lookups returned the expected IPv4 address while the group remained
live. The two records accounted for 324 live requested bytes during lookup.
Neither DNS nor IGMP attempted to send a packet during these operations.
Separate controls exercised each native sender. For `broker.example` the
DNS packet requested 104 bytes. An IGMP membership report requested 80.
The replay refused these two allocations before transmission. Sizes are
requested payloads at `mem_malloc` and exclude heap bookkeeping.

```powershell
python -B -m tools.target.dns_igmp_probe --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

The first physical trace reached the group allocator at startup. The
Wi-Fi station held one membership for `224.0.0.1`, with one reference.
The loopback interface had none. MQTT readiness timed out on this run, so
the reconnect measurement used a separate backed-up trial.

In the second trial the ESP completed four BMP388/LD2450 reads, then the
debugger armed five hardware breakpoints before the broker restart. Four
more sensor results matched their individual MQTT frames on the same boot.
From arming through the final sensor result, two calls entered the native
address allocator and two entered its matching release function. The DNS
query and IGMP report packet allocation sites were never reached in that
window. Two extra stops at the release instruction single-stepped into the
interrupt vector. Counting entries into `memp_free` distinguished the two
actual calls. The station still held only its all-systems group when sampled.
MQTT confirmed completed request 16 after the debugger resumed the CPU.

Both trials restored and independently verified the original 4 MiB flash,
active and saved profile 943, laptop uplink and temporary host settings.
The 308-byte address result exceeds the remaining 113 application bytes.
The next change should remove that heap request for the validated numeric
endpoint while preserving the separate TLS certificate name. The 16-byte
station membership also needs fixed storage. Packet allocation for other
DNS or multicast workloads remains outside this measurement.

#### Fixed numeric broker endpoint

On image `1e2ae91c` (ELF `f01163e8`), the serialized MQTT connection
borrows a 32-byte address result and a 16-byte IPv4 socket address from
fixed storage. ESP-TLS copies the address, sets the port and releases the
result. The binding applies to the static Wi-Fi build's ESP-TLS socket
source. The configured certificate name still goes to the TLS verifier
separately. The previous image requested 308 heap bytes per numeric lookup.

The three MQTT topic buffers now reserve precisely the space needed for
the existing 31-character maximum node name, including the terminator. A
host test checked the complete availability, request and result topics at
that limit. The MQTT module occupies 1,802 of its 1,816 reserved bytes.
All original memory limits passed.

| Linked memory | Used (bytes) | Limit (bytes) | Free (bytes) |
|---|---:|---:|---:|
| Application fixed storage | 147,329 | 147,456 | 127 |
| Total fixed DRAM | 275,270 | 278,528 | 3,258 |

The compiled ESP-TLS socket path preserved IPv4 address and port bytes,
rejected invalid input, and recovered after socket creation failed.
A second lookup while the record was occupied failed without changing
the first address. An unrelated release left the record occupied, and the
matching release allowed reuse. Across four calls to the socket test double,
the replay recorded zero general resolver or address-lookup heap calls.
The same check detected the general resolver in the previous ELF. The
93 affected host tests and the C standard check across 342 files passed.

```powershell
python -B -m tools.target.dns_igmp_probe --fixed-endpoint --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

On the physical ESP32-C3, each set of four reads comprised BMP388 chip ID,
16 radar bytes, BMP388 pressure bytes and 32 radar bytes. Four ran before
the broker restart and four after it. Each result matched its individual
MQTT reply. The ESP retained its boot identity and confirmed request 16
after the debugger resumed it.

The debugger armed after the first four reads. Across the broker restart
it confirmed two entries into the fixed lookup and two into its release
function. Both releases received the same reserved record. The final
snapshot found that record free and one all-systems multicast membership
on the station interface. The original 4 MiB flash and active and saved
profile 943 were restored and verified. The temporary hotspot and firewall
rule were removed and the laptop retained its original uplink.

The station's 16-byte multicast membership at Wi-Fi startup remains the
next allocation to qualify. Certificate verification and the Wi-Fi driver
allocation inventory remain open.

#### Fixed station multicast membership

On image `9c2f7f33` (ELF `8c05b218`), the Wi-Fi station holds its
`224.0.0.1` membership in one fixed 16-byte IGMP record with a one-byte
occupancy flag. ESP-IDF's IGMP source uses the existing fixed record adapter
for allocation and release. The SDK still owns the group list and reference
count. The one-broker demo uses one station interface and no additional
multicast subscriptions.

| Linked memory | Used (bytes) | Limit (bytes) | Free (bytes) |
|---|---:|---:|---:|
| lwIP record adapter | 1,806 | 1,806 | 0 |
| Application fixed storage | 147,346 | 147,456 | 110 |
| Total fixed DRAM | 275,286 | 278,528 | 3,242 |

The 17-byte increase in the adapter's reservation came from the existing
application budget. All group and aggregate limits stayed the same.
In the compiled SDK replay, three successful starts and stops reused the
same slot. While the station held it, a join for a distinct group and
startup on a second interface both returned `ERR_MEM`. The original entry
retained its address and reference count. The test observed zero general
allocator calls or packet sends. The same test detected the native group
allocator on the previous firmware at the first start.

```powershell
python -B -m tools.target.dns_igmp_probe --fixed-membership --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

On the ESP32-C3, each set of four reads covered BMP388 chip ID, 16 radar
bytes, BMP388 pressure bytes and 32 radar bytes. All eight matched their
individual MQTT replies on the same boot. Immediately before the broker
restart (request 8) and after the second set (request 16), the debugger
found the station's sole membership at the linked address of the reserved
record. At both observations the occupancy flag was set, the group address
was `224.0.0.1`, its reference count was one and loopback had no membership.
The full 4 MiB flash and original active and saved profile 943 were restored
and verified. The temporary hotspot and firewall rule were removed and the
original laptop network settings verified.

The fixed broker endpoint replay also passed on this ELF. The 116 affected
host tests passed, as did the pinned BARR-C gate across 342 files. Remaining
work includes attributing general allocations during Wi-Fi startup and
reconnection, and bounding certificate verification.

#### Wi-Fi startup and broker reconnect heap callers

The station constructor in image `9c2f7f33` (ELF `8c05b218`) leaves seven
general-heap blocks live after it returns. The compiled constructor ran in
RV32 replay with the linked station defaults and a counting heap.

| ESP-NETIF station storage | Requested bytes |
|---|---:|
| Parent interface object | 104 |
| lwIP interface | 84 |
| Current and previous IPv4 information | 12 each |
| Global interface list entry | 8 |
| Copies of the interface key and description, including terminators | 13 and 4 |
| **Total payload** | **237** |

The replay followed pointers from the returned interface and the SDK list
to each distinct allocation. The lwIP interface pointed back to its parent.
The four initial object allocations were refused one at a time. Each failure
released all earlier blocks and left the interface list empty. A subsequent
creation succeeded. These requests occur before the station driver attaches.
The 237 bytes exclude allocator bookkeeping, event registration and driver
buffers.

```powershell
python -B -m tools.target.netif_creation_probe --elf PATH_TO_ESP_ELF --gdb riscv32-esp-elf-gdb
```

#### Fixed-slot certificate verification experiment

A separate RV32 replay tested the public P-256 demo broker on the six-key-slot
diagnostic ELF (`a0f482aa`). The emulator provided fixed 16-byte-aligned
addresses, handed out the smallest free slot that fit each request, zeroed
the slot and returned NULL at exhaustion. The compiled SDK performed the
certificate parsing, verification and key copy. The layout below covers
those observed lifetimes.

| Slot size (bytes) | 16 | 32 | 64 | 80 | 128 | 160 | 448 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Number of slots | 4 | 9 | 4 | 36 | 1 | 1 | 2 |

The verifier accepted the project broker, rejected a wrong hostname and an
empty trust list, and accepted the broker again. With all free slots held
immediately before verification, the first unavailable allocation caused
verification to fail. The live allocation set matched its pre-call state.
Releasing the held slots allowed that same verifier to succeed. All slots
were free after cleanup.

The 57 slots require 4,672 bytes of payload, 548 bytes above the diagnostic
image's 4,124-byte application headroom before bookkeeping. For the default
image, even a four-byte-rounded fixed-slot layout needs at least 6,228 bytes
for the observed verification trace. To obtain this lower bound, sort the
live allocation sizes at every allocation and take the largest size ever
needed at each rank. It applies to distinct, non-moving slots with one
allocation per slot. Other memory layouts can have different costs.

This experiment used a simulated allocator and hardware services. A deployable
budget must include the storage manager, remaining handshake work and concurrent
TCP/IP buffers. The original board image still uses its SDK allocators.
Private results are in `build/static-requirement-20261120/six-fixed-exhaustion.json`.

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

The six-slot ESP32-C3 diagnostic build uses the same pinned application and
SDK as the 30-slot comparison. All 1,050 C/C++ compile commands selected
the six-slot configuration. The build passed the 334-file coding-standard
gate and the same fixed-memory budget.

| Fixed allocation | Six slots | Budget | Headroom |
|---|---:|---:|---:|
| SDK/kernel | 54,318 bytes | 55,296 bytes | 978 bytes |
| Total fixed DRAM | 272,522 bytes | 278,528 bytes | 6,006 bytes |

Reducing the table from 30 slots to six saved 960 bytes of fixed DRAM.
In RV32 emulation of this exact ELF, six native keys filled the table.
The seventh import returned `PSA_ERROR_INSUFFICIENT_MEMORY` and an empty
handle. After releasing one slot, SHA-256 and SHA-384 derivations succeeded
and the other five keys retained their contents. The probe matched 80
derivations and two known answers. The 821,616-byte application has SHA-256
`7670fc069cdbf1c82c7ca4b3a014560e849f884c06e558fee29be4fd472a1332`.

On the physical ESP32-C3, the six-slot image joined Wi-Fi on the normal
saved profile and answered an authenticated 96-byte MQTT status request.
After a full broker stop and restart, the ESP reconnected and answered
a second request. The server then requested a full TLS 1.2 renegotiation
on that connection and the ESP answered a third request. The three replies
matched distinct request IDs on the same boot and generation. The TLS peer
recorded two server Certificate messages and four Finished messages on the
reconnected session.

The trial ran without a debugger. All 4 MiB of original flash and the
active and saved profile were verified after restoration. Wi-Fi group-key
renewal during renegotiation remains open for production qualification.

A separate 600-second observation used the same six-slot image and a Windows
2.4 GHz hotspot. The debugger detected the initial group-key installation.
It detected zero subsequent group-key installations or group replies during
the window. The ESP answered two distinct authenticated 96-byte MQTT status
requests on the same TLS connection, boot and generation, one before the
watch and one after. With no renewal cadence to target, the peer kept the
original connection and issued no renegotiation request. The hotspot's renewal
interval and the ESP's behavior during overlap remain unknown. Resume the
overlap check with a known renewal time or a configurable access point.
The full original flash, active and saved profile, and host cleanup were
verified. [Observer procedure](../../../tools/target/README.md#observing-wi-fi-group-key-renewal-on-the-six-slot-image)

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

OpenOCD could not identify the running target in that run. In a later
attempt, attaching GDB after the first status reset the core and broke the
connection. The event tally disagreed with the native table at the end of
that attempt, so its count was excluded.

In the corrected run on firmware `2b3f703`, GDB attached before Wi-Fi
started and stayed connected. After the first authenticated MQTT status,
a scan of the initialized PSA table found two occupied slots. The ESP
answered a second status query with the counters armed. The server then
requested renegotiation, and the ESP answered a third query. Each 96-byte
reply matched a distinct request ID on the same boot and generation.
The TLS peer recorded two server Certificate messages and four Finished
messages on one connection.

| ESP phase | Occupied at start | Reservations | Wipes | Peak occupied | Occupied at end |
|---|---:|---:|---:|---:|---:|
| Server request through the next verified MQTT status | 2 | 11 | 11 | 5 | 2 |

A second native table scan confirmed the final count of two. The full
4 MiB flash and saved profile were restored and verified. Five is the
observed peak for this workload. Simultaneous Wi-Fi group-key renewal
remains unmeasured.

#### Wi-Fi key lifetime during TLS renegotiation

The linked WPA2 station code in `2b3f703` uses the shared PSA store for
message integrity and key unwrapping. In the SHA-1 integrity path,
`hmac_vector` imports a temporary HMAC key and destroys it after use.
In the AES unwrap path, `aes_decrypt_init` imports a temporary AES key;
`aes_decrypt_deinit` destroys it after the unwrap. The driver installs the
group key through a separate call. These call paths identify where a Wi-Fi
key can occupy a slot while TLS is active. They do not establish how many
Wi-Fi keys are live at the ESP's TLS peak.

A host test held one independent AES-128 key across a server-requested TLS
1.2 renegotiation in the 30-slot fixture. Two controls peaked at five slots.
Both runs with the extra key peaked at six. Each completed two full handshakes
on one connection and echoed 96 encrypted bytes. The extra key decrypted a
known block before and after renegotiation. Releasing it returned occupancy
from three to two. This is a software measurement with deliberately overlapping
key lifetimes. A live ESP group-key renewal remains unmeasured.

To check that boundary, two laptop builds of the same fixture changed only
`MBEDTLS_PSA_KEY_SLOT_COUNT` in the crypto configuration. All 117 C
translation units in each build selected its configuration. The extra key,
when used, was imported after the initial verified handshake and kept until
renegotiation finished or failed.

| Fixed host slots | Renegotiation with no extra key | Renegotiation with one extra AES key |
|---:|---|---|
| 5 | Passed twice, peak 5 | Failed twice at the next reservation with all 5 slots occupied |
| 6 | Passed twice, peak 5 | Passed twice, peak 6 |

Both failures reported `PSA_ERROR_INSUFFICIENT_MEMORY` in the native slot
allocator. Neither connection completed the second handshake or echoed any
application bytes afterward. The independently held key remained usable and
was released in both cases. Every successful run echoed 96 bytes after
renegotiation. Six slots sufficed for this controlled overlap. Production
capacity remains open pending board evidence.

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

### Broker certificate parser heap

The diagnostic six-slot RV32 ELF ran its compiled `mbedtls_x509_crt_parse_der_nocopy`
on the bench broker's P-256 certificate and a freshly generated RSA-2048
compatibility chain. Each row includes the certificates parsed in earlier rows
of the same chain. The 952-byte chain head was already reserved in firmware.

| Input processed | DER bytes added | Peak live heap bytes requested | Heap bytes still live after parse |
|---|---:|---:|---:|
| Bench broker leaf | 488 | 461 | 32 |
| RSA leaf | 850 | 650 | 0 |
| RSA first intermediate | 816 | 1,602 | 952 |
| RSA second intermediate | 816 | 2,554 | 1,904 |

The bench leaf retained two 16-byte subject alternative name nodes. For the
RSA chain, each intermediate added a 952-byte certificate record that stayed
live until chain cleanup. Temporary RSA public-key parsing added 650 bytes
at the peak. Cleanup released all tracked requests for both inputs. Failing
each of the 11 bench and 14 RSA allocation requests in turn returned an error.
Every retry reproduced the original peak and retained blocks.

These are concurrent requested payload bytes in instruction emulation.
Allocator overhead, the input record, fixed storage, trust verification and
the subsequent handshake copy of the peer public key are outside the figures.
The measured chains do not establish a maximum for other brokers. The two
intermediate records are a distinct storage cost to account for when preserving
the tested chain compatibility. [Probe and reproduction](../../../tools/target/README.md#measuring-broker-certificate-parsing)

#### Trust verification and peer-key copy

The same six-slot ELF verified both fixture chains with the configured hostname
and the SDK default X.509 profile. The native verifier rejected the wrong name
and an empty trust list. The compiled peer-key parser consumed the entire leaf
public key after the certificate chain was released.

| Concurrent requested heap payload | Bench P-256 | RSA-2048 with two intermediates |
|---|---:|---:|
| CA bytes retained after loading the configured PEM | 433 | 816 |
| Peak during X.509 trust verification, including CA and chain | 3,834 | 6,994 |
| Peak when copying the peer key after freeing the chain | 894 | 1,466 |
| Still live after key cleanup | 433 | 816 |

Removing the CA released the remaining bytes. The public-key copy used inline
SDK key storage after its temporary allocations were freed. A 6,006-byte
payload ceiling admitted the bench verification and caused the RSA verification
to fail closed. Both passed again with the limit lifted. The RSA peak exceeds
the total fixed DRAM headroom by 988 bytes before allocator overhead.

The instruction probe modeled ROM operations, SHA-256, MPI hardware and its
completion interrupt. Signature verification ran in the compiled SDK. This
measurement excludes transport and whole-handshake peaks. The selected image
has the SDK calendar-date check disabled; these results establish signature,
chain and hostname checks only. Local evidence is under
`build/tls-verify-memory-20261119/`.

### Shared USB views and peer intermediates

The static Wi-Fi build now shares the existing 4,096-byte USB configuration
workspace with up to four temporary peer intermediates. The default and six-slot
images both passed the pinned 336-file C gate, target build, source snapshot
and linked memory budgets. The JSON, TLS adapter and build-qualification suites
passed 87 host tests. Top-level RAM limits are unchanged.

| Trusted broker chain | Peak verification payload | Outcome at 5,990 bytes |
|---|---:|---|
| Project broker, P-256 leaf | 3,834 bytes | Accepted |
| RSA-2048 leaf and two intermediates | 5,090 bytes | Accepted |
| RSA-2048 leaf and six intermediates | 6,978 bytes | Rejected |

The six-slot image leaves 5,990 bytes of total fixed DRAM headroom and 4,124
bytes of application headroom. The ceiling experiment limits the sum of live
allocation requests in the emulator. A deployable fixed allocator must also
meet its group budget and cover alignment, bookkeeping and other simultaneous
handshake allocations. The firmware continues to support the SDK's normal
chain handling. All three fixtures verified at normal allocation, rejected a
wrong hostname and an empty trust list, and released all tracked allocations.
The seven-certificate fixture was 8,018 PEM bytes.

The parser-only RSA peak fell from 2,554 to 650 bytes. The emulator exercised
a live USB document, failed appends and retries, all four shared nodes and the
first additional SDK node. Both owners recovered their storage. The normal
broker is the physical demo acceptance target. General fixed-memory proof and
a simultaneous Wi-Fi renewal remain open. Local evidence is under
`build/tls-shared-20261119/`. The six-slot ELF SHA-256 is
`a0f482aa97de3f0319c986a61947e4076d8464974cd233c838e33a6f7acdbbb5`.

### Physical demo-broker acceptance

On 19 November 2026 the USB-connected ESP32-C3 joined the temporary 2.4 GHz
hotspot with the normal saved profile. The board ran the repository-default
firmware from commit `370d09e6f16f500292f9f9c6ab25ae77f9ef8c66`, with the
configured P-256 broker certificate and MQTT credentials. The controller
matched three fresh request IDs to 96-byte replies on one ESP boot.

| Board check | Observed result |
|---|---|
| Initial connection | The broker and ESP completed TLS and exchanged a status request and reply. |
| Broker restart | A fresh broker accepted a new connection and the ESP returned the second status. |
| Server-requested renegotiation | Both peers sent a second Finished message on the reconnected session. The third status arrived afterward. |
| Recovery | The complete 4 MiB original flash matched its verified backup. Active and saved profile revisions and CRCs matched their preflight values. The temporary hotspot and firewall rule were removed. |

On the default ELF, the separate instruction test accepted the demo broker and
rejected a wrong hostname and an empty trust list. Its verification peak was
5,946 requested heap bytes including the dynamic PSA tables. Cleanup left the
two expected table allocations of 1,408 and 704 bytes. Calendar-date checks are
disabled in this SDK configuration. The live board trial exercised the valid
broker. Its application SHA-256 was
`4fd99ab283ee237084a1f1f83cc6b49849850973707867c9bb27ec19ab20d8d5`.

This completes the demo-broker TLS acceptance target. General fixed-memory
qualification, Wi-Fi key-renewal overlap, idle-profile timing and end-to-end
physical Home Assistant acceptance have separate evidence requirements. Local
trial evidence is in `build/tls-demo-broker-20261119/verified-result.json`.

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
