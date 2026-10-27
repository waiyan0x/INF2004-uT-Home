# UART acceptance

The retained-handle/fixed-storage implementation has target-build and scoped
USB-core allocation evidence. External baud/framing and the isolated native-ring
fault suite are separate acceptance work. See
[qualification](../firmware/qualification/README.md); earlier unavailable-SDK or
style-gate failures are historical, not current blockers.

## Required tests

| Remaining requirement | Required evidence |
|---|---|
| Isolated native-ring faults | Run the dedicated fixture with UART senders/receivers disconnected; retain wrong-owner, overflow, stale-buffer and cleanup/retry assertions. |
| External baud/framing | Measure all supported modes with a configurable peer and suitable instruments; attribute readback/loopback alone is insufficient. |
| Startup failures | Inject mutex/native-open/reset failures on separate boots; retries must remain unavailable without new creation calls. |
| Live owner transitions | Peer session markers distinguish old buffered bytes from new traffic; include cleanup/unlock failures and incoming races. |
| Combined resource budget | Repeat allocation/object and stack checks with radio/services and account shared storage exactly once. |

## Run the board acceptance image

Use `tools/target/qualify_kernel.py build --runtime-port --native-uart-fixture`,
the pinned kernel and
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

## Review and evidence

The [completed-work log](COMPLETED_WORK_LOG.md) retains earlier local test/build
attempts. Fake-GDB tests validate accounting logic, not board execution.

Manual review: new C uses matching headers, C99, fixed stack buffers, bounded
nested loops, native typed interfaces and no heap calls. The one-shot read
failure hook is acceptance-only and serialized. Native ring injection requires
an electrically quiet port; no claim is made about ISR concurrency under live
input. Third-party sources and existing gate requirements are preserved.
