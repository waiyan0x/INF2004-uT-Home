"""Compile bounded three-bus rules and issue one TLS MQTT update/execution.

No automatic retries. Use the current node plan generation and increasing
request IDs. A transport timeout means outcome unknown, never safe to replay a
write with a new ID. Explicit exact-ID retry is available through the codec.
"""
import argparse
import json
import os
from pathlib import Path
import re
import ssl
import struct
import time

WIRE = 96
PROTOCOLS = {'i2c': 1, 'spi': 2, 'uart': 3}
STATUSES = ('ok', 'invalid', 'unconfigured', 'mismatch', 'timeout', 'io',
            'busy', 'unsupported', 'stale_id', 'cancelled')
FIELDS = {'version', 'device', 'revision', 'protocol', 'address', 'mode',
          'dc_pin', 'dc_level', 'cs_pin', 'rate', 'timeout_ms', 'write_hex', 'read_length'}


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{label}: expected integer {low}..{high}')
    return value


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate field: {key}')
        result[key] = value
    return result


def validate(profile):
    if not isinstance(profile, dict) or set(profile) != FIELDS:
        raise ValueError('unexpected or missing transaction fields')
    integer(profile['version'], 2, 2, 'version')
    if not isinstance(profile['device'], str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,15}', profile['device']):
        raise ValueError('device: expected 1..15 ASCII identifier characters')
    integer(profile['revision'], 1, 0xffffffff, 'revision')
    protocol = profile['protocol']
    if not isinstance(protocol, str) or protocol not in PROTOCOLS:
        raise ValueError('protocol: expected i2c, spi or uart')
    for key, low, high in [('address',0,119), ('mode',0,3), ('dc_pin',0,255),
                           ('dc_level',0,1), ('cs_pin',0,255), ('rate',9600,8000000),
                           ('timeout_ms',10,1000), ('read_length',0,32)]:
        integer(profile[key], low, high, key)
    if not isinstance(profile['write_hex'], str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2}(?: [0-9a-fA-F]{2})*)?', profile['write_hex']):
        raise ValueError('write_hex: use space-separated hex bytes, or empty for receive only')
    data = bytes.fromhex(profile['write_hex'])
    if len(data)>30 or not (data or profile['read_length']):
        raise ValueError('expected 0..30 TX bytes and/or 1..32 RX bytes')
    if protocol == 'spi':
        pins = {3,4,5,10}
        if (profile['address'] or profile['cs_pin'] not in pins or
                profile['dc_pin'] not in pins | {255} or
                profile['dc_pin'] == profile['cs_pin'] or
                (profile['dc_pin']==255 and profile['dc_level']) or
                not 100000 <= profile['rate'] <= 8000000):
            raise ValueError('invalid fixed-board SPI settings')
        if profile['read_length']:
            raise ValueError('SPI receive is unsupported; this board service is write only')
    else:
        if profile['mode'] or profile['dc_pin']!=255 or profile['cs_pin']!=255 or profile['dc_level']:
            raise ValueError('SPI-only settings must be absent on I2C/UART')
        if protocol=='i2c' and not (8 <= profile['address'] <=119 and 10000 <=profile['rate']<=400000):
            raise ValueError('invalid I2C address/clock')
        if protocol=='uart' and (profile['address'] or not 9600 <=profile['rate']<=460800):
            raise ValueError('invalid UART address/baud')
    return profile


def query(request_id, boot_id=0):
    """Read-only nonce; zero boot discovers identity without changing ID history."""
    integer(request_id, 1, 0xffffffff, 'request_id')
    integer(boot_id, 0, 0xffffffffffffffff, 'boot_id')
    data = bytearray(WIRE)
    data[:4] = b'TJ\x02\x03'
    struct.pack_into('<IQ', data, 4, request_id, boot_id)
    return bytes(data)


def request(profile, request_id, generation, boot_id, *, execute=False):
    profile = validate(profile)
    integer(request_id, 1, 0xffffffff, 'request_id')
    integer(generation, 0, 0xffffffff, 'generation')
    integer(boot_id, 1, 0xffffffffffffffff, 'boot_id')
    data = bytearray(WIRE)
    data[:4] = b'TJ\x02' + bytes([2 if execute else 1])
    struct.pack_into('<IQII', data, 4, request_id, boot_id, generation, profile['revision'])
    data[24:24+len(profile['device'])] = profile['device'].encode('ascii')
    if not execute:
        raw = bytes.fromhex(profile['write_hex'])
        data[40:44] = bytes([PROTOCOLS[profile['protocol']], profile['address'], profile['mode'], profile['dc_pin']])
        struct.pack_into('<IH', data, 44, profile['rate'], profile['timeout_ms'])
        data[50:54] = bytes([len(raw), profile['read_length'], profile['cs_pin'], profile['dc_level']])
        data[56:56+len(raw)] = raw
    return bytes(data)


def _header(data, request_id, boot_id, kind):
    if (not isinstance(data, bytes) or len(data) != WIRE or data[:3] != b'TJ\x02' or
            data[3] not in (0, 1, 2, 3) or data[40] > 3 or
            data[41] >= len(STATUSES) or data[95]):
        raise ValueError('invalid transaction result')
    rid, boot, gen, revision = struct.unpack_from('<IQII', data, 4)
    if not boot or (data[3] and not rid) or (not data[3] and (rid or data[41] != 1)):
        raise ValueError('invalid response session')
    if request_id is not None and rid != request_id:
        raise ValueError('response request ID does not match')
    if boot_id is not None and boot != boot_id:
        raise ValueError('response boot identity changed')
    if kind is not None and data[3] != kind:
        raise ValueError('response operation kind does not match')
    name, marker, padding = data[24:40].partition(b'\0')
    if not marker or any(padding) or (name and not re.fullmatch(rb'[A-Za-z0-9_-]{1,15}', name)):
        raise ValueError('invalid response identity')
    settings = dict(address=data[84], mode=data[85], cs_pin=data[86], dc_pin=data[87],
                    rate=struct.unpack_from('<I', data, 88)[0],
                    timeout_ms=struct.unpack_from('<H', data, 92)[0], dc_level=data[94])
    if data[40]:
        # Reuse rule validation for the returned bus settings. A dummy outgoing
        # byte supplies only the required nonempty transfer, never sensor data.
        validate(dict(version=2, device=name.decode('ascii'), revision=revision,
                      protocol=next(key for key, value in PROTOCOLS.items() if value == data[40]),
                      write_hex='00', read_length=0, **settings))
    elif name or revision or any(data[84:95]):
        raise ValueError('unconfigured response contains rule settings')
    return dict(request_id=rid, boot_id=boot, generation=gen, revision=revision,
                device=name.decode('ascii'), kind={0: 'invalid', 1: 'update', 2: 'execute', 3: 'status'}[data[3]],
                protocol=data[40], status=STATUSES[data[41]], **settings)


def result(data, *, request_id=None, boot_id=None, kind=None, generation=None,
           device=None, revision=None):
    decoded = _header(data, request_id, boot_id, kind)
    if (data[3] == 3 or data[42] > 30 or data[43] > 32 or
            any(data[44+data[43]:76]) or (data[41] and data[43]) or
            (data[3] != 2 and (data[42] or data[43])) or (data[40] == 2 and data[43]) or
            (not data[41] and not data[40])):
        raise ValueError('invalid transaction counts or raw bytes')
    if data[41] == 0:
        for key, expected in [('generation', generation), ('device', device), ('revision', revision)]:
            if expected is not None and decoded[key] != expected:
                raise ValueError(f'response {key} does not match')
    return dict(decoded, tx_accepted=data[42], rx_received=data[43],
                completed_ms=struct.unpack_from('<Q', data, 76)[0],
                raw_hex=data[44:44+data[43]].hex(' ').upper())


def status_result(data, *, request_id=None, boot_id=None):
    decoded = _header(data, request_id, boot_id, 3)
    last_id, = struct.unpack_from('<I', data, 44)
    last_ms, = struct.unpack_from('<Q', data, 48)
    pending_id, = struct.unpack_from('<I', data, 56)
    flags = data[60]
    last_gen, = struct.unpack_from('<I', data, 68)
    now_ms, = struct.unpack_from('<Q', data, 72)
    if (data[41] not in (0, 3) or any(data[42:44]) or any(data[65:68]) or any(data[80:84]) or
            flags > 15 or data[63] > 30 or data[64] > 32 or data[62] >= len(STATUSES) or
            bool(flags & 1) != bool(data[40]) or bool(flags & 2) != bool(pending_id) or
            ((flags & 4) and not (flags & 2)) or ((flags & 8) and (flags & 2)) or
            (pending_id and pending_id <= last_id) or
            (not (flags & 1) and (data[63] or data[64])) or
            ((flags & 1) and not (data[63] or data[64])) or (data[40] == 2 and data[64]) or
            (last_id and data[61] not in (1, 2)) or
            (not last_id and (last_ms or data[61] or data[62] or last_gen))):
        raise ValueError('invalid session status')
    return dict(decoded, loaded=bool(flags & 1), busy=bool(flags & 2),
                closing=bool(flags & 4), ready=bool(flags & 8),
                last_request_id=max(last_id, pending_id), last_completed_id=last_id,
                last_completed_ms=last_ms, last_kind={0: None, 1: 'update', 2: 'execute'}[data[61]],
                last_status=STATUSES[data[62]] if last_id else None,
                last_generation=last_gen, pending_id=pending_id,
                write_length=data[63], read_length=data[64], now_ms=now_ms)


class Mqtt:
    """A single controller connection; verified TLS, SUBACK before publication."""
    def __init__(self, broker, port, ca, node, timeout=10):
        import paho.mqtt.client as mqtt
        if not re.fullmatch(r'uthome-[0-9a-f]{12}',node):
            raise ValueError('node must be the ESP MAC identity uthome-xxxxxxxxxxxx')
        self.timeout=timeout; self.ready=False; self.failure=None; self.responses=[]
        self.request_topic=f'uthome/v1/{node}/transaction/request'
        self.result_topic=f'uthome/v1/{node}/transaction/result'
        self.client=mqtt.Client(client_id=f'uthome-controller-{os.getpid()}',clean_session=True)
        user=os.environ.get('UTHOME_MQTT_USER'); password=os.environ.get('UTHOME_MQTT_PASSWORD')
        if not user or not password:
            raise ValueError('set UTHOME_MQTT_USER and UTHOME_MQTT_PASSWORD privately')
        self.client.username_pw_set(user,password)
        tls=ssl.create_default_context(cafile=str(ca))
        self.client.tls_set_context(tls)
        def connected(client, userdata, flags, code):
            if code: self.failure='broker rejected connection'
            else: client.subscribe(self.result_topic,qos=0)
        def subscribed(client, userdata, mid, qos):
            if list(qos)!=[0]: self.failure='result subscription rejected'
            else: self.ready=True
        def message(client, userdata, msg):
            if msg.topic==self.result_topic and not msg.retain and len(msg.payload)==WIRE:
                if len(self.responses)<4: self.responses.append(bytes(msg.payload))
        self.client.on_connect=connected; self.client.on_subscribe=subscribed; self.client.on_message=message
        try:
            self.client.connect(broker,port,keepalive=30)
            self.wait(lambda:self.ready)
        except Exception:
            self.client.disconnect()
            raise

    def wait(self, condition):
        deadline=time.monotonic()+self.timeout
        while not condition():
            if self.failure: raise RuntimeError(self.failure)
            if time.monotonic()>=deadline: raise TimeoutError('MQTT outcome unknown; no automatic retry was sent')
            code=self.client.loop(timeout=0.1)
            if code: raise ConnectionError('MQTT connection lost; outcome may be unknown')

    def exchange(self, payload):
        rid, boot = struct.unpack_from('<IQ', payload, 4)
        kind = payload[3]
        self.responses.clear()
        published = self.client.publish(self.request_topic, payload, qos=0, retain=False)
        if published.rc:
            raise ConnectionError('request could not be queued')
        answer = None
        def matching():
            nonlocal answer
            # Drop stale/malformed messages so they cannot fill the bounded inbox.
            while self.responses:
                reply = self.responses.pop(0)
                try:
                    if kind == 3:
                        status_result(reply, request_id=rid, boot_id=boot or None)
                    else:
                        result(reply, request_id=rid, boot_id=boot, kind=kind)
                except ValueError:
                    continue
                answer = reply
                return True
            return False
        self.wait(matching)
        return answer

    def close(self):
        self.client.disconnect(); self.client.loop(timeout=0.1)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile',type=Path,nargs='?')
    parser.add_argument('--status',action='store_true',help='read session state without peripheral I/O')
    parser.add_argument('--boot-id',type=lambda value:int(value,0),help='observed nonzero ESP boot ID (decimal or 0x hex)')
    parser.add_argument('--generation',type=int)
    parser.add_argument('--request-id',type=int,required=True)
    parser.add_argument('--execute',action='store_true',help='execute the rule after a successful update')
    parser.add_argument('--output',type=Path,help='save compiled bytes/report without network access')
    parser.add_argument('--broker');parser.add_argument('--port',type=int,default=8883)
    parser.add_argument('--ca',type=Path);parser.add_argument('--node')
    args=parser.parse_args(argv)
    if args.status:
        if args.profile or args.execute or args.output or args.generation is not None:
            parser.error('--status cannot be combined with profile, execute, output or generation')
        if not (args.broker and args.ca and args.node):
            parser.error('--status requires --broker, --ca and --node')
        packet=query(args.request_id,args.boot_id or 0)
        link=Mqtt(args.broker,args.port,args.ca,args.node)
        try:
            answer=status_result(link.exchange(packet),
                                 request_id=args.request_id,boot_id=args.boot_id or None)
            print(json.dumps(answer))
        finally: link.close()
        return 0 if answer['status']=='ok' else 1
    if args.profile is None or args.generation is None or args.boot_id is None:
        parser.error('a profile, --boot-id and --generation are required for commands')
    profile=validate(json.loads(args.profile.read_text(),object_pairs_hook=unique))
    update=request(profile,args.request_id,args.generation,args.boot_id)
    if args.execute: integer(args.request_id,1,0xfffffffe,'request_id')
    if args.output:
        if args.output.resolve()==args.profile.resolve(): raise ValueError('output would overwrite profile')
        report={'update_hex':update.hex(), 'hardware_executed':False}
        if args.execute: report['execute_hex']=request(profile,args.request_id+1,args.generation,args.boot_id,execute=True).hex()
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')
        return 0
    if not (args.broker and args.ca and args.node): parser.error('provide --output or --broker, --ca and --node')
    link=Mqtt(args.broker,args.port,args.ca,args.node)
    try:
        answer=result(link.exchange(update),request_id=args.request_id,boot_id=args.boot_id,kind=1,generation=args.generation,
                      device=profile['device'],revision=profile['revision'])
        print(json.dumps(answer))
        if args.execute and answer['status']=='ok':
            message=request(profile,args.request_id+1,args.generation,args.boot_id,execute=True)
            answer=result(link.exchange(message),request_id=args.request_id+1,boot_id=args.boot_id,kind=2,generation=args.generation,
                          device=profile['device'],revision=profile['revision'])
            print(json.dumps(answer))
    finally: link.close()
    return 0 if answer['status']=='ok' else 1

if __name__=='__main__': raise SystemExit(main())
