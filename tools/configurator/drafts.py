"""Catalogue-to-profile construction; no serial access or device identification."""
from copy import deepcopy

from tools.config_validator.planner import require


def i2c_builder_options(descriptor):
    """Expose the reviewed builder's form contract without leaking schema policy.

    Driver availability and physical identity remain separate checks. The same
    eligibility gate applies when the server receives the eventual draft request.
    """
    if (descriptor is None or descriptor['protocol'] != 'i2c'
            or descriptor['execution']['kind'] != 'periodic'
            or descriptor['defaults']['behaviour'].get('profile') != 'periodic_sensor'):
        return {'eligible': False, 'addresses': [], 'needs_enable': False}
    interface = descriptor['instance_schema']['properties']['interface']
    addresses = interface['properties']['address'].get('enum')
    if not isinstance(addresses, list) or not addresses:
        return {'eligible': False, 'addresses': [], 'needs_enable': False}
    return {'eligible': True, 'addresses': list(addresses),
            'needs_enable': 'enable_gpio' in interface.get('required', [])}


def descriptor_instance(descriptor, reference, *, device_id, task_name, interface):
    """Copy reviewed defaults into a new instance, without retaining input aliases."""
    defaults = descriptor['defaults']
    return dict(device_id=device_id, driver=descriptor['driver_id'],
                descriptor=deepcopy(reference), enabled=True, interface=deepcopy(interface),
                behaviour=deepcopy(defaults['behaviour']),
                task=dict(name=task_name, priority=defaults['priority'],
                          stack_size_bytes=defaults['stack_size_bytes']))


def append_instance(draft, instance, queue_type):
    """Append an owned instance and its output queue to an owned draft.

    Callers copy the profile first and enforce their connection policy. The
    shared planner remains responsible for complete admission after construction.
    """
    draft['devices'].append(instance)
    if queue_type:
        draft['queues'].append(dict(id=instance['behaviour']['destination'],
                                    item_type=queue_type, depth=draft['runtime']['queue_depth'],
                                    overflow_policy='drop_oldest'))


def prepare_i2c_draft(document, descriptor, reference, *, device_id, address,
                      sda_gpio, scl_gpio, enable_gpio=None):
    """Add a user-selected periodic reader while preserving existing bus speed.

    The connection owner verifies confirmations, descriptor support and current
    capabilities. This pure builder validates the selected wiring and copies the
    input document; neither a draft nor a name match proves physical identity.
    """
    require(isinstance(device_id, str) and 1 <= len(device_id) <= 22 and
            isinstance(address, str) and type(sda_gpio) is int and type(scl_gpio) is int and
            0 <= sda_gpio <= 21 and 0 <= scl_gpio <= 21 and sda_gpio != scl_gpio,
            'Choose a device name, address and distinct GPIO pins')
    require(all(device['device_id'] != device_id for device in document['devices']),
            'This device name is already in the profile')
    interface_schema = descriptor['instance_schema']['properties']['interface']
    needs_enable = 'enable_gpio' in interface_schema.get('required', [])
    if needs_enable:
        require(type(enable_gpio) is int and 0 <= enable_gpio <= 21 and
                enable_gpio not in (sda_gpio, scl_gpio), 'Choose a distinct enable GPIO')
    else:
        require(enable_gpio is None, 'This reviewed device does not use an enable GPIO')

    draft = deepcopy(document)
    buses = [bus for bus in draft['interfaces'] if bus['protocol'] == 'i2c' and bus['port'] == 0]
    if buses:
        bus = buses[0]
        require((bus['sda_gpio'], bus['scl_gpio']) == (sda_gpio, scl_gpio),
                'The existing I2C controller uses different pins')
    else:
        bus = dict(id='i2c0', protocol='i2c', port=0, sda_gpio=sda_gpio, scl_gpio=scl_gpio, clock_hz=100000)
        draft['interfaces'].append(bus)
    connection = dict(ref=bus['id'], address=address)
    if needs_enable:
        connection['enable_gpio'] = enable_gpio
    instance = descriptor_instance(descriptor, reference, device_id=device_id,
                                   task_name='read_' + device_id, interface=connection)
    instance['behaviour']['destination'] = 'readings.' + device_id
    append_instance(draft, instance, descriptor['routing']['output_type'])
    return draft
