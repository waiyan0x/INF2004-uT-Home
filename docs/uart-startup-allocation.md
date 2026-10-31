# UART retained-handle lifecycle

The adapter retains one native UART descriptor and mutex for the boot lifetime.
Native record storage is fixed by [the storage adapter](uart-static-storage.md).
Use [UART acceptance](uart-acceptance.md) for unperformed external timing/fault
tests and [qualification](../firmware/qualification/README.md) for scoped results.

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

## Memory and acceptance

The earlier proposed 2,048-byte UART / 128-byte stack allowance is not an approved
or measured total. The fixed native-record reservation is 1,280 bytes; account
wrapper state, native state, kernel tables and call-chain stack separately in
the [storage ledger](uart-static-storage.md#fixed-ram-accounting). UART has no
blanket heap exemption. Constant live-object counts cannot prove zero creation.

The source/build checks and instrumented USB lifecycle have scoped evidence.
External baud/framing across supported modes, marked peer traffic through owner
changes, startup/attribute/unlock failures and incoming-traffic races remain
acceptance work. Follow [the checklist](uart-acceptance.md); do not inject faults
into the live driver or assume software loopback proves wire timing.

Manual C review: paired declarations/callers updated; C99, fixed locals and no
recursion; no new application allocation; polling calls and capped snapshots;
owner required for lifecycle, worker mutex for transfers; native private header
usage is intentionally pinned and requires re-review on dependency changes.
External timing and complete-system qualification require separate evidence.

A failed logical open makes one STOP attempt and retains ownership. A failed
mutex unlock also attempts STOP, poisons the port and retains ownership until
reboot. Neither may create objects. Source guards do not replace target fault
injection for these branches.
