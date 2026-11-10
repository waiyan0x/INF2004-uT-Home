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
    integer(profile['version'], 1, 1, 'version')
    if not isinstance(profile['device'], str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,31}', profile['device']):
        raise ValueError('device: expected 1..31 ASCII identifier characters')
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


def request(profile, request_id, generation, *, execute=False):
    profile = validate(profile)
    integer(request_id,1,0xffffffff,'request_id')
    integer(generation,0,0xffffffff,'generation')
    data = bytearray(WIRE)
    data[:4] = b'TJ\x01'+bytes([2 if execute else 1])
    struct.pack_into('<II',data,4,request_id,profile['revision'])
    data[12:12+len(profile['device'])] = profile['device'].encode('ascii')
    struct.pack_into('<I',data,90,generation)
    if not execute:
        raw = bytes.fromhex(profile['write_hex'])
        data[44:48] = bytes([PROTOCOLS[profile['protocol']],profile['address'],profile['mode'],profile['dc_pin']])
        struct.pack_into('<IH',data,48,profile['rate'],profile['timeout_ms'])
        data[54:58] = bytes([len(raw),profile['read_length'],profile['cs_pin'],profile['dc_level']])
        data[60:60+len(raw)] = raw
    return bytes(data)


def result(data, *, request_id=None, generation=None, device=None, revision=None):
    if (not isinstance(data, bytes) or len(data)!=WIRE or data[:3]!=b'TJ\x01' or
            data[3] not in (0,1,2) or data[44]>3 or data[45]>=len(STATUSES) or
            data[46]>30 or data[47]>32 or data[83] or any(data[94:]) or
            any(data[48+data[47]:80]) or (data[45] and data[47])):
        raise ValueError('invalid transaction result')
    expected_revision = revision
    rid, revision = struct.unpack_from('<II', data, 4)
    gen, = struct.unpack_from('<I',data,90)
    if request_id is not None and rid!=request_id:
        raise ValueError('response request ID does not match')
    if generation is not None and gen!=generation:
        raise ValueError('response plan generation changed')
    name, marker, padding = data[12:44].partition(b'\0')
    if not marker or any(padding) or (name and not re.fullmatch(rb'[A-Za-z0-9_-]{1,31}',name)):
        raise ValueError('invalid response identity')
    name = name.decode('ascii')
    if device is not None and name != device:
        raise ValueError('response device does not match')
    if expected_revision is not None and revision != expected_revision:
        raise ValueError('response revision does not match')
    return dict(request_id=rid, revision=revision, device=name,
                generation=gen, protocol=data[44], status=STATUSES[data[45]],
                tx_accepted=data[46], raw_hex=data[48:48+data[47]].hex(' ').upper())


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
        rid,=struct.unpack_from('<I',payload,4)
        self.responses.clear()
        published=self.client.publish(self.request_topic,payload,qos=0,retain=False)
        if published.rc: raise ConnectionError('request could not be queued')
        def matching():
            return any(struct.unpack_from('<I',reply,4)[0]==rid for reply in self.responses)
        self.wait(matching)
        return next(reply for reply in self.responses if struct.unpack_from('<I',reply,4)[0]==rid)

    def close(self):
        self.client.disconnect(); self.client.loop(timeout=0.1)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile',type=Path)
    parser.add_argument('--generation',type=int,required=True)
    parser.add_argument('--request-id',type=int,required=True)
    parser.add_argument('--execute',action='store_true',help='execute the rule after a successful update')
    parser.add_argument('--output',type=Path,help='save compiled bytes/report without network access')
    parser.add_argument('--broker');parser.add_argument('--port',type=int,default=8883)
    parser.add_argument('--ca',type=Path);parser.add_argument('--node')
    args=parser.parse_args(argv)
    profile=validate(json.loads(args.profile.read_text(),object_pairs_hook=unique))
    update=request(profile,args.request_id,args.generation)
    if args.execute: integer(args.request_id,1,0xfffffffe,'request_id')
    if args.output:
        if args.output.resolve()==args.profile.resolve(): raise ValueError('output would overwrite profile')
        report={'update_hex':update.hex(), 'hardware_executed':False}
        if args.execute: report['execute_hex']=request(profile,args.request_id+1,args.generation,execute=True).hex()
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')
        return 0
    if not (args.broker and args.ca and args.node): parser.error('provide --output or --broker, --ca and --node')
    link=Mqtt(args.broker,args.port,args.ca,args.node)
    try:
        answer=result(link.exchange(update),request_id=args.request_id,generation=args.generation,
                      device=profile['device'],revision=profile['revision'])
        print(json.dumps(answer))
        if args.execute and answer['status']=='ok':
            message=request(profile,args.request_id+1,args.generation,execute=True)
            print(json.dumps(result(link.exchange(message),request_id=args.request_id+1,generation=args.generation,
                                    device=profile['device'],revision=profile['revision'])))
    finally: link.close()
    return 0

if __name__=='__main__': raise SystemExit(main())
