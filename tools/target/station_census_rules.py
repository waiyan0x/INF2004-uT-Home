"""Emit verified RV32 caller-walk rules for the station heap census."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

from elftools.dwarf.callframe import FDE
from elftools.elf.elffile import ELFFile


LAYERS = set("""
heap_caps_aligned_alloc_base heap_caps_malloc_base heap_caps_calloc_base
heap_caps_calloc heap_caps_malloc heap_caps_malloc_default heap_caps_realloc_default
heap_caps_realloc_base heap_caps_realloc malloc calloc realloc wifi_malloc
wifi_realloc wifi_calloc wifi_zalloc_wrapper esp_coex_common_malloc_internal_wrapper
esp_timer_create ets_timer_setfn esp_coex_common_timer_setfn_wrapper os_zalloc
heap_caps_aligned_alloc mbedtls_calloc wifi_malloc_wrapper c3tout_prf_alloc
c3peer_psa_alloc psa_allocate_buffer_to_slot psa_copy_key_material_into_slot
psa_import_key psa_generate_key_custom psa_key_derivation_input_internal
psa_key_derivation_input_bytes psa_key_derivation_input_key psa_tls12_prf_input
psa_tls12_prf_set_key os_memdup aes_crypt_init aes_encrypt_init aes_decrypt_init
calloc_internal_wrapper zalloc_internal_wrapper
""".split())
HOOKS = {"census_alloc", "__wrap_c3audit_alloc", "c3audit_alloc"}
HEADER = re.compile(r"^(\w+) <(.+)>:")
CALL = re.compile(r"^\s*(\w+):\s+(\w+)\s+(?:jalr?|c\.jal)\s+.*<([^>]+)>")


def read_rules(elf, objdump):
    disassembly = subprocess.check_output([objdump, "-d", str(elf)], text=True)
    rules = []
    function = ""
    with elf.open("rb") as stream:
        frames = {
            entry["initial_location"]: entry
            for entry in ELFFile(stream).get_dwarf_info().CFI_entries()
            if isinstance(entry, FDE)
        }
        for line in disassembly.splitlines():
            header = HEADER.match(line)
            if header:
                base = int(header[1], 16)
                function = header[2]
                continue
            if function not in LAYERS:
                continue
            call = CALL.match(line)
            if not call or call[3] not in LAYERS | HOOKS:
                continue
            pc = int(call[1], 16)
            table = frames[base].get_decoded().table
            row = next(entry for entry in reversed(table) if entry["pc"] <= pc)
            cfa = row["cfa"]
            saved_ra = row[1]
            if cfa.reg != 2 or saved_ra.type != "OFFSET":
                raise ValueError(f"Unsupported frame at {pc:#x}")
            offset = cfa.offset + saved_ra.arg
            if offset % 4 or not 0 <= offset < cfa.offset <= 512:
                raise ValueError(f"Invalid stack offset at {pc:#x}")
            target = "census_alloc" if call[3] == "__wrap_c3audit_alloc" else call[3]
            rules.append([pc + len(call[2]) // 2, offset, cfa.offset, function, target])
    if len(rules) >= 64 or not any(row[4] in HOOKS for row in rules):
        raise ValueError("Rules exceed recorder capacity or omit the heap hook")
    return {"elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(), "rules": rules}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--objdump", default="riscv32-esp-elf-objdump")
    args = parser.parse_args()
    print(json.dumps(read_rules(args.elf, args.objdump), indent=2))
