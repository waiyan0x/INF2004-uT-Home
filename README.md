# μT-Home

μT-Home is an INF2004 embedded systems project that lets an ESP32-C3 change how it communicates with peripherals at runtime. The board runs uT-Kernel. A host-side Configuration Desk sends transaction rules over authenticated MQTT with TLS, and Home Assistant displays the results through MQTT discovery.

## How it works

1. In the Desk, the operator selects a peripheral description and an operation. The host compiles that selection into a bounded transaction rule.
2. Loading the rule makes it active on the ESP. Executing it tells the board to perform the bus transaction and return the raw bytes and transfer counts.
3. Home Assistant displays the active rule and the latest result. When a result expires, its readings become unavailable. USB provides local profile configuration and recovery.

The physical demo switches between reading the BMP388 chip ID over I2C and receiving 16 bytes from the LD2450 radar over UART. The board can run from a power bank with the laptop USB cable disconnected.

## Week 13 static-memory requirement (AAI13)

**Verified for the ESP32-C3 release using one authenticated MQTT broker, the BMP388 over I2C and the LD2450 over UART.** The application uses fixed storage on the qualified workload. The documented Wi-Fi driver exemption covers radio association, authentication and packet handling. The evidence below covers the firmware; the laptop broker, Configuration Desk and Home Assistant run on the host.

| Requirement | Implementation and verification |
|---|---|
| No application heap allocation | A diagnostic build recorded every successful general allocation across the measured demo and reconciled both the count and requested bytes against the firmware audit. All 410 calls (118,202 cumulative bytes) came from identified Wi-Fi driver paths, including WPA authentication. The SDK reported zero allocation failures. [Call-site inventory](docs/reports/wifi-static-memory/station-disconnect-owners.csv) |
| Create kernel objects at startup | The station reserves task stacks, control blocks and communication objects before normal operation. All six hardware audit samples reported zero kernel object creations after startup, including after broker disconnect. [Static design and measurements](docs/reports/wifi-static-memory/README.md#week-13-evidence-for-aai13) |
| Fixed RAM limits per sub-module and overall | All 41 budgeted modules fit. Application modules use **147,057 of 147,456 bytes**. Including the task pool, kernel and SDK fixed data, and code resident in RAM, the release uses **274,764 of 278,528 DRAM bytes**, leaving **3,764 bytes**. The report records the ELF and linker-map hashes and the reproduction command. [Per-module budgets](docs/reports/wifi-static-memory/transaction-module-budget.csv) |
| Bounded parsing, validation, Apply and rollback | The parser reserves 512 slots. On the ESP, a 1,033-byte Apply containing 513 array elements was rejected. Active and saved revision 943 and its CRC survived, and the next valid Apply succeeded. Host fault-injection tests exercise rollback after activation failure and recovery when rollback itself fails. [Parser boundary tests](tests/target/test_admission.py) and [Rollback cases](tests/fault_injection/rttrans_main.c) |
| Bounded network, supervisor, inter-task and temporary storage | Tasks use reserved stacks; queues, TCP packet slots, timer records and TLS buffers have fixed capacities. The demo certificate verifier shares the USB parser workspace under the qualified scheduling model. Parser exhaustion and TCP slot reuse are covered alongside physical MQTT reconnect, TLS renewal and both peripheral reads. [Capacities](tools/target/transaction-budget.json) and [TCP slot test](tools/target/tcp_packet_probe.py) |

**Scope of the result.** The release RAM figures describe fixed linked storage. The Wi-Fi exemption is justified per allocation site, with peak live payload reported separately. Before application entry, SDK interrupt and logging setup allocated 209 bytes. Arbitrary broker certificate chains and forced concurrent USB parsing and certificate verification fall outside this qualification. The latter can reach the general allocator. [Week 13 evidence and exemption rationale](docs/reports/wifi-static-memory/README.md#week-13-evidence-for-aai13)

## Project guides

| Area | Guide |
|---|---|
| Operator interface and wiring visualiser | [Configuration Desk](tools/configurator/README.md) |
| Peripheral descriptions and rule compilation | [Runtime transaction tools](tools/transaction/README.md) |
| On-board rule execution | [Transaction service](firmware/src/services/transaction/README.md) |
| USB configuration and recovery | [Configuration transfer](tools/config_transfer/README.md) |
| Software and target verification | [Tests](tests/README.md) |
