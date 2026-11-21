"""Verify RV32 station console/socket dispatch with SDK VFS I/O disabled."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_TP

from .tls_output_probe import OutputProbe
from .tls_fragment_probe import SSL


def verify(elf):
    p = OutputProbe(elf)
    seen = []
    def intercept(name, count):
        def observe(*_):
            seen.append((name, tuple(p.uc.reg_read(UC_RISCV_REG_A0 + i) for i in range(count))))
            p.finish(31)
        p.hook(p.symbols[name], observe)
    for name, count in [('_read_r_console', 4), ('_write_r_console', 4),
                        ('lwip_read', 3), ('lwip_write', 3), ('lwip_close', 1), ('lwip_fcntl', 3)]:
        intercept(name, count)
    for name in ('esp_vfs_register_fd_range','esp_vfs_register_fs_common','esp_vfs_select'):
        assert name not in p.symbols, name
    register = p.symbols['esp_vfs_lwip_sockets_register']
    assert bytes(p.uc.mem_read(register, 2)) == bytes.fromhex('8280')  # compressed ret
    context, data = SSL + 0x10000, SSL + 0x11000
    p.uc.reg_write(UC_RISCV_REG_TP, context)
    cases = []
    for op in ('read', 'write'):
        for fd in (0, 1, 2, 57, 58, 63):
            args = (context, fd, data, 17)
            assert p.run('_' + op + '_r', *args) == 31
            expected = ('_' + op + '_r_console', args) if fd < 58 else ('lwip_' + op, args[1:])
            assert seen.pop() == expected
            cases.append([op, fd, expected[0]])
    for fd in (58, 63):
        assert p.run('_close_r', context, fd) == 31 and seen.pop() == ('lwip_close', (fd,))
        for cmd in (3, 4):
            assert p.run('_fcntl_r', context, fd, cmd, 3) == 31
            assert seen.pop() == ('lwip_fcntl', (fd, cmd, 3))
        cases.append(['close/fcntl', fd])
    assert p.run('_close_r', context, 1) == -1
    assert int.from_bytes(p.uc.mem_read(context, 4), 'little') == 88
    assert not seen and not p.allocations
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), cases=cases,
                console_close_errno=88, socket_registration='single ret', native_allocations=0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__ or args.output.exists():
        parser.error('enable assertions and use a fresh report path')
    result = verify(args.elf)
    with args.output.open('x') as stream:
        json.dump(result, stream, indent=2)
    print('Compiled station console/socket dispatch passed.')
