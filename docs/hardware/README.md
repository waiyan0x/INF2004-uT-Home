# Hardware documentation

Use the root [hardware acceptance guide](../../HARDWARE_ACCEPTANCE.md) for
current wiring, board preparation and complete-system acceptance. The guides
here describe narrower UART/GPIO contracts and their remaining checks; they
do not replace that procedure or establish physical acceptance by themselves.

- [UART and GPIO integration](uart-gpio.md): maintained modules, ownership and
  physical-test boundaries.
- [UART retained-handle lifecycle](uart-startup-allocation.md): startup,
  permanent handles and checked owner transitions.
- [UART fixed native storage](uart-static-storage.md): typed storage,
  build adaptation and RAM accounting.
- [UART acceptance](uart-acceptance.md): isolated faults, creation-call audit
  and external timing checks.
- [GPIO mutex lifetime](gpio-startup-mutexes.md): pulse/PWM startup resources
  and failure behavior.

Keep fresh measurements, build identity and raw output under ignored `build/`.
