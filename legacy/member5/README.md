# Member 5 prototype toolkit

Preserved from `origin/main` commit
`6a16f2473a348fda0d95eedf12c3839b0010f687` (PR #1, including the later
recommendation-import, health-snapshot and verified-bundle changes).

This toolkit uses **ESP32-S3 / profile schema 1.0** and supports offline review
and filesystem bundles. It does not deploy the current C3 firmware. Its schema,
examples, UI assets and tests stay together here so those assumptions do not
replace the active C3 schema 2.0/2.1 or USB configurator.

For current hardware work, use the [C3 configurator](../../tools/configurator/README.md)
and [hardware acceptance guide](../../CONTRIBUTING.md). Do not deploy these
legacy profiles to the C3 board.

To run the preserved tools, open a fresh terminal and use this directory as the
working directory; its `tools` package is intentionally separate from the root
package. From the repository root:

```powershell
Push-Location legacy/member5
try {
    python -m pip install -r tools/requirements.txt
    python -B -m unittest discover -s tests -v
    python -B -m tools.configurator serve --host 127.0.0.1 --port 8081
} finally {
    Pop-Location
}
```

The optional legacy UI is at `http://127.0.0.1:8081`; stop it with Ctrl+C.
Original [usage](docs/configuration/README.md) and
[architecture](docs/configuration/ARCHITECTURE.md) refer to paths relative to
this directory. Their pending hardware work describes that historical version.

The copied source is unchanged except for `tests/unit/test_validator.py`: its
runtime-header check now points to the active root header and accepts BARR-C
spacing and unsigned constant suffixes. The same three limit assertions remain.
No archived C implementation or alternate kernel is added. Future reuse of its
recommendation and health import features must go through the current planner,
review and USB deployment contracts.

On Windows, the symlink-preservation test requires symlink creation privileges.
If Windows reports error 1314, that check has not run; do not count it as a pass.
The complete legacy suite also runs in the repository's Ubuntu CI job.
