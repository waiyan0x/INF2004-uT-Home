# Sensor services

Sensor services convert driver results into hardware-independent queue items:

- `environment_sample`: pressure, temperature, timestamp, status;
- `target_report`: zero to three targets, timestamp, status;
- `distance_sample`: distance, timestamp, status.

Services apply units and freshness rules. They do not access registers or GPIOs
directly.
