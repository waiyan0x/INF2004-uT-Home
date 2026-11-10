# Actuator services

`pwmwork.c/h` owns recurring source sampling, rule state, duty changes and the
zero-duty fallback. `cfgbind` supplies admitted settings, opens the PWM instance
and wires its worker and cleanup callbacks. The service reuses the
[rule evaluator](../../automation/README.md) and
[PWM component](../../../drivers/pwm_output/README.md).

Each worker owns its fixed instance/rule/state context. A failed sample uses
the current clock when available; unavailable/regressed time forces zero duty.
Evaluation or write failure attempts zero duty and reports health failure;
cleanup retains the resource until the existing driver succeeds. There is no
additional task, pointer, queue, heap storage or actuator feedback mechanism.

Manual BARR-C review: C99, initialized bounded values, unchanged field layout,
borrowed component/port lifetime and single-worker rule ownership. No casts,
new hardware access or exceptions. Host binding tests exercise stale inputs,
rule changes, write failures and safe-state retries. Physical load acceptance
remains open.
