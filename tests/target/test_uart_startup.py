"""Lifecycle guards, not a substitute for target register/wire qualification."""
from pathlib import Path
import re
import unittest
from test_gpio_startup import body

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/hal/ports/esp32c3/c3uart.c"


def definition(source, name):
    signature = re.search(r"(?:static )?int " + name + r" \([^;]+?\n\{", source)
    return body(source, signature.group())


class UartStartupTests(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text()

    def test_only_startup_allocates(self):
        startup = definition(self.source, "c3uart_init")
        for operation in ("tk_cre_mtx(", "tk_opn_dev("):
            self.assertEqual(1, self.source.count(operation))
            self.assertIn(operation, startup)
            self.assertLess(startup.index("g_state.b_attempted = true"),
                            startup.index(operation))
        for operation in ("tk_cls_dev(", "tk_del_mtx(", "malloc("):
            self.assertNotIn(operation, self.source)
        self.assertIn("g_state.b_ready ? 0 : UARTIO_STATE", startup)

    def test_owner_is_independent_of_mutex(self):
        opened = definition(self.source, "open_port")
        self.assertIn("g_state.b_owned", opened)
        self.assertNotIn("0 != g_state.h_mutex", opened)
        self.assertLess(opened.index("g_state.b_owned = true"),
                        opened.index("reset_buffers()"))
        self.assertLess(opened.index("set_attributes(p_config)"),
                        opened.index("LLD_SER_START"))
        closed = definition(self.source, "close_port")
        self.assertLess(closed.index("unlock_port(reset_buffers())"),
                        closed.index("g_state.b_owned = false"))
        self.assertIn("if (0 == result)", closed)

    def test_bounded_reset_masks_before_draining(self):
        reset = definition(self.source, "reset_buffers")
        commands = re.findall(r"dev_ser_llctl\(0U, (LLD_SER_\w+)", reset)
        self.assertEqual(["LLD_SER_STOP", "LLD_SER_START", "LLD_SER_STOP"], commands)
        self.assertLess(reset.rindex("LLD_SER_STOP"), reset.index("dev_ser_get_snddat"))
        self.assertIn("index < DEVCONF_SER_BUFFSIZE", reset)
        self.assertIn("return discard_receive()", reset)
        discard = definition(self.source, "discard_receive")
        self.assertIn("available(false, &pending)", discard)
        self.assertIn("pending -= count", discard)
        self.assertIn("(0U == count)", discard)

    def test_startup_precedes_activation(self):
        application = (ROOT / "firmware/app/c3app.c").read_text()
        self.assertLess(application.index("activate_init()"), application.index("c3uart_init()"))
        self.assertLess(application.index("c3uart_init()"), application.index("cfgctrl_restore("))

    def test_detect_apply_do_not_initialize_uart_lazily(self):
        for path in ("firmware/src/config/cfgbind.c",
                     "firmware/src/config/cfgctrl.c",
                     "firmware/hal/ports/esp32c3/c3bind.c"):
            self.assertNotIn("c3uart_init(", (ROOT / path).read_text())
        opened = definition(self.source, "open_port")
        self.assertIn("!g_state.b_ready", opened)
        self.assertNotIn("c3uart_init(", opened)

    def test_failed_open_and_unlock_stop_communication(self):
        opened = definition(self.source, "open_port")
        self.assertLess(opened.index("LLD_SER_START"), opened.index("LLD_SER_STOP"))
        self.assertIn("if (0 != result)", opened)
        self.assertNotIn("g_state.b_owned = false", opened)
        unlocked = definition(self.source, "unlock_port")
        self.assertIn("g_state.b_poisoned = true", unlocked)
        self.assertIn("LLD_SER_STOP", unlocked)
        self.assertNotIn("g_state.b_owned = false", unlocked)

    def test_error_reporting_retained(self):
        fault = definition(self.source, "read_fault")
        self.assertIn("DEV_SER_ERR_ROVR | DEV_SER_ERR_OE", fault)
        self.assertIn("UARTIO_OVERFLOW", fault)
        self.assertIn("UARTIO_FRAME", fault)
        read = definition(self.source, "read_bytes")
        self.assertEqual(2, read.count("read_fault()"))


if __name__ == "__main__":
    unittest.main()
