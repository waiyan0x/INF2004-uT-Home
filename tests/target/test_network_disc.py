"""Compile the portable Home Assistant discovery formatter and check exact bytes."""
import ctypes as c
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NODE = 'uthome-a4cf12345678'

# Builds copied cache entries in C so the tests never mirror native layouts.
FIXTURE = r"""
#include "mqttdisc.h"
#include <string.h>
static sampcache_entry_t g_entry;
static mqttdisc_message_t g_message;
void fx_entry(uint16_t slot,uint16_t type,const char *name,size_t name_size,
              int seen,uint16_t status,uint64_t stamp,uint32_t stale)
{ memset(&g_entry,0,sizeof(g_entry));
  memcpy(g_entry.source.name,name,name_size>32U?32U:name_size);
  g_entry.source.type=type;g_entry.source.source_index=slot;g_entry.source.stale_ms=stale;
  g_entry.b_expected=true;g_entry.b_seen=seen!=0;g_entry.header.status=status;
  g_entry.header.timestamp_ms=stamp;g_entry.header.source_index=slot; }
void fx_unexpected(void) { g_entry.b_expected=false; }
void fx_environment(float temperature,float pressure)
{ g_entry.value.environment.temperature_c=temperature;g_entry.value.environment.pressure_pa=pressure; }
void fx_distance(uint32_t millimetres) { g_entry.value.distance.distance_mm=millimetres; }
void fx_light(uint32_t millilux) { g_entry.value.light.millilux=millilux; }
void fx_targets(unsigned int mask)
{ for(unsigned int i=0;i<UTHOME_SAMPLE_MAX_TARGETS;++i) { g_entry.value.targets.targets[i].b_present=(mask>>i)&1U; } }
void fx_raw(const uint8_t *bytes,uint32_t length)
{ g_entry.value.raw.length=length;memcpy(g_entry.value.raw.bytes,bytes,length>16U?16U:length); }
int fx_config(uint32_t step,const char *node,int absent)
{ memset(&g_message,0xa5,sizeof(g_message));
  return mqttdisc_config(step,node,absent?NULL:&g_entry,&g_message); }
int fx_state(const char *node,uint64_t now)
{ memset(&g_message,0xa5,sizeof(g_message));return mqttdisc_state(node,&g_entry,now,&g_message); }
const char *fx_topic(void) { return g_message.topic; }
size_t fx_topic_size(void) { return g_message.topic_size; }
const char *fx_payload(void) { return g_message.payload; }
size_t fx_payload_size(void) { return g_message.payload_size; }
"""

ENVIRONMENT, TARGETS, DISTANCE, HEALTH, LIGHT, RAW = 1, 2, 3, 4, 5, 6
SUFFIXES = ['temperature', 'pressure', 'distance', 'illuminance', 'occupancy',
            'targets', 'raw']
# Full-key payload as published before compaction: the reference that the
# abbreviated payload must expand back to, field for field.
FULL_DEVICE = (f',"expire_after":30,"availability_topic":"uthome/v1/{NODE}/availability",'
               f'"device":{{"identifiers":["{NODE}"],"name":"{NODE}",'
               '"manufacturer":"uT-Home","model":"ESP32-C3"}}')


def full_config(name, slot, suffix, template, fields):
    return (f'{{"name":"{name} {suffix}","unique_id":"{NODE}_s{slot}_{suffix}",'
            f'"state_topic":"uthome/v1/{NODE}/state/{name}",'
            f'"value_template":"{template}"{fields}{FULL_DEVICE}').encode()


# Abbreviations as listed in Home Assistant's MQTT discovery documentation
# ("Supported abbreviations in MQTT discovery messages" and the device list).
ABBREVIATIONS = {'uniq_id': 'unique_id', 'stat_t': 'state_topic', 'val_tpl': 'value_template',
                 'dev_cla': 'device_class', 'unit_of_meas': 'unit_of_measurement',
                 'stat_cla': 'state_class', 'exp_aft': 'expire_after',
                 'avty_t': 'availability_topic', 'dev': 'device'}
DEVICE_ABBREVIATIONS = {'ids': 'identifiers', 'mf': 'manufacturer', 'mdl': 'model'}


def expand(payload):
    """Expand as Home Assistant documents: abbreviations, then a "~" base topic
    at the start or end of values whose key ends in _topic."""
    config = {ABBREVIATIONS.get(key, key): value for key, value in json.loads(payload).items()}
    config['device'] = {DEVICE_ABBREVIATIONS.get(key, key): value
                        for key, value in config['device'].items()}
    base = config.pop('~')
    for key, value in config.items():
        if key.endswith('_topic') and value.startswith('~'):
            config[key] = base + value[1:]
        elif key.endswith('_topic') and value.endswith('~'):
            config[key] = value[:-1] + base
    return config


def compact_fields(fields):
    for full, short in [('"device_class"', '"dev_cla"'),
                        ('"unit_of_measurement"', '"unit_of_meas"'),
                        ('"state_class"', '"stat_cla"')]:
        fields = fields.replace(full, short)
    return fields


def config(name, slot, suffix, template, fields):
    return (f'{{"~":"uthome/v1/{NODE}","name":"{name} {suffix}",'
            f'"uniq_id":"{NODE}_s{slot}_{suffix}","stat_t":"~/state/{name}",'
            f'"val_tpl":"{template}"{compact_fields(fields)},"exp_aft":30,'
            f'"avty_t":"~/availability","dev":{{"ids":["{NODE}"],"name":"{NODE}",'
            '"mf":"uT-Home","mdl":"ESP32-C3"}}').encode()


EXPECTED = {
    'temperature': ('sensor', '{{ value_json.t_c100 / 100 }}',
                    ',"device_class":"temperature","unit_of_measurement":"\\u00b0C",'
                    '"state_class":"measurement"'),
    'pressure': ('sensor', '{{ value_json.p_pa }}',
                 ',"device_class":"pressure","unit_of_measurement":"Pa",'
                 '"state_class":"measurement"'),
    'distance': ('sensor', '{{ value_json.d_mm }}',
                 ',"device_class":"distance","unit_of_measurement":"mm",'
                 '"state_class":"measurement"'),
    'illuminance': ('sensor', '{{ value_json.lx_m / 1000 }}',
                    ',"device_class":"illuminance","unit_of_measurement":"lx",'
                    '"state_class":"measurement"'),
    'occupancy': ('binary_sensor', "{{ 'ON' if value_json.n > 0 else 'OFF' }}",
                  ',"device_class":"occupancy"'),
    'targets': ('sensor', '{{ value_json.n }}', ',"state_class":"measurement"'),
    'raw': ('sensor', '{{ value_json.hex }}', ''),
}
OWNED = {ENVIRONMENT: ['temperature', 'pressure'], DISTANCE: ['distance'],
         LIGHT: ['illuminance'], TARGETS: ['occupancy', 'targets'], RAW: ['raw'],
         HEALTH: []}


class DiscoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC required')
        cls.folder = tempfile.TemporaryDirectory(prefix='uthome-disc-')
        cls.addClassCleanup(cls.folder.cleanup)
        folder = Path(cls.folder.name)
        (folder / 'fixture.c').write_text(FIXTURE, encoding='utf-8')
        binary = folder / 'disc.dll'
        includes = [ROOT / 'firmware/src/network', ROOT / 'firmware/src/runtime',
                    ROOT / 'firmware/include']
        subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-O2', '-shared', *(['-fPIC'] if sys.platform != 'win32' else []),
                        *['-I' + str(path) for path in includes],
                        str(ROOT / 'firmware/src/network/mqttdisc.c'),
                        str(ROOT / 'firmware/src/runtime/sampcache.c'),
                        str(folder / 'fixture.c'), '-o', str(binary)],
                       check=True, capture_output=True)
        cls.lib = c.CDLL(str(binary))
        lib = cls.lib
        lib.fx_entry.argtypes = [c.c_uint16, c.c_uint16, c.c_char_p, c.c_size_t, c.c_int,
                                 c.c_uint16, c.c_uint64, c.c_uint32]
        lib.fx_environment.argtypes = [c.c_float, c.c_float]
        lib.fx_distance.argtypes = [c.c_uint32]
        lib.fx_light.argtypes = [c.c_uint32]
        lib.fx_targets.argtypes = [c.c_uint]
        lib.fx_raw.argtypes = [c.c_char_p, c.c_uint32]
        lib.fx_config.argtypes = [c.c_uint32, c.c_char_p, c.c_int]
        lib.fx_state.argtypes = [c.c_char_p, c.c_uint64]
        for name in ('fx_topic', 'fx_payload'):
            getattr(lib, name).restype = c.c_void_p
        for name in ('fx_topic_size', 'fx_payload_size'):
            getattr(lib, name).restype = c.c_size_t
        lib.mqttdisc_slot.argtypes = [c.c_uint32]
        lib.mqttdisc_slot.restype = c.c_uint16
        cls.addClassCleanup(cls.unload)

    @classmethod
    def unload(cls):
        import _ctypes
        if sys.platform == 'win32':
            _ctypes.FreeLibrary(cls.lib._handle)

    def entry(self, slot=0, kind=ENVIRONMENT, name=b'env1', seen=1, status=1,
              stamp=1000, stale=5000):
        self.lib.fx_entry(slot, kind, name, len(name) + 1, seen, status, stamp, stale)

    def message(self):
        topic_size = self.lib.fx_topic_size()
        topic = c.string_at(self.lib.fx_topic(), topic_size + 1)
        self.assertEqual(topic[-1:], b'\x00')
        return topic[:-1], c.string_at(self.lib.fx_payload(), self.lib.fx_payload_size())

    def configure(self, step, node=NODE.encode(), absent=0):
        result = self.lib.fx_config(step, node, absent)
        return (result, *self.message()) if 0 == result else (result, None, None)

    def test_exact_config_and_removal_bytes_for_every_sample_type(self):
        for kind, owned in OWNED.items():
            slot = kind  # Exercise several slots, including 6.
            self.entry(slot=slot, kind=kind, name=b'dev%d' % kind)
            for index, suffix in enumerate(SUFFIXES):
                component, template, fields = EXPECTED[suffix]
                result, topic, payload = self.configure(slot * 7 + index)
                self.assertEqual(result, 0)
                self.assertEqual(topic, f'homeassistant/{component}/{NODE}_s{slot}_{suffix}'
                                        '/config'.encode())
                if suffix in owned:
                    self.assertEqual(payload, config(f'dev{kind}', slot, suffix, template,
                                                     fields))
                    self.assertEqual(expand(payload)['availability_topic'],
                                     f'uthome/v1/{NODE}/availability')
                else:
                    self.assertEqual(payload, b'')

    def test_expanded_abbreviations_match_previous_full_payload(self):
        for kind, owned in OWNED.items():
            self.entry(slot=kind, kind=kind, name=b'dev%d' % kind)
            for suffix in owned:
                component, template, fields = EXPECTED[suffix]
                payload = self.configure(kind * 7 + SUFFIXES.index(suffix))[2]
                previous = json.loads(full_config(f'dev{kind}', kind, suffix, template,
                                                  fields))
                self.assertEqual(expand(payload), previous, suffix)
                # Only documented abbreviations, "~" and unabbreviated "name" remain.
                self.assertLessEqual(set(json.loads(payload)) - {'~', 'name'},
                                     set(ABBREVIATIONS))
                self.assertLessEqual(set(json.loads(payload)['dev']) - {'name'},
                                     set(DEVICE_ABBREVIATIONS))

    def test_temperature_config_literal_and_degree_escape(self):
        self.entry()
        self.assertEqual(self.configure(0), (
            0, b'homeassistant/sensor/uthome-a4cf12345678_s0_temperature/config',
            b'{"~":"uthome/v1/uthome-a4cf12345678","name":"env1 temperature",'
            b'"uniq_id":"uthome-a4cf12345678_s0_temperature","stat_t":"~/state/env1",'
            b'"val_tpl":"{{ value_json.t_c100 / 100 }}","dev_cla":"temperature",'
            b'"unit_of_meas":"\\u00b0C","stat_cla":"measurement","exp_aft":30,'
            b'"avty_t":"~/availability","dev":{"ids":["uthome-a4cf12345678"],'
            b'"name":"uthome-a4cf12345678","mf":"uT-Home","mdl":"ESP32-C3"}}'))
        self.assertEqual(json.loads(self.configure(0)[2])['unit_of_meas'], '°C')

    def test_absent_slot_removes_every_suffix_and_steps_are_bounded(self):
        for step in range(56):
            result, topic, payload = self.configure(step, absent=1)
            self.assertEqual((result, payload), (0, b''))
            self.assertIn(f'_s{step // 7}_{SUFFIXES[step % 7]}/config'.encode(), topic)
            self.assertEqual(self.lib.mqttdisc_slot(step), step // 7)
        self.assertEqual(self.configure(56, absent=1)[0], -1)
        self.assertEqual(self.lib.mqttdisc_slot(56), 0xffff)

    def test_maximum_node_and_name_fit_and_longer_inputs_fail(self):
        node = b'n' * 31
        name = b'a' + b'b' * 30
        self.entry(slot=7, name=name)
        sizes = []
        for step in range(49, 56):
            result, topic, payload = self.configure(step, node=node)
            self.assertEqual(result, 0)
            self.assertLessEqual(len(topic), 127)
            sizes.append(len(payload))
        self.entry(slot=7, kind=TARGETS, name=name)
        for step in range(49, 56):
            sizes.append(len(self.configure(step, node=node)[2]))
        self.assertEqual(max(sizes), 490)  # Temperature; 10 bytes below the bound.
        self.assertEqual(sizes[0], 490)
        self.assertEqual(sizes, [490, 468, 0, 0, 0, 0, 0, 0, 0, 0, 0, 446, 422, 0])
        self.assertEqual(self.configure(49, node=b'n' * 32)[0], -1)
        self.lib.fx_entry(7, ENVIRONMENT, b'a' * 32, 32, 1, 1, 1000, 5000)  # No NUL.
        self.assertEqual(self.configure(49, node=node)[0], -1)
        self.assertEqual(self.lib.fx_state(node, 1000), -1)

    def test_worst_case_payload_size_per_entity(self):
        node = b'n' * 31
        name = b'a' + b'b' * 30
        sizes = {}
        for kind, owned in OWNED.items():
            self.entry(slot=7, kind=kind, name=name)
            for suffix in owned:
                sizes[suffix] = len(self.configure(49 + SUFFIXES.index(suffix), node=node)[2])
        self.assertEqual(sizes, dict(temperature=490, pressure=468, distance=468,
                                     illuminance=484, occupancy=446, targets=422, raw=391))

    def test_unsafe_node_and_name_characters_are_rejected(self):
        self.entry()
        for node in [b'', b'a/b', b'a+b', b'a#b', b'a b', b'a.b', b'a"b', b'\xc3\xa9']:
            self.assertEqual(self.configure(0, node=node)[0], -1, node)
            self.assertEqual(self.lib.fx_state(node, 1000), -1, node)
        for name in [b'', b'a/b', b'a b', b'a"b', b'1abc', b'a\\b', b'a+b']:
            self.entry(name=name)
            self.assertEqual(self.configure(0)[0], -1, name)
            self.assertEqual(self.lib.fx_state(NODE.encode(), 1000), -1, name)

    def test_entry_must_match_slot_and_be_expected(self):
        self.entry(slot=1)
        self.assertEqual(self.configure(0)[0], -1)
        self.assertEqual(self.configure(7)[0], 0)
        self.lib.fx_unexpected()
        self.assertEqual(self.configure(7)[0], -1)

    def state(self, now=1000):
        result = self.lib.fx_state(NODE.encode(), now)
        return (result, *self.message()) if 0 == result else (result, None, None)

    def test_exact_integer_state_payloads_for_each_sample_type(self):
        self.entry()
        self.lib.fx_environment(25.12, 101325.4)
        self.assertEqual(self.state(), (0, f'uthome/v1/{NODE}/state/env1'.encode(),
                                        b'{"t_c100":2512,"p_pa":101325}'))
        self.lib.fx_environment(-5.126, 0.0)
        self.assertEqual(self.state()[2], b'{"t_c100":-513,"p_pa":0}')
        self.entry(kind=DISTANCE, name=b'dist')
        self.lib.fx_distance(4294967295)
        self.assertEqual(self.state()[1:], (f'uthome/v1/{NODE}/state/dist'.encode(),
                                            b'{"d_mm":4294967295}'))
        self.entry(kind=LIGHT)
        self.lib.fx_light(523000)
        self.assertEqual(self.state()[2], b'{"lx_m":523000}')
        self.lib.fx_light(0)
        self.assertEqual(self.state()[2], b'{"lx_m":0}')
        self.entry(kind=TARGETS)
        for mask, count in [(0, 0), (4, 1), (7, 3)]:
            self.lib.fx_targets(mask)
            self.assertEqual(self.state()[2], b'{"n":%d}' % count)
        self.entry(kind=RAW)
        self.lib.fx_raw(b'\x0a\x1b\xff', 3)
        self.assertEqual(self.state()[2], b'{"hex":"0a1bff"}')
        self.lib.fx_raw(bytes(range(240, 256)), 16)
        self.assertEqual(self.state()[2], b'{"hex":"' + bytes(range(240, 256)).hex().encode()
                         + b'"}')

    def test_no_state_for_missing_stale_fault_or_unexposed_samples(self):
        cases = [dict(seen=0), dict(status=0), dict(status=2), dict(status=3),
                 dict(status=4), dict(stamp=1001), dict(stamp=0, stale=1000),
                 dict(kind=HEALTH), dict(kind=99)]
        for case in cases:
            self.entry(**case)
            self.lib.fx_environment(20.0, 100000.0)
            self.assertEqual(self.state()[0], 1, case)
        self.entry(stamp=1, stale=1000)
        self.lib.fx_environment(20.0, 100000.0)
        self.assertEqual(self.state()[0], 0)  # Age 999 ms is still fresh.
        self.lib.fx_unexpected()
        self.assertEqual(self.state()[0], 1)

    def test_out_of_range_or_nonfinite_values_publish_nothing(self):
        self.entry()
        for temperature, pressure in [(float('nan'), 1.0), (float('inf'), 1.0),
                                      (1000.01, 1.0), (-1000.01, 1.0), (20.0, -1.0),
                                      (20.0, 2000001.0), (20.0, float('nan'))]:
            self.lib.fx_environment(temperature, pressure)
            self.assertEqual(self.state()[0], 1, (temperature, pressure))
        self.lib.fx_environment(1000.0, 2000000.0)
        self.assertEqual(self.state()[2], b'{"t_c100":100000,"p_pa":2000000}')
        self.entry(kind=LIGHT)
        self.lib.fx_light(200000001)
        self.assertEqual(self.state()[0], 1)
        self.entry(kind=RAW)
        for length in (0, 17):
            self.lib.fx_raw(b'\x01' * 16, length)
            self.assertEqual(self.state()[0], 1, length)


if __name__ == '__main__':
    unittest.main()
