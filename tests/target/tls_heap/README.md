# TLS worker heap measurement

Run the following with the pinned Windows ESP-IDF Python environment. Supply
the `build` directory of a qualified static Wi-Fi target and a fresh output
directory. The script reuses its compiled objects, verifies that a control
relink reproduces its application image byte for byte, then builds the
instrumented ELF and application image in the output directory.

```powershell
python tests/target/tls_heap/build_probe.py QUALIFIED_BUILD NEW_OUTPUT
```

The first asynchronous ESP-TLS connection identifies the MQTT worker. Its
native mbedTLS calloc requests are grouped by the TLS 1.2 client state at
entry. Requests outside a client step have their own bucket. Freeing a tracked
block retires it even if a different task frees it. The SDK performs every
allocation and release. The observer uses 64 fixed records, and a failed
allocation, exhausted record table or unexpected owner change invalidates the
measurement. Counters record requested payload sizes and exclude allocator
metadata, other tasks and allocations bypassing the mbedTLS allocator.

Read the file-local symbols in `tlsheap.c` with the ESP halted. At each boundary,
record `g_stages`, `g_live`, `g_released`, `g_frees`, `g_errors` and the
occupied entries of `g_blocks`. Clear only `g_stages` before resuming. Its
peak field measures all live tracked bytes at an allocation in that state.
Include the live total at the start of the interval when calculating its peak.
Check that requests and bytes reconcile against frees and retained blocks
over each interval. Accept a run only with zero errors, the same owner and
three authenticated replies from the same ESP boot. See the [board measurements](../../../docs/reports/wifi-static-memory/README.md#tls-worker-heap-across-three-handshakes).

To observe USB validation, run `source tests/target/tls_heap/usb_views.py` in
ESP GDB on the diagnostic ELF. Before resuming from the reset halt, run
`python owner_events = install("owner-events.jsonl")` with a fresh output
path. The two hardware breakpoints record task IDs, workspace ownership,
TLS state and parser size at parser release and X25519 key generation.
They resume automatically. At each test checkpoint, hold the broker's
response to the client handshake flight until USB has returned a schema
rejection. Match both events to that interval and verify the original profile
revision and CRC, the authenticated MQTT reply and the allocator counters.
[USB validation during MQTT TLS](../../../docs/reports/wifi-static-memory/README.md#usb-validation-during-mqtt-tls)
