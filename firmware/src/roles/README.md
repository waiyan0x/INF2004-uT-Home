# Role composition

The first prototype does not select one mutually exclusive node role. Its
single profile enables sensor, display, and monitoring components together.
The configuration manager and component registry start each enabled device
directly.

Higher-level roles such as `sensor_node`, `actuator_node`, or `monitor_node` may
be added after the multi-protocol prototype is stable. They must remain profile
templates rather than separate firmware builds or compile-time switches.
