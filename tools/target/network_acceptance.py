"""Finite read-only client captures. No flashing, provisioning, gateway or Apply.

Run `python -m tools.target.network_acceptance --help` from the repository.
Live subcommands contact only explicitly supplied endpoints. We do not infer a
board/port, change a broker or claim system acceptance from one capture.
A BLE backend may resolve the explicitly supplied address during connection.
"""
from __future__ import annotations
import argparse
import asyncio
import ipaddress
import json
import os
from pathlib import Path
import re
import socket
import ssl
import struct
import time
import uuid

from tools.target.network_client import BleReassembler, decode_mqtt, fetch

MAX_PACKET=1154
IDENTITY='ec441e20-8a4b-4aee-9db3-75b77d880011'
STATUS='ec441e20-8a4b-4aee-9db3-75b77d880012'
TELEMETRY='ec441e20-8a4b-4aee-9db3-75b77d880013'
STATES=('offline','tls','connect','connack','availability','ready','unavailable')


def diagnostics(value):
    """Validate additive MQTT status without confusing disabled with offline."""
    if not isinstance(value,dict) or type(value.get('enabled')) is not bool:
        raise ValueError('invalid MQTT diagnostics')
    for field in ('state','failures','replaced','discarded'):
        item=value.get(field)
        if type(item) is not int or not 0<=item<=0xffffffff:
            raise ValueError('invalid MQTT counter/state')
    if value['state']>=len(STATES):
        raise ValueError('unknown MQTT state')
    return dict(value,state_name=STATES[value['state']] if value['enabled'] else 'disabled')


def https_capture(origin,ca,token,source):
    identity=fetch(origin,'identity',token,ca=ca)
    status=fetch(origin,'status',token,ca=ca)
    sample=fetch(origin,'telemetry',token,source=source,ca=ca)
    if identity.get('boot_id')!=status.get('boot_id') or status.get('boot_id')!=sample['boot_id']:
        raise ValueError('boot changed; restart capture')
    if (status.get('runtime_generation')!=sample['runtime_generation'] or
            status.get('active_revision')!=sample['active_revision']):
        raise ValueError('plan changed; restart capture')
    return dict(identity=identity,status=status,mqtt=diagnostics(status.get('mqtt')),sample=sample)


def _budget(sock,deadline):
    remaining=deadline-time.monotonic()
    if remaining<=0:
        raise TimeoutError('capture deadline')
    sock.settimeout(remaining)


def _exact(sock,size,deadline):
    data=bytearray()
    while len(data)<size:
        _budget(sock,deadline)
        chunk=sock.recv(size-len(data))
        if not chunk:
            raise ConnectionError('broker closed')
        data.extend(chunk)
    return bytes(data)


def _packet(sock,deadline):
    header=_exact(sock,1,deadline)[0]
    size=0
    for index in range(4):
        digit=_exact(sock,1,deadline)[0]
        size+=(digit&127)*(128**index)
        if size>MAX_PACKET:
            raise ValueError('MQTT packet exceeds bound')
        if not digit&128:
            if index and digit==0:
                raise ValueError('noncanonical MQTT length')
            return header,_exact(sock,size,deadline)
    raise ValueError('invalid MQTT length')


def _wire(header,body):
    if len(body)>MAX_PACKET:
        raise ValueError('MQTT packet exceeds bound')
    length=len(body)
    prefix=bytearray([header])
    while True:
        digit=length%128
        length//=128
        prefix.append(digit|(128 if length else 0))
        if not length:
            return bytes(prefix)+body


def _field(value,maximum):
    if not isinstance(value,str) or not 1<=len(value)<=maximum or any(not 33<=ord(c)<=126 for c in value):
        raise ValueError('invalid MQTT text')
    data=value.encode('ascii')
    return struct.pack('>H',len(data))+data


def mqtt_capture(address,server_name,port,ca,user,password,node,count=3,timeout=30):
    """Verified TLS subscriber; bounded frames, one absolute receive deadline.

    PC observation only. It sends CONNECT/SUBSCRIBE/DISCONNECT, never PUBLISH.
    Observer credentials must have read-only ACLs; environment values stay out
    of reports and command-line arguments. Numeric address avoids resolver waits.
    """
    ipaddress.IPv4Address(address)
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,31}',node) or not 1<=count<=32 or not 1<=timeout<=120:
        raise ValueError('invalid capture bounds')
    if type(port) is not int or not 1<=port<=65535 or not server_name:
        raise ValueError('invalid broker endpoint')
    client='uthome-test-'+uuid.uuid4().hex[:12]
    connect=b'\x00\x04MQTT\x04\xc2\x00\x00'+_field(client,32)+_field(user,64)+_field(password,64)
    filters=[f'uthome/v1/{node}/availability',f'uthome/v1/{node}/telemetry/#']
    subscribe=b'\x00\x01'+b''.join(_field(topic,128)+b'\x00' for topic in filters)
    deadline=time.monotonic()+timeout
    context=ssl.create_default_context(cafile=ca)
    with socket.create_connection((address,port),timeout=timeout) as raw:
        _budget(raw,deadline)
        with context.wrap_socket(raw,server_hostname=server_name) as secure:
            _budget(secure,deadline);secure.sendall(_wire(0x10,connect))
            if _packet(secure,deadline)!=(0x20,b'\x00\x00'):
                raise ValueError('broker rejected clean session')
            _budget(secure,deadline);secure.sendall(_wire(0x82,subscribe))
            subscribed=False
            received=[]
            packets=0
            while not subscribed or len(received)<count:
                header,body=_packet(secure,deadline)
                packets+=1
                if packets>count+8:
                    raise ValueError('unexpected broker packet count')
                if header==0x90 and not subscribed and body==b'\x00\x01\x00\x00':
                    subscribed=True
                    continue
                if header not in (0x30,0x31) or len(body)<2:
                    raise ValueError('unexpected MQTT packet')
                size=int.from_bytes(body[:2],'big')
                if not 1<=size<=128 or size+2>len(body):
                    raise ValueError('invalid topic length')
                topic=body[2:2+size].decode('ascii')
                if not topic.startswith(f'uthome/v1/{node}/'):
                    raise ValueError('unexpected node')
                observation=decode_mqtt(topic,body[2+size:])
                if len(received)<count:
                    received.append(dict(topic=topic,retained=bool(header&1),observation=observation))
            _budget(secure,deadline);secure.sendall(b'\xe0\x00')
            return dict(messages=received,certificate_verified=True)


def _ble_identity(data):
    if len(data)!=44 or data[:4]!=b'\x01\x00\x00\x00':
        raise ValueError('invalid GATT identity')
    name,separator,padding=data[12:].partition(b'\0')
    if not separator or any(padding) or not re.fullmatch(rb'[A-Za-z0-9_-]{1,31}',name):
        raise ValueError('invalid GATT node')
    return dict(node_id=name.decode('ascii'),boot_id=f'{struct.unpack_from("<Q",data,4)[0]:016x}')


async def ble_capture(address,count=3,timeout=30,mtu=None):
    """Explicit address only, finite notification subscription; optional bleak."""
    if not address or not 1<=count<=32 or not 1<=timeout<=120:
        raise ValueError('invalid BLE capture bounds')
    if mtu is not None and (type(mtu) is not int or not 23<=mtu<=517):
        raise ValueError('invalid ATT decoder MTU')
    from bleak import BleakClient
    async with asyncio.timeout(timeout):
        async with BleakClient(address,timeout=min(timeout,10)) as client:
            identity=_ble_identity(bytes(await client.read_gatt_char(IDENTITY)))
            status=bytes(await client.read_gatt_char(STATUS))
            if len(status)!=20 or status[0]!=1 or status[2]>1 or status[3]>1:
                raise ValueError('invalid GATT status')
            if f'{struct.unpack_from("<Q",status,12)[0]:016x}'!=identity['boot_id']:
                raise ValueError('boot changed')
            receiver=BleReassembler(client.mtu_size if mtu is None else mtu)
            complete=asyncio.Event()
            samples=[]
            errors=[]
            def notification(sender,data):
                if complete.is_set():
                    return
                try:
                    sample=receiver.feed(bytes(data))
                    if sample is not None:
                        if sample['boot_id']!=identity['boot_id']:
                            raise ValueError('boot changed')
                        samples.append(sample)
                        if len(samples)==count:
                            complete.set()
                except ValueError as error:
                    errors.append(error)
                    complete.set()
            await client.start_notify(TELEMETRY,notification)
            try:
                await complete.wait()
            finally:
                await client.stop_notify(TELEMETRY)
                receiver.reset()
            if errors:
                raise errors[0]
            return dict(identity=identity,samples=samples,paired_security_verified=False)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,help='new JSON output; never overwrites')
    sub=parser.add_subparsers(dest='transport',required=True)
    https=sub.add_parser('https')
    https.add_argument('origin');https.add_argument('--ca',required=True)
    https.add_argument('--token-env',default='UTHOME_TOKEN');https.add_argument('--source',type=int,choices=range(8),default=0)
    mqtt=sub.add_parser('mqtt')
    mqtt.add_argument('--address',required=True);mqtt.add_argument('--server-name',required=True)
    mqtt.add_argument('--port',type=int,default=8883);mqtt.add_argument('--ca',required=True)
    mqtt.add_argument('--node',required=True);mqtt.add_argument('--user-env',default='UTHOME_OBSERVER_USER')
    mqtt.add_argument('--password-env',default='UTHOME_OBSERVER_PASSWORD')
    ble=sub.add_parser('ble');ble.add_argument('--address',required=True)
    ble.add_argument('--mtu',type=int)
    for command in (mqtt,ble):
        command.add_argument('--count',type=int,choices=range(1,33),default=3)
        command.add_argument('--timeout',type=int,choices=range(1,121),default=30)
    args=parser.parse_args(argv)
    if args.report and args.report.exists():
        parser.error('report already exists; use a new path')
    if args.report and not args.report.parent.is_dir():
        parser.error('create the report directory first')
    result=dict(kind='network-client-observation',transport=args.transport,
                whole_system_qualified=False,firmware_identity_verified=False)
    try:
        if args.transport=='https':
            data=https_capture(args.origin,args.ca,os.environ.get(args.token_env,''),args.source)
        elif args.transport=='mqtt':
            data=mqtt_capture(args.address,args.server_name,args.port,args.ca,
                 os.environ.get(args.user_env,''),os.environ.get(args.password_env,''),
                 args.node,args.count,args.timeout)
        else:
            data=asyncio.run(ble_capture(args.address,args.count,args.timeout,args.mtu))
        result.update(status='captured',data=data)
    except Exception as error:
        # Never print credentials, request headers or arbitrary remote errors.
        result.update(status='failed',error_type=type(error).__name__)
    encoded=json.dumps(result,indent=2,allow_nan=False)+'\n'
    if args.report:
        with args.report.open('x',encoding='utf-8') as output:
            output.write(encoded)
    print(encoded,end='')
    return 0 if result['status']=='captured' else 1


if __name__=='__main__':
    raise SystemExit(main())
