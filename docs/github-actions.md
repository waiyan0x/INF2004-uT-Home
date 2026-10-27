# GitHub Actions: pinned ESP32-C3 build and tests

The workflow is `.github/workflows/c-standard.yml`. It runs on pushes, pull
requests and manual dispatch. It requires no local ESP-IDF installation or
board connection and uses read-only repository permissions.

## What runs

1. The existing C-standard and host-acceptance job remains mandatory. It runs
   the pinned clang-format 20.1.8 gate, checker tests, foundation, runtime,
   target-host and preserved Member 5 suites. The standalone UART/GPIO and
   optional-output suites also run with warnings treated as errors.
2. Only after that job succeeds, two Ubuntu 24.04 jobs build the acceptance and
   configuration application images. Hardware tests are compiled, not executed.
3. Each job reads the exact revisions from `tools/target/qualify_kernel.py`:
   - ESP-IDF: `fff9895c82d744c7237be8847347bdd1b07c6643` (project's 6.1 pin).
   - Kernel: `ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2`.
   - The qualification tool checks compiler package `esp-15.2.0_20251204`.
4. The pinned SDK's install script installs only the selected ESP32-C3 tool
   set. The SDK and kernel are separate pristine checkouts. Existing reviewed
   compatibility/resource patches are applied only to the isolated build copy.
5. The existing qualification helper verifies revisions, SDK submodules, source
   hashes, warning/language profiles, managed cJSON identity, UART allocation
   substitutions and acceptance-only fault-hook scope. Failed commands cannot
   be hidden by tee: build steps use `set -euo pipefail`.

The artifact `esp32c3-<image>-<run-id>` contains available installation/build
logs, evidence.json, source manifest, sdkconfig, dependency lock, firmware,
ELF, linker map, compiler commands and flash metadata. Artifacts are retained
for 14 days and uploaded even when a target job fails. ELF/map and compiler
commands are also required and hashed by qualification; a zero compiler exit
alone is insufficient. If dependency setup fails before logs exist, use the
GitHub step log. A host-gate failure prevents target jobs from starting.

## Enable it on GitHub

The workflow is included on the `system-integration` branch. Pushing a commit
starts Actions automatically. Open GitHub -> Actions -> "C standard and host
acceptance" and select the run for this branch, then inspect both ESP32-C3
jobs. Manual dispatch is also available once GitHub recognizes the workflow
on the default branch. Download each image's artifacts after its run.

Use this `Code` Git checkout for integration; exclude `build/`, caches and local
dependencies from publication. Raw captures/backups are local artifacts.

A successful cloud run proves a target build, not board execution, physical
baud/framing, timing or complete AAI13 compliance. Follow `uart-acceptance.md`
for board tests and actual kernel creation-call counting. Finish RAM accounting
with the map and stack evidence; do not treat a stable live-object count as
proof that no creation calls occurred.

## Standalone API compatibility notes

No maintained UART, radar, sonar, runtime or GPIO algorithm was changed for
this cleanup. The older standalone prototype required source-level naming
changes to satisfy the existing policy; callers and test build inputs were
updated together. These names are not backward-compatible aliases:

| Previous | Current |
| --- | --- |
| `uart_gpio_io.h`, `uart_gpio_io` | `ugio.h`, `ugio_t` |
| `ug_status`, `ug_direction`, `ug_edge`, `ug_uart_config` | `ugio_status_t`, `ugio_direction_t`, `ugio_edge_t`, `ugio_uart_config_t` |
| `uart_gpio_host.c/h`, `uart_gpio_host_*` | `ughost.c/h`, `ughost_*` |
| `ug_motor`, `ug_motor_init/set/stop` | `pwm_motor_t`, `pwm_motor_init/set/stop` |
| `test_uart_gpio.c` | `ugtest_main.c/h` |

Callback fields now use p_ and Boolean fields b_ prefixes. Callback signatures
have named types. Fixed capacities, enum values, callback order, timeout
arithmetic, PWM duty limits and error branches are preserved. Names such as
`c3uart_init`, `uartio_open`, `ld2450_poll` and `hc_sr04_read` are unchanged.

The old standalone test referenced retired `ug_ld2450`/`ug_hc_sr04` APIs and
could not compile against the existing maintained headers. Its scenarios now
exercise the maintained parser/UART/sonar APIs plus the standalone GPIO/PWM
port. Updated expectations follow the already-existing nonblocking sensor
contracts: radar timeout recovery has a flush step, sonar cleanup follows a
failed open, and sonar errors zero outputs. Sensor implementations were not
changed to satisfy tests. Buffer overflow, fragmented frames, signed values,
timeouts, rising edges/detach, invalid pins and PWM stop coverage are retained.
The runtime test discovery now invokes the correct dedicated build scripts
for both standalone suites, rather than omitting them or using unrelated
runtime-only includes. The optional-output script now stops immediately on
standard-gate failure. Existing tests/standard checks were not disabled.

## Verification limits

Check the Actions run for the exact commit; no retained local result establishes
a new cloud pass. Hardware tests are compiled, not executed in CI. On Windows,
symlink tests may require additional privilege; keep them enabled on supported
CI hosts. Historical local counts and unavailable-toolchain notes are in the
[completed-work log](COMPLETED_WORK_LOG.md).

Manual review covered module/header consistency, typed callback ABI, fixed
buffers, exact branch/evaluation preservation, integer ranges, no new heap
allocation, and test coverage. No deviation entries, pinned dependencies,
formatter versions or compiler-warning requirements were weakened.
