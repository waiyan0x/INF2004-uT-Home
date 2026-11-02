"""Resolve compatible CLI flags into one immutable target build selection."""
from dataclasses import dataclass
from enum import Enum


class QualificationError(RuntimeError):
    """The requested build or its evidence does not satisfy qualification gates."""


UPSTREAM_EXAMPLES = {
    'tasks': 'mtk3c3_tasks',
    'ble_gatt': 'mtk3c3_ble_gatt',
    'udp_pingpong_c3': 'udp_pingpong_c3',
}


class BuildMode(Enum):
    UPSTREAM = 'upstream'
    RUNTIME = 'runtime'
    CONFIGURATION = 'configuration'
    NATIVE_UART = 'native_uart_fixture'
    WIFI = 'wifi_probe'
    BLE = 'ble_probe'
    MODEL = 'model_probe'
    MODEL_LOAD = 'model_load_probe'
    SENLUX = 'senlux_probe'
    SENLUX_APP = 'senlux_app_probe'


@dataclass(frozen=True)
class ModeSpec:
    configuration_app: bool
    requires_static_queues: bool
    prerequisite: str


# Mode prerequisites and their emitted CMake flags share this registry. Table
# order keeps the more specific diagnostic error when incompatible flags coexist.
PROBES = {
    BuildMode.SENLUX_APP: ModeSpec(True, True, 'SEN0390 application probe requires configuration app and static queues only'),
    BuildMode.SENLUX: ModeSpec(False, True, 'SEN0390 probe requires the isolated runtime fixture and static queues'),
    BuildMode.MODEL_LOAD: ModeSpec(True, True, 'Loaded model probe requires the configuration application and static queues only'),
    BuildMode.MODEL: ModeSpec(False, True, 'Model probe requires the isolated runtime fixture and static queues'),
    BuildMode.WIFI: ModeSpec(False, True, 'Wi-Fi probe requires the isolated runtime fixture and static queues'),
    BuildMode.BLE: ModeSpec(False, True, 'BLE probe requires the isolated runtime fixture and static queues'),
    BuildMode.NATIVE_UART: ModeSpec(False, False, 'native UART fixture requires runtime-port without application/example'),
}


@dataclass(frozen=True)
class BuildSelection:
    mode: BuildMode = BuildMode.UPSTREAM
    static_net_queues: bool = False
    ble_gatt_probe: bool = False
    ble_static_host: bool = False
    upstream_example: str = 'tasks'

    def __post_init__(self):
        if not isinstance(self.mode, BuildMode):
            raise QualificationError('Unknown build mode')
        if self.upstream_example not in UPSTREAM_EXAMPLES:
            raise QualificationError('Unsupported upstream diagnostic example')
        if self.runtime_port and self.upstream_example != 'tasks':
            raise QualificationError('upstream example cannot be combined with application/runtime mode')
        if self.ble_gatt_probe and self.mode is not BuildMode.BLE:
            raise QualificationError('GATT probe requires the isolated BLE diagnostic')
        if self.ble_static_host and self.mode is not BuildMode.BLE:
            raise QualificationError('Static host experiment requires the isolated BLE diagnostic')
        if self.static_net_queues and self.mode in (BuildMode.UPSTREAM, BuildMode.NATIVE_UART):
            raise QualificationError('Static network queues require ordinary runtime/application mode')
        spec = PROBES.get(self.mode)
        if spec and spec.requires_static_queues and not self.static_net_queues:
            raise QualificationError(spec.prerequisite)

    @property
    def runtime_port(self):
        return self.mode is not BuildMode.UPSTREAM

    @property
    def configuration_app(self):
        spec = PROBES.get(self.mode)
        return spec.configuration_app if spec else self.mode is BuildMode.CONFIGURATION

    def flags(self):
        """Exact legacy flag names for CMake and evidence, derived from the mode."""
        return dict(configuration_app=self.configuration_app,
                    native_uart_fixture=self.mode is BuildMode.NATIVE_UART,
                    static_net_queues=self.static_net_queues,
                    wifi_probe=self.mode is BuildMode.WIFI,
                    ble_probe=self.mode is BuildMode.BLE,
                    ble_gatt_probe=self.ble_gatt_probe,
                    ble_static_host=self.ble_static_host,
                    model_probe=self.mode is BuildMode.MODEL,
                    model_load_probe=self.mode is BuildMode.MODEL_LOAD,
                    senlux_probe=self.mode is BuildMode.SENLUX,
                    senlux_app_probe=self.mode is BuildMode.SENLUX_APP)

    def cmake_settings(self):
        return [f'-DUTHOME_{name.upper()}={int(enabled)}' for name, enabled in self.flags().items()]

    def evidence(self):
        flags = self.flags()
        flags['configuration_application'] = flags.pop('configuration_app')
        flags['static_network_queues'] = flags.pop('static_net_queues')
        return dict(flags,
                    example='tests/target/runtime_port_board' if self.runtime_port else f'examples/{self.upstream_example}',
                    diagnostic_only=self.mode not in (BuildMode.RUNTIME, BuildMode.CONFIGURATION, BuildMode.NATIVE_UART))


def resolve_mode(*, runtime_port=False, configuration_app=False, upstream_example=None,
                 native_uart_fixture=False, static_net_queues=False, wifi_probe=False,
                 ble_probe=False, model_probe=False, ble_gatt_probe=False, ble_static_host=False,
                 model_load_probe=False, senlux_probe=False, senlux_app_probe=False):
    """Accept the existing CLI vocabulary, rejecting mixed modes before I/O."""
    enabled = dict(native_uart_fixture=native_uart_fixture, wifi_probe=wifi_probe,
                   ble_probe=ble_probe, model_probe=model_probe, model_load_probe=model_load_probe,
                   senlux_probe=senlux_probe, senlux_app_probe=senlux_app_probe)
    selected = [mode for mode in PROBES if enabled[mode.value]]
    if selected:
        mode = selected[0]
        spec = PROBES[mode]
        if (len(selected) != 1 or not runtime_port or upstream_example is not None or
                configuration_app != spec.configuration_app or
                (spec.requires_static_queues and not static_net_queues)):
            raise QualificationError(spec.prerequisite)
    elif upstream_example is not None:
        if upstream_example not in UPSTREAM_EXAMPLES:
            raise QualificationError('Unsupported upstream diagnostic example')
        if runtime_port or configuration_app:
            raise QualificationError('upstream example cannot be combined with application/runtime mode')
        mode = BuildMode.UPSTREAM
    elif configuration_app:
        if not runtime_port:
            raise QualificationError('configuration application requires --runtime-port')
        mode = BuildMode.CONFIGURATION
    else:
        mode = BuildMode.RUNTIME if runtime_port else BuildMode.UPSTREAM
    return BuildSelection(mode=mode, static_net_queues=static_net_queues,
                          ble_gatt_probe=ble_gatt_probe, ble_static_host=ble_static_host,
                          upstream_example=upstream_example or 'tasks')
