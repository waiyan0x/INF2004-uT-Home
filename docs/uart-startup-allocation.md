# UART retained-handle adaptation â€” 2026-10-20

Update: the two UART-record heap paths now have a project-owned fixed-storage
adaptation; see [UART fixed native storage](uart-static-storage.md). Statements
below describing unresolved native Kmalloc are the earlier review record;
target verification and system-wide compliance remain outstanding.

Status: implemented for review; source lifecycle guards pass. Target build,
register/wire verification and final RAM accounting remain outstanding.
This is not evidence of full AAI13 compliance.

## Lifecycle and integration

`c3uart_init()` runs after `activate_init()` and before configuration restore
in `c3app_run()`. The board acceptance entry calls it twice to exercise
idempotence. It creates one priority-inheritance mutex and opens `sera` once.
Initialization failure is sticky: abort startup and reboot, rather than create
more kernel objects on retry. A partial startup retains its resources.

Normal open/close never call `tk_opn_dev`, `tk_cls_dev`, `tk_cre_mtx` or
`tk_del_mtx`. The native descriptor and mutex remain reserved until reboot,
even when no configuration uses UART. UART1/GPIO20/21 remain reserved.
`b_owned` and `b_active` are independent of the permanent resource handles.
Lifecycle operations require the runtime owner with all UART workers stopped.
No other code may open, read, write or change the attributes of `sera`.

The portable `uartio.c` and `uartio.h` already retain the caller's claim on
backend open/close failure, reject other owners, and zero read counts on errors;
they required no API change. The backend releases its claim only after cleanup
and mutex unlock both succeed. Recoverable cleanup failure can be retried by
the same owner. An unlock failure poisons the backend until reboot.

## Pinned implementation review

Reviewed pristine upstream commit
`ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2` in an isolated `build` checkout.
The pinned dependency and its existing patches were not edited.

- `kernel/tkernel/deviceio.c`: `tk_opn_dev` obtains an open control block and
  creates `abort_semid` with `tk_cre_sem`. Retaining the native descriptor avoids
  repeating this creation during normal operation.
- `device/ser/ser.c`: MODE and SPEED writes update the driver attributes and
  pass them into low-level caches. They do not program hardware immediately.
- `device/ser/sysdepend/esp32c3/ser_esp32c3.c`: `LLD_SER_START` calls
  `start_com` with those caches. It programs data length, parity, stop bits and
  baud rate, resets RX/TX hardware FIFOs, clears interrupt status, enables
  RX/error sources and enables the CPU interrupt. This is the same low-level
  path previously reached by reopening the native device.
- `LLD_SER_STOP` disables the CPU interrupt, masks ALL peripheral interrupt
  sources and clears status. It does not reset FIFOs or software rings.
- `dev_ser_get_snddat` removes a software TX byte without transmitting it.
  The receive path can read a known available count without sleeping.

Settings application: STOP/reset/discard, write MODE/SPEED and polling timeouts,
then START. Encoding remains `8 - data_bits`, 2-stop flag, and odd/even flags.
This source trace establishes why settings reach the hardware setter path;
it is not a measured baud/framing or electrical qualification. The pinned
baud setter's return value is discarded upstream, so no successful API return
can by itself prove the achieved baud rate.

## Reset sequencing and bounds

A reset executes STOP â†’ START â†’ STOP before discarding software rings.
The intermediate START is necessary because the pinned STOP does not reset
hardware FIFOs. Previous STOP masks TX-empty; START enables only RX/error
sources, so it cannot service queued software TX. An RX ISR during the short
START/STOP interval is included in the subsequent discard.

After the second STOP, software TX is popped at most `DEVCONF_SER_BUFFSIZE`
times (512 bytes plus the empty check with the existing resource patch).
RX is discarded from one validated snapshot of at most 512 bytes, in chunks
of at most 64 bytes. Zero progress or invalid counts return an error. Native
reads may re-enable the CPU interrupt, but peripheral sources remain masked.
The subsequent owner's START resets hardware RX again, dropping bytes that
arrived while stopped. No retained software buffer is delivered to that owner.

Close is an abort of queued output, not a guarantee of wire completion.
A byte already transmitted cannot be recalled. A peer that must identify
sessions needs its own framing/session handshake; bytes sent by that peer
after the new START are new input, not distinguishable stale messages.

Normal flush still discards one RX snapshot without restarting the UART.
Read checks COMERR before and after receiving; overrun maps to OVERFLOW and
other native errors to FRAME. Explicit cleanup consumes old error flags.
Reads/writes remain limited to 64 bytes and use polling mutex/timeouts.
Software bounds do not establish worst-case interrupt latency on hardware.

## UART RAM accounting â€” no exemption

Provisional UART reservation: **2,048 bytes persistent RAM**, plus **128 bytes
of task-stack headroom** for wrapper locals/scratch (part of, not additional to,
the owning task's fixed stack budget). These are proposed limits, not measured
usage. Do not approve them or add them to a claimed compliant system total
until target sizes, allocator overhead and stack usage have been checked.

| Charged item | Quantity / fixed capacity | Verification needed |
| --- | --- | --- |
| Wrapper state and borrowed `uartio_t` | One each, persistent; no application heap | Linker symbols `g_state`, `g_port`, alignment |
| Operations/name constants | One table and one name | Check map placement; charge RAM if copied there |
| Native RX and TX buffers and serial DCB | Two 513-byte arrays, 512 usable bytes each, plus indices/attributes | `sizeof(T_SER_DCB)` and allocator metadata; arrays are already inside the DCB, do not count twice |
| Native low-level state | One unit's caches/metadata | Native map symbols and section placement |
| Native mSDI record | One `T_MSDI` | `sizeof(T_MSDI)` and allocator overhead |
| Native registration lock | One FastLock semaphore | Kernel semaphore control-block pool contribution |
| Native open state | One open control block and abort semaphore | Kernel open/control-block pool contribution |
| Wrapper mutex | One persistent mutex | Kernel mutex pool contribution |
| Synchronous I/O requests | Serialized; use native device-manager request pool | Attribute the reserved request/control pool and manager overhead once |
| Temporary discard scratch | One 64-byte buffer; no recursion | Compiler stack-usage output plus nested kernel call depth |

IMPORTANT: `msdi_def_dev()` unconditionally calls `Kmalloc(sizeof(T_MSDI))`
and `CreateLock`; `CreateLock` creates a semaphore. Serial DCB allocation also
uses `Kmalloc` when `TK_SUPPORT_MEMLIB` is enabled (otherwise static storage).
Startup-only allocation does not mean heap-free allocation. No UART exemption
has been assumed. If AAI13 prohibits these native allocations too, this design
still needs an approved fixed-storage native registration route or an owned
replacement driver/registration layer. The present change intentionally does
not modify the pinned dependency to conceal that gap.

A linker map alone cannot show the payload of a runtime `Kmalloc` allocation.
Combine the map with the pinned configuration, allocation-call audit, target
`sizeof`/pool capacities, allocator metadata and startup high-water evidence.
The total system budget must include other modules and shared kernel pools;
no measured total-system claim is made here.

## Verification record and remaining acceptance

- Five `test_uart_startup.py` source guards pass. Three GPIO startup guards pass.
  These check allocation placement, state separation, sequence/bounds, preserved
  fault handling and startup ordering; they do not execute native UART code.
- Full pinned formatter/style gate was run. The changed files have no findings;
  the repository gate remains blocked by existing findings in unrelated
  pwm_motor/uart_gpio files. See `build/uart-standard-check.txt`.
- Target build was attempted with `qualify_kernel.py build --kernel
  build/uart-kernel-reference --runtime-port --configuration-app`. It stopped
  because ESP-IDF was not activated/available. See `build/uart-target-build.txt`.
- No target binary, map, register readback or wire test is claimed.

Before accepting this adaptation:

1. Restore a clean required style gate and activate the pinned SDK/toolchain.
   Build using the existing compatibility/resource patches and preserve logs,
   compiler commands, linker map and stack-usage evidence.
2. Instrument kernel object creation and allocation counters. Initialize twice,
   then repeatedly open/close/reconfigure; prove counters do not increase after
   startup and the native descriptor remains unchanged.
3. With a configurable external UART peer/logic analyzer, exercise 9600, 115200,
   256000 and 460800 baud; all supported 5â€“8 data bits, parity none/odd/even,
   and 1/2 stop bits. Verify registers and wire timing after each change without
   native reopen. Self-loopback alone cannot prove the actual baud rate.
4. Queue TX and RX, close and claim with a different owner. Verify old software
   bytes do not appear and new traffic works. Repeat with incoming traffic
   across the close/open boundary, using explicit peer session markers.
5. Inject full RX, framing/parity errors, full TX, mutex contention, short I/O,
   failed attribute writes, cleanup failure and unlock failure. Verify bounded
   return, count semantics, retained ownership, retry or reboot policy.
6. Measure and enforce the UART reservation and total application budget. Resolve
   the native Kmalloc compliance decision explicitly; UART is not Wi-Fi.

Manual C review: paired declarations/callers updated; C99, fixed locals and no
recursion; no new application allocation; polling calls and capped snapshots;
owner required for lifecycle, worker mutex for transfers; native private header
usage is intentionally pinned and requires re-review on dependency changes.
Hardware timing, target ABI compilation and actual memory totals are pending.

## Follow-up requirement review

A failed logical open now makes one STOP attempt before returning, retaining
ownership. A failed mutex unlock also makes one STOP attempt and poisons the
port, retaining ownership until reboot. Neither path creates kernel objects.
These defensive branches require fault-injection execution on the target;
the source guard alone does not validate hardware behavior.

Six UART source guards now cover these branches. The earlier five-test record
above describes the initial implementation, not this follow-up run.

Requirements still NOT established: hardware baud/framing correctness across
all supported modes; target compilation; measured/enforced complete UART RAM
budget and total system budget; absence of native heap allocation. The proposed
2,048/128-byte reservations are unverified planning values, not compliant budget
evidence. Native `Kmalloc` remains a compliance gap, not an exemption.
