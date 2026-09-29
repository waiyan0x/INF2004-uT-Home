# Rejected node profiles

These schema 2.0.0 fixtures must fail the shared host validator:

- `pin-conflict.json`: sonar Trigger conflicts with the I2C SDA binding.
- `unsupported-protocol.json`: declares a CAN interface outside the supported bus contract.

Run `python -B -m unittest discover -s tests/foundation -v` for these and other
admission failures. Rejection must precede active configuration changes.
