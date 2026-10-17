# HLK-LD2450 UART driver

Bounded 256000 8N1 reports: 30 bytes, header `AA FF 03 00`, footer `55 CC`,
up to three signed target positions/speeds/resolutions. Basic live readings
passed; the stationary empty-scene check and complete acceptance remain open.

## Modules and ownership

| Module | Responsibility |
|---|---|
| `uartio` | Portable exclusive ownership, checked byte bounds and monotonic time |
| `c3uart` | Existing kernel UART1 device, serialized nonblocking RX/TX |
| `uartfake` | Deterministic host buffers, clock, overflow and cleanup faults |
| `ldparse` | Bounded 30-byte streaming parser with independent state |
| `ld2450` | One borrowed UART, finite polling, stale-data and fault recovery |
| `ldinst` | Registry binding, typed queue publication and health reporting |

The parser adapts Member 3's locally cached `feat/uart-gpio` contribution:
`974eb9c2e3792a1be70a64a4ba7fa9c322ba150d`, author `kimNewpsysk1t`.
It retains the sliding-prefix recovery and signed-magnitude decoding, replaces
the busy read loop with bounded polls, and uses common millisecond samples.
No branch was merged. Remote freshness has not been established.

Protocol authority is Hi-Link's **HLK-LD2450 Serial Communication Protocol
v1.03**, sections 1.2 and 2.3: [manufacturer product page](https://www.hlktech.com/en/Goods-226.html)
and [manufacturer PDF, hosted mirror](https://make.net.za/wp-content/datasheets/HLK%20LD2450%20Serial%20Communication%20Protocol%20v1.03.pdf).
The manufacturer's example is a host fixture. Reports are little-endian,
30 bytes, with three preserved eight-byte target slots and no checksum.
For coordinates/speed, bit 15 means positive; the low 15 bits hold magnitude.
An all-zero slot is absent. No occupancy inference or filtering is added.

## Binding recipe

1. Zero one `ldinst_t` per configured instance and set its borrowed `p_port`.
   On C3 use `c3uart_port()`, which performs no I/O. Only one instance can own
   that physical UART; host tests use two separate fake UARTs to prove context
   independence. Generality does not imply unlimited hardware ports.
2. Register `*ldinst_descriptor()` and bind by catalogue ID `hlk_ld2450` using
   the existing generic registry. Copy `ldinst_config_t` into activation config
   storage. Include the plan's source index and target-report queue handle.
3. Map profile UART settings to `uartio_config_t`: baseline port 1, TX21/RX20,
   256000 baud, 8 data bits, no parity, one stop bit, 512-byte receive capacity.
   Map the report timeout to 200..500 ms; the initial fixture uses 200 ms.
4. Provide `ldinst_step`, `ldinst_cleanup` and the instance context as the worker
   callbacks. Use an explicit **10 ms periodic polling worker**, initially
   3072-byte stack and priority 12; validate actual stack use at final acceptance.
   This is the adapter for the catalogue's stream workload. Native readiness
   dispatch is not implemented; the production binder uses this polling worker.
5. Cleanup only after that worker stops. Port/callback/context storage remains
   borrowed through successful cleanup, including failed setup. A competing
   instance cannot close or release the current instance's UART.

No changes to generic runtime/activation dispatch were needed. Full target
admission must still check queue type/size/routes, port conflicts and resources
before applying a profile. Configure checks queue existence and source bounds;
it does not replace admission. Sensor baud or tracking-mode commands are not
sent; the physical sensor must already use the selected baud.

## Transport limits

Each call accepts at most 64 bytes, with immediate retry on contention/no data.
TX may accept fewer bytes than requested; the count means accepted, not fully
sent on the wire. Read errors invalidate the entire returned batch. Lifecycle
changes require the controller and stopped users. Driver polling is single-owner;
direct concurrent component reads or native `sera` access are forbidden.

The C3 adapter wraps native `sera` with an exclusive device handle and one
kernel mutex. It queries available bytes before reading; asking for unavailable
bytes would cause the native driver to sleep while holding its device lock and
could leave a waiter registered. TX reserves the ring's one empty slot. Kernel
send/receive timeouts are also set to polling. Flush drains only the entry
snapshot, bounded by the compiled ring, without waiting for a quiet wire.

Native baud/mode setters only cache attributes. Setup therefore sets them and
performs a checked close/reopen to apply the hardware settings. The pinned
kernel frees a descriptor before returning a driver's close error. An unexpected
close failure poisons the wrapper and retains its application claim until reboot;
it never retries a potentially recycled descriptor. A mutex deletion failure is
retryable. These boundaries were source-reviewed and compiled, not executed on
hardware or replaced with a new fake kernel interface.

The backend supports the fixed native UART1 pins, 9600..460800 baud, 5..8 data
bits, 1/2 stop bits and none/odd/even parity, without flow control. The radar
driver narrows this to 8N1. GPIO20/21 remain reserved from the shared GPIO ledger
even when UART is closed. USB uses its separate reserved pins. No second UART
driver, SDK register path, DMA, ISR, heap allocator or C11 feature was added.

Use the unchanged `0001-uthome-c3-resources.patch` alongside the compatibility
patch in an isolated build. Its 513 ring entries provide **512 usable bytes**
per direction and it disables unused ADC startup. The wrapper rejects requests
larger than the compiled usable capacity. The helper records both patch hashes.
The kernel checkout and dependency pins remain pristine. The fixture retains
the upstream 32 KiB kernel heap; the configuration application uses 64 KiB.
Neither figure establishes spare memory under combined load.

## Timing, recovery and diagnostics

One poll processes at most 64 bytes, publishing the latest complete report in
that batch. All complete frames increment the parser's frame counter; earlier
reports in the same batch are intentionally superseded. Completion timestamps
use the kernel's monotonic millisecond clock. No new sample means no publication.
The report timestamp is host receipt completion, not a sensor timestamp.

Noise does not postpone timeout. At timeout, partial state is reset and one
zeroed timeout sample is emitted; subsequent polls discard the buffered snapshot
before receiving fresh data. Another timeout can occur after a further full
interval. This conservative policy may discard a report that arrived during a
scheduler stall; stale buffered bytes must not masquerade as a fresh report.
Overrun, framing/parity or I/O errors likewise reset fragments and request a
bounded flush. The sensor can recover on a later valid frame.

Parser counters track complete frames, rejected complete-frame footers and
discarded bytes. Driver counters track timeout intervals, observed overflow
epochs and other read faults. Native error flags coalesce faults, so overflow
counts cannot claim the exact number of lost bytes. Health counts one framing
episode per affected poll (including a later valid frame in that batch), plus
observed timeout/overflow/I/O errors. Shared-lock contention can undercount
health. Per-instance counters and `last_io_error` remain available.

`UART_RX_OVERFLOW` appends health code 7; codes 0..6 are unchanged. Errors
publish zeroed targets with timeout or I/O status. Sequence advances for each
publication attempt, including queue contention; idle polls do not advance it.
Queue overflow follows the existing configured policy. Timebase/state failure
stops the worker for controlled cleanup instead of publishing invented time.

## Verification and manual C review

`tests/target/uarttest.c` checks manufacturer values, signed limits, preserved
slots, fragmented headers, bad footers/noise recovery, byte bounds, short TX,
exclusive ownership, partial setup, cleanup retry, timeout despite noise,
overflow invalidation, bounded batches, independent registry instances, typed
queue records/health and activation recovery. The host kernel manually invokes
steps; it does not prove native scheduling, mutex/device semantics or wire timing.

The mandatory BARR-C gate covers 113 owned C/H files. All 115 Python suite tests
pass (54 checker, 26 foundation, 18 runtime, 17 target), including the extended C
fixture and resource-patch failure evidence. New ordinary code compiles under
C99 on the actual target with enabled warnings treated as errors. Board fixture
additions inspect factories/descriptor sizes only and do not open UART.
See [fresh software/build evidence](../../qualification/esp32c3-uart-20260928.json).

Manual BARR-C review checked all new modules and affected callers: names and
module boundaries; borrowed storage/lifetime; const/static; initialization and
error paths; fixed-width shifts/sign conversion; frame/buffer bounds; finite
loops; aligned config copies; external kernel size/handle conversions; serialized
device calls; partial setup/cleanup; and no fresh data after a failed receive.
Signed-magnitude casts stay within -32767..32767. Kernel size conversions are
bounded by 64-byte calls or validated nonnegative ring counts. No policy,
deviation, approval or compiler-profile exception was changed.

Final hardware acceptance must verify the actual module, supply/ground, crossed
UART wiring, baud/8N1, reporting, disconnect/reconnect, overrun recovery, polling
latency, stack/heap headroom, simultaneous peripherals and repeated plan updates.


Current [acceptance status](../../qualification/README.md) and
[wiring procedure](../../../CONTRIBUTING.md). Historical test counts above
refer to their dated evidence, not a new qualification.
