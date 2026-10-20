"""Generate and verify bounded C inference using the mandatory project gate."""
import os
import hashlib
from pathlib import Path
import random
import shutil
import subprocess

from ai.model import predict, validate_model
from ai.timing import digest
from tools.config_validator.planner import ROOT, require
from tools.c_standard.check import canonical_format, check_format, check_modules, check_source


def export_c(model, folder):
    validate_model(model)
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=False)
    formatter = os.environ.get("BARR_CLANG_FORMAT", "clang-format")
    require(not check_format(formatter, [], "20.1.8"), "Pinned clang-format 20.1.8 is required")
    (folder / ".clang-format").write_bytes((ROOT / ".clang-format").read_bytes())
    source = (ROOT / "ai/templates/timtree.c").read_text(encoding="utf-8")
    rows = [[n["feature"] + 1, n["threshold"], n["left"], n["right"], n["score"]] for n in model["nodes"]]
    for marker, data in (("NODES", rows), ("DOMAIN", model["domain"])):
        start = source.index(f"/* {marker}_BEGIN */") + len(f"/* {marker}_BEGIN */")
        end = source.index(f"/* {marker}_END */")
        literals = ",\n".join("{" + ", ".join(f"{v}U" for v in row) + "}" for row in data)
        source = source[:start] + "\n" + literals + "\n" + source[end:]
    source = source.replace('#include "timtree.h"', '#include "timtree.h"\n\n'
                            f"/* Experimental {model['origin']} model; deployment disabled. */\n"
                            f"/* Model SHA256: */\n/* {digest(model)} */")
    for name, text in (("timtree.c", source), ("timtree.h", (ROOT / "ai/templates/timtree.h").read_text(encoding="utf-8"))):
        path = folder / name
        path.write_text(text, encoding="utf-8", newline="\n")
        formatted = subprocess.run([formatter, "--style=file", str(path)], capture_output=True, check=True)
        path.write_text(canonical_format(formatted.stdout.decode("utf-8")), encoding="utf-8", newline="\n")
    sources = {name: (folder / name).read_text(encoding="utf-8") for name in ("timtree.c", "timtree.h")}
    findings = check_modules(sources)
    for name, text in sources.items():
        findings += check_source(name, text)
    errors = check_format(formatter, [folder / name for name in sources], "20.1.8")
    require(not findings and not errors, f"Generated C failed BARR-C checks: {findings!r} {errors!r}")
    return dict(model_sha256=digest(model), origin=model["origin"], deployment_allowed=False,
                generated_c_findings=0, generated_format_errors=0,
                constant_data_bytes=4 * (5 * len(model["nodes"]) + 2 * len(model["domain"])))


def verify_c(model, folder, extra_vectors=()):
    validate_model(model)
    folder = Path(folder)
    compiler = shutil.which("gcc")
    require(compiler is not None, "GCC is required for host C parity")
    executable = folder / ("timcheck.exe" if os.name == "nt" else "timcheck")
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
                    "-I", str(folder), str(folder / "timtree.c"), str(ROOT / "tests/ai/tim_main.c"),
                    "-o", str(executable)], check=True, capture_output=True)
    vectors = [list(v) for v in extra_vectors]
    low = [bounds[0] for bounds in model["domain"]]
    high = [bounds[1] for bounds in model["domain"]]
    vectors += [low, high, [0] * 6, [0xFFFFFFFF] * 6]
    for index, (minimum, maximum) in enumerate(model["domain"]):
        for value in {max(0, minimum - 1), minimum, maximum, maximum + 1}:
            v = list(low)
            v[index] = value
            vectors.append(v)
    for node in model["nodes"]:
        if node["feature"] >= 0:
            for value in (max(0, node["threshold"] - 1), node["threshold"], node["threshold"] + 1):
                v = list(low)
                v[node["feature"]] = value
                vectors.append(v)
    rng = random.Random(42)
    vectors += [[rng.randint(a, b) for a, b in model["domain"]] for _ in range(1000)]
    text = "".join(" ".join(map(str, v)) + "\n" for v in vectors)
    completed = subprocess.run([str(executable.resolve())], input=text, text=True,
                               capture_output=True, check=True, timeout=20)
    actual = [int(line) for line in completed.stdout.splitlines()]
    require(actual == [predict(model, v) for v in vectors], "Python/C prediction mismatch")
    return dict(vectors=len(vectors), mismatches=0, compiler=compiler, standard="c99", hardware_tested=False)


def cross_check(folder, compiler):
    """Compile only the exported module for C3; no firmware link or board access."""
    folder, compiler = Path(folder).resolve(), Path(compiler).resolve()
    require(compiler.name in ("riscv32-esp-elf-gcc", "riscv32-esp-elf-gcc.exe"), "Select the ESP RISC-V compiler")
    command = [str(compiler), "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-Os",
               "-march=rv32imc", "-mabi=ilp32", "-fstack-usage", "-c", "timtree.c", "-o", "timtree.o"]
    subprocess.run(command, cwd=folder, check=True, capture_output=True)
    size_tool = compiler.with_name(compiler.name.replace("gcc", "size"))
    sizes = subprocess.run([str(size_tool), "timtree.o"], cwd=folder, capture_output=True, text=True, check=True)
    usage = (folder / "timtree.su").read_text(encoding="utf-8")
    return dict(compiler_version=subprocess.run([str(compiler), "--version"], capture_output=True,
                text=True, check=True).stdout.splitlines()[0], command=command,
                source_sha256=hashlib.sha256((folder / "timtree.c").read_bytes()).hexdigest(),
                object_sha256=hashlib.sha256((folder / "timtree.o").read_bytes()).hexdigest(),
                size_output=sizes.stdout, stack_usage=usage, firmware_linked=False, hardware_tested=False)
