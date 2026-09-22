# Services

Services translate between device drivers and application-level data:

- `sensor/` normalises BMP388, LD2450, and HC-SR04 results;
- `display/` combines the latest queue values into the OLED status view;
- `actuator/` is reserved for later safe-state-controlled outputs.

Services do not own pins or buses and do not include target-specific headers.
