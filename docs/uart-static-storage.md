# UART fixed native storage

The native UART records use fixed typed storage in project-owned code; the
pinned dependency remains unchanged. This replaces the earlier record-allocation
concern in [the retained-handle guide](uart-startup-allocation.md). Target build,
map and scoped USB-core evidence are indexed in
[qualification](../firmware/qualification/README.md). Isolated fault injection,
external baud/framing and complete wireless-system acceptance remain open.

## Build adaptation

The project CMake applies source-local definitions in the mtkernel target:

| Native source | Native name | Project implementation |
| --- | --- | --- |
| `device/ser/ser.c` | `Kmalloc` / `Kfree` | `c3umem_serial_alloc` / `c3umem_serial_free` |
| `device/common/drvif/msdrvif.c` | `Kmalloc` / `Kfree` | `c3umem_record_alloc` / `c3umem_record_free` |

These definitions rename both the native declarations and call sites. There is
no global allocator wrapper, native-source edit, general-purpose arena, variable
allocation or fallback to the heap. `c3umem.c` uses the real pinned native types,
so pointer alignment and effective type do not depend on byte-buffer casts.
The project requires CMake 3.18 for target-directory source properties.

`qualify_kernel.py` snapshots the new files and verifies generated compiler
commands: both native sources must use their exact allocator pair, with no
conflicting definitions or leakage into another translation unit.

## Startup and failure behavior

Native device registration precedes application startup. Each allocator accepts
only the exact size of its one typed record, clears it and marks it claimed.
Wrong sizes, a second claim or any claim after sealing return NULL. Native
registration propagates its existing E_NOMEM failure. Native startup unwind
can return a slot before sealing; foreign pointers do not release a slot.

`c3uart_init()` calls `c3umem_seal()` before creating/opening its retained
objects. Sealing is permanent even when provisioning is incomplete; startup
fails if either record was not claimed. After sealing, neither slot can be
freed or reassigned. No new kernel objects or heap calls are introduced by
this module. Calls during the registration phase must remain single-threaded.

The supported build has one native serial unit and native ADC disabled by the
existing resource patch. ADC shares mSDI, so the adapter rejects an ADC-enabled
build at compile time instead of accidentally starving another driver. This
does not disable a feature silently. Expanding native device registration
requires explicit additional typed slots and budget review. MEMLIB must remain
enabled for this particular source-redirection design; globally changing the
kernel configuration is outside this adaptation.

## Fixed RAM accounting

| Item | Reservation / source of charge |
| --- | --- |
| Native serial DCB, RX/TX arrays, mSDI record and slot flags | Exactly **1,280 bytes** in `c3umem.c:g_storage`; compile-time assertions enforce both fit and final union size |
| RX/TX capacity | Existing patch: 513 bytes per array, 512 usable each; included in the 1,280 bytes, not additional |
| Wrapper state and `uartio_t` | Existing fixed globals in c3uart.c; obtain target symbol sizes from map |
| Native low-level UART state | Existing native static globals; charge their RAM sections |
| UART mutex | One entry in static `knl_mtxcb_table` |
| Native registration lock and open abort semaphore | Two entries in static `knl_semcb_table` |
| Device/open/request bookkeeping | Static `knl_DevCBtbl`, `knl_OpnCBtbl`, `knl_ReqCBtbl`, shared manager objects; attribute capacity once in the system table |
| Discard scratch | 64-byte automatic buffer charged to the existing task stack; verify full call-chain peak |

The 1,280-byte reservation includes padding and bookkeeping and is not a heap
allocation. The native kernel mutex, semaphore, device, open and request tables
were reviewed as statically declared in the pinned source. Existing table
capacity exhaustion remains a startup error, never permission to allocate more.

The earlier 2,048-byte total UART / 128-byte stack-headroom proposals are NOT
measured compliance evidence. Only the new 1,280-byte record reservation is
compile-time enforced here. A final UART total and total-system budget still
require target symbols, shared-pool attribution and stack evidence. Kernel/SDK
startup or other application modules may still use heap; they are not exempted
or globally intercepted by this UART-specific change.

## Verification and required target evidence

For each changed adapter/build, run the source-routing tests, full C gate and
fresh target qualification. The build helper verifies source-local substitutions
and the 1,280-byte reservation; retained results do not qualify a different image.
The ordinary fixture exercises storage guards without freeing live driver records.

Required checks when changing this path:

1. Build the configuration application with the existing resource patch and
   mandatory style gate. Verify the recorded compiler substitutions and all
   C99/warnings-as-errors profiles.
2. Inspect the two native object files: undefined references must name the
   c3umem allocator pair, not Kmalloc/Kfree or another allocator. Inspect the
   ELF/map: g_storage must reserve exactly 1,280 bytes and both native records
   must reside within it. Keep this evidence in the repository.
3. Execute board startup and lifecycle tests. In a standalone allocator fixture,
   exercise exact-size success, duplicate/wrong-size rejection, startup free and
   reuse, foreign-pointer free, incomplete seal and post-seal rejection. Do not
   free records belonging to the live native driver to run these tests.
4. Trace allocator/object creation counters across repeated UART reconfiguration
   and owner transfers. There must be no UART heap activity or new kernel object
   creation after startup. Retain the earlier baud/framing, stale RX/TX and
   failure-injection test requirements.
5. Finish the system-wide heap audit and memory-budget table. This removes the
   two identified UART-record heap paths by construction; it does not certify
   unrelated application, network or SDK allocation paths.

Manual review: C99 with matching public header; native signatures preserve
size_t/void*; slots use native types and static lifetime; bounded exact-size
operations; no recursion or heap fallback; registration-only ownership followed
by irreversible sealing; explicit build rejection for unsupported native-device
configuration. Keep external UART timing, isolated faults and complete-system
qualification separate from the scoped core/build evidence.
