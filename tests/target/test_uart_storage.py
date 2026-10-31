"""Build-routing rejection checks; no target ABI or wire claims."""
from pathlib import Path
import unittest
from test_qualification import qualification


class UartStorageTests(unittest.TestCase):
    def setUp(self):
        self.root = Path("build/source").resolve()
        self.serial = self.root / "device/ser/ser.c"
        self.record = self.root / "device/common/drvif/msdrvif.c"
        self.commands = {
            self.serial: "cc -DKmalloc=c3umem_serial_alloc -DKfree=c3umem_serial_free",
            self.record: "cc -DKmalloc=c3umem_record_alloc -DKfree=c3umem_record_free",
        }

    def test_exact_scoped_substitutions_pass(self):
        self.assertEqual(2, len(qualification.verify_uart_storage(self.commands, self.root)))

    def test_missing_native_source_fails(self):
        del self.commands[self.serial]
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_uart_storage(self.commands, self.root)

    def test_missing_or_overridden_substitutions_fail(self):
        for command in ("cc", self.commands[self.serial] + " -DKmalloc=Kmalloc"):
            with self.subTest(command=command):
                self.commands[self.serial] = command
                with self.assertRaises(qualification.QualificationError):
                    qualification.verify_uart_storage(self.commands, self.root)

    def test_substitution_on_wrong_source_fails(self):
        self.commands[self.root / "kernel/tkernel/task.c"] = self.commands[self.serial]
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_uart_storage(self.commands, self.root)

    def test_wrong_pool_fails(self):
        self.commands[self.serial] = self.commands[self.record]
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_uart_storage(self.commands, self.root)


class FaultHookScopeTests(unittest.TestCase):
    def setUp(self):
        self.project = Path("build/project").resolve()
        self.main = self.project / "main/main.c"
        self.uart = self.project / "hal/c3uart.c"
        self.test = self.project / "main/uartcheck.c"

    def test_production_has_no_hook(self):
        commands = {self.main: "cc -DUTHOME_CONFIGURATION_APP=1 -DUTHOME_NATIVE_UART_FIXTURE=0", self.uart: "cc"}
        self.assertFalse(qualification.verify_uart_fault_hook(commands, self.project))
        commands[self.uart] += " -Dtk_srea_dev=uartcheck_read"
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_uart_fault_hook(commands, self.project)

    def test_acceptance_requires_hook_and_test(self):
        commands = {self.main: "cc -DUTHOME_CONFIGURATION_APP=0 -DUTHOME_NATIVE_UART_FIXTURE=1",
                    self.uart: "cc -Dtk_srea_dev=uartcheck_read", self.test: "cc"}
        self.assertTrue(qualification.verify_uart_fault_hook(commands, self.project))
        del commands[self.test]
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_uart_fault_hook(commands, self.project)

    def test_assembled_runtime_fixture_has_no_uart_traffic_hook(self):
        commands = {self.main: "cc -DUTHOME_CONFIGURATION_APP=0 -DUTHOME_NATIVE_UART_FIXTURE=0", self.uart: "cc"}
        self.assertFalse(qualification.verify_uart_fault_hook(commands, self.project))
        commands[self.test] = "cc"
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_uart_fault_hook(commands, self.project)

    def test_ambiguous_or_unsafe_mode_fails(self):
        for flags in ("", "-DUTHOME_NATIVE_UART_FIXTURE=0 -DUTHOME_NATIVE_UART_FIXTURE=1",
                      "-DUTHOME_NATIVE_UART_FIXTURE=1"):
            commands = {self.main: "cc -DUTHOME_CONFIGURATION_APP=1 " + flags, self.uart: "cc"}
            with self.subTest(flags=flags), self.assertRaises(qualification.QualificationError):
                qualification.verify_uart_fault_hook(commands, self.project)


if __name__ == "__main__":
    unittest.main()
