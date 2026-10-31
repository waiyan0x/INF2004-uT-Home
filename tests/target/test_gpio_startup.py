"""Source lifecycle guards; these do not simulate RMT/LEDC or the kernel."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "firmware/hal/ports/esp32c3"


def body(source, signature):
    start = source.index("{", source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class StartupContractTests(unittest.TestCase):
    def test_creation_is_confined_to_one_shot_startup(self):
        for module, capacity in (("c3pulse", 2), ("c3pwm", 4)):
            with self.subTest(module=module):
                source = (PORT / f"{module}.c").read_text()
                startup = body(source, f"int {module}_startup (void)")
                self.assertEqual(source.count("tk_cre_mtx("), 1)
                self.assertEqual(startup.count("tk_cre_mtx("), 1)
                self.assertNotIn("tk_del_mtx(", source)
                self.assertRegex(startup, rf"index < {capacity}U")
                self.assertLess(startup.index("if (gb_attempted)"),
                                startup.index("tk_cre_mtx("))
                self.assertLess(startup.index("gb_attempted" , startup.index("if (gb_attempted)") + 1),
                                startup.index("tk_cre_mtx("))
                self.assertIn("return gb_ready ? 0", startup)
                self.assertIn("if (h_mutex <= 0)", startup)
                self.assertNotRegex(source, r"h_mutex\s*=\s*0\s*;")

    def test_open_requires_initialization_and_retains_partial_claims(self):
        for module in ("c3pulse", "c3pwm"):
            source = (PORT / f"{module}.c").read_text()
            # Select definition rather than the earlier prototype.
            definitions = list(re.finditer(r"static int open_port[^;]+?\n\{", source))
            self.assertEqual(len(definitions), 1)
            opened = body(source, definitions[0].group())
            self.assertIn("!gb_ready", opened)
            self.assertIn("b_open", opened)
            self.assertNotIn("tk_cre_", opened)

    def test_application_initializes_before_restore_and_requests(self):
        source = (ROOT / "firmware/app/c3app.c").read_text()
        for module in ("c3pulse", "c3pwm"):
            position = source.index(f"{module}_startup()")
            self.assertLess(source.index("activate_init()"), position)
            self.assertLess(position, source.index("cfgctrl_restore("))
            self.assertLess(position, source.index("cfgpump_step("))


if __name__ == "__main__":
    unittest.main()
