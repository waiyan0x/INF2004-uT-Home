"""Read-only Home Assistant display of copied transaction-controller snapshots."""
import json
import os
import re
import ssl
import time

from .client import _MqttSocket

ENTITIES = {
    'status': ('Operation status', None),
    'active_rule': ('Active rule', 'active_available'),
    'protocol': ('Protocol', 'active_available'),
    'raw_hex': ('Raw bytes', 'data_available'),
    'tx_accepted': ('TX bytes accepted', 'data_available'),
    'rx_received': ('RX bytes received', 'data_available'),
}


def topics(node):
    if not isinstance(node, str) or not re.fullmatch(r'uthome-[0-9a-f]{12}', node):
        raise ValueError('invalid node identity')
    base = f'uthome/v1/{node}/display/'
    return base+'state', base+'availability'


def discovery(node):
    """Stable entity identities; no command topics or sensor-value decoding."""
    state, availability = topics(node)
    messages = {}
    for key, (name, gate) in ENTITIES.items():
        config = dict(name=name, unique_id=f'{node}_transaction_{key}',
                      state_topic=state, value_template='{{ value_json.'+key+' }}',
                      expire_after=2, availability_mode='all',
                      availability=[dict(topic=availability)],
                      device=dict(identifiers=[node], name=f'uT-Home {node[7:]}'),
                      origin=dict(name='uT-Home transaction display'))
        if gate:
            config['availability'].append(dict(topic=state, value_template=
                "{{ 'online' if value_json."+gate+" else 'offline' }}"))
        if key == 'status':
            config.update(json_attributes_topic=state,
                          json_attributes_template='{{ value_json.details | tojson }}')
        messages[f'homeassistant/sensor/{node}/{key}/config'] = config
    return messages


def project(view, *, invalidate=False):
    """Whitelist public fields; acknowledgements never become fresh raw data."""
    online = bool(view['connected'] and view['status_fresh'])
    active = view['active'] if online and not view['issue'] else None
    rule = active['rule'] if active else None
    last = view['last']
    result = last['result'] if last else None
    observed = view['observed'] if online else None
    current = bool(result and observed and result['boot_id'] == observed['boot_id']
                   and result['generation'] == observed['generation']
                   and result['request_id'] == observed['last_completed_id'])
    data = bool(online and not invalidate and not view['journal_fault']
                and view['display']['available'] and current)
    if invalidate or view['pending'] is not None:
        status = 'outcome_unknown' if view['pending'] is not None else 'request_pending'
    elif not view['connected']:
        status = 'esp_disconnected'
    elif not online:
        status = 'status_expired'
    elif view['issue'] or view['journal_fault']:
        status = 'controller_blocked'
    elif current:
        status = result['kind']+'_'+result['status']
        if result['kind'] == 'execute' and result['status'] == 'ok' and not data:
            status = 'result_expired'
    else:
        status = 'ready' if view['ready'] else 'not_ready'
    details = dict(node=view['node'], ready=view['ready'],
                   boot_id=f"{observed['boot_id']:016x}" if observed else None,
                   generation=observed['generation'] if observed else None,
                   configured=None, last_result=None, operation=None)
    if rule:
        details['configured'] = {key: rule[key] for key in (
            'device', 'revision', 'protocol', 'address', 'mode', 'rate',
            'dc_pin', 'dc_level', 'cs_pin', 'timeout_ms', 'write_hex', 'read_length')}
        details['configured']['write_length'] = len(bytes.fromhex(rule['write_hex']))
        details['operation'] = active['source'].get('operation')
    if current:
        details['last_result'] = {key: result[key] for key in (
            'request_id', 'kind', 'status', 'completed_ms', 'tx_accepted', 'rx_received')}
    return dict(status=status, active_available=rule is not None, data_available=data,
                active_rule=f"{rule['device']} / revision {rule['revision']}" if rule else None,
                protocol=rule['protocol'] if rule else None,
                raw_hex=view['display']['raw_hex'] if data else None,
                tx_accepted=result['tx_accepted'] if data else None,
                rx_received=result['rx_received'] if data else None, details=details)


class Display:
    """One owner calls poll/close. Display failures never send controller commands.

    A failed connection is discarded, including queued messages. Reconnect
    announces discovery then the current snapshot, never a saved raw reading.
    """
    def __init__(self, broker, port, ca, node, *, clock=time.monotonic):
        self.state_topic, self.availability_topic = topics(node)
        self.configs = discovery(node)
        self.broker, self.port, self.ca, self.node = broker, port, ca, node
        self.clock = clock
        self.client = None
        self.ready = False
        self.failure = None
        self.error = None
        self.announce = True
        self.next_connect = self.next_state = 0
        self.backoff = 1
        self.previous = None

    def _connect(self):
        import paho.mqtt.client as mqtt
        user = os.environ.get('UTHOME_MQTT_USER')
        password = os.environ.get('UTHOME_MQTT_PASSWORD')
        if not user or not password:
            raise ValueError('set UTHOME_MQTT_USER and UTHOME_MQTT_PASSWORD privately')
        self.client = mqtt.Client(client_id=f'{self.node}-display', clean_session=True)
        self.client.max_inflight_messages_set(8)
        self.client.max_queued_messages_set(16)
        self.client.username_pw_set(user, password)
        tls = ssl.create_default_context(cafile=str(self.ca))
        tls.sslsocket_class = _MqttSocket
        self.client.tls_set_context(tls)
        self.client.will_set(self.availability_topic, 'offline', qos=1, retain=True)
        self.client.on_connect = self._connected
        self.client.on_subscribe = self._subscribed
        self.client.on_disconnect = self._disconnected
        self.client.on_message = self._message
        self.ready, self.failure = False, None
        self.announce, self.previous = True, None
        self.client.connect(self.broker, self.port, keepalive=15)
        self.deadline = self.clock()+5

    def _connected(self, client, userdata, flags, code):
        if code:
            self.failure = 'display login rejected'
        else:
            result, _ = client.subscribe('homeassistant/status', qos=0)
            if result:
                self.failure = 'display birth subscription failed'

    def _subscribed(self, client, userdata, mid, qos):
        self.ready = list(qos) == [0]
        if not self.ready:
            self.failure = 'display birth subscription rejected'

    def _disconnected(self, client, userdata, code):
        self.ready = False
        self.failure = 'display disconnected'

    def _message(self, client, userdata, message):
        if message.topic == 'homeassistant/status' and message.payload == b'online':
            self.announce = True

    def _publish(self, topic, payload, *, retain=False):
        if isinstance(payload, dict):
            payload = json.dumps(payload, separators=(',', ':'), allow_nan=False)
        # Raw state is QoS 0 and non-retained: it must not replay on reconnect.
        sent = self.client.publish(topic, payload, qos=1 if retain else 0, retain=retain)
        if sent.rc:
            raise ConnectionError('display publication unavailable')

    def poll(self, view, *, invalidate=False):
        if view['node'] != self.node:
            raise ValueError('display snapshot belongs to another node')
        try:
            if self.client is None:
                if self.clock() < self.next_connect:
                    return self.status()
                self._connect()
            code = self.client.loop(timeout=0.01)
            if code or self.failure:
                raise ConnectionError(self.failure or 'display connection lost')
            if not self.ready:
                if self.clock() >= self.deadline:
                    raise TimeoutError('display connection timeout')
                return self.status()
            self.error, self.backoff = None, 1
            state = project(view, invalidate=invalidate)
            if self.announce:
                for topic, config in self.configs.items():
                    self._publish(topic, config, retain=True)
            if self.announce or state != self.previous or self.clock() >= self.next_state:
                self._publish(self.state_topic, state)
                self.previous = state
                self.next_state = self.clock()+0.5
            if self.announce:
                self._publish(self.availability_topic, 'online', retain=True)
                self.announce = False
            code = self.client.loop(timeout=0.01)
            if code or self.failure:
                raise ConnectionError('display connection lost')
        except (OSError, RuntimeError, ValueError) as error:
            self.close()
            self.error = 'Home Assistant display unavailable: '+type(error).__name__
            self.next_connect = self.clock()+self.backoff
            self.backoff = min(30, self.backoff*2)
        return self.status()

    def status(self):
        return dict(connected=self.ready, error=self.error)

    def close(self):
        if self.client is not None:
            try:
                if self.ready:
                    self._publish(self.availability_topic, 'offline', retain=True)
                    self.client.loop(timeout=0.01)
            except (OSError, RuntimeError, ValueError):
                pass  # Broker loss already makes the last will authoritative.
            finally:
                try:
                    self.client.disconnect()
                    self.client.loop(timeout=0.01)
                except OSError:
                    pass  # Discard this connection and its queues below.
                self.client = None
        self.ready = False
