# ESP32-C3 dependency patches

Both patches target upstream commit
`ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2`. Keep the submodule pristine and apply
patches only to separate build/integration copies. Record the upstream revision
and every applied patch's SHA-256 with build evidence.

## Kernel/SDK qualification: 0002-esp-idf-6.1-build.patch

The unmodified task example failed to compile with the selected ESP-IDF v6.1
because the port's CMake definitions omit `esp_hal_systimer`. The SDK's own
FreeRTOS component declares that dependency, and the kernel timer also includes
its headers. This patch adds the dependencies to the shadow FreeRTOS component
and `mtkernel`. It changes build metadata only, leaving task/kernel C sources
and the example's runtime settings unchanged.

Use `tools/target/qualify_kernel.py build --compat-patch` as described in
[the qualification tool guide](../../../tools/target/README.md). The tool checks
and applies the patch to its staged source, retains a copy and records its hash.
Omitting the option reproduces the original upstream build failure. This
compatibility patch must also be carried into later application builds.

## Application integration: 0001-uthome-c3-resources.patch

Apply only after the kernel task example is qualified, when building the actual
application. It is not needed by the task-example qualification build.

- Native serial ring: 50 to 513 physical bytes for 512 usable bytes. Upstream
  leaves one slot empty; both TX and RX use this macro.
- Disable native ADC startup so the application can assign those pads through
  its reviewed board/driver configuration.

In a separate integration checkout, check then apply each required patch:

```powershell
git apply --check <path-to-reviewed-patch>
git apply <path-to-reviewed-patch>
```

The application resource patch does not supply the UART HAL wrapper, finite
timeouts, the application's 64 KiB kernel-heap override, runtime worker adapter,
or a complete application build. See [the platform baseline](../../../PROJECT_BASELINE.md).

When a reviewed upstream revision includes a patch's changes, update the
submodule pin and remove that superseded patch in the same reviewed change.

Checkpoint 4b applies the unchanged application patch using
`qualify_kernel.py build --resource-patch` alongside `--compat-patch` in the
isolated UART software build. This does not alter the dependency checkout or
claim that the resource-patched image has run on the board.
