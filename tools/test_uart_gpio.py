"""Compile and run the standalone C11 UART/GPIO host tests; no kernel needed."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shutil.which(os.environ.get("CC", "gcc"))
if not compiler:
    raise SystemExit("Install a C11 compiler (GCC/Clang), or set CC to its executable.")
includes = ["firmware/hal/include", "firmware/hal/ports/host_sim",
            "firmware/drivers/hlk_ld2450", "firmware/drivers/hc_sr04",
            "firmware/drivers/pwm_motor"]
sources = ["firmware/hal/ports/host_sim/uart_gpio_host.c",
           "firmware/drivers/hlk_ld2450/ld2450.c",
           "firmware/drivers/hc_sr04/hc_sr04.c",
           "firmware/drivers/pwm_motor/pwm_motor.c", "tests/unit/test_uart_gpio.c"]
with tempfile.TemporaryDirectory(prefix="uthome-uart-gpio-") as tmp:
    exe = str(Path(tmp) / ("test_uart-gpio.exe" if os.name == "nt" else "test_uart-gpio"))
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    *["-I" + str(root / p) for p in includes],
                    *[str(root / p) for p in sources], "-o", exe], check=True)
    env = dict(os.environ)
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    subprocess.run([exe], check=True, env=env)
