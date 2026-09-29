# Third-party software

## Selected kernel dependency

| Field | Value |
|---|---|
| Repository | https://github.com/sirfonzie/esp32c3-mtk3.git |
| Commit | `ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2` |
| Local path | `esp32c3-mtk3/` (Git submodule) |
| Board kit | `iote_esp32c3_mini` |
| Build target | `esp32c3`, single-core RISC-V, native USB-Serial-JTAG |
| Local source modifications | None |
| Licence | Mixed; upstream `LICENSE`, `LICENSES/`, and individual file headers govern |

This is the project-selected independent ESP32-C3 port of TRON Forum's
μT-Kernel. Selection does not imply a separate lecturer endorsement. On
2026-09-28 the task example compiled with the tracked SDK compatibility patch
and passed startup and repeated USB-reset checks on our ESP32-C3 SuperMini.
Application runtime and peripheral integration remain separate work. See
[the setup guide](../SETUP_ESP32C3.md) and [qualification evidence](../qualification/README.md).

The upstream kernel directories use T-License 2.2, with some historical
T-License 2.1 headers retained; port components and examples use Apache-2.0.
Keep the complete licence files and source notices in the submodule.

From the μT-Home repository root:

```powershell
git submodule update --init --recursive firmware/third_party/esp32c3-mtk3
git -C firmware/third_party/esp32c3-mtk3 rev-parse HEAD
```

The revision must match the table. Use the committed Git submodule revision,
not `git submodule update --remote`, for reproducible team checkouts.

## Updating the dependency

Review a specific upstream commit on a project branch. Fetch it in the
submodule, check it out detached, build and run the baseline on the board,
and record the ESP-IDF/compiler revisions and result. Update the Git submodule
pointer and the revision in this file and `../SETUP_ESP32C3.md` together.
Commit those changes in the same reviewed project change. Keep local kernel
fixes in a documented fork/commit rather than an unrecorded submodule worktree.

## Other dependencies

Bosch BMP3 SensorAPI v2.0.6 is now retained under `bmp3/`, copied byte-for-byte
from Member 2's pinned source increment. Its three files retain complete
BSD-3-Clause notices. See [source hashes and provenance](bmp3/SOURCE.json).
Owned adaptations belong in `../drivers/bmp388/`; do not edit vendor source.

Use [espressif/cjson 1.7.19~2](https://components.espressif.com/components/espressif/cjson/versions/1.7.19~2),
an MIT-licensed managed component. ESP-IDF 6 removed the built-in `json`
component. Pin the exact version in the future configuration component's
`idf_component.yml` and preserve its resolved dependency lock with the build
evidence. Enforce project size, nesting and allocation limits around parsing.
Do not add a second JSON library. Additional components need provenance.
MQTT is outside the first prototype.

The SDK build-compatibility and application resource patches are recorded in
[patches/](patches/README.md). They are checked against the pin and applied only
to separate build/integration copies. The submodule retains zero local changes.

## cJSON integration

The managed version is now pinned in
[idf_component.yml](../../tests/target/runtime_port_board/main/idf_component.yml).
The [host mirror provenance](cjson/SOURCE.json) records exact hashes and intact
MIT licensing. Qualification verifies managed/host source equality and retains
the dependency lock. Owned bounds are in
[admission notes](../src/config/ADMISSION.md); no vendor changes apply.
