# UART acceptance checklist and fresh evidence

Update: the standard-gate cleanup and cloud build setup are documented in
[GitHub Actions setup](github-actions.md). Earlier failed-gate records below
are retained as history; hardware/target evidence still requires execution.

Status: NOT COMPLETE. No target execution or physical baud/framing pass is
claimed. This record distinguishes implemented tests from executed tests.

## Required tests

| Requirement | Implemented check | Current evidence |
| --- | --- | --- |
| No creation calls after startup | GDB entry breakpoints on actual `tk_cre_mtx` and `tk_cre_sem` across `uartcheck_run` | Audit-accounting unit tests pass; target audit pending |
| Baud/framing reach hardware | Board cycles through 4 rates x 4 data lengths x 3 parities x 2 stop lengths | API exercise implemented; physical/register verification pending |
| Reject wrong owner | Real adapter read/write/close calls with different owner | Board assertions added; not executed |
| Report overflow without growth | Inject more than the native RX ring capacity; expect OVERFLOW and zero returned count | Board assertions added; ring capacity fixed at compile time |
| No stale data between owners | Seed native RX, queue TX, retry cleanup, claim next owner and check empty RX/TX plus fresh RX | Board assertions added; not executed |
| Cleanup failure prevents reuse | Acceptance-only read hook returns E_IO once during cleanup; assert retained owner, blocked new open/read, successful retry | Board assertions added; not executed |
| Startup precedes restore/requests | Application ordering and no Detect/Apply initialization | Source guards pass |
| Failed startup stays unavailable until reboot | One-shot attempted/ready state; no creation in ordinary open | Source guard only; startup failure-injection execution pending |
| Fixed RAM, kernel objects and capacities documented | `uart-static-storage.md` and compile-time 1,280-byte record reservation | Map, stack and full system totals pending |
| Pinned standard gate and warnings-as-errors C tests | Gate and affected host suite attempted | BLOCKED by 588 existing findings and 6 errors; changed files have no reported findings |
| Fresh ESP32-C3 build | Qualification attempted | BLOCKED: ESP-IDF unavailable/unactivated |

## Run the board acceptance image

Use `tools/target/qualify_kernel.py build --runtime-port`, the pinned kernel and
SDK, and the existing compatibility/resource patches required by the project.
Do not use `--configuration-app` for this fixture: the fault hook and
`uartcheck.c` are deliberately excluded from the production configuration image.
The qualification tool rejects a hook in that production image, and rejects an
acceptance image missing its hook/test. No native source is edited.

Disconnect UART1 from external senders and from RX/TX loopback for the internal
ring test. Use the separate debug/console connection. Injected RX is synchronous
into the native ring; the test assumes no concurrent physical sender. Each of
96 configurations performs two owner acquisitions, a failed cleanup and retry.
It checks overflow and framing reports, old RX removal, empty software TX after
close, and reception of a new owner's byte. It does not assert wire completion.
The fault-injection hook forwards all other reads to the real kernel.

The existing board CHECK machinery reports a nonzero `uartcheck_run()` result
as failure. Successful board assertions alone do not establish creation-call
compliance; run the debugger audit as well.

## Count actual kernel CREATION CALLS

Load the freshly built acceptance ELF in the matching RISC-V GDB and connect to
the halted board before `uartcheck_run` executes. From the repository root:

```gdb
source tools/target/uart_creation_audit.py
uart-creation-audit
continue
```

The script requires resolved kernel/test symbols. It counts every entry into
`tk_cre_mtx` and `tk_cre_sem`, including calls returning errors. At the test's
entry, it snapshots cumulative counters. At test return it stops the debugger
and reports the deltas and board result. PASS requires both deltas to be zero
and the board function to return zero. No deletion subtracts from the counters.
The baseline cannot be reset to hide a creation/deletion cycle. Missing symbols,
no completed test, an unavailable return value, or nonzero return is not a pass.
Save the GDB output alongside the ELF/map and qualification logs.

This measures all creation calls during the UART test window, including other
running tasks. It does not establish system-wide behavior outside that window.
The full application also needs tracing across Detect, Apply, rollback, profile
restore and normal requests. Do not infer that a constant live-object count
proves any of these paths compliant.

Debugger timing disturbs scheduling. Use the audit for allocation behavior, and
perform physical communication timing tests separately without breakpoint stops.

## Still-required physical and failure tests

- With an external UART peer and logic analyzer, verify measured baud, data
  length, parity and stop bits after each requested change. Internal ring tests
  and cached attribute readback cannot prove hardware configuration.
- Repeat owner changes with identifiable peer session patterns; distinguish
  pre-existing buffered data from genuinely new bytes sent after acquisition.
- On separate reset runs, inject failure at UART mutex creation, native open,
  and startup reset. Assert the first init fails, subsequent init and runtime
  requests stay unavailable, and creation-call counters never increase on retry.
  Do not run this destructive startup-failure scenario against a live profile.
- Exercise the other cleanup/unlock failure paths and incoming-traffic races.
- Verify the static-storage object and native object-file references in the
  target map/object files, then complete the total RAM and stack budget.

## Local validation record

18 Python source/build-routing/debugger-accounting tests pass. The debugger
unit tests use a fake GDB object; they validate counter logic, not the board.
The affected C host suite was attempted with its required gate and was blocked
before compilation. The fresh target build attempt also failed before compiling
because ESP-IDF was unavailable. Logs: `build/uart-acceptance-standard.txt`,
`build/uart-acceptance-host.txt`, `build/uart-acceptance-target.txt`.

Manual review: new C uses matching headers, C99, fixed stack buffers, bounded
nested loops, native typed interfaces and no heap calls. The one-shot read
failure hook is acceptance-only and serialized. Native ring injection requires
an electrically quiet port; no claim is made about ISR concurrency under live
input. Third-party sources and existing gate requirements are preserved.
