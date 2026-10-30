# Buddy 1 fixed task-stack and lifecycle report

## Outcome and scope

The isolated ESP32-C3 Wi-Fi and BLE diagnostics now create shim-managed native
tasks from bounded, caller-owned stacks. The staged compatibility patch contains
no task-stack `heap_caps_malloc`/`heap_caps_free` path, uses generation-tagged
opaque handles, and defers unsafe cleanup to one permanent low-priority reaper.
The pinned `firmware/third_party/esp32c3-mtk3` checkout remains unchanged.

This closes the source/build/host-test portion of the task-stack gap only. No
board was flashed. Runtime stack high-water, radio scheduling, lifecycle timing,
CPU cost, remaining SDK heap use and whole-system static compliance are not
qualified.

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

## Verification

- The actual `c3task.c` allocator passed alignment, best-fit, exhaustion,
  ownership/stale-release and 10,000-cycle tests under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- The real staged shim patch passed creation rollback, pool exhaustion, task/
  semaphore/stack/observer cleanup failure, self/concurrent/stale deletion and
  1,000 reuse cycles under the same sanitizers. Leak detection was disabled
  because the macOS sanitizer runtime does not implement it.
- Legacy reservation, failure, self-delete and concurrent-delete regressions,
  seven fixed-pool unit tests and the 1,000-cycle model stress test passed.
- The complete BARR-C formatter/lexical gate passed 285 project C files with the
  pinned clang-format 20.1.8. The 54 checker unit tests passed. Manual review found
  no new deviation: fixed widths and bounds are explicit; shared lifecycle state
  uses the existing single-core interrupt guard; no ISR blocks; pointer values are
  compared only; and the 32-bit generation token is confined to the target handle
  ABI. Both real target builds compile owned code with warnings as errors.
- The staged patch applies cleanly to the pinned vendor source. Its SHA-256 is
  `e9ba7df15feecc37229c93c7e447c9a4c77da5473e4873aaf9feb206de5c0348`.

Final unflashed build evidence:

| Build | Evidence | Result |
|---|---|---|
| Wi-Fi scan/restart | `/private/tmp/uthome_buddy1_final2_wifi/evidence.json`; `memory-budget.json` | build and budget passed; static-stack/reaper symbols linked |
| BLE advertising | `/private/tmp/uthome_buddy1_final2_ble/evidence.json`; `memory-budget.json` | build and budget passed; static-stack/reaper symbols linked |

The qualification tool verifies the patched source, rejects task-stack heap calls,
checks the static APIs and lifecycle tokens, and confirms the final ELF contains
the allocator, reaper stack/task and shim cleanup function. These local build
folders are evidence inputs, not installed firmware or hardware observations.

## Memory impact

The fixed stacks move radio task memory into the ELF. Historical pre-change maps
excluded dynamically allocated task stacks and therefore are not equivalent
runtime-peak measurements.

| Profile | Historical ELF DRAM | Final ELF DRAM | Change | Final cap headroom |
|---|---:|---:|---:|---:|
| Wi-Fi | 262,106 | 278,402 | +16,296 | 126 bytes |
| BLE | 247,703 | 259,127 | +11,424 | 19,401 bytes |

For Wi-Fi, the final group attribution is 168,081 bytes application fixed,
53,183 SDK/kernel fixed and 57,138 RAM code. For BLE it is 163,228, 39,561 and
56,338 bytes respectively. `network-budget.json` retains the 278,528-byte total
and redistributes it to 168,192 application, 53,184 SDK/kernel and 57,152 RAM-code
limits. Wi-Fi margins are only 111, 1 and 14 bytes in those groups. Module
reservations total 168,124 bytes, leaving 68 bytes inside the application group.
This is a measured fit for these isolated links, not comfortable growth capacity.
The detailed stack table is in [wireless_stack_budget.csv](wireless_stack_budget.csv).

## Hardware work still required

On a deliberately selected board and only after backup/identity checks, build and
flash fresh diagnostic images separately. For each profile:

1. Confirm every requested task maps to the expected slot and record peak concurrent
   occupancy; reject unexpected sizes or fifth-task creation without heap fallback.
2. Measure native stack high-water for every radio task and the reaper under scan/
   advertise, stop, restart, failure and repeated cleanup. The compile-time sizes
   are selections, not measured margins.
3. Exercise creation rollback, self-delete, external/concurrent delete and cleanup
   retry while checking that no stack is reused before task dormancy and deletion.
4. Measure reaper wake/cleanup latency, CPU effect and radio progress; confirm its
   1,024-byte stack margin on target.
5. Compare startup/normal allocation and kernel-object counters across repeated
   radio cycles and reset. Continue auditing timer, notification, controller,
   NimBLE/lwIP and service storage; the task-stack change grants no heap exemption.
6. Repeat under combined peripheral/radio traffic and flash-save stress before any
   integrated or whole-system claim. Wi-Fi/BLE coexistence remains a separate task.

The 126-byte Wi-Fi aggregate margin is the primary build-time risk. Any source,
SDK configuration or toolchain change requires a fresh map and budget record.
