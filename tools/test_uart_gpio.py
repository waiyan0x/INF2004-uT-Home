"""Compile and run the standalone C99 UART/GPIO host tests; no kernel needed."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(root / "tools/c_standard/check.py"), "--root", str(root)], check=True)
compiler = shutil.which(os.environ.get("CC", "gcc"))
if not compiler:
    raise SystemExit("Install a C99 compiler (GCC/Clang), or set CC to its executable.")
includes = ["firmware/include","firmware/hal/include", "firmware/hal/ports/host_sim",
            "firmware/drivers/hlk_ld2450", "firmware/drivers/hc_sr04",
            "firmware/drivers/pwm_motor"]
sources = ["firmware/hal/uartio.c", "firmware/hal/pulseio.c", "firmware/hal/pwmio.c", "firmware/hal/pinpool.c",
           "firmware/hal/ports/host_sim/uartfake.c", "firmware/hal/ports/host_sim/gpiofake.c",
           "firmware/drivers/hlk_ld2450/ldparse.c","firmware/hal/ports/host_sim/ughost.c",
           "firmware/drivers/hlk_ld2450/ld2450.c",
           "firmware/drivers/hc_sr04/hc_sr04.c",
           "firmware/drivers/pwm_motor/pwm_motor.c", "tests/unit/ugtest_main.c"]
with tempfile.TemporaryDirectory(prefix="uthome-uart-gpio-") as tmp:
    exe = str(Path(tmp) / ("test_uart-gpio.exe" if os.name == "nt" else "test_uart-gpio"))
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    *["-I" + str(root / p) for p in includes],
                    *[str(root / p) for p in sources], "-o", exe], check=True)
    env = dict(os.environ)
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    subprocess.run([exe], check=True, env=env)
