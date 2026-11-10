"""Offline and MQTT rule-switching demo. Inputs are data, never generated C."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import queue
import re
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
STATUS = ("ok", "invalid", "unconfigured", "mismatch", "timeout", "io")
SOURCES = [
    "firmware/src/services/transaction/txrule.c",
    "firmware/src/services/transaction/txwire.c",
    "firmware/src/services/transaction/txi2c.c",
    "firmware/drivers/generic_i2c/geni2c.c",
    "firmware/hal/i2cbus.c",
    "tests/target/txsim.c",
]
INCLUDES = [
    "firmware/src/services/transaction", "firmware/drivers/generic_i2c",
    "firmware/hal/include", "tests/target",
]


def load_rule(path):
    return validate_rule(json.loads(Path(path).read_text(encoding="utf-8")))


def validate_rule(obj):
    """Shared admission for JSON fixtures and Markdown-compiled rules."""
    if not isinstance(obj, dict) or set(obj) != {"version", "revision", "device", "address", "register", "read_length"}:
        raise ValueError("unexpected rule fields")
    for key, low, high in (("version", 1, 1), ("revision", 1, 0xffffffff),
                           ("address", 8, 119), ("register", 0, 255),
                           ("read_length", 1, 16)):
        if type(obj[key]) is not int or not low <= obj[key] <= high:
            raise ValueError("invalid " + key)
    name = obj["device"]
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,31}", name):
        raise ValueError("invalid device")
    return obj


def request(rule, request_id, update):
    name = rule["device"].encode("ascii")
    return (struct.pack("<2sBBII32s", b"TR", 1, 1 if update else 2,
                        request_id, rule["revision"], name)
            + bytes([rule["address"], rule["register"], rule["read_length"], 0]
                    if update else [0, 0, 0, 0]))


def result(payload):
    if len(payload) != 65 or payload[:3] != bytes([84, 82, 1]):
        raise ValueError("invalid reply envelope")
    _, _, kind, rid, rev, name = struct.unpack("<2sBBII32s", payload[:44])
    address, register, length, status, size = payload[44:49]
    if status >= len(STATUS) or size > 16 or (status != 0 and size != 0):
        raise ValueError("invalid reply status/length")
    return dict(kind="update" if kind == 1 else "read", request_id=rid,
                revision=rev, device=name.split(bytes([0]))[0].decode("ascii"),
                address=address, register=register, read_length=length,
                status=STATUS[status], raw_hex=payload[49:49+size].hex(" "))


def build(output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which("gcc")
    if not compiler:
        raise RuntimeError("gcc is required")
    binary = output / ("txsim.dll" if __import__("os").name == "nt" else "txsim.so")
    command = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
               "-UNDEBUG", "-shared", "-fPIC", "-o", str(binary)]
    command += ["-I" + str(ROOT / inc) for inc in INCLUDES]
    command += [str(ROOT / source) for source in SOURCES]
    subprocess.run(command, check=True)
    evidence = dict(compiler=subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
                    command=command, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    sources={s: hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in SOURCES})
    (output/"build.json").write_text(json.dumps(evidence, indent=2))
    return binary


class Offline:
    def __init__(self, binary):
        self.lib = c.CDLL(str(Path(binary).resolve()))
        self.lib.txsim_exchange.argtypes = [c.c_void_p, c.c_size_t, c.c_void_p]
        self.lib.txsim_size.argtypes = [c.c_uint32]
        self.lib.txsim_size.restype = c.c_size_t
        self.lib.txsim_init()

    def exchange(self, payload):
        output = (c.c_uint8 * 65)()
        source = c.create_string_buffer(payload)
        self.lib.txsim_exchange(source, len(payload), output)
        return bytes(output)


class Usb:
    """Temporary diagnostic transport; open only the approved rule-probe image."""
    def __init__(self, port):
        import serial
        self.port = serial.Serial(port=None, baudrate=115200, timeout=3, write_timeout=3)
        self.port.dtr = False
        self.port.rts = False
        self.port.port = port
        self.port.open()
        self.port.reset_input_buffer()

    def exchange(self, payload):
        if self.port.write(payload) != len(payload):
            raise IOError("short USB request; wait 2 seconds before reconnecting")
        reply = self.port.read(65)
        if len(reply) != 65 or reply[4:8] != payload[4:8]:
            raise IOError("USB response missing/mismatched; no automatic retry")
        return reply

    def close(self):
        self.port.close()


class Mqtt:
    def __init__(self, broker, port, prefix):
        import paho.mqtt.client as mqtt
        self.incoming = queue.Queue()
        self.ready = __import__("threading").Event()
        self.prefix = prefix
        self.client = mqtt.Client(client_id="runtime-rules-host")
        self.client.on_connect = self._connect
        self.client.on_subscribe = lambda *args: self.ready.set()
        self.client.on_message = lambda client, userdata, msg: self.incoming.put(msg.payload)
        self.client.connect(broker, port)
        self.client.loop_start()
        if not self.ready.wait(10):
            self.close()
            raise TimeoutError("MQTT subscription not ready")

    def _connect(self, client, userdata, flags, rc):
        if rc == 0:
            client.subscribe(self.prefix + "/reply", qos=0)

    def exchange(self, payload):
        rid = struct.unpack_from("<I", payload, 4)[0]
        self.client.publish(self.prefix + "/request", payload, qos=0, retain=False)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            reply = self.incoming.get(timeout=max(0.001, deadline-time.monotonic()))
            if len(reply) == 65 and struct.unpack_from("<I", reply, 4)[0] == rid:
                return reply
        raise TimeoutError("No matching MQTT reply; no automatic retry")

    def close(self):
        self.client.disconnect()
        self.client.loop_stop()


def demonstrate(exchange, rule_a, rule_b):
    messages = [request(rule_a, 1, True), request(rule_a, 2, False),
                request(rule_b, 3, True), request(rule_b, 4, False)]
    bad = bytearray(request(rule_b, 5, True))
    bad[46] = 17
    messages += [bytes(bad), request(rule_b, 6, False)]
    outputs = [result(exchange(message)) for message in messages]
    expected = ["ok", "ok", "ok", "ok", "invalid", "ok"]
    if [o["status"] for o in outputs] != expected:
        raise RuntimeError(json.dumps(outputs, indent=2))
    for index, rule in ((1, rule_a), (3, rule_b), (5, rule_b)):
        out = outputs[index]
        assert (out["revision"], out["register"], out["read_length"]) == (
            rule["revision"], rule["register"], rule["read_length"])
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("offline", "mqtt", "usb"), default="offline")
    parser.add_argument("--output", type=Path, default=ROOT/"build/runtime-rules")
    parser.add_argument("--serial-port", help="confirmed port running the isolated rule probe")
    parser.add_argument("--broker", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--prefix", default="uthome/rule-lab")
    parser.add_argument("--a", type=Path, default=HERE/"examples/bmp-id.json")
    parser.add_argument("--b", type=Path, default=HERE/"examples/bmp-pressure.json")
    args = parser.parse_args()
    a, b = load_rule(args.a), load_rule(args.b)
    if args.mode == "usb" and not args.serial_port:
        parser.error("USB mode requires --serial-port")
    if args.mode == "offline":
        client = Offline(build(args.output))
    elif args.mode == "usb":
        client = Usb(args.serial_port)
    else:
        client = Mqtt(args.broker, args.port, args.prefix)
    try:
        outputs = demonstrate(client.exchange, a, b)
    finally:
        if not isinstance(client, Offline):
            client.close()
    report = dict(mode=args.mode,
                  data_origin="simulated" if args.mode == "offline" else "remote_unverified",
                  results=outputs)
    if isinstance(client, Offline):
        report["actual_driver_calls"] = client.lib.txsim_calls()
        report["host_sizes"] = {name: client.lib.txsim_size(i)
                                for i, name in enumerate(("rule", "state", "result"))}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/"demo.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
