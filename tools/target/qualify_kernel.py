"""Build the pinned kernel example and record evidence; never flash.

Run from an activated, isolated ESP-IDF terminal. This tool does not install an
SDK, change Git state or qualify a physical board. An explicit compatibility
patch affects only the isolated source copy; the dependency remains pristine.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
KERNEL_REVISION = "ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2"
SDK_REVISION = "fff9895c82d744c7237be8847347bdd1b07c6643"
COMPILER_PACKAGE = "esp-15.2.0_20251204"
# Exact reviewed configuration.csv bytes, including MD5 and erased padding.
# cfgpart's independent host tests match this digest and attack every byte.
CONFIGURATION_TABLE_SHA256 = "7f7b089b96a3f7c3d7c431bd974909909e60a2bb8bdf4bb6802e9edbb6d9167e"

# Support both the documented script command and package imports in host tests.
if __package__:
    from .build_modes import BuildMode, BuildSelection, QualificationError, UPSTREAM_EXAMPLES, resolve_mode
else:
    from build_modes import BuildMode, BuildSelection, QualificationError, UPSTREAM_EXAMPLES, resolve_mode

RUNTIME_SOURCE_FILES = {
    "firmware/src/config/cfgmux.c": "config/cfgmux.c",
    "firmware/src/config/cfgmux.h": "config/cfgmux.h",
    "firmware/src/network/netbroker.c": "network/netbroker.c",
    "firmware/src/network/netbroker.h": "network/netbroker.h",
    "firmware/hal/ports/esp32c3/c3mqtt.c": "hal/c3mqtt.c",
    "firmware/hal/ports/esp32c3/c3mqtt.h": "hal/c3mqtt.h",
    "firmware/src/network/netmqtt.c": "network/netmqtt.c",
    "firmware/src/network/netmqtt.h": "network/netmqtt.h",
    "firmware/src/network/netretry.c": "network/netretry.c",
    "firmware/src/network/netretry.h": "network/netretry.h",
    "firmware/src/network/nethttp.c": "network/nethttp.c",
    "firmware/src/network/nethttp.h": "network/nethttp.h",
    "firmware/src/network/netread.c": "network/netread.c",
    "firmware/src/network/netread.h": "network/netread.h",
    "firmware/src/network/netble.c": "network/netble.c",
    "firmware/src/network/netble.h": "network/netble.h",
    "firmware/hal/ports/esp32c3/c3wsta.c": "hal/c3wsta.c",
    "firmware/hal/ports/esp32c3/c3wsta.h": "hal/c3wsta.h",
    "firmware/hal/ports/esp32c3/c3https.c": "hal/c3https.c",
    "firmware/hal/ports/esp32c3/c3https.h": "hal/c3https.h",
    "firmware/hal/ports/esp32c3/c3gsvc.c": "hal/c3gsvc.c",
    "firmware/hal/ports/esp32c3/c3gsvc.h": "hal/c3gsvc.h",
    "firmware/app/c3net.c": "app/c3net.c",
    "firmware/app/c3net.h": "app/c3net.h",
    "tests/target/runtime_port_board/network.defaults": "network.defaults",
    "tools/target/network_setup.py": "network_setup.py",

    "tests/target/runtime_port_board/ble-static.defaults": "ble-static.defaults",
    "firmware/hal/ports/esp32c3/network/c3gatt.c": "hal/c3gatt.c",
    "firmware/hal/ports/esp32c3/network/c3gatt.h": "hal/c3gatt.h",
    "tests/target/runtime_port_board/ble-gatt.defaults": "ble-gatt.defaults",
    "firmware/hal/ports/esp32c3/network/c3bport.c": "hal/c3bport.c",
    "firmware/hal/ports/esp32c3/network/c3bport.h": "hal/c3bport.h",
    "tests/target/runtime_port_board/radio/CMakeLists.txt": "components/uthome_radio/CMakeLists.txt",
    "firmware/hal/ports/esp32c3/network/c3ble.c": "hal/c3ble.c",
    "firmware/hal/ports/esp32c3/network/c3ble.h": "hal/c3ble.h",
    "tests/target/runtime_port_board/ble.defaults": "ble.defaults",
    "firmware/hal/ports/esp32c3/network/c3boot.c": "hal/c3boot.c",
    "firmware/hal/ports/esp32c3/network/c3boot.h": "hal/c3boot.h",
    "firmware/hal/ports/esp32c3/network/c3ewait.c": "hal/c3ewait.c",
    "firmware/hal/ports/esp32c3/network/c3ewait.h": "hal/c3ewait.h",
    "firmware/hal/ports/esp32c3/network/c3event.c": "hal/c3event.c",
    "firmware/hal/ports/esp32c3/network/c3event.h": "hal/c3event.h",
    "firmware/hal/ports/esp32c3/c3task.c": "hal/c3task.c",
    "firmware/hal/ports/esp32c3/c3task.h": "hal/c3task.h",
    "tests/target/runtime_port_board/task/CMakeLists.txt": "components/uthome_task/CMakeLists.txt",
    "firmware/src/network/netevent.c": "network/netevent.c",
    "firmware/src/network/netevent.h": "network/netevent.h",
    "firmware/hal/ports/esp32c3/network/c3wifi.c": "hal/c3wifi.c",
    "firmware/hal/ports/esp32c3/network/c3wifi.h": "hal/c3wifi.h",
    "tests/target/runtime_port_board/wifi.defaults": "wifi.defaults",
    "tests/target/runtime_port_board/wifi-station.defaults": "wifi-station.defaults",
    "tests/target/runtime_port_board/main/nqcheck.c": "main/nqcheck.c",
    "tests/target/runtime_port_board/main/nqcheck.h": "main/nqcheck.h",
    'firmware/src/network/netring.c': 'network/netring.c',
    'firmware/src/network/netring.h': 'network/netring.h',
    'firmware/src/network/netwait.c': 'network/netwait.c',
    'firmware/src/network/netwait.h': 'network/netwait.h',
    'firmware/src/network/netpool.c': 'network/netpool.c',
    'firmware/src/network/netpool.h': 'network/netpool.h',
    'firmware/hal/ports/esp32c3/network/c3lbox.c': 'hal/c3lbox.c',
    'firmware/hal/ports/esp32c3/network/c3lbox.h': 'hal/c3lbox.h',
    'firmware/hal/ports/esp32c3/network/c3nq.c': 'hal/c3nq.c',
    'firmware/hal/ports/esp32c3/network/c3nq.h': 'hal/c3nq.h',
    'tests/target/runtime_port_board/main/netqueue.cmake': 'main/netqueue.cmake',
    'tests/target/runtime_port_board/main/netqueue.lf': 'main/netqueue.lf',
    "firmware/src/runtime/stackmon.c": "runtime/stackmon.c",
    "firmware/src/runtime/stackmon.h": "runtime/stackmon.h",
    "firmware/src/runtime/workcost.c": "runtime/workcost.c",
    "firmware/src/runtime/workcost.h": "runtime/workcost.h",
    "firmware/src/runtime/workmeter.c": "runtime/workmeter.c",
    "firmware/src/runtime/workmeter.h": "runtime/workmeter.h",
    "firmware/hal/ports/esp32c3/c3clock.c": "hal/c3clock.c",
    "firmware/hal/ports/esp32c3/c3clock.h": "hal/c3clock.h",
    "firmware/hal/ports/esp32c3/c3cost.c": "hal/c3cost.c",
    "firmware/hal/ports/esp32c3/c3cost.h": "hal/c3cost.h",
    "firmware/src/services/costview.c": "services/costview.c",
    "firmware/src/services/costview.h": "services/costview.h",
    "firmware/src/runtime/memaudit.c": "runtime/memaudit.c",
    "firmware/src/runtime/memaudit.h": "runtime/memaudit.h",
    "firmware/hal/ports/esp32c3/c3audit.c": "hal/c3audit.c",
    "firmware/hal/ports/esp32c3/c3audit.h": "hal/c3audit.h",
    "firmware/hal/ports/esp32c3/c3start.c": "hal/c3start.c",
    "firmware/hal/ports/esp32c3/c3start.h": "hal/c3start.h",
    "tests/target/runtime_port_board/main/audit.lf": "main/audit.lf",
    "tests/target/runtime_port_board/audit.defaults": "audit.defaults",
    "firmware/hal/ports/esp32c3/storage/cfgpart.c": "hal/storage/cfgpart.c",
    "firmware/hal/ports/esp32c3/storage/cfgslot.c": "hal/storage/cfgslot.c",
    "firmware/hal/ports/esp32c3/storage/cfgslot.h": "hal/storage/cfgslot.h",
    "firmware/hal/ports/esp32c3/storage/cfgpart.h": "hal/storage/cfgpart.h",
    "tests/target/runtime_port_board/main/uartcheck.c": "main/uartcheck.c",
    "tests/target/runtime_port_board/main/uartcheck.h": "main/uartcheck.h",
    "firmware/hal/ports/esp32c3/c3umem.c": "hal/c3umem.c",
    "firmware/hal/ports/esp32c3/c3umem.h": "hal/c3umem.h",
    "firmware/src/discovery/discuart.c": "discovery/discuart.c",
    "firmware/src/discovery/discuart.h": "discovery/discuart.h",
    "firmware/src/discovery/disci2c.c": "discovery/disci2c.c",
    "firmware/src/discovery/disci2c.h": "discovery/disci2c.h",
    "firmware/src/discovery/cfgdisc.c": "discovery/cfgdisc.c",
    "firmware/src/discovery/cfgdisc.h": "discovery/cfgdisc.h",
    "firmware/src/services/display/oledview.c": "services/oledview.c",
    "firmware/src/services/display/oledview.h": "services/oledview.h",
    "firmware/src/services/display/oledwork.c": "services/oledwork.c",
    "firmware/src/services/display/oledwork.h": "services/oledwork.h",
    "firmware/src/services/actuator/pwmwork.c": "services/pwmwork.c",
    "firmware/src/services/actuator/pwmwork.h": "services/pwmwork.h",
    "firmware/src/services/telemetry.c": "services/telemetry.c",
    "firmware/src/services/telemetry.h": "services/telemetry.h",

    "firmware/src/services/statusmon.c": "services/statusmon.c",
    "firmware/src/services/statusmon.h": "services/statusmon.h",

    "firmware/src/runtime/sampcache.c": "runtime/sampcache.c",
    "firmware/src/runtime/sampcache.h": "runtime/sampcache.h",

    "firmware/src/config/cfgpump.c": "config/cfgpump.c",
    "firmware/src/config/cfgpump.h": "config/cfgpump.h",
    "firmware/hal/ports/esp32c3/c3usb.c": "hal/c3usb.c",
    "firmware/hal/ports/esp32c3/c3usb.h": "hal/c3usb.h",
    "firmware/hal/ports/esp32c3/c3store.c": "hal/c3store.c",
    "firmware/hal/ports/esp32c3/c3store.h": "hal/c3store.h",
    "firmware/include/uthome/txjob.h": "include/uthome/txjob.h",
    "firmware/src/services/transaction/txjob.c": "services/txjob.c",
    "firmware/src/services/transaction/txport.h": "services/txport.h",
    "firmware/src/services/transaction/txport.c": "services/txport.c",
    "firmware/app/c3tx.h": "app/c3tx.h",
    "firmware/app/c3tx.c": "app/c3tx.c",
    "firmware/app/c3app.c": "app/c3app.c",
    "firmware/app/c3app.h": "app/c3app.h",
    "tests/target/runtime_port_board/configuration.defaults": "configuration.defaults",
    "tests/target/runtime_port_board/configuration.csv": "configuration.csv",
    "firmware/src/config/jsonscan.c": "config/jsonscan.c",
    "firmware/src/config/jsonscan.h": "config/jsonscan.h",
    "firmware/src/config/cfgjson.c": "config/cfgjson.c",
    "firmware/src/config/cfgjson.h": "config/cfgjson.h",
    "firmware/src/config/cfgplan.c": "config/cfgplan.c",
    "firmware/src/config/cfgplan.h": "config/cfgplan.h",
    "firmware/src/config/cfgdev.c": "config/cfgdev.c",
    "firmware/src/config/cfgdev.h": "config/cfgdev.h",
    "firmware/src/config/cfgrule.c": "config/cfgrule.c",
    "firmware/src/config/cfgrule.h": "config/cfgrule.h",
    "firmware/src/automation/rulecalc.c": "automation/rulecalc.c",
    "firmware/src/automation/rulecalc.h": "automation/rulecalc.h",
    "firmware/src/config/cfgbind.c": "config/cfgbind.c",
    "firmware/src/config/cfgbind.h": "config/cfgbind.h",
    "firmware/hal/ports/esp32c3/c3bind.c": "hal/c3bind.c",
    "firmware/hal/ports/esp32c3/c3bind.h": "hal/c3bind.h",
    "firmware/third_party/cjson/cJSON.c": "third_party/cjson/cJSON.c",
    "firmware/third_party/cjson/cJSON.h": "third_party/cjson/cJSON.h",
    "firmware/third_party/cjson/LICENSE": "third_party/cjson/LICENSE",
    "firmware/third_party/cjson/SOURCE.json": "third_party/cjson/SOURCE.json",
    "tests/target/runtime_port_board/main/idf_component.yml": "main/idf_component.yml",
    "tests/target/runtime_port_board/parser.defaults": "parser.defaults",

    "firmware/src/config/cfgbytes.c": "config/cfgbytes.c",
    "firmware/src/config/cfgbytes.h": "config/cfgbytes.h",
    "firmware/src/config/cfgwire.c": "config/cfgwire.c",
    "firmware/src/config/cfgwire.h": "config/cfgwire.h",
    "firmware/src/config/cfgstore.c": "config/cfgstore.c",
    "firmware/src/config/cfgstore.h": "config/cfgstore.h",
    "firmware/src/config/cfgctrl.c": "config/cfgctrl.c",
    "firmware/src/config/cfgctrl.h": "config/cfgctrl.h",
    "firmware/hal/include/pulseio.h": "hal/include/pulseio.h",
    "firmware/hal/pulseio.c": "hal/pulseio.c",
    "firmware/hal/include/pwmio.h": "hal/include/pwmio.h",
    "firmware/hal/pwmio.c": "hal/pwmio.c",
    "firmware/hal/include/digiin.h": "hal/include/digiin.h",
    "firmware/hal/ports/esp32c3/c3pulse.c": "hal/c3pulse.c",
    "firmware/hal/ports/esp32c3/c3pulse.h": "hal/c3pulse.h",
    "firmware/hal/ports/esp32c3/c3pwm.c": "hal/c3pwm.c",
    "firmware/hal/ports/esp32c3/c3pwm.h": "hal/c3pwm.h",
    "firmware/drivers/hc_sr04/hc_sr04.c": "drivers/hc_sr04.c",
    "firmware/drivers/hc_sr04/hc_sr04.h": "drivers/hc_sr04.h",
    "firmware/drivers/hc_sr04/hcinst.c": "drivers/hcinst.c",
    "firmware/drivers/hc_sr04/hcinst.h": "drivers/hcinst.h",
    "firmware/drivers/pwm_output/pwminst.c": "drivers/pwminst.c",
    "firmware/drivers/pwm_output/pwminst.h": "drivers/pwminst.h",
    "firmware/hal/include/uartio.h": "hal/include/uartio.h",
    "firmware/hal/uartio.c": "hal/uartio.c",
    "firmware/hal/ports/esp32c3/c3uart.h": "hal/c3uart.h",
    "firmware/hal/ports/esp32c3/c3uart.c": "hal/c3uart.c",
    "firmware/drivers/hlk_ld2450/ldparse.c": "drivers/ldparse.c",
    "firmware/drivers/hlk_ld2450/ldparse.h": "drivers/ldparse.h",
    "firmware/drivers/hlk_ld2450/ld2450.c": "drivers/ld2450.c",
    "firmware/drivers/hlk_ld2450/ld2450.h": "drivers/ld2450.h",
    "firmware/drivers/hlk_ld2450/ldinst.c": "drivers/ldinst.c",
    "firmware/drivers/hlk_ld2450/ldinst.h": "drivers/ldinst.h",

    "tests/target/runtime_port_board/CMakeLists.txt": "CMakeLists.txt",
    "tests/target/runtime_port_board/main/CMakeLists.txt": "main/CMakeLists.txt",
    "tests/target/runtime_port_board/main/main.c": "main/main.c",
    "tests/target/runtime_port_board/main/main.h": "main/main.h",
    "firmware/src/runtime/tkport.h": "runtime/tkport.h",
    "firmware/src/runtime/tkport.c": "runtime/tkport.c",
    "firmware/src/runtime/rt_port.h": "runtime/rt_port.h",
    "firmware/src/runtime/workport.h": "runtime/workport.h",
    "firmware/include/uthome/runtime.h": "include/uthome/runtime.h",
    "firmware/include/uthome/component.h": "include/uthome/component.h",
    "firmware/src/runtime/activate.c": "runtime/activate.c",
    "firmware/src/runtime/activate.h": "runtime/activate.h",
    "firmware/src/runtime/shared.c": "runtime/shared.c",
    "firmware/src/runtime/shared.h": "runtime/shared.h",
    "firmware/src/runtime/component.c": "runtime/component.c",
    "firmware/src/messaging/msgbus.c": "messaging/msgbus.c",
    "firmware/src/health/health.c": "health/health.c",
    "firmware/src/health/watchdog.c": "health/watchdog.c",
    "firmware/include/uthome/msgbus.h": "include/uthome/msgbus.h",
    "firmware/include/uthome/health.h": "include/uthome/health.h",
    "firmware/include/uthome/watchdog.h": "include/uthome/watchdog.h",
    "firmware/include/uthome/sample.h": "include/uthome/sample.h",
    "firmware/include/uthome/drvreg.h": "include/uthome/drvreg.h",
    "firmware/src/config/drvreg.c": "config/drvreg.c",
    "firmware/hal/include/i2cbus.h": "hal/include/i2cbus.h",
    "firmware/hal/i2cbus.c": "hal/i2cbus.c",
    "firmware/hal/include/i2cown.h": "hal/include/i2cown.h",
    "firmware/hal/i2cown.c": "hal/i2cown.c",
    "firmware/hal/include/buslife.h": "hal/include/buslife.h",
    "firmware/hal/buslife.c": "hal/buslife.c",
    "firmware/hal/bootonce.c": "hal/bootonce.c",
    "firmware/hal/include/bootonce.h": "hal/include/bootonce.h",
    "firmware/hal/ports/esp32c3/c3ukern.c": "hal/c3ukern.c",
    "firmware/hal/ports/esp32c3/c3ukern.h": "hal/c3ukern.h",
    "firmware/hal/include/pinpool.h": "hal/include/pinpool.h",
    "firmware/hal/pinpool.c": "hal/pinpool.c",
    "firmware/hal/include/digiout.h": "hal/include/digiout.h",
    "firmware/hal/include/spibus.h": "hal/include/spibus.h",
    "firmware/hal/spibus.c": "hal/spibus.c",
    "firmware/hal/include/spiown.h": "hal/include/spiown.h",
    "firmware/hal/spiown.c": "hal/spiown.c",
    "firmware/hal/ports/esp32c3/c3pin.h": "hal/c3pin.h",
    "firmware/hal/ports/esp32c3/c3pin.c": "hal/c3pin.c",
    "firmware/hal/ports/esp32c3/c3spi.h": "hal/c3spi.h",
    "firmware/hal/ports/esp32c3/c3spi.c": "hal/c3spi.c",
    "firmware/drivers/ssd1306_spi/ssd1306.h": "drivers/ssd1306.h",
    "firmware/drivers/ssd1306_spi/ssd1306.c": "drivers/ssd1306.c",
    "firmware/drivers/ssd1306_spi/oledinst.h": "drivers/oledinst.h",
    "firmware/drivers/ssd1306_spi/oledinst.c": "drivers/oledinst.c",
    "firmware/hal/ports/esp32c3/c3i2c.h": "hal/c3i2c.h",
    "firmware/hal/ports/esp32c3/c3i2c.c": "hal/c3i2c.c",
    "firmware/hal/ports/esp32c3/c3bwait.c": "hal/c3bwait.c",
    "firmware/hal/ports/esp32c3/c3bwait.h": "hal/c3bwait.h",
    "firmware/hal/ports/esp32c3/c3ihw.c": "hal/c3ihw.c",
    "firmware/hal/ports/esp32c3/c3ihw.h": "hal/c3ihw.h",
    "firmware/hal/ports/esp32c3/c3shw.c": "hal/c3shw.c",
    "firmware/hal/ports/esp32c3/c3shw.h": "hal/c3shw.h",
    "firmware/drivers/generic_i2c/geni2c.h": "drivers/geni2c.h",
    "firmware/drivers/generic_i2c/geni2c.c": "drivers/geni2c.c",
    "firmware/drivers/generic_i2c/recinst.h": "drivers/recinst.h",
    "firmware/drivers/generic_i2c/recinst.c": "drivers/recinst.c",
    "firmware/drivers/generic_i2c/senlux.h": "drivers/senlux.h",
    "firmware/drivers/generic_i2c/senlux.c": "drivers/senlux.c",
    "firmware/drivers/generic_i2c/seninst.h": "drivers/seninst.h",
    "firmware/drivers/generic_i2c/seninst.c": "drivers/seninst.c",
    "firmware/drivers/bmp388/bmp388.h": "drivers/bmp388.h",
    "firmware/drivers/bmp388/bmp388.c": "drivers/bmp388.c",
    "firmware/drivers/bmp388/bmpinst.h": "drivers/bmpinst.h",
    "firmware/drivers/bmp388/bmpinst.c": "drivers/bmpinst.c",
    "firmware/third_party/bmp3/bmp3.h": "third_party/bmp3/bmp3.h",
    "firmware/third_party/bmp3/bmp3.c": "third_party/bmp3/bmp3.c",
    "firmware/third_party/bmp3/bmp3_defs.h": "third_party/bmp3/bmp3_defs.h",
}


def check_compiler_flags(command, expected_standard):
    """Check effective language and warning-error options, not CMake intent."""
    standards = re.findall(r"(?:^|\s)-std=([^\s]+)", command)
    if not standards or standards[-1] != expected_standard:
        raise QualificationError(f"Effective C standard must be {expected_standard}")
    flags = re.findall(r"(?:^|\s)(-W[^\s]+|-w)(?=\s|$)", command)
    if "-w" in flags:
        raise QualificationError("Compiler warnings are disabled")
    for enabled, disabled in (("-Wall", "-Wno-all"), ("-Wextra", "-Wno-extra"),
                              ("-Werror", "-Wno-error")):
        relevant = [flag for flag in flags if flag in (enabled, disabled)]
        if not relevant or relevant[-1] != enabled:
            raise QualificationError(f"Effective compiler profile requires {enabled}")
    overrides = {}
    for flag in flags:
        if flag.startswith("-Wno-error="):
            overrides[flag.split("=", 1)[1]] = False
        elif flag.startswith("-Werror="):
            overrides[flag.split("=", 1)[1]] = True
    if not all(overrides.values()):
        raise QualificationError("SDK warning-error exemptions were not overridden")


def verify_uart_storage(commands, source_root):
    """Fail closed if scoped native allocator substitutions are absent/leak."""
    expected = {
        (source_root / "device/ser/ser.c").resolve(): "serial",
        (source_root / "device/common/drvif/msdrvif.c").resolve(): "record",
    }
    for path, role in expected.items():
        command = commands.get(path)
        if command is None:
            raise QualificationError(f"Missing native UART compiler command: {path}")
        for original, suffix in (("Kmalloc", "alloc"), ("Kfree", "free")):
            values = re.findall(r'(?:^|\s)-D\s*"?' + original + r'=([A-Za-z_]\w*)"?', command)
            if values != [f"c3umem_{role}_{suffix}"]:
                raise QualificationError(f"Invalid UART storage substitution: {path}: {original}")
    for path, command in commands.items():
        if path not in expected and re.search(r'-D\s*"?K(?:malloc|free)=c3umem_', command):
            raise QualificationError(f"UART storage substitution leaked to {path}")
    return {str(path): role for path, role in expected.items()}


def verify_uart_fault_hook(commands, project):
    """Keep native UART traffic/fault injection in its explicit isolated mode."""
    main = commands.get((project / "main/main.c").resolve(), "")
    modes = re.findall(r'(?:^|\s)-D\s*"?UTHOME_CONFIGURATION_APP=([01])"?', main)
    if len(modes) != 1:
        raise QualificationError("Missing or ambiguous application build mode")
    application = modes[0] == "1"
    selections = re.findall(r'(?:^|\s)-D\s*"?UTHOME_NATIVE_UART_FIXTURE=([01])"?', main)
    if len(selections) != 1:
        raise QualificationError("Missing or ambiguous native UART fixture mode")
    native_uart = selections[0] == "1"
    if application and native_uart:
        raise QualificationError("Application cannot include native UART fixture")
    native_source = (project / "hal/c3ukern.c").resolve()
    uart = commands.get(native_source, "")
    hooks = re.findall(r'(?:^|\s)-D\s*"?tk_srea_dev=([A-Za-z_]\w*)"?', uart)
    if hooks != (["uartcheck_read"] if native_uart else []):
        raise QualificationError("Invalid UART fault-injection build scope")
    for source, command in commands.items():
        if source != native_source and re.search(r'(?:^|\s)-D\s*"?tk_srea_dev=', command):
            raise QualificationError("UART read hook escaped its native boundary")
    test_present = (project / "main/uartcheck.c").resolve() in commands
    if test_present != native_uart:
        raise QualificationError("UART acceptance source does not match build mode")
    return native_uart


def verify_flash_guard(commands):
    """Require both checked-layout guards only in the pinned flash OS adapter."""
    guards = ("esp_partition_is_flash_region_writable", "esp_partition_main_flash_region_safe")
    selected = [path for path in commands
                if path.as_posix().endswith("/components/spi_flash/spi_flash_os_func_app.c")]
    if len(selected) != 1:
        raise QualificationError("Missing or ambiguous SDK flash OS adapter")
    for path, command in commands.items():
        for guard in guards:
            values = re.findall(r'(?:^|\s)-D\s*"?' + guard + r'=([A-Za-z_]\w*)"?', command)
            expected = ["c3store_region_allowed"] if path == selected[0] else []
            if values != expected:
                raise QualificationError(f"Invalid static flash guard scope: {path}: {guard}")
    return str(selected[0])


AUDIT_CREATORS = (
    ("task_manage", "tk_cre_tsk", "task"), ("mutex", "tk_cre_mtx", "mutex"),
    ("semaphore", "tk_cre_sem", "sem"), ("eventflag", "tk_cre_flg", "flag"),
    ("mailbox", "tk_cre_mbx", "mailbox"), ("messagebuf", "tk_cre_mbf", "message"),
    ("mempfix", "tk_cre_mpf", "fixed_pool"), ("mempool", "tk_cre_mpl", "pool"),
    ("rendezvous", "tk_cre_por", "port"), ("time_calls", "tk_cre_cyc", "cycle"),
    ("time_calls", "tk_cre_alm", "alarm"))
AUDIT_RENAMES = {"/components/heap/heap_caps_base.c": {
    "esp_heap_trace_alloc_hook": "c3audit_alloc", "esp_heap_trace_free_hook": "c3audit_free"}}
for _file, _symbol, _name in AUDIT_CREATORS:
    AUDIT_RENAMES.setdefault(f"/kernel/tkernel/{_file}.c", {})[_symbol] = f"c3audit_native_{_name}"
AUDIT_RENAMES["/kernel/tkernel/memory.c"] = {
    f"knl_I{name}": f"c3audit_native_{name}" for name in ("malloc", "calloc", "realloc", "free")}


def verify_audit_scope(commands):
    """Reject missing, duplicate, overridden or globally leaked substitutions."""
    selected = {}
    all_names = {symbol for renames in AUDIT_RENAMES.values() for symbol in renames}
    for suffix in AUDIT_RENAMES:
        paths = [path for path in commands if path.as_posix().endswith(suffix)]
        if len(paths) != 1:
            raise QualificationError(f"Missing or ambiguous audit source: {suffix}")
        selected[paths[0]] = AUDIT_RENAMES[suffix]
    for path, command in commands.items():
        for symbol in all_names:
            values = re.findall(r'(?:^|\s)-D\s*"?' + symbol + r'=([A-Za-z_]\w*)"?', command)
            expected = selected.get(path, {}).get(symbol)
            if values != ([] if expected is None else [expected]):
                raise QualificationError(f"Invalid audit scope: {path}: {symbol}")
    return {str(path): renames for path, renames in selected.items()}


STARTUP_RENAMES = {
    "/kernel/tkernel/device.c": {
        "knl_initialize_devmgr": "c3start_native_devmgr"},
    "/lib/libtk/fastmlock.c": {"CreateMLock": "c3start_native_mlock"},
    "/kernel/sysdepend/iote_esp32c3_mini/devinit.c": {
        "knl_start_device": "c3start_native_device"},
    "/device/ser/ser.c": {"dev_init_ser": "c3start_native_serial"},
    "/device/adc/adc.c": {"dev_init_adc": "c3start_native_adc"},
}

STARTUP_SYMBOLS = (
    ("knl_initialize_devmgr", "c3start_devmgr", "c3start_native_devmgr"),
    ("CreateMLock", "c3start_mlock", "c3start_native_mlock"),
    ("knl_start_device", "c3start_device", "c3start_native_device"),
    ("dev_init_ser", "c3start_serial", "c3start_native_serial"),
    ("dev_init_adc", "c3start_adc", "c3start_native_adc"),
)


def verify_startup_scope(commands, project, enabled):
    """Confine startup tracing to the Wiâ€‘Fi diagnostic boundaries only."""
    selected = {}
    all_names = {symbol for renames in STARTUP_RENAMES.values() for symbol in renames}
    for suffix, renames in STARTUP_RENAMES.items():
        paths = [path for path in commands if path.as_posix().endswith(suffix)]
        if len(paths) != 1:
            raise QualificationError(f"Missing or ambiguous startup source: {suffix}")
        selected[paths[0]] = renames
    for path, command in commands.items():
        for symbol in all_names:
            values = re.findall(r'(?:^|\s)-D\s*"?' + symbol + r'=([A-Za-z_]\w*)"?', command)
            expected = selected.get(path, {}).get(symbol) if enabled else None
            if values != ([] if expected is None else [expected]):
                raise QualificationError(f"Invalid startup trace scope: {path}: {symbol}")
    trace_present = (project / "hal/c3start.c").resolve() in commands
    if trace_present != enabled:
        raise QualificationError("Startup trace source does not match Wiâ€‘Fi mode")
    return {"enabled": enabled,
            "renames": {str(path): renames for path, renames in selected.items()} if enabled else {}}


def check_audit_symbols(symbols):
    """Verify live wrappers/aliases and cache-safe hook primitives in a C3 ELF."""
    required = ("c3audit_alloc", "c3audit_free", "memaudit_heap", "memaudit_create",
                "memaudit_kernel", "disint", "enaint")
    for name in required:
        if not 0x40380000 <= symbols.get(name, 0) < 0x403e0000:
            raise QualificationError(f"Audit hook path is missing or outside IRAM: {name}")
    pairs = [(symbol, f"c3audit_create_{name}", f"c3audit_native_{name}")
             for _, symbol, name in AUDIT_CREATORS]
    pairs += [(f"knl_I{name}", f"c3audit_k{name}", f"c3audit_native_{name}")
              for name in ("malloc", "calloc", "realloc", "free")]
    retained = {}
    for original, wrapper, native in pairs:
        address = symbols.get(wrapper, 0)
        alias = symbols.get(f"__wrap_{original}", 0)
        if original in symbols or alias != address or bool(address) != bool(symbols.get(native)):
            raise QualificationError(f"Invalid linked audit forwarding: {original}")
        # Unreferenced wrappers can be garbage-collected along with their native
        # implementations. A zero alias is valid only for such an unused pair.
        retained[original] = bool(address)
    return retained


def check_startup_symbols(symbols, enabled):
    """Verify diagnostic aliases preserve each exact native startup result."""
    pairs = STARTUP_SYMBOLS
    names = {name for pair in pairs for name in pair}
    names.update("__wrap_" + pair[0] for pair in pairs)
    if not enabled:
        leaked = sorted(name for name in names if name.startswith("c3start_") and name in symbols)
        if leaked:
            raise QualificationError("Startup trace leaked outside Wiâ€‘Fi probe: " + ", ".join(leaked))
        return {"enabled": False}
    linked = {}
    for original, wrapper, native in pairs:
        address = symbols.get(wrapper, 0)
        if (not address or symbols.get("__wrap_" + original) != address
                or not symbols.get(native) or original in symbols):
            raise QualificationError("Invalid linked startup forwarding: " + original)
        linked[original] = {"wrapper": wrapper, "wrapper_address": address,
                            "native": native, "native_address": symbols[native]}
    return {"enabled": True, "linked": linked}


def verify_wifi_probe(work):
    """Require the diagnostic-only Wi-Fi storage and tick settings."""
    config_lines = set((work / "sdkconfig").read_text().splitlines())
    required = {"CONFIG_FREERTOS_HZ=1000", "# CONFIG_BT_ENABLED is not set",
                "# CONFIG_ESP_WIFI_NVS_ENABLED is not set",
                "# CONFIG_ESP_WIFI_IRAM_OPT is not set",
                "# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set",
                "# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set"}
    if not required <= config_lines:
        raise QualificationError("Wi-Fi probe storage/tick isolation settings differ")
    return "Noncompliant diagnostic only: scan/stop/restart; no association/AP/IP/NVS; USB application state retained but not running"


def verify_configuration_layout(work):
    """Require the exact partition bytes reviewed by the write guard."""
    table = work / "build/partition_table/partition-table.bin"
    if sha256(table) != CONFIGURATION_TABLE_SHA256:
        raise QualificationError("Generated partition table differs from the reviewed static layout")


def verify_managed_cjson(project):
    """Require pinned component metadata and byte-identical host/target sources."""
    dependency_lock = project / "dependencies.lock"
    if not dependency_lock.is_file():
        raise QualificationError("Missing managed-component dependency lock")
    import yaml
    lock = yaml.safe_load(dependency_lock.read_text(encoding="utf-8"))
    dependency = lock.get("dependencies", {}).get("espressif/cjson", {})
    if dependency.get("version") != "1.7.19~2":
        raise QualificationError("Unexpected cJSON managed-component version")
    managed = project / "managed_components/espressif__cjson/cJSON"
    hashes = {}
    for name in ("cJSON.c", "cJSON.h", "LICENSE"):
        if sha256(managed / name) != sha256(ROOT / "firmware/third_party/cjson" / name):
            raise QualificationError(f"Host/target cJSON bytes differ: {name}")
        hashes[name] = sha256(managed / name)
    return {
        "version": dependency["version"], "component_hash": dependency.get("component_hash"),
        "source_sha256": hashes, "lock_sha256": sha256(dependency_lock)}


def verify_ble_probe(work, gatt=False, static_host=False):
    """Check real roles and linked radio paths, not an unwritable SDK capability."""
    config_lines = set((work / "sdkconfig").read_text().splitlines())
    required = {"CONFIG_FREERTOS_HZ=1000", "CONFIG_BT_ENABLED=y", "CONFIG_BT_NIMBLE_ENABLED=y",
                "# CONFIG_BT_NIMBLE_NVS_PERSIST is not set",
                "# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set",
                "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y" if gatt else "# CONFIG_BT_NIMBLE_ROLE_PERIPHERAL is not set",
                "# CONFIG_BT_NIMBLE_ROLE_CENTRAL is not set",
                "# CONFIG_BT_NIMBLE_ROLE_OBSERVER is not set",
                "CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y", "CONFIG_BT_BLE_CCA_MODE_HW=y",
                "CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y"}
    if gatt:
        required.add("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1")
    if static_host:
        required.add("# CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC is not set")
    if not required <= config_lines:
        raise QualificationError("BLE probe storage/role/tick isolation settings differ")
    elf = work / "build/uthome_runtime_port.elf"
    output = run(["riscv32-esp-elf-nm", "-n", elf])
    symbols = {name: int(address, 16) for address, _, name in
               re.findall(r"^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$", output, re.MULTILINE)}
    required_symbols = {"c3ble_probe", "c3bport_enter", "c3bport_leave", "c3bport_delay",
                        "c3bport_exit", "c3bport_capture", "c3bport_seal", "nimble_port_init"}
    if gatt:
        required_symbols.update(("c3gatt_init", "c3gatt_reads", "ble_gatts_add_svcs"))
    elif any(name.startswith("c3gatt_") for name in symbols):
        raise QualificationError("GATT code leaked into advertising-only diagnostic")
    if not required_symbols <= symbols.keys() or any(not symbols[name] for name in required_symbols):
        raise QualificationError("BLE probe or owned native bridge is absent")
    if any(name in symbols for name in ("esp_wifi_init", "esp_wifi_start", "esp_wifi_connect", "c3wifi_probe")):
        raise QualificationError("Wi-Fi activation code leaked into BLE diagnostic")
    if any(name in symbols for name in ("shim_ble_patch_modules_funcs", "c3ble_native_patch", "ke_event_schedule_safe")):
        raise QualificationError("Legacy ROM patch leaked into the flash BLE controller")
    return {"status": "passed", "elf_sha256": sha256(elf), "settings": sorted(required),
            "bridge_symbols": {name: symbols[name] for name in sorted(required_symbols)},
            "legacy_patch": False, "controller_in_flash": True, "wifi_activation_linked": False,
            "read_only_gatt_diagnostic": gatt, "static_host_experiment": static_host,
            "whole_system_static_qualified": False}


def verify_audit_link(work):
    sdkconfig = (work / "sdkconfig").read_text(encoding="utf-8")
    if "CONFIG_HEAP_USE_HOOKS=y" not in sdkconfig.splitlines():
        raise QualificationError("SDK heap hooks are disabled")
    elf = work / "build/uthome_runtime_port.elf"
    output = run(["riscv32-esp-elf-nm", "-n", elf])
    symbols = {name: int(address, 16) for address, _, name in
               re.findall(r"^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$", output, re.MULTILINE)}
    retained = check_audit_symbols(symbols)
    return {"status": "passed", "elf_sha256": sha256(elf), "retained": retained,
            "symbols": {name: address for name, address in symbols.items()
                        if name.startswith(("c3audit_", "memaudit_", "__wrap_"))
                        or name in ("disint", "enaint", "add_count", "record_create", "record_kernel")}}


def verify_startup_link(work, enabled):
    """Inspect the final ELF rather than trusting linker command text."""
    elf = work / "build/uthome_runtime_port.elf"
    output = run(["riscv32-esp-elf-nm", "-n", elf])
    symbols = {name: int(address, 16) for address, _, name in
               re.findall(r"^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$", output, re.MULTILINE)}
    result = check_startup_symbols(symbols, enabled)
    result["elf_sha256"] = sha256(elf)
    return result


def queue_aliases(path):
    """Read only the declared ABI table, never source-substitution definitions."""
    content = Path(path).read_text(encoding="utf-8")
    table = re.search(r"set\(UTHOME_QUEUE_ALIASES\s+(.*?)\n\)", content, re.DOTALL)
    if table is None:
        raise QualificationError("Missing owned queue ABI table")
    pairs = re.findall(r"^\s*(\w+)=(c3nq_\w+)\s*$", table[1], re.MULTILINE)
    if len(pairs) != 24 or len(dict(pairs)) != 24:
        raise QualificationError("Unexpected owned queue ABI inventory")
    return dict(pairs)


def check_queue_symbols(symbols, aliases):
    """Reject absent/misdirected ABI aliases or cache-unsafe owned entry points."""
    if len(aliases) != 24 or len(set(aliases)) != 24:
        raise QualificationError("Unexpected owned queue ABI inventory")
    retained = {}
    for original, owned in aliases.items():
        address = symbols.get(owned, 0)
        if (not 0x40380000 <= address < 0x403e0000
                or symbols.get("__wrap_" + original) != address):
            raise QualificationError("Invalid owned queue alias: " + original)
        retained[original] = dict(owned=owned, address=address)
    return retained


def check_event_symbols(symbols, path):
    if not 0x40380000 <= symbols.get("c3ewait_available", 0) < 0x403e0000:
        raise QualificationError("Event cache-availability check is missing or outside IRAM")
    content = Path(path).read_text(encoding="utf-8")
    table = re.search(r"set\(UTHOME_EVENT_ALIASES\s+(.*?)\n\)", content, re.DOTALL)
    pairs = [] if table is None else re.findall(r"^\s*(\w+)=(c3event_\w+)\s*$", table[1], re.MULTILINE)
    if len(pairs) != 11 or len(dict(pairs)) != 11:
        raise QualificationError("Unexpected owned event ABI inventory")
    retained = {}
    for original, owned in pairs:
        address = symbols.get(owned, 0)
        if (not 0x40380000 <= address < 0x403e0000
                or symbols.get("__wrap_" + original) != address):
            raise QualificationError("Invalid owned event alias: " + original)
        retained[original] = dict(owned=owned, address=address)
    for original, owned in (("__wrap_esp_startup_start_app", "c3boot_start"),
                             ("knl_main", "c3boot_native_main"),
                             ("knl_lowmem_top", "gp_c3boot_begin"),
                             ("knl_lowmem_limit", "gp_c3boot_end")):
        if not symbols.get(original) or symbols.get(original) != symbols.get(owned):
            raise QualificationError("Invalid fixed startup alias: " + owned)
    return retained


def check_mailbox_symbols(symbols, enabled):
    """Mailbox identities must stay typed, unique and in flash, never heap/RAM."""
    pairs = {"sys_mbox_new": "c3lbox_new", "sys_mbox_free": "c3lbox_free"}
    if not enabled:
        if any(name in symbols for name in pairs.values()):
            raise QualificationError("Mailbox adaptation escaped Wi-Fi services")
        return dict(enabled=False)
    for original, owned in pairs.items():
        address = symbols.get(owned, 0)
        if (not 0x42000000 <= address < 0x43000000
                or symbols.get("__wrap_" + original) != address
                or original in symbols):
            raise QualificationError("Invalid fixed mailbox alias: " + original)
    for name in ("c3nq_bind", "c3nq_release"):
        if not 0x42000000 <= symbols.get(name, 0) < 0x43000000:
            raise QualificationError("Mailbox helper missing or consumes RAM code: " + name)
    identities = [symbols.get("g_mailbox" + str(index), 0) for index in range(8)]
    if (len(set(identities)) != 8
            or any(not 0x3c000000 <= address < 0x3d000000 for address in identities)):
        raise QualificationError("Mailbox identities must be distinct flash objects")
    return dict(enabled=True, aliases=pairs, identities=identities,
                storage="Existing eight shared fixed queue payloads; no added mutable storage")


def verify_queue_adapter(work, enabled, wifi_services=False):
    commands = json.loads((work / "build/compile_commands.json").read_text(encoding="utf-8"))
    owned = [entry for entry in commands if Path(entry["file"]).name == "c3nq.c"]
    if len(owned) != 1:
        raise QualificationError("Missing owned queue compiler input")
    values = re.findall(r'(?:^|\s)-D\s*"?UTHOME_STATIC_NET_QUEUES=([01])"?', owned[0]["command"])
    if values != [str(int(enabled))]:
        raise QualificationError("Owned queue selection differs from requested mode")
    vendor = [entry for entry in commands if Path(entry["file"]).name == "shim_queue.c"]
    if len(vendor) != (0 if enabled else 1):
        raise QualificationError("Unexpected vendor queue implementation selection")
    for name in ("shim_evtgrp.c", "mtk_startup.c"):
        if sum(Path(entry["file"]).name == name for entry in commands) != (0 if enabled else 1):
            raise QualificationError("Unexpected fixed network startup/event input: " + name)
    if not enabled:
        return dict(enabled=False, status="compiled_but_not_selected")
    port = [entry for entry in commands if Path(entry["file"]).name == "shim_port.c"]
    if len(port) != 1 or "__wrap_xTaskGetSchedulerState=c3nq_legacy_scheduler" not in port[0]["command"]:
        raise QualificationError("Pinned scheduler-state definition was not isolated")
    if "__real_knl_init_object=c3nq_bootstrap" not in port[0]["command"]:
        raise QualificationError("Owned early startup boundary is absent")
    aliases = queue_aliases(work / "project/main/netqueue.cmake")
    output = run(["riscv32-esp-elf-nm", "-n", work / "build/uthome_runtime_port.elf"])
    symbols = {name: int(address, 16) for address, _, name in
               re.findall(r"^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$", output, re.MULTILINE)}
    retained = check_queue_symbols(symbols, aliases)
    mailboxes = check_mailbox_symbols(symbols, wifi_services)
    events = check_event_symbols(symbols, work / "project/main/netqueue.cmake")
    for name in ("netevent_create", "netevent_begin", "netevent_finish", "netwait_init", "netwait_remaining",
                 "c3nq_init", "c3nq_bootstrap", "boot_mutex"):
        if not 0x42000000 <= symbols.get(name, 0) < 0x43000000:
            raise QualificationError("Cache-guarded helper unexpectedly consumes RAM code: " + name)
    if ("knl_init_object" in symbols or not symbols.get("c3nq_native_tables")
            or not symbols.get("c3nq_bootstrap")
            or symbols["c3nq_native_tables"] == symbols.get("__wrap_knl_init_object")
            or symbols["c3nq_native_tables"] == symbols["c3nq_bootstrap"]):
        raise QualificationError("Invalid native table initialization boundary")
    for entry in commands:
        definition = "-Dknl_init_object=c3nq_native_tables" in entry["command"]
        if definition != (Path(entry["file"]).name == "tkinit.c"):
            raise QualificationError("Native initialization rename escaped its source")
    return dict(enabled=True, status="linked_abi_verified", aliases=retained, event_aliases=events,
                mailboxes=mailboxes,
                resources=dict(queue_slots=8, queue_bytes_by_slot=[512, 512, 1024, 1024, 1024, 1024, 1024, 2048],
                               queue_payload_bytes=8192, semaphore_slots=16, mutex_slots=32,
                               startup_native_semaphores=48,startup_native_mutexes=32,
                               event_groups=8,event_waiters=16,native_startup_pool_bytes=12288),
                qualification="Experimental queue/event/startup scope; cache-off I/O rejects, radio/task/timer paths remain unqualified")


def verify_compile_profiles(work, startup_trace=False):
    """Verify every snapshotted owned C file's generated compiler command."""
    database = work / "build/compile_commands.json"
    entries = json.loads(database.read_text(encoding="utf-8"))
    commands = {Path(entry["file"]).resolve(): entry["command"] for entry in entries}
    policy = json.loads((ROOT / "tools/c_standard/policy.json").read_text(encoding="utf-8"))
    native_uart = verify_uart_fault_hook(commands, work / "project")
    profiles = {}
    for source, destination in RUNTIME_SOURCE_FILES.items():
        if not source.endswith(".c") or "/third_party/" in source:
            continue
        expected = "c11" if source in policy["atomic_profile_paths"] else "c99"
        # Pinned SDK LL headers require GNU typeof; owned adapter syntax is C99.
        if source in {"firmware/hal/ports/esp32c3/c3ihw.c",
                      "firmware/hal/ports/esp32c3/c3pin.c",
                      "firmware/hal/ports/esp32c3/c3shw.c",
                      "firmware/hal/ports/esp32c3/c3pulse.c",
                      "firmware/hal/ports/esp32c3/c3pwm.c"}:
            expected = "gnu99"
        command = commands.get((work / "project" / destination).resolve())
        if destination == "main/uartcheck.c" and not native_uart:
            continue
        if destination == "hal/c3start.c" and not startup_trace:
            continue
        if command is None:
            raise QualificationError(f"Missing compiler command for {source}")
        check_compiler_flags(command, expected)
        profiles[source] = expected
    for path, command in commands.items():
        adapted = "-Dasm=__asm__" in command
        if adapted != (path in {(work / "project/hal" / name).resolve() for name in ("c3wifi.c", "c3ble.c", "c3gatt.c", "c3wsta.c", "c3https.c", "c3gsvc.c", "c3mqtt.c", "c3lbox.c")}):
            raise QualificationError("SDK assembly spelling adaptation escaped its adapter")
    for name in ("c3wifi.c", "c3ble.c", "c3gatt.c", "c3wsta.c", "c3https.c", "c3gsvc.c", "c3mqtt.c", "c3lbox.c"):
        owned_radio = (work / "project/hal" / name).read_text()
        if re.search(r"\b(?:asm|__asm__|typeof|__typeof__)\b", owned_radio):
            raise QualificationError("Owned radio adapter must remain C99: " + name)
    radio_includes = verify_radio_include_isolation(commands, work)
    storage = verify_uart_storage(commands, work / "source")
    flash_guard = verify_flash_guard(commands)
    audit = verify_audit_scope(commands)
    startup = verify_startup_scope(commands, work / "project", startup_trace)
    return {"status": "passed", "profiles": profiles, "uart_storage": storage,
            "flash_guard": flash_guard, "memory_audit": audit,
            "radio_include_isolation": radio_includes, "startup_trace": startup,
            "compile_commands_sha256": sha256(database)}


def verify_radio_include_isolation(commands, work):
    """Keep the native queue namespace out of every BLE-facing adapter."""
    native = (work / "source/include").as_posix()
    isolated = []
    for name in ("c3ble.c", "c3gatt.c", "c3gsvc.c"):
        path = (work / "project/hal" / name).resolve()
        command = commands.get(path)
        if command is None:
            raise QualificationError("Missing BLE-facing compiler command: " + name)
        if native in command.replace("\\", "/"):
            raise QualificationError(
                "Native sys/queue.h namespace leaked into NimBLE component")
        isolated.append(name)
    return {"native_include": native, "isolated_sources": isolated}


def run(arguments, cwd=None):
    result = subprocess.run([str(arg) for arg in arguments], cwd=cwd,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise QualificationError(f"Command failed ({result.returncode}): {arguments[0]}\n"
                                 + result.stdout + result.stderr)
    return result.stdout.strip()


def check_c_standard():
    """Run the mandatory repository gate before creating a build workspace."""
    command = [sys.executable, str(ROOT / "tools/c_standard/check.py"),
               "--root", str(ROOT)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        raise QualificationError("C coding-standard gate failed before build:\n"
                                 + result.stdout + result.stderr)
    return {"status": "passed", "command": command, "output": result.stdout.strip()}


def pinned_checkout(path, expected):
    revision = run(["git", "-C", path, "rev-parse", "HEAD"])
    if revision != expected:
        raise QualificationError(f"Unexpected revision at {path}: {revision}; expected {expected}")
    dirty = run(["git", "-C", path, "status", "--porcelain", "--untracked-files=normal"])
    if dirty:
        raise QualificationError(f"Checkout must be pristine: {path}\n{dirty}")
    return revision


def no_spaces(path):
    if any(char.isspace() for char in str(path)):
        raise QualificationError(f"ESP-IDF build and SDK paths must contain no whitespace: {path}")


def inspect(kernel, sdk):
    no_spaces(sdk)
    compiler = shutil.which("riscv32-esp-elf-gcc")
    if not compiler:
        raise QualificationError("Activate ESP-IDF first: riscv32-esp-elf-gcc is missing from PATH")
    if not (sdk / "tools/idf.py").is_file():
        raise QualificationError(f"ESP-IDF is missing at {sdk}")
    versions = {"kernel_revision": pinned_checkout(kernel, KERNEL_REVISION),
                "sdk_revision": pinned_checkout(sdk, SDK_REVISION)}
    submodules = run(["git", "-C", sdk, "submodule", "status", "--recursive"])
    if any(line and line[0] in "-+U" for line in submodules.splitlines()):
        raise QualificationError("SDK submodules are missing or differ from their pinned revisions")
    compiler_version = run([compiler, "--version"]).splitlines()[0]
    if COMPILER_PACKAGE not in compiler_version:
        raise QualificationError(f"Expected compiler package {COMPILER_PACKAGE}, found {compiler_version}")
    if run([compiler, "-dumpmachine"]) != "riscv32-esp-elf":
        raise QualificationError("Compiler does not target riscv32-esp-elf")
    run([sys.executable, sdk / "tools/idf_tools.py", "--idf-path", sdk, "check"])
    versions.update(compiler=compiler_version,
                    idf_version=run([sys.executable, sdk / "tools/idf.py", "--version"]),
                    python=sys.version.split()[0],
                    cmake=run(["cmake", "--version"]).splitlines()[0],
                    ninja=run(["ninja", "--version"]))
    return versions


def apply_patch(patch, source):
    """Check, then apply, one staged patch identically on every host.

    The pinned kernel stores config/config_device.h with CRLF endings, while the
    reviewed patches are LF. Windows Git (core.autocrlf=true) matched them, which
    is how the recorded builds succeeded; Linux CI (unset) rejected the same
    bytes. Pin the setting so every host reproduces the recorded behaviour.
    """
    command = ["git", "-c", "core.autocrlf=true", "apply"]
    run(command + ["--check", patch], cwd=source)
    run(command + [patch], cwd=source)


def unpack_source(archive_path, destination):
    """Reject escapes and symlinks before extracting a Git source archive."""
    with zipfile.ZipFile(archive_path) as archive:
        for entry in archive.infolist():
            name = PurePosixPath(entry.filename)
            if name.is_absolute() or ".." in name.parts or "\\" in entry.filename or ":" in entry.filename:
                raise QualificationError(f"Unsafe source archive entry: {entry.filename}")
            if stat.S_ISLNK(entry.external_attr >> 16):
                raise QualificationError(f"Unexpected source symlink: {entry.filename}")
        archive.extractall(destination)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_environment(sdk):
    environment = os.environ.copy()
    # Upstream CMake interpolates this environment path into CMake strings.
    # Backslashes such as \U are escapes there, so normalize just this process.
    environment["IDF_PATH"] = sdk.as_posix()
    environment["PYTHONUNBUFFERED"] = "1"
    environment["PYTHONUTF8"] = "1"
    return environment


def verify_runtime_snapshot(project, root=None):
    """Reject changed originals or copied build inputs before target compilation."""
    root = ROOT if root is None else root
    project = project.resolve()
    root = root.resolve()
    manifest_path = project / "source-manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise QualificationError(f"Missing or invalid source manifest: {manifest_path}") from exc
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise QualificationError("Unsupported source manifest schema; create a fresh qualification build")
    sources = manifest.get("sources")
    if not isinstance(sources, dict) or set(sources) != set(RUNTIME_SOURCE_FILES):
        raise QualificationError("Source manifest does not contain the exact required project inputs")
    for source, destination in RUNTIME_SOURCE_FILES.items():
        entry = sources[source]
        if (not isinstance(entry, dict) or entry.get("staged_path") != destination
                or not isinstance(entry.get("sha256"), str)
                or len(entry["sha256"]) != 64
                or any(char not in "0123456789abcdef" for char in entry["sha256"])):
            raise QualificationError(f"Invalid source manifest entry: {source}")
        for label, base, relative in (("original", root, source),
                                      ("staged", project, destination)):
            path = base / relative
            if not path.resolve().is_relative_to(base) or not path.is_file():
                raise QualificationError(f"Missing or escaped {label} project input: {path}")
            if sha256(path) != entry["sha256"]:
                raise QualificationError(
                    f"Changed {label} project input: {path}; create a fresh qualification build")
    if "network_setup" in manifest:
        try:
            network_setup_module().verify(project, manifest["network_setup"])
        except (ValueError, OSError) as error:
            raise QualificationError("Private network setup changed; use a fresh build") from error
    return manifest_path


def network_setup_module():
    """Load the snapshotted setup generator without modifying import paths."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'uthome_network_setup', ROOT / 'tools/target/network_setup.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module



def stage_runtime_port(work):
    """Snapshot project-owned adapter/tests beside the isolated kernel source."""
    project = work / "project"
    sources = {}
    for source, destination in RUNTIME_SOURCE_FILES.items():
        target = project / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((ROOT / source).read_bytes())
        sources[source] = {"staged_path": f"project/{destination}", "sha256": sha256(target)}
    manifest = {"schema_version": 1, "sources": {
        source: {"staged_path": destination, "sha256": sources[source]["sha256"]}
        for source, destination in RUNTIME_SOURCE_FILES.items()}}
    (project / "source-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    defaults = project / "sdkconfig.defaults"
    defaults.write_bytes((work / "source/examples/tasks/sdkconfig.defaults").read_bytes())
    sources["firmware/third_party/esp32c3-mtk3/examples/tasks/sdkconfig.defaults"] = {
        "staged_path": "project/sdkconfig.defaults", "sha256": sha256(work / "source/examples/tasks/sdkconfig.defaults")}
    return project, sources


def verify_senlux_app_probe(work, enabled):
    """Check the binder's actual compiler mode, including normal builds."""
    entries = json.loads((work / "build/compile_commands.json").read_text(encoding="utf-8"))
    binder = (work / "project/config/cfgbind.c").resolve()
    commands = [entry["command"] for entry in entries if Path(entry["file"]).resolve() == binder]
    if len(commands) != 1:
        raise QualificationError("Missing or ambiguous SEN0390 application binder")
    modes = re.findall(r'(?:^|\s)-D\s*"?UTHOME_SENLUX_APP_PROBE=([01])"?', commands[0])
    if modes != [str(int(enabled))]:
        raise QualificationError("SEN0390 application capability differs from requested mode")
    return {"enabled": enabled, "diagnostic_only": enabled}




def verify_static_task_source(source, enabled):
    """Verify the staged vendor adapter has the bounded project contract."""
    text = source.read_text(encoding="utf-8")
    if not enabled:
        return {"enabled": False}
    component = source.with_name("CMakeLists.txt").read_text(encoding="utf-8")
    registration = re.search(r"idf_component_register\((.*?)\)\s*", component, re.DOTALL)
    if registration is None or not re.search(
            r"\bPRIV_REQUIRES\b[^)]*\buthome_task\b", registration[1]):
        raise QualificationError(
            "Patched task shim does not privately require uthome_task")
    if re.search(r"\b(?:main|uthome_radio)\b", registration[1]):
        raise QualificationError(
            "Patched task shim creates a circular application dependency")
    required = {
        "fixed stack acquisition": "c3task_stack_acquire",
        "post-deletion release": "c3task_stack_release",
        "surviving reaper binding": "c3task_reaper_bind",
        "bounded cleanup step": "shim_task_reap_one",
        "generation-aware handles": "s_generations",
        "caller-owned kernel stack": "TA_USERBUF",
        "startup allocation seal": "c3task_stack_commit",
        "notification pending state": "SHIM_FLAG_NOTIFY_PENDING",
        "no-overwrite notification": "eSetValueWithoutOverwrite",
        "notification state clear": "__wrap_xTaskGenericNotifyStateClear",
        "notification value clear": "__wrap_ulTaskGenericNotifyValueClear",
    }
    missing = [description for description, token in required.items()
               if token not in text]
    if missing:
        raise QualificationError("Static task patch is incomplete: " +
                                 ", ".join(missing))
    if re.search(r"\bheap_caps_(?:malloc|free)\s*\(", text):
        raise QualificationError("Patched task shim still allocates task stacks from the heap")
    if not re.search(r"#define\s+SHIM_TASK_MAX\s+4\b", text):
        raise QualificationError("Patched task shim capacity differs from reviewed bound")
    return {
        "enabled": True,
        "source": "components/freertos_shim/shim_task.c",
        "source_sha256": sha256(source),
        "component_dependency": "uthome_task",
        "shim_task_capacity": 4,
        "dynamic_task_stack_calls": 0,
    }


def verify_static_task_project(project):
    """Verify the staged component graph owns c3task without dependency cycles."""
    task = (project / "components/uthome_task/CMakeLists.txt").read_text(
        encoding="utf-8")
    main = (project / "main/CMakeLists.txt").read_text(encoding="utf-8")
    radio = (project / "components/uthome_radio/CMakeLists.txt").read_text(
        encoding="utf-8")
    registration = re.search(r"idf_component_register\((.*?)\)\s*", task, re.DOTALL)
    if registration is None or "../../hal/c3task.c" not in registration[1] or not re.search(
            r"\bPRIV_REQUIRES\s+mtkernel\b", registration[1]):
        raise QualificationError("uthome_task does not own c3task over mtkernel")
    if re.search(r"\b(?:freertos_shim|main|uthome_radio)\b", registration[1]):
        raise QualificationError("uthome_task has an application or shim dependency")
    for name, content in (("main", main), ("uthome_radio", radio)):
        if not re.search(r"\bPRIV_REQUIRES\s+uthome_task\b", content):
            raise QualificationError(f"{name} does not privately require uthome_task")
    if re.search(r"^[^#\n]*\.\./hal/c3task\.c", main, re.MULTILINE):
        raise QualificationError("main still compiles the shared c3task implementation")
    return {"component": "uthome_task", "private_requires": ["mtkernel"],
            "consumers": ["freertos_shim", "main", "uthome_radio"],
            "acyclic": True}


def verify_static_task_link(work, enabled, observations=False):
    """Verify the final radio ELF retained its selected static-task boundary."""
    if not enabled:
        return {"enabled": False}
    output = run(["riscv32-esp-elf-nm", "-n",
                  work / "build/uthome_runtime_port.elf"])
    symbols = {name for _, _, name in re.findall(
        r"^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$", output, re.MULTILINE)}
    required = {"c3task_init", "c3task_stack_acquire", "c3task_stack_release",
                "c3task_reaper_bind", "c3task_reaper_task",
                "c3task_stack_commit", "c3task_seal",
                "g_reaper_stack", "shim_task_reap_one"}
    if observations:
        required.add("c3task_resource_snapshot")
    missing = sorted(required - symbols)
    if missing:
        raise QualificationError("Static task symbols missing from ELF: " +
                                 ", ".join(missing))
    return {"enabled": True, "observation_snapshot": observations,
            "linked_symbols": sorted(required)}


def static_task_selection(selection, observer_patch):
    """Resolve the reviewed single-radio static task-pool configuration."""
    probe = selection.mode in (BuildMode.WIFI, BuildMode.BLE)
    service = selection.network_services in ("wifi", "ble")
    if probe and observer_patch is None:
        raise QualificationError("Radio probes require the reviewed static-task patch")
    if observer_patch is not None and not (probe or service):
        if selection.network_services == "both":
            raise QualificationError(
                "Combined Wi-Fi/BLE services exceed the reviewed single-radio static task pool")
        raise QualificationError(
            "The static-task patch requires a Wi-Fi/BLE probe or one real radio service")
    return observer_patch is not None


def build(kernel, sdk, work, *legacy_args, selection=None, compat_patch=None,
          runtime_port=False, resource_patch=None, configuration_app=False,
          upstream_example=None, native_uart_fixture=False, static_net_queues=False,
          wifi_probe=False, ble_probe=False, model_probe=False, ble_gatt_probe=False,
          ble_static_host=False, observer_patch=None, network_settings=None, **legacy_flags):
    """Build one resolved mode while keeping the legacy positional CLI API working."""
    if legacy_args:
        if len(legacy_args) != 13:
            raise TypeError("legacy build() positional arguments are fixed")
        compat_patch, runtime_port, resource_patch, configuration_app, upstream_example, \
            native_uart_fixture, static_net_queues, wifi_probe, ble_probe, model_probe, \
            ble_gatt_probe, ble_static_host, observer_patch = legacy_args
    if model_probe or legacy_flags.get("model_load_probe", False):
        raise QualificationError("Historical model probes were removed")
    if selection is None:
        selection = resolve_mode(
            runtime_port=runtime_port, configuration_app=configuration_app,
            upstream_example=upstream_example, native_uart_fixture=native_uart_fixture,
            static_net_queues=static_net_queues, wifi_probe=wifi_probe,
            ble_probe=ble_probe,
            ble_gatt_probe=ble_gatt_probe, ble_static_host=ble_static_host,
            network_services=legacy_flags.get('network_services'))
    static_tasks = static_task_selection(selection, observer_patch)
    if selection.network_services in ('wifi', 'both') and network_settings is None:
        raise QualificationError('Wi-Fi services require --network-settings (private local JSON)')
    if network_settings is not None and selection.network_services not in ('wifi', 'both'):
        raise QualificationError('Network settings require wifi/both service mode')
    if selection.network_services is not None and selection.mode is not BuildMode.CONFIGURATION:
        raise QualificationError('Network services require application + static queues, without diagnostics')
    example = selection.upstream_example

    no_spaces(work)
    if kernel == work or kernel in work.parents or sdk == work or sdk in work.parents:
        raise QualificationError("Build output must be outside the kernel and SDK checkouts")
    # Never clean, overwrite or reuse an existing build directory.
    if work.exists():
        raise QualificationError(f"Use a new work directory; destination already exists: {work}")
    standard = check_c_standard()
    versions = inspect(kernel, sdk)
    work.mkdir(parents=True, exist_ok=False)
    evidence = {"schema_version": "1.0.0", "created_utc": datetime.now(timezone.utc).isoformat(),
                **selection.evidence(),
                "board_kit": "iote_esp32c3_mini", "target": "esp32c3",
                "versions": versions, "build_status": "in_progress", "board_status": "not_tested",
                "application_runtime_status": "not_tested", "artifacts": {},
                "coding_standard": standard, "static_task_pool": static_tasks}
    report = work / "evidence.json"
    report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    try:
        archive = work / "kernel-source.zip"
        run(["git", "-C", kernel, "archive", "--format=zip", f"--output={archive}", KERNEL_REVISION])
        unpack_source(archive, work / "source")
        evidence["kernel_archive_sha256"] = sha256(archive)
        evidence["compatibility_patch"] = None
        if compat_patch is not None:
            staged_patch = work / "kernel-compatibility.patch"
            staged_patch.write_bytes(compat_patch.read_bytes())
            evidence["compatibility_patch"] = {"name": compat_patch.name, "sha256": sha256(staged_patch)}
            apply_patch(staged_patch, work / "source")
        evidence["resource_patch"] = None
        if resource_patch is not None:
            staged_patch = work / "kernel-resources.patch"
            staged_patch.write_bytes(resource_patch.read_bytes())
            evidence["resource_patch"] = {"name": resource_patch.name, "sha256": sha256(staged_patch)}
            apply_patch(staged_patch, work / "source")
        evidence["observer_patch"] = None
        if observer_patch is not None:
            staged_patch = work / "kernel-observer.patch"
            staged_patch.write_bytes(observer_patch.read_bytes())
            evidence["observer_patch"] = {"name": observer_patch.name, "sha256": sha256(staged_patch)}
            apply_patch(staged_patch, work / "source")
        evidence["static_task_source"] = verify_static_task_source(
            work / "source/components/freertos_shim/shim_task.c",
            static_tasks)
        project = work / "source/examples" / example
        image_name = UPSTREAM_EXAMPLES[example]
        if selection.runtime_port:
            project, evidence["project_sources"] = stage_runtime_port(work)
            evidence["static_task_components"] = verify_static_task_project(project)
            if network_settings is not None:
                try:
                    network_setup_module().stage(project, network_settings)
                except (ValueError, OSError) as error:
                    raise QualificationError("Invalid private network setup; inspect local settings") from error
            manifest = verify_runtime_snapshot(project)
            evidence["source_manifest_sha256"] = sha256(manifest)
            image_name = "uthome_runtime_port"
        command = [sys.executable, str(sdk / "tools/idf.py"), "-C", str(project), "-B", str(work / "build"),
                   "-DIDF_TARGET=esp32c3", "-DMTK_BOARD=iote_esp32c3_mini",
                   f"-DSDKCONFIG={work / 'sdkconfig'}", "build"]
        if selection.runtime_port:
            # Every direct rebuild also checks the source repository. Paths are
            # separate arguments, including the source checkout with spaces.
            command[-1:-1] = selection.cmake_settings() + [
                f"-DUTHOME_STATIC_TASKS={int(static_tasks)}",
                f"-DUTHOME_SOURCE_ROOT={ROOT.as_posix()}",
                f"-DUTHOME_CHECK_PYTHON={Path(sys.executable).as_posix()}"]
        evidence["command"] = command
        environment = build_environment(sdk)
        evidence["environment_overrides"] = {name: environment[name] for name in
                                              ("IDF_PATH", "PYTHONUNBUFFERED", "PYTHONUTF8")}
        with (work / "build.log").open("w", encoding="utf-8") as log:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, encoding="utf-8", errors="replace", env=environment)
            for line in process.stdout:
                log.write(line)
                log.flush()
                print(line, end="", flush=True)
            result = process.wait()
        evidence["build_exit_code"] = result
        if result:
            raise QualificationError(f"Kernel build failed ({result}); inspect {work / 'build.log'}")
        if selection.runtime_port:
            evidence["compiler_profiles"] = verify_compile_profiles(work, selection.mode is BuildMode.WIFI)
            evidence["senlux_app_isolation"] = verify_senlux_app_probe(work, selection.mode is BuildMode.SENLUX_APP)
            evidence["memory_audit_link"] = verify_audit_link(work)
            evidence["startup_trace_link"] = verify_startup_link(work, selection.mode is BuildMode.WIFI)
            evidence["network_queue_adapter"] = verify_queue_adapter(
                work, selection.static_net_queues, selection.network_services in ("wifi", "both"))
            evidence["static_task_link"] = verify_static_task_link(
                work, static_tasks,
                selection.mode in (BuildMode.WIFI, BuildMode.BLE))
            if selection.mode is BuildMode.WIFI:
                config_lines = set((work / "sdkconfig").read_text().splitlines())
                required = {"CONFIG_FREERTOS_HZ=1000", "# CONFIG_BT_ENABLED is not set",
                            "# CONFIG_ESP_WIFI_NVS_ENABLED is not set",
                            "# CONFIG_ESP_WIFI_IRAM_OPT is not set",
                            "# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set",
                            "# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set"}
                if not required <= config_lines:
                    raise QualificationError("Wi-Fi probe storage/tick isolation settings differ")
                evidence["radio_scope"] = "Noncompliant diagnostic only: scan/stop/restart; no association/AP/IP/NVS; USB application state retained but not running"
            if selection.mode is BuildMode.BLE:
                evidence["ble_isolation"] = verify_ble_probe(work, selection.ble_gatt_probe, selection.ble_static_host)
                evidence["radio_scope"] = ("Noncompliant diagnostic only: read-only GATT reconnect/read window; no configuration writes, pairing, NVS or Wi-Fi; USB core inactive; SDK heap remains unqualified" if selection.ble_gatt_probe else "Noncompliant diagnostic only: nonconnectable advertising/stop/restart; no GATT, pairing, NVS or Wi-Fi; SDK flash controller without legacy ROM patch; USB core inactive; flash writes during BLE are unqualified")
            if selection.configuration_app:
                verify_configuration_layout(work)
            evidence["managed_cjson"] = verify_managed_cjson(project)
        # A successful process with incomplete artifacts is not build evidence.
        for relative in (f"build/{image_name}.bin", f"build/{image_name}.elf",
                         f"build/{image_name}.map", "build/compile_commands.json",
                         "build/bootloader/bootloader.bin", "build/partition_table/partition-table.bin",
                         "build/flasher_args.json", "sdkconfig", "build.log"):
            artifact = work / relative
            if not artifact.is_file() or not artifact.stat().st_size:
                raise QualificationError(f"Missing build artifact: {relative}")
            evidence["artifacts"][relative] = {"sha256": sha256(artifact), "size_bytes": artifact.stat().st_size}

        if selection.runtime_port:
            for relative in ("project/dependencies.lock", "project/sdkconfig.defaults"):
                artifact = work / relative
                evidence["artifacts"][relative] = {
                    "sha256": sha256(artifact), "size_bytes": artifact.stat().st_size}
        evidence["build_status"] = "passed"
    except (Exception, KeyboardInterrupt) as exc:
        evidence["build_status"] = "failed"
        evidence["error"] = str(exc) or "Interrupted"
        raise
    finally:
        report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("inspect", "build", "verify-snapshot"))
    parser.add_argument("--kernel", type=Path, default=ROOT / "firmware/third_party/esp32c3-mtk3")
    parser.add_argument("--sdk", type=Path, default=Path(os.environ["IDF_PATH"]) if "IDF_PATH" in os.environ else None)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--project-dir", type=Path, help="staged project for verify-snapshot")
    parser.add_argument("--compat-patch", type=Path, help="explicit reviewed build compatibility patch; recorded by hash")
    parser.add_argument("--resource-patch", type=Path, help="explicit reviewed kernel resource patch; isolated and hashed")
    parser.add_argument("--observer-patch", type=Path, help="explicit reviewed task-observer patch; isolated and hashed")
    parser.add_argument("--runtime-port", action="store_true",
                        help="build the project task-port acceptance test instead of the upstream example")
    parser.add_argument("--configuration-app", action="store_true",
                        help="run USB configuration application; requires --runtime-port")
    parser.add_argument("--native-uart-fixture", action="store_true",
                        help="isolated UART fault fixture; requires runtime-port and disconnected UART peripherals")
    parser.add_argument("--static-net-queues", action="store_true",
                        help="experimental owned queue adapter; requires runtime-port, does not enable radios")
    parser.add_argument("--network-services", choices=("ble", "wifi", "both"),
                        help="experimental real app services, requires configuration-app + static-net-queues")
    parser.add_argument("--network-settings", type=Path,
                        help="private local Wi-Fi/TLS setup JSON; never put credentials in command arguments")
    parser.add_argument("--wifi-probe", action="store_true",
                        help="isolated scan-only Wi-Fi diagnostic; requires static-net-queues, never joins a network")
    parser.add_argument("--ble-probe", action="store_true",
                        help="isolated nonconnectable BLE advertising diagnostic; requires static-net-queues")
    parser.add_argument("--ble-gatt-probe", action="store_true",
                        help="enable a bounded read-only GATT diagnostic; requires --ble-probe")
    parser.add_argument("--ble-static-host", action="store_true",
                        help="disable NimBLE dynamic global-state conversion in an isolated BLE diagnostic; not full static compliance")
    parser.add_argument("--senlux-probe", action="store_true",
                        help="isolated SEN0390 I2C/enable diagnostic; requires static-net-queues")
    parser.add_argument("--senlux-app-probe", action="store_true",
                        help="diagnostic application with SEN0390 enabled; requires configuration-app and static-net-queues")
    parser.add_argument("--upstream-example", choices=tuple(UPSTREAM_EXAMPLES),
                        help="build a pinned diagnostic example; excludes project runtime/application modes")
    args = parser.parse_args(argv)
    if args.action == "verify-snapshot":
        if args.project_dir is None:
            parser.error("verify-snapshot requires --project-dir")
        try:
            verify_runtime_snapshot(args.project_dir)
        except (QualificationError, OSError) as exc:
            print(str(exc), file=sys.stderr)
            return 1
        print("Project snapshot and original inputs match the qualification manifest.")
        return 0
    if args.sdk is None:
        parser.error("activate ESP-IDF or supply --sdk")
    if args.action == "build" and args.work_dir is None:
        parser.error("build requires --work-dir (a new path without whitespace)")
    try:
        kernel, sdk = args.kernel.resolve(), args.sdk.resolve()
        if args.action == "inspect":
            print(json.dumps(inspect(kernel, sdk), indent=2))
        else:
            selection = resolve_mode(
                runtime_port=args.runtime_port, configuration_app=args.configuration_app,
                upstream_example=args.upstream_example, native_uart_fixture=args.native_uart_fixture,
                static_net_queues=args.static_net_queues, wifi_probe=args.wifi_probe,
                ble_probe=args.ble_probe,
                ble_gatt_probe=args.ble_gatt_probe, ble_static_host=args.ble_static_host,
                senlux_probe=args.senlux_probe,
                senlux_app_probe=args.senlux_app_probe, network_services=args.network_services)
            report = build(kernel, sdk, args.work_dir.resolve(), selection=selection,
                           compat_patch=args.compat_patch.resolve() if args.compat_patch else None,
                           resource_patch=args.resource_patch.resolve() if args.resource_patch else None,
                           network_settings=args.network_settings.resolve() if args.network_settings else None,
                           observer_patch=args.observer_patch.resolve() if args.observer_patch else None)
            print(f"Build evidence: {report}\nPhysical board and application runtime remain untested.")
    except (QualificationError, OSError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
