# BMP388 driver

I2C pressure (Pa) and temperature (Celsius) with per-instance compensation,
bounded reads and typed samples. Basic bench readings passed; full acceptance
and multi-device electrical checks remain open.

## Modules and provenance

- `bmp388.c/.h`: instance-owned wrapper using Bosch BMP3 SensorAPI v2.0.6 for
  configuration and compensation. It accepts only BMP388 ID 0x50 and addresses
  0x76/0x77. It creates no tasks, queues or controller instances.
- `bmpinst.c/.h`: component-registry and periodic-worker adapter. It copies
  scalar settings, maps samples/errors and uses the runtime's shared wrappers.
- `../../hal/include/i2cbus.h`: portable bounded transport, address ownership,
  time and required delay callbacks.
- `../../hal/ports/esp32c3/c3i2c.c`: single-controller target backend,
  configured with pins and clock supplied by the application.

Adapted from Jian Xuan's locally cached I2C/BMP388 work, stack tip
`56f718e825674c70716f52d798e5bf011ebaa9f0` (BMP388 increment
`1c0ab7bf2991ee68e49838a7b541c87104da6aba`).
The [vendor source record](../../third_party/bmp3/SOURCE.json) identifies the
three unchanged Bosch files, retaining their BSD-3-Clause notices.
The [official Bosch repository](https://github.com/boschsensortec/BMP3_SensorAPI)
documents the integration interfaces. The pinned source defines this wrapper's
callback behaviour.

The retained multi-register adapter sends explicit register/value pairs:
v2.0.6 reports a spare final byte it does not initialize. The wrapper never reads
or transmits that byte or writes through the reserved ODR/CONFIG gap. Host tests
check the resulting register values.

## Binding to the platform

1. Startup calls `c3i2c_init()` once, before profile restore or requests; it
   creates the bus's only kernel object. From the lifecycle owner, open the
   admitted I2C bus before creating workers. Multiple devices borrow this
   bus; do not open a controller per sensor.
2. Zero each distinct `bmpinst_t` and assign its borrowed `p_bus`.
3. Add `*bmpinst_descriptor()` to the compiled registry and call `drvreg_bind`
   with the instance name, context and actual size. Binding performs no I/O.
4. Populate `activate_device_t` with that component, `bmpinst_step`,
   `bmpinst_cleanup` and the same context. Copy scalar `bmpinst_config_t`
   settings: address, timeout, source index and destination queue handle.
5. Call `activate_apply`. The trusted binding layer must map the descriptor,
   source index, environment queue type and full scheduling/wiring limits.
   This typed adapter is not a firmware JSON loader.
6. Close the bus only after empty-plan shutdown/recovery releases all workers
   and sensor claims. Close rejects remaining claims or an in-flight transfer,
   and never deletes the permanent mutex.

`tests/target/periph.c` is the executable two-instance example. Manually invoked
steps exercise the registry/driver/queue path; its fake kernel does not schedule
tasks. Another I2C driver can use the same bus without changing the runtime.

Addresses are claimed before initialization. A second context cannot reset an
already claimed sensor. Partial setup retains ownership for cleanup. If no write
was attempted, cleanup just releases the claim. Once hardware may have changed,
cleanup writes sleep mode before release. A disconnected sensor therefore
retains ownership until cleanup can be retried. Keep its context and bus alive
while the activation manager reports recovery.

## Deadlines and sample errors

Open/read/close share one absolute deadline across their transfers, waits and
vendor delays, using the configured 10..100 ms budget. Delays cannot be omitted.
Slow bus clocks or contention can cause initialization to time out; select a
budget for the whole operation. The application binding layer must also enforce
the descriptor's minimum 200 ms period and 3072-byte stack. Configure checks
that the destination queue exists; full type/routing admission remains the
application's responsibility.

The driver retains pressure x4, temperature x2, IIR coefficient 3 and 25 Hz normal
sampling. This policy belongs to the driver. Reads wait for both ready bits;
compensation warnings, nonfinite data and out-of-range values are rejected.

The adapter stamps completed attempts with the common monotonic clock and
publishes copied environment records. Failed values are zero, with TIMEOUT, IO
or INVALID status. Sequence advances per publication attempt, including queue
failure. Shared-queue contention drops that attempt; the next step reads again.
`last_io_error` and `last_queue_error` retain instance diagnostics.

Recoverable read failures produce error records and allow a later periodic retry.
Timeout uses the existing health event; other device failures use the appended
`UTHOME_HEALTH_DEVICE_IO` (code 6). Existing event values remain stable; all
health snapshot consumers must recompile. These structs are not a wire ABI.
Shared-lock contention may suppress a health increment while the sample/context
still records the error. No bus mutex is held during queue/health operations.

## Verification and hardware boundary

Tests cover request bounds, duplicate claims, independent calibration, analytic
100000/110000 Pa and 8 C results, sampling settings, wrong chip, deadline expiry,
disconnect, zero failed values, fresh timestamps, health mapping, reconstruction,
cleanup retry and enable/disable. Synthetic coefficients test compensation
plumbing, not physical measurement accuracy.

The target acceptance build includes the C3 backend and Bosch library. Added
invalid-argument checks retain these functions in the link without accessing
pins. Those original fixture checks did not exercise the physical sensor. See the
[bench summary](../../qualification/README.md#peripheral-bench-observations) for later observations.

Before physical acceptance, identify the breakout and verify supply, pull-ups,
I2C mode/address and wiring. The existing example profile uses SDA GPIO0 and SCL
GPIO1; pins remain configurable. Then capture identity, plausible samples,
disconnect/reconnect, timeout, cleanup/recovery and repeated replacements.
A second sensor at a distinct address is needed to qualify physical bus sharing.
The target-pending peripheral manifest remains empty until hardware acceptance.

Manual BARR-C review covered C99/fixed storage, public/private boundaries,
integer/cast ranges, aligned configuration copies, borrowed contexts and claims,
mutex ownership, deadline arithmetic, raw MMIO status, yielding waits and
retryable cleanup. Vendor files are unchanged. No new exception or C11 scope
was introduced.


Current [acceptance status](../../qualification/README.md) and
[wiring procedure](../../../CONTRIBUTING.md). Keep dated results in the qualification records, separate from current usage.
