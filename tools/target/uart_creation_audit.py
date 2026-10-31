"""Source in target GDB after loading the acceptance ELF, before uartcheck_run.

Run `uart-creation-audit`, then continue. Breakpoints count entry into the actual
kernel creation functions, including failed attempts. No dependency edits.
Debugger stops at test return and prints PASS only for zero creation calls and
a successful board test. Missing symbols or an incomplete run are not a pass.
"""
import gdb


class CreationAudit:
    def __init__(self):
        self.calls = {"tk_cre_mtx": 0, "tk_cre_sem": 0}
        self.baseline = None
        self.breakpoints = []
        self.finished = False

    def begin(self):
        if self.baseline is not None:
            raise gdb.GdbError("UART audit may run only once per debugger setup")
        self.baseline = dict(self.calls)

    def report(self, result):
        self.finished = True
        delta = {name: total - self.baseline[name]
                 for name, total in self.calls.items()}
        passed = result == 0 and all(count == 0 for count in delta.values())
        gdb.write(f"UART creation-call audit: {'PASS' if passed else 'FAIL'} "
                  f"calls_after_startup={delta} board_result={result}\n")
        return passed


class CreationEntry(gdb.Breakpoint):
    def __init__(self, audit, name):
        super().__init__(name, internal=True)
        if self.pending:
            self.delete()
            raise gdb.GdbError(f"Missing kernel symbol: {name}")
        self.audit = audit
        self.name = name

    def stop(self):
        self.audit.calls[self.name] += 1
        return False


class TestReturn(gdb.FinishBreakpoint):
    def __init__(self, audit):
        super().__init__(gdb.selected_frame(), internal=True)
        self.audit = audit

    def stop(self):
        result = None if self.return_value is None else int(self.return_value)
        self.audit.report(result)
        return True

    def out_of_scope(self):
        gdb.write("UART creation-call audit: FAIL (test did not return normally)\n")


class TestEntry(gdb.Breakpoint):
    def __init__(self, audit):
        super().__init__("uartcheck_run", internal=True)
        if self.pending:
            self.delete()
            raise gdb.GdbError("Missing uartcheck_run: use the acceptance ELF")
        self.audit = audit

    def stop(self):
        self.audit.begin()
        self.audit.breakpoints.append(TestReturn(self.audit))
        return False


class AuditCommand(gdb.Command):
    def __init__(self):
        super().__init__("uart-creation-audit", gdb.COMMAND_USER)
        self.audit = None

    def invoke(self, argument, from_tty):
        if argument.strip() or self.audit is not None:
            raise gdb.GdbError("No arguments; install once before the test starts")
        audit = CreationAudit()
        try:
            for name in audit.calls:
                audit.breakpoints.append(CreationEntry(audit, name))
            audit.breakpoints.append(TestEntry(audit))
        except Exception:
            for breakpoint in audit.breakpoints:
                breakpoint.delete()
            raise
        self.audit = audit
        gdb.write("UART creation-call audit armed. Continue to uartcheck_run; "
                  "no result until its return.\n")


AuditCommand()
