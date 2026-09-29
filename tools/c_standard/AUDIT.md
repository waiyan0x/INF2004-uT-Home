# BARR-C:2018 audit — 2026-09-28

The current branch's project-owned firmware and C tests were reviewed against
the supplied PDF. The audit covers 63 C/header files, including the maintained
templates. Pinned vendor code was preserved. Other remote teammate branches
were not rewritten; their C must pass the same gate when integrated.

## Changes and review

- Shortened public identifiers, corrected pointer/Boolean/handle names and
  introduced function-signature typedefs without changing callback ABI.
- Paired module sources/headers, separated queue-health/watchdog interfaces,
  renamed test executables and updated every build/test caller.
- Applied braces, precedence/equality conventions, unsigned literals,
  declaration layout, LF endings, 80-column formatting and EOF markers.
- Added module/function contracts, cast rationale and private prototypes;
  split large test scenarios while preserving assertion and CHECK counts.
- Reviewed task ownership, atomics, cancellation, timeout, cleanup retry,
  host rollback, registry matching, queue counters and health/watchdog timing.
  Host structure size/offset probes matched the previous representation; the
  health last-event flag is now Boolean and consumers must recompile.
- Added mandatory build checks, pinned formatting, CI, future-agent guidance,
  module templates and an explicit manual-review checklist.
- Corrected test discovery after filename changes and added checks that reject
  altered original or staged source snapshots before target rebuilds.

Public source API names changed. See the migration table in
[C_CODING_STANDARD.md](../../C_CODING_STANDARD.md) when merging teammate branches.
JSON configuration keys and device-independent architecture remain unchanged.

## Validation

| Check | Result |
|---|---|
| Full standard gate | 63 files; zero findings or tool/policy errors |
| Checker regression tests | 54 passed |
| Foundation acceptance, C99 | 23 passed |
| Runtime C suites, C99 | 17 passed; discovery count explicitly verified |
| Target failure/build-tool tests, C11 adapter | 15 passed |
| Real ESP32-C3 build | Passed with pinned SDK/kernel; owned-code warnings are errors |
| Connected ESP32-C3, COM8 | Two reset runs, each passing all 369 checks |
| Git whitespace check | Passed; required C EOF blank lines are allowed explicitly |

[Machine-readable audit](audit-20260928.json) records source hashes and counts.
[Board/build evidence](../../firmware/qualification/esp32c3-barr-20260928.json)
records independent workers, cancellation, timeout ownership, cleanup retry and
24 repeated four-slot allocation/release cycles. Physical power cycling,
peripherals and whole-plan target activation are outside this verification.

## Compatibility sign-off and limits

**Approved by Neoweikang on 2026-09-28.** The user explicitly replied "approved"
to the compatibility sign-off request in this conversation. The recorded name
comes from the effective repository Git `user.name` (global Git configuration);
the user did not separately state their name in the approval reply.

The [scoped approval record](approval-20260928.json) covers the existing C11
atomic profile under rule 1.1.a in `firmware/src/runtime/tkport.c` and
`tests/target/runtime_port_board/main/main.c`, plus the 49 existing fixed
kernel-interface exceptions. Exact exception IDs and a canonical payload hash
bind the approved definitions. Reasons and interface authority remain in the
project standard and exception manifest. New or altered exceptions, expanded
paths and additional C11 features require separate review and explicit approval.
The CHECK stringification macro is a reviewed use permitted by rule 6.3.a and is
outside this deviation approval.

The PDF's Deviation Procedure (printed page 6) requires project-manager approval
and documentation of the approver's name. This record documents the user's
explicit approval; the checker does not authenticate approval metadata. No
approval is inferred from a green checker result. This is a code review and
regression record, not independent certification of complete conformance.
Future changes still require the gate, affected tests and manual semantic review.

The changes are local and uncommitted. The pre-existing Git index and unrelated
work were preserved. GitHub branch protection was not changed; the added workflow
becomes available after the changes are pushed.

## UART software review (2026-09-28)

Checkpoint 4b adds portable UART ownership, a native-kernel adapter, the adapted
radar parser, polling driver, registry adapter and host tests. Manual review is
recorded in [UART integration](../../firmware/drivers/hlk_ld2450/INTEGRATION.md).
The mandatory gate passes for 113 C/H files, and 115 host tests pass. Fresh
target build/compiler evidence is in
[the UART record](../../firmware/qualification/esp32c3-uart-20260928.json).
New ordinary code uses C99; the approved C11 scope, three existing SDK-header
GNU99 boundaries, policy, 49 exceptions and approval record are unchanged.
No new deviation is requested. Native device behavior was source-reviewed and
compiled; physical execution remains unqualified.

## GPIO distance/PWM checkpoint review (2026-09-28)

Added pulse/PWM contracts, digital input, C3 RMT/LEDC backends, HC-SR04 and generic
PWM registry adapters plus host faults/scenarios. Reviewed names/types, range
and MMIO casts, lifetime/ownership, bounded timing, copied configuration,
zeroed error records, zero-duty cleanup and retained failures. The mandatory
gate covers 132 owned C/H files. Two new exact GNU99 translation units parse
excluded SDK LL syntax while owned code remains C99. The approved C11 scope,
policy, formatter, 49 deviations and approval record are unchanged.
See [detailed review and limits](../../firmware/drivers/hc_sr04/INTEGRATION.md)
and [fresh validation evidence](../../firmware/qualification/esp32c3-gpio-20260928.json).
Hardware timing and electrical acceptance are deferred, not certified by the gate.

## Configuration transfer/storage core review (2026-09-28)

Reviewed `cfgbytes`, `cfgwire`, `cfgstore`, `cfgctrl`, the host fixture and board
callers for BARR-C naming/types, widths/casts, buffer bounds, initialization,
borrowed lifetime, serialized callbacks, finite work, explicit error outcomes
and interrupted-save preservation. The unchanged gate covers 142 owned C/H
files. Ordinary code stays C99; no approved compatibility/language scope or
vendor revision changes. See [detailed review](../../firmware/src/config/TRANSFER.md)
and [fresh evidence](../../firmware/qualification/esp32c3-config-core-20260928.json).
Host storage faults do not qualify physical flash; no board I/O was performed.

## Target admission/binding review (2026-09-28)

Reviewed bounded vendor parsing, independent admission, three sensor bindings,
generic plan resource lifetime and empty recovery. Review covers range/aliasing
conversions, aligned limited allocations, tree release, disabled wiring,
resource bounds, callback lifetime and rollback. The unchanged gate covers
158 C/H files; C11/GNU99 boundaries, policy, formatter, 49 exceptions and approval
are unchanged. See [manual review](../../firmware/src/config/ADMISSION.md)
and [fresh evidence](../../firmware/qualification/esp32c3-admission-20260928.json).
No physical qualification or complete-standard certification is inferred.

## USB/storage application review (2026-09-28)

Reviewed the bounded pump, C3 USB/partition boundaries, application lifetime,
PROBE extension, existing callers and fault fixtures. The gate covers 166 C/H
files with no new policy/deviation/approval changes. New modules compile as C99;
the existing C11 and external-header GNU99 scopes are unchanged. See the
[manual review](../../firmware/src/config/USB_STORAGE.md#manual-barr-c-review)
and [fresh evidence](../../firmware/qualification/esp32c3-usbstore-20260928.json).
Host/build evidence does not certify all BARR-C rules or physical behavior.

## Checkpoint 6a manual review (2026-09-28)

Reviewed sampcache/statusmon/telemetry, shared guard integration, copied binder
settings, cfgwire/cfgpump, c3app and affected tests against the required manual
checklist. See [ownership, type and error-path findings](../../firmware/src/services/TELEMETRY.md#manual-barr-c-review).
No exception, compiler-language approval, checker policy or dependency pin was
changed. New modules are C99. Fresh software/build evidence is recorded under
`firmware/qualification/esp32c3-telemetry-20260928.json`; this review does not
claim physical sensor, timing, USB or flash qualification.

## Checkpoint 6b manual review (2026-09-28)

Reviewed the single OLED renderer, guarded sample selection, display binding,
SPI/control-line lifecycle and extended existing fixtures. See
[the checklist and limits](../../firmware/src/services/DISPLAY.md#manual-barr-c-review).
No policy, exception, language approval, dependency pin or formatter changed.
Fresh evidence: `firmware/qualification/esp32c3-display-20260928.json`.
Hardware acceptance remains deferred.

## Checkpoints 6c/7 manual review (2026-09-28)

Reviewed read-only capabilities, canonical signed integers, rule admission,
typed sample evaluation, guarded source lookup, PWM binding and extended fault
fixtures. The unchanged gate covers 180 owned C/H files. See the
[manual checklist review](../../firmware/src/automation/README.md#manual-barr-c-review)
and [fresh evidence](../../firmware/qualification/esp32c3-ui-rules-20260928.json).
No policy, exception, language approval, formatter or dependency pin changes.
Hardware acceptance and AI remain deferred at the user's request.
