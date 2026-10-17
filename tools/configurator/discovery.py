"""Catalogue recommendations and additive drafts; no serial access or activation."""
from copy import deepcopy

from tools.config_validator.planner import ROOT, load_document, require


PRESETS = ('bench-i2c', 'bench-uart', 'bench-oled', 'bench-distance', 'bench-pwm')


def connection_presets():
    return [load_document(ROOT / f'catalogue/connections/{name}.json') for name in PRESETS]


def connection_preset(preset_id='bench_i2c'):
    preset = next((row for row in connection_presets() if row['preset_id'] == preset_id), None)
    require(preset is not None, 'Choose a reviewed connection')
    return preset


def recommend(report, catalogue, preset, *, document=None, source_queue=None):
    """Enrich verified, supported identities using descriptor/preset data."""
    for device in report["devices"]:
        recommendation = device["recommendation"]
        if recommendation["status"] != "awaiting_review":
            continue
        ref = device["identity"]["selected"]
        descriptor = catalogue.descriptors[(ref["id"], ref["version"])]
        defaults = descriptor["defaults"]
        if preset['method'] != 'i2c_chip_id':
            recommend_confirmed(device, descriptor, preset, document, source_queue)
            continue
        # This builder supports periodic I2C readers, not arbitrary operations.
        if (descriptor["execution"]["kind"] != "periodic" or
                defaults["behaviour"].get("profile") != "periodic_sensor"):
            recommendation["status"] = "blocked"
            continue
        behaviour = deepcopy(defaults["behaviour"])
        override = next((row for row in preset.get("recommendations", [])
                         if row["descriptor"] == ref), None)
        if override:
            behaviour.update(override["behaviour"])
        address = device["observation"]["address"]
        suffix = f"i2c{preset['interface']['port']}_{address:02x}"
        behaviour["destination"] = f"readings.{suffix}"
        instance = dict(device_id=f"detected_{suffix}", driver=descriptor["driver_id"],
                        descriptor=deepcopy(ref), enabled=True,
                        interface=dict(ref=preset["interface"]["id"], address=f"0x{address:02x}"),
                        behaviour=behaviour,
                        task=dict(name=f"read_{suffix}", priority=defaults["priority"],
                                  stack_size_bytes=defaults["stack_size_bytes"]))
        settings = []

        def setting(label, value, source, reason):
            settings.append(dict(label=label, value=value, source=source, reason=reason))

        setting("Protocol", descriptor["protocol"].upper(), "descriptor",
                "Matched chip identity and reviewed connection mode.")
        setting("Address", instance["interface"]["address"], "observation",
                "This address returned the matching chip ID.")
        bus = preset["interface"]
        setting("Pins", f"SDA GPIO{bus['sda_gpio']}; SCL GPIO{bus['scl_gpio']}",
                "connection_preset", "Must match the wiring you confirmed.")
        setting("Bus speed", f"{bus['clock_hz']} Hz", "connection_preset", "Reviewed probe connection.")
        for label, key in (("Reading interval", "interval_ms"), ("Read timeout", "timeout_ms")):
            if key in behaviour:
                custom = override and key in override["behaviour"]
                setting(label, f"{behaviour[key]} ms", override["source"] if custom else "descriptor_default",
                        override["reason"] if custom else "Catalogue starting value; review for this workload.")
        setting("Task priority", defaults["priority"], "descriptor_default", "Checked by the existing planner.")
        setting("Task stack", f"{defaults['stack_size_bytes']} bytes", "descriptor_default",
                "Checked against the driver and board limits.")
        setting("Readings queue", behaviour["destination"], "rule",
                "A separate queue is added using the draft's queue depth; existing routes stay unchanged.")
        recommendation.update(settings=settings, instance=instance,
                              queue_type=descriptor["routing"]["output_type"])
    return report


def source_queues(document, descriptor):
    """Offer only existing queues of a display-supported type."""
    return [row['id'] for row in document['queues']
            if row['item_type'] in descriptor['routing']['input_types']]


def recommend_confirmed(device, descriptor, preset, document, source_queue):
    defaults = descriptor['defaults']
    behaviour = deepcopy(defaults['behaviour'])
    suffix = preset['preset_id'].removeprefix('bench_')
    instance = dict(device_id=f'confirmed_{suffix}', driver=descriptor['driver_id'],
                    descriptor=deepcopy(device['identity']['selected']), enabled=True,
                    interface=deepcopy(preset['device_interface']), behaviour=behaviour,
                    task=dict(name=f'work_{suffix}', priority=defaults['priority'],
                              stack_size_bytes=defaults['stack_size_bytes']))
    output = descriptor['routing']['output_type']
    if output:
        behaviour['destination'] = f'readings.{suffix}'
    if preset.get('interface'):
        instance['interface']['ref'] = preset['interface']['id']
    if descriptor['execution']['kind'] == 'queue_consumer':
        require(document is not None and source_queue in source_queues(document, descriptor),
                'Choose an existing readings queue for the OLED; add a sensor first if needed')
        behaviour['sources'] = [source_queue]
        instance['display'] = deepcopy(preset['display'])
    if behaviour['profile'] == 'rule_output':
        instance['enabled'] = False  # No guessed rule, duty, load or supply.
    settings = [dict(label='Protocol', value=descriptor['protocol'].upper(), source='descriptor',
                     reason='User-confirmed connection; this does not prove operation.'),
                dict(label='Wiring', value=preset['wiring'], source='connection_preset',
                     reason='Must match the wiring you confirmed.')]
    bus = preset.get('interface', {})
    if bus.get('protocol') == 'uart':
        settings.append(dict(label='Serial settings', source='connection_preset',
                             value=f"{bus['baud_rate']} baud; {bus['data_bits']} data bits; {bus['parity']} parity; {bus['stop_bits']} stop bit; {bus['rx_buffer_bytes']} byte receive buffer",
                             reason='Reviewed settings for the compatible report stream.'))
    elif bus.get('protocol') == 'spi':
        settings.append(dict(label='Bus speed', value=f"{bus['clock_hz']} Hz", source='connection_preset',
                             reason='Reviewed six-pin OLED connection.'))
    for key, value in behaviour.items():
        if key not in ('profile', 'operation', 'frame'):
            settings.append(dict(label=key.replace('_', ' ').capitalize(), value=value,
                                 source='user_selection' if key == 'sources' else 'rule' if key == 'destination' else 'descriptor_default',
                                 reason='Review this starting value for your workload.'))
    settings.extend([dict(label='Task priority', value=defaults['priority'], source='descriptor_default', reason='Checked by the planner.'),
                     dict(label='Task stack', value=f"{defaults['stack_size_bytes']} bytes", source='descriptor_default', reason='Checked against board limits.')])
    if not instance['enabled']:
        settings.append(dict(label='Activation', value='Disabled; needs schema 2.1.0 and a separately reviewed rule', source='rule',
                             reason='No output is energized by confirmation or adding to the draft.'))
    device['recommendation'].update(settings=settings, instance=instance, queue_type=output)


def add_to_draft(document, device, preset):
    """Add one reviewed suggestion; conflicts are errors, never replacements."""
    recommendation = device["recommendation"]
    require(recommendation["status"] == "awaiting_review" and "instance" in recommendation,
            "This observation has no supported settings to add")
    draft = deepcopy(document)
    instance = deepcopy(recommendation["instance"])
    if preset.get('interface'):
        bus = deepcopy(preset['interface'])
        key = 'host' if bus['protocol'] == 'spi' else 'port'
        for existing in draft['interfaces']:
            if existing['protocol'] == bus['protocol'] and existing[key] == bus[key]:
                require(existing | {'id': bus['id']} == bus,
                        f"The draft uses different {bus['protocol'].upper()} pins or speed; review that interface first")
                bus = existing
                break
        else:
            require(all(row['id'] != bus['id'] for row in draft['interfaces']),
                    'The draft already uses the recommended interface ID')
            draft['interfaces'].append(bus)
        instance['interface']['ref'] = bus['id']
        for existing in draft['devices']:
            endpoint = existing['interface']
            if endpoint.get('ref') == bus['id']:
                require(not preset.get('sole_bus'), 'The six-pin OLED must be alone on its SPI bus')
                require(bus['protocol'] != 'uart', 'This UART is already in the draft')
                require(bus['protocol'] != 'i2c' or endpoint.get('address') != instance['interface']['address'],
                        'This address is already in the draft; edit its existing entry')
    if instance['behaviour']['profile'] == 'rule_output':
        require(draft['schema_version'] == '2.1.0', 'PWM needs schema 2.1.0; update and review the draft first')
    # The shared planner checks duplicate device/task/queue IDs and all limits.
    draft["devices"].append(instance)
    if recommendation['queue_type']:
        draft["queues"].append(dict(id=instance["behaviour"]["destination"],
                                item_type=recommendation["queue_type"],
                                depth=draft["runtime"]["queue_depth"], overflow_policy="drop_oldest"))
    return draft
