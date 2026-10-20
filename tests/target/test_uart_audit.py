"""Test audit accounting with a fake debugger; this is not a board run."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch


class FakeCommand:
    def __init__(self, *args, **kwargs):
        pass


class FakeBreakpoint(FakeCommand):
    pending = False


class AuditTests(unittest.TestCase):
    def setUp(self):
        fake = types.SimpleNamespace(Command=FakeCommand, Breakpoint=FakeBreakpoint,
            FinishBreakpoint=FakeBreakpoint, COMMAND_USER=0, GdbError=RuntimeError,
            write=lambda message: None)
        path = Path(__file__).resolve().parents[2] / "tools/target/uart_creation_audit.py"
        spec = importlib.util.spec_from_file_location("uart_audit", path)
        self.module = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"gdb": fake}):
            spec.loader.exec_module(self.module)
        self.audit = self.module.CreationAudit()

    def test_startup_calls_are_not_runtime_calls(self):
        self.audit.calls["tk_cre_sem"] = 2
        self.audit.begin()
        self.assertTrue(self.audit.report(0))

    def test_creation_attempt_fails_even_if_later_deleted_or_failed(self):
        for name in ("tk_cre_mtx", "tk_cre_sem"):
            with self.subTest(name=name):
                audit = self.module.CreationAudit()
                audit.begin()
                entry = self.module.CreationEntry(audit, name)
                self.assertFalse(entry.stop())
                self.assertEqual(1, audit.calls[name])
                self.assertFalse(audit.report(0))

    def test_board_failure_or_missing_return_is_not_a_pass(self):
        self.audit.begin()
        self.assertFalse(self.audit.report(-1))
        self.assertFalse(self.audit.report(None))

    def test_baseline_cannot_be_reset_to_hide_calls(self):
        self.audit.begin()
        self.audit.calls["tk_cre_sem"] += 1
        with self.assertRaises(RuntimeError):
            self.audit.begin()


if __name__ == "__main__":
    unittest.main()
