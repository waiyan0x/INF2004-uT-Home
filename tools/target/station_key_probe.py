"""Replay station key registration and pthread cleanup on linked RV32 code."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2

from .tls_fragment_probe import SSL
from .tls_output_probe import OutputProbe


def verify(elf, check_delete):
    probe = OutputProbe(elf)
    cpu, symbols = probe.uc, probe.symbols
    live = {}
    requests = []
    destructed = []
    thread = [0]
    accepting = False
    next_address = SSL + 0x19000

    def word(address):
        return int.from_bytes(cpu.mem_read(address, 4), "little")

    def allocate(size):
        nonlocal next_address
        requests.append(size)
        assert accepting, ("unexpected startup allocation", size)
        address = next_address
        next_address += 16
        assert 0 < size <= 16
        live[address] = size
        cpu.mem_write(address, bytes(size))
        probe.finish(address)

    def release(*_):
        address = cpu.reg_read(UC_RISCV_REG_A0)
        assert address in live, ("free of unowned or fixed storage", address)
        del live[address]
        probe.finish(0)

    def mutex(*_):
        cpu.mem_write(cpu.reg_read(UC_RISCV_REG_A0), struct.pack("<I", SSL + 0x18000))
        probe.finish(0)

    def set_thread(*_):
        thread[0] = cpu.reg_read(UC_RISCV_REG_A2)
        probe.finish(0)

    def destructor(*_):
        destructed.append(cpu.reg_read(UC_RISCV_REG_A0))
        probe.finish(0)

    for name in ("__wrap_vPortEnterCritical", "__wrap_vPortExitCritical",
                 "c3bport_enter", "c3bport_leave"):
        probe.hook(symbols[name], lambda *_: probe.finish(0))
    probe.hook(symbols["malloc"], lambda *_: allocate(cpu.reg_read(UC_RISCV_REG_A0)))
    probe.hook(symbols["calloc"], lambda *_: allocate(
        cpu.reg_read(UC_RISCV_REG_A0) * cpu.reg_read(UC_RISCV_REG_A1)))
    probe.hook(symbols["free"], release)
    probe.hook(symbols["sys_mutex_new"], mutex)
    probe.hook(symbols["__wrap_pvTaskGetThreadLocalStoragePointer"],
               lambda *_: probe.finish(thread[0]))
    for name in ("__wrap_vTaskSetThreadLocalStoragePointer",
                 "__wrap_vTaskSetThreadLocalStoragePointerAndDelCallback"):
        probe.hook(symbols[name], set_thread)
    probe.hook(symbols["sys_thread_sem_free"], destructor)

    # Execute the toolchain constructor, including the native registry call.
    probe.run("_GLOBAL__sub_I__ZN17__eh_globals_init7_S_initE")
    generic_node = word(symbols["s_keys"])
    assert generic_node == symbols["g_keys"]
    assert word(generic_node) == 1
    assert word(generic_node + 4) == symbols["_ZL15eh_globals_dtorPv"]
    assert word(symbols["_ZL4init"]) == 1 and not requests
    fixed = symbols["g_socket_key"]
    # A full key-ID range cannot publish zero or corrupt the existing list.
    cpu.mem_write(generic_node, bytes.fromhex("ffffffff"))
    assert probe.run("c3lsem_key", symbols["sys_thread_sem_key"], symbols["sys_thread_sem_free"]) != 0
    assert word(symbols["s_keys"]) == generic_node and word(fixed) == 0
    assert word(symbols["sys_thread_sem_key"]) == 0
    cpu.mem_write(generic_node, struct.pack("<I", 1))
    for _ in range(2):
        probe.run("sys_init")
        assert word(symbols["sys_thread_sem_key"]) == word(fixed) == 2
        assert word(symbols["s_keys"]) == fixed and word(fixed + 8) == generic_node
        assert word(fixed + 4) == symbols["sys_thread_sem_free"]
    generic = SSL + 0x18400
    wifi_destructor = symbols["wifi_thread_semphr_free"]
    assert probe.run("pthread_key_create", generic, wifi_destructor) == 0
    second_node = word(symbols["s_keys"])
    assert word(generic) == 3 and second_node == generic_node + 12
    assert word(second_node + 4) == wifi_destructor and word(second_node + 8) == fixed
    # A third generic key must leave the list, caller output and live slots intact.
    assert probe.run("pthread_key_create", generic, 0) == 12  # SDK ENOMEM
    assert word(generic) == 3 and word(symbols["s_keys"]) == second_node
    assert not requests

    # A foreign thread still gets the native value list and destructor.
    accepting = True
    value = SSL + 0x18800
    assert probe.run("pthread_setspecific", 2, value) == 0
    assert probe.run("pthread_getspecific", 2) == value
    assert requests == [4, 12]
    probe.run("pthread_cleanup_thread_specific_data_callback", 0, thread[0])
    assert destructed == [value] and not live

    if check_delete:
        # This function is normally discarded by the production linker.
        accepting = False
        assert probe.run("pthread_key_delete", 1) == 0
        assert word(fixed + 8) == 0 and word(second_node + 8) == fixed
        assert probe.run("pthread_key_create", generic, 0) == 0
        assert word(generic) == 4 and word(symbols["s_keys"]) == generic_node
        assert word(generic_node + 4) == 0 and word(generic_node + 8) == second_node
        assert probe.run("pthread_key_delete", 3) == 0
        assert probe.run("pthread_key_delete", 4) == 0
        assert word(symbols["s_keys"]) == fixed
        assert probe.run("pthread_key_delete", 2) == 0
        assert word(fixed) == 0 and word(symbols["s_keys"]) == 0
        probe.run("sys_init")
        assert word(symbols["s_keys"]) == fixed and word(fixed) == 1
        assert word(fixed + 8) == 0
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                startup_heap_requests=0, repeated_registration=True,
                generic_slots=2, exhausted_without_heap=True, foreign_value_requests=requests, destructor_calls=len(destructed),
                native_key_delete_and_reuse=check_delete)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--check-delete", action="store_true",
                        help="Require a verification link retaining pthread_key_delete")
    args = parser.parse_args()
    if not __debug__:
        parser.error("Assertions must be enabled")
    print(json.dumps(verify(args.elf, args.check_delete), indent=2))
