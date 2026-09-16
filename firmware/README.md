# Firmware

One image for every node. `app/main.c` boots μT-Kernel, loads and validates the profile,
then starts only the roles and services the profile enables.

Data flow: sensor driver → sensor task → bounded queue → automation → message bus / command queue → actuator task → actuator driver.
The health task observes all of the above.
