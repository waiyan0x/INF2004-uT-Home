# Continuing this project

Start with `README.md` (especially "Resume next session") and the guide for the
area being changed. Use this uT-Home `Code` repository, not the separate Pico
workspace. Check Git status before editing; preserve unrelated work.

Keep implementations modular and as simple as practical. The user is Member 4
(discovery and AI). Preserve Member 5's existing UI and visual style; present
proposed UI adjustments to the user before implementing them. The previously
approved discovery additions are already implemented, not a new task.

Do not assume equipment is available, COM8 is current, or a previously built
image is installed. Separate software checks, bench observations and completed
hardware acceptance. Current next steps and local-only artifacts are linked
from the root README; update existing guides rather than adding session diaries.

# C code requirements

All new or changed project-owned C sources and headers must follow
`C_CODING_STANDARD.md` and the supplied `barr_c_coding_standard_2018.pdf`
(BARR-C:2018). This applies to firmware, HAL, drivers, tests, examples and
generated C templates. Do not copy upstream naming or formatting into new code.

Before changing C, read the project standard, its compatibility notes and
`tools/c_standard/deviations.json`. Use the maintained module templates. Keep
public APIs, their callers, build inputs and documentation consistent.

Before completing C work:

1. Run `python tools/c_standard/check.py` with the pinned formatter installed.
   Diagnostic `--skip-format` output is not a passing gate.
2. Complete the manual review checklist in `C_CODING_STANDARD.md`. The lexical
   checker is deliberately not a substitute for type-aware and hardware review.
3. Run the affected tests with warnings treated as errors; do not disable
   assertions, remove checks or suppress diagnostics to obtain a pass.
4. Build the real target when changing its adapter, ABI or board test. Record
   fresh evidence; do not rewrite historical qualification results.

Preserve third-party dependencies and their pinned revisions. Put adaptations
in owned wrappers. Never broaden an exception, add a wildcard exemption, change
the formatter version or weaken the checks without explicit review. Any actual
departure from BARR-C needs the project owner's explicit approval and a named,
reasoned record. Existing exceptions do not authorize similar future code.

Do not commit, push, erase hardware or discard unrelated work unless the current
task authorizes that action. Prefer isolated builds and preserve board backups.

## Documentation

Maintain one main Markdown guide per maintained folder where practical. Update
that guide instead of adding checkpoint diaries or repeating parent guidance.
Keep current usage, contracts and limitations; link to immutable qualification
records for historical results. Preserve separate coding policy, approvals,
licence notices and the hardware acceptance guide. Repair links when moving
documentation, and never rewrite evidence/source hashes to follow a rename.
