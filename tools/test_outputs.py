"""Run the required standard gate, then optional relay/indicator host tests."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    env = dict(os.environ)
    bundled = ROOT / "build/dev-tools/clang_format/data/bin/clang-format.exe"
    if "BARR_CLANG_FORMAT" not in env and bundled.is_file():
        env["BARR_CLANG_FORMAT"] = str(bundled)
    gate = subprocess.run([sys.executable, str(ROOT / "tools/c_standard/check.py"), "--json"],
                          cwd=ROOT, env=env, capture_output=True, text=True)
    report_dir = ROOT / "build/output-tests"
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / "c-standard.json").write_text(gate.stdout, encoding="utf-8")
    if gate.returncode:
        raise SystemExit("Repository C standard gate FAILED; details: build/output-tests/c-standard.json")
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    if not compiler:
        raise SystemExit("GCC or a compatible C99 compiler is required; set CC.")
    includes = ["firmware/hal/include", "firmware/include",
                "firmware/drivers/relayctl", "firmware/drivers/signalout",
                "firmware/src/services", "firmware/src/automation"]
    sources = ["firmware/drivers/relayctl/relayctl.c",
               "firmware/drivers/signalout/signalout.c",
               "firmware/src/services/fanctrl.c",
               "firmware/src/automation/rulecalc.c",
               "firmware/src/services/sampcache.c",
               "firmware/hal/pinpool.c", "tests/unit/outputs_main.c"]
    with tempfile.TemporaryDirectory(prefix="uthome-outputs-") as folder:
        binary = str(Path(folder) / ("outputs.exe" if os.name == "nt" else "outputs"))
        subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-pedantic", *["-I" + str(ROOT / p) for p in includes],
                        *[str(ROOT / p) for p in sources], "-lm", "-o", binary],
                       check=True, cwd=ROOT)
        env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        subprocess.run([binary], check=True, env=env)
    if gate.returncode:
        raise SystemExit("Functional tests passed, but the repository standard gate remains failing.")


if __name__ == "__main__":
    main()
