"""GDB command to read native lwIP allocation counters on a halted ESP32-C3."""

import json

import gdb


def counters(record):
    record.fetch_lazy()
    return {name: int(record[name]) for name in ("used", "max", "err", "illegal")}


class LwipMemory(gdb.Command):
    """Print requested live/peak bytes and per-pool live/peak record counts."""

    def __init__(self):
        super().__init__("uthome-lwip-memory", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        if argument.strip():
            raise gdb.GdbError("Usage: uthome-lwip-memory")
        memory = gdb.parse_and_eval("lwip_stats.mem")
        pools = gdb.parse_and_eval("memp_pools")
        pool_type = gdb.lookup_type("memp_t")
        records = {}
        for index in range(int(gdb.parse_and_eval("MEMP_MAX"))):
            name = str(gdb.Value(index).cast(pool_type))
            descriptor = pools[index].dereference()
            record = counters(descriptor["stats"].dereference())
            record["element_bytes"] = int(descriptor["size"])
            records[name] = record
        gdb.write(json.dumps({"requested_bytes": counters(memory), "pools": records}) + "\n")


LwipMemory()
