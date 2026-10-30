# Static Wireless Task Resource Plan

## Scope

This document records the resource requirements for the
ESP32-C3 wireless task compatibility adapter.

The implementation must not allocate application heap memory.
All required native task and notification objects must be
created before startup completes.

## Existing Resources

- Four permanent peripheral-worker stacks: 8192 bytes each.
- Native startup pool: 12288 bytes.
- Existing fixed queue and event resources remain unchanged.

## Service Task Requirements

| Service | Requested Stack | Shim Headroom | Proposed Reservation | Evidence |
|---|---:|---:|---:|---|
| ESP timer service | 4096 B | 2048 B | 6144 B | sdkconfig + esp_task.h + esp_timer.c |
| lwIP TCP/IP | 3584 B | 2048 B | 5632 B | sdkconfig + esp_task.h + lwipopts.h + sys_arch.c |
| Wi-Fi task | 6656 B | 2048 B | 8704 B (candidate) | Earlier observed shim request; verify current build |
| BLE host | 4096 B | 2048 B | 6144 B (candidate) | Verify active NimBLE configuration |
| Other SDK tasks | Pending | Pending | Pending | Requires complete task inventory |

These values are candidates, not approved reservations.
Simultaneous task counts and measured stack requirements
must be verified before allocating static storage.

## Budget Evidence

- Application limit: 155648 B
- Existing module reservations: 152692 B
- Unreserved application budget: 2956 B
- Scheduler build application usage: 148809 B
- Scheduler build total DRAM usage: 230152 B
- Total DRAM limit: 278528 B

The historical scheduler build excludes a qualified
full Wi-Fi/BLE/task/timer lifecycle.

## Outstanding Decisions

1. Confirm all simultaneously required SDK tasks.
2. Agree resource requirements with Member 2.
3. Determine whether reservations can be redistributed.
4. Verify stack headroom using measurements.
5. Obtain a reviewed memory budget before allocating stacks.
6. Test task-slot exhaustion and task reuse.