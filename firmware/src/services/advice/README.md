# Fixed timing advice

`adviser.c/h` owns a caller-supplied, bounded candidate evaluation state machine.
`adfeat.c/h` derives the model's eighteen inputs from an independently admitted
configuration plan. Neither changes the active configuration, allocates memory,
creates a worker, nor bypasses model eligibility checks.

The [AI guide](../../../../ai/README.md#implemented-bounded-controller-preparation) owns usage,
manual C review, experimental status and qualification limits. Host tests in
`ai/tests/test_adviser.py` and `ai/tests/test_adfeat.py` compile these actual
modules with warnings treated as errors. Normal advice remains disabled.
