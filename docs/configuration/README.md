# Member 5 Configuration and Integration Toolkit

This host-side toolkit turns a bounded recommendation into a reviewed deployment bundle. It validates before approval and keeps **packaged**, **transferred**, **stored**, and **activated** as different states. The current filesystem adapter can prove only that a verified bundle was transferred to another directory; it cannot prove that an ESP32 stored or activated it.

## Setup

From the repository root:

```bash
python3 -m pip install -r tools/requirements.txt
python3 -m unittest discover -s tests -p 'test_*.py'
```

Python 3.11 or newer is required.

## Workflow

Validate an existing profile:

```bash
python3 -m tools.configurator validate configs/nodes/first-prototype.json
```

Generate a candidate from the registered template, approve it explicitly, then package it:

```bash
mkdir -p build
python3 -m tools.configurator generate configs/recommendations/first-prototype.json --output build/candidate.json
python3 -m tools.configurator approve build/candidate.json --reviewer member5 --output build/approved.json
python3 -m tools.configurator bundle build/approved.json --output build/deployment
```

To include the current model without modifying its bytes, add `--model PATH` to the bundle command. To test the transfer boundary locally:

```bash
python3 -m tools.configurator deploy-filesystem build/deployment build/mock-device
```

Run the review and monitoring UI:

```bash
python3 -m tools.configurator serve --host 127.0.0.1 --port 8080
```

Then open `http://127.0.0.1:8080`. The UI can load/import, edit, validate, approve/reject, export a reviewed artifact, and display a bounded health snapshot. It intentionally does not activate hardware.

Generate host-side integration evidence:

```bash
python3 -m tools.integration_runner configs/scenarios/first-prototype.json --output build/integration-report.json
```

The report distinguishes passed host checks from manual hardware steps.

## Exit codes

- `0`: command completed; for validation, the profile is valid.
- `2`: invalid input, failed validation, unsafe transition, or packaging error.

## Stable validation codes

- `SCHEMA_INVALID`: profile does not match the strict schema.
- `UNSUPPORTED_PROTOCOL`, `UNSUPPORTED_DRIVER`: allowlist violation.
- `PIN_CONFLICT`: a GPIO is assigned more than once.
- `DUPLICATE_INTERFACE_ID`, `DUPLICATE_QUEUE_ID`, `DUPLICATE_DEVICE_ID`, `DUPLICATE_TASK_NAME`: identifier collision.
- `INTERFACE_NOT_FOUND`, `QUEUE_NOT_FOUND`: broken reference.
- `VOLTAGE_DIVIDER_REQUIRED`: HC-SR04 Echo safety declaration is absent.
- `TASK_LIMIT_EXCEEDED`, `STACK_BUDGET_EXCEEDED`, `QUEUE_BUDGET_EXCEEDED`, `BUFFER_BUDGET_EXCEEDED`: configured resource budget is exceeded.

## Hardware integration contract

A future serial, network, or flash adapter should accept a verified bundle directory and return a structured state. It must re-check manifest hashes before transfer. It may return `transferred` after host I/O, `stored` only after a target acknowledgement, and `activated` only after the firmware confirms that the exact profile digest is running. The adapter must not bypass host or firmware validation.

Still awaiting agreement with the firmware owners:

- transport framing and retry/time-out rules;
- storage location and atomic rollback behaviour;
- acknowledgement containing profile digest and activation state;
- live telemetry framing for health snapshots;
- model format, compatibility identifier, and activation rules.
