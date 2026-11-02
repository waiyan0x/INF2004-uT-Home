"""Physical ownership admission, including wiring of disabled devices."""

from .documents import ConfigurationError, check, require


class WiringClaims:
    """Track one candidate's pins, controllers, addresses and UART owners."""

    def __init__(self, board, target_limits):
        self.board = board
        self.target_limits = target_limits
        self.pins = {}
        self.ports = set()
        self.addresses = set()
        self.uart_owners = set()

    def claim_pins(self, fields, owner):
        for field, pin in fields.items():
            if not field.endswith("_gpio"):
                continue
            require(
                type(pin) is int and pin in self.board["allowed_gpios"],
                f"Unavailable GPIO {pin}: {owner}",
            )
            require(
                pin
                in self.target_limits.get("protocol_gpios", {}).get(
                    fields.get("protocol"),
                    self.target_limits.get(
                        "allowed_gpios", self.board["allowed_gpios"]
                    ),
                ),
                f"GPIO {pin} is unavailable in the target backend",
            )
            rule = self.board["conditional_pins"].get(str(pin))
            if rule:
                require(
                    field in rule["allowed_fields"]
                    and all(
                        fields.get(key) == value
                        for key, value in rule["required_fields"].items()
                    ),
                    f"GPIO {pin} boot condition is not satisfied: {owner}",
                )
            require(
                pin not in self.pins,
                f"GPIO {pin} conflict: {owner} and {self.pins.get(pin)}",
            )
            self.pins[pin] = owner

    def admit_interfaces(self, interfaces):
        for iface in interfaces.values():
            protocol = iface["protocol"]
            port = iface["host" if protocol == "spi" else "port"]
            require(
                port in self.board["ports"][protocol],
                f"Unavailable {protocol} controller {port}",
            )
            require(
                (protocol, port) not in self.ports,
                "A bus controller must have one shared interface declaration",
            )
            self.ports.add((protocol, port))
            check(
                iface,
                {
                    "type": "object",
                    "properties": self.board["interface_limits"][protocol],
                },
                "board.interface",
            )
            for field, minimum in (
                self.target_limits.get("interface_minimums", {})
                .get(protocol, {})
                .items()
            ):
                require(
                    iface[field] >= minimum,
                    f"Target {protocol}.{field} must be at least {minimum}",
                )
            fixed = self.board["fixed_pins"].get(protocol, {}).get(str(port), {})
            require(
                all(iface.get(key) == value for key, value in fixed.items()),
                "Selected port has fixed pin bindings",
            )
            self.claim_pins(iface, iface["id"])

    def admit_connection(self, device, descriptor, interfaces):
        connection = device["interface"]
        protocol = descriptor["protocol"]
        if protocol != "gpio":
            require(connection.get("ref") in interfaces, "Unknown interface reference")
            bus = interfaces[connection["ref"]]
            require(bus["protocol"] == protocol, "Descriptor/bus protocol mismatch")
            check(bus, descriptor["bus_schema"], device["device_id"] + ".bus")
            if protocol == "i2c":
                try:
                    address = int(connection["address"], 16)
                except (KeyError, ValueError, TypeError) as error:
                    raise ConfigurationError(
                        "I2C device needs a hexadecimal address"
                    ) from error
                require(0x08 <= address <= 0x77, "Reserved I2C address")
                bus_address = (bus["id"], address)
                require(bus_address not in self.addresses, "Duplicate I2C bus address")
                self.addresses.add(bus_address)
            elif protocol == "uart":
                require(
                    bus["id"] not in self.uart_owners,
                    "UART requires one owning instance",
                )
                self.uart_owners.add(bus["id"])
        else:
            require(
                connection.get("protocol") == "gpio",
                "GPIO descriptor requires GPIO binding",
            )
        self.claim_pins(connection, device["device_id"])
