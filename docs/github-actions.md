# GitHub Actions: pinned ESP32-C3 build and tests

The workflow is `.github/workflows/c-standard.yml`. It runs on pushes, pull
requests and manual dispatch. It requires no local ESP-IDF installation or
board connection and uses read-only repository permissions.

## What runs

1. The existing C-standard and host-acceptance job remains mandatory. It runs
   the pinned clang-format 20.1.8 gate, checker tests, foundation, runtime,
   target-host and Buddy 1 static-task suites. The standalone UART/GPIO suite
   also runs with warnings treated as errors.
2. Only after that job succeeds, three Ubuntu 24.04 jobs build the acceptance, configuration and
   MQTT transaction images. Hardware tests are compiled, not executed.
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
acceptance" and select the run for this branch, then inspect all ESP32-C3
jobs. Manual dispatch is also available once GitHub recognizes the workflow
on the default branch. Download each image's artifacts after its run.

Use this `Revised` Git checkout for integration; exclude `build/`, caches and local
dependencies from publication. Raw captures/backups are local artifacts.

A successful cloud run proves a target build, not board execution, physical
baud/framing, timing or complete AAI13 compliance. Follow [UART acceptance](hardware/uart-acceptance.md)
for board tests and actual kernel creation-call counting. Finish RAM accounting
with the map and stack evidence; do not treat a stable live-object count as
proof that no creation calls occurred.

## Verification limits

Check the Actions run for the exact commit; no retained local result establishes
a new cloud pass. Hardware tests are compiled, not executed in CI. On Windows,
symlink tests may require additional privilege; keep them enabled on supported
CI hosts. Keep fresh local verification output under ignored `build/`.

Manual review covered module/header consistency, typed callback ABI, fixed
buffers, exact branch/evaluation preservation, integer ranges, no new heap
allocation, and test coverage. No deviation entries, pinned dependencies,
formatter versions or compiler-warning requirements were weakened.
