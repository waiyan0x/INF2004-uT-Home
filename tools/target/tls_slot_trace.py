"""ESP GDB command for counting shared PSA slots from an empty key store."""
import json
import time

import gdb


def address(name):
    return int(gdb.parse_and_eval('&' + name))


def reserve_return():
    """Require the reviewed RV32 reservation function's single native return."""
    entry = address('psa_reserve_free_key_slot')
    block = gdb.block_for_pc(entry)
    arch = gdb.selected_inferior().architecture()
    if arch.name() != 'riscv:rv32' or block.start != entry:
        raise gdb.GdbError('Expected the RV32 PSA reservation function')
    instructions = arch.disassemble(entry, block.end - 1)
    returns = [i['addr'] for i in instructions if i['asm'].strip() == 'ret']
    if len(returns) != 1:
        raise gdb.GdbError('Review the reservation exits for this ELF')
    return returns[0]


class SlotEvent(gdb.Breakpoint):
    def __init__(self, pc, kind, trace):
        super().__init__(f'*{pc:#x}', type=gdb.BP_HARDWARE_BREAKPOINT)
        self.kind = kind
        self.trace = trace

    def stop(self):
        trace = self.trace
        try:
            if self.kind == 'reserve':
                status = int(gdb.parse_and_eval('(int)$a0'))
                if status:
                    trace.record('reserve_failed', status=status)
                else:
                    trace.used += 1
                    trace.peak = max(trace.peak, trace.used)
                    trace.record('reserve')
            else:
                state = int(gdb.parse_and_eval('((psa_key_slot_t *)$a0)->state'))
                if state not in trace.occupied or trace.used <= 0:
                    raise ValueError('Wipe does not match an occupied slot')
                trace.used -= 1
                trace.record('wipe')
        except Exception as error:
            trace.record('invalid', reason=str(error))
            return True
        return False


class SlotTrace(gdb.Command):
    """Start the observation at the first instruction of cold PSA initialization."""

    def __init__(self):
        super().__init__('uthome-slot-trace', gdb.COMMAND_USER)
        self.output = None

    def record(self, event, **fields):
        row = dict(event=event, monotonic_ns=time.monotonic_ns(),
                   used=self.used, peak=self.peak, **fields)
        self.output.write(json.dumps(row) + '\n')
        self.output.flush()

    def invoke(self, argument, from_tty):
        args = gdb.string_to_argv(argument)
        if len(args) != 1 or self.output is not None:
            raise gdb.GdbError('Use uthome-slot-trace NEW_JSONL_FILE once per boot')
        if int(gdb.parse_and_eval('$pc')) != address('psa_initialize_key_slots'):
            raise gdb.GdbError('Stop at cold psa_initialize_key_slots first')
        store = gdb.parse_and_eval("'psa_crypto_slot_management.c'::global_data")
        slices = store['key_slices']
        low, high = slices.type.range()
        if int(store['key_slots_initialized']) or any(int(slices[i]) for i in range(low, high + 1)):
            raise gdb.GdbError('Key store already initialized or occupied')
        self.occupied = {int(gdb.parse_and_eval(name)) for name in
                         ('PSA_SLOT_FILLING', 'PSA_SLOT_FULL', 'PSA_SLOT_PENDING_DELETION')}
        reserve = reserve_return()
        wipe = address('psa_wipe_key_slot')
        self.used = self.peak = 0
        self.output = open(args[0], 'x', encoding='utf-8')
        self.probes = (SlotEvent(reserve, 'reserve', self), SlotEvent(wipe, 'wipe', self))
        self.record('armed', reserve_pc=reserve, wipe_pc=wipe)


SlotTrace()
