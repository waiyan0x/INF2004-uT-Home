"""Private development-only Wi-Fi/TLS build inputs, verified on rebuild.

No credentials are printed or placed in command arguments. The generated header,
ELF and flash image contain secrets and must stay private. This is immutable
development provisioning, not a second runtime configuration or save mechanism.
"""
import ipaddress
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


def unique(pairs):
    result={}
    for name,value in pairs:
        if name in result:
            raise ValueError('duplicate setup field')
        result[name]=value
    return result


def digest(data):
    return hashlib.sha256(data).hexdigest()


def generate(source):
    source=Path(source).resolve()
    raw=source.read_bytes()
    if len(raw)>4096:
        raise ValueError('setup JSON exceeds bound')
    config=json.loads(raw.decode('utf-8'),object_pairs_hook=unique)
    required={'ssid','password','token','certificate_file','key_file'}
    if (not isinstance(config,dict) or not required<=config.keys() or
            config.keys()-required-{'port','mqtt','esphome_lab','tx_power_quarter_dbm'}):
        raise ValueError('invalid setup fields')
    tx_power=config.get('tx_power_quarter_dbm',0)
    if ('tx_power_quarter_dbm' in config and
            (type(tx_power) is not int or not 8<=tx_power<=84)):
        raise ValueError('invalid Wi-Fi TX power')
    ssid=config['ssid']
    if not isinstance(ssid,str) or not ssid.isprintable():
        raise ValueError('invalid SSID text')
    ssid_bytes=ssid.encode('utf-8')
    if not 1<=len(ssid_bytes)<=32:
        raise ValueError('SSID exceeds byte bound')
    for name,minimum,maximum in [('password',8,63),('token',32,64)]:
        value=config[name]
        if (not isinstance(value,str) or not minimum<=len(value)<=maximum or
                any(ord(char)<(0x21 if name=='token' else 0x20) or ord(char)>0x7e for char in value)):
            raise ValueError('invalid setup text')
    lab=config.get('esphome_lab',False)
    if type(lab) is not bool or (lab and 'port' in config):
        raise ValueError('ESPHome lab uses dedicated port 6053 instead of HTTPS')
    port=config.get('port',6053 if lab else 8443)
    if type(port) is not int or not 1<=port<=65535:
        raise ValueError('invalid HTTPS port')
    blobs={}
    for name in ['certificate','key']:
        relative=config[name+'_file']
        if not isinstance(relative,str) or not relative or '\0' in relative:
            raise ValueError('invalid PEM input path')
        data=(source.parent/relative).resolve().read_bytes()
        if not 1<=len(data)<=8191 or b'\0' in data:
            raise ValueError('PEM exceeds bound')
        if name=='certificate' and b'-----BEGIN CERTIFICATE-----' not in data:
            raise ValueError('missing certificate PEM')
        if name=='key' and not any(marker in data for marker in
                [b'-----BEGIN PRIVATE KEY-----',b'-----BEGIN EC PRIVATE KEY-----',
                 b'-----BEGIN RSA PRIVATE KEY-----']):
            raise ValueError('missing unencrypted private key PEM')
        blobs[name]=data+b'\0'
    mqtt=config.get('mqtt')
    mqtt_text={}
    if mqtt is not None:
        fields={'address','server_name','port','user','password','ca_file'}
        if not isinstance(mqtt,dict) or set(mqtt)!=fields:
            raise ValueError('invalid MQTT setup fields')
        try:
            ipaddress.IPv4Address(mqtt['address'])
        except (ValueError,TypeError,ipaddress.AddressValueError) as error:
            raise ValueError('MQTT requires numeric IPv4 address') from error
        if type(mqtt['port']) is not int or not 1<=mqtt['port']<=65535:
            raise ValueError('invalid MQTT port')
        for name,maximum in [('address',15),('server_name',253),('user',64),('password',64)]:
            value=mqtt[name]
            if (not isinstance(value,str) or not 1<=len(value)<=maximum or
                    any(ord(char)<0x21 or ord(char)>0x7e for char in value)):
                raise ValueError('invalid MQTT identity')
            if name=='server_name' and not re.fullmatch(r'[A-Za-z0-9.-]+',value):
                raise ValueError('invalid MQTT certificate name')
            mqtt_text['mqtt_'+name]=value
        if not isinstance(mqtt['ca_file'],str) or not mqtt['ca_file'] or '\0' in mqtt['ca_file']:
            raise ValueError('invalid MQTT CA path')
        ca=(source.parent/mqtt['ca_file']).resolve().read_bytes()
        if not 1<=len(ca)<=8191 or b'\0' in ca or b'-----BEGIN CERTIFICATE-----' not in ca:
            raise ValueError('invalid MQTT CA PEM')
        blobs['mqtt_ca']=ca+b'\0'
    lines=['/** @file netsetup.h',
           ' * @brief Private generated development setup; never publish. */',
           '#ifndef UTHOME_NETSETUP_H','#define UTHOME_NETSETUP_H',
           '#include <stdint.h>',f'#define UTHOME_SETUP_PORT ({port}U)']
    lines.append(f'#define UTHOME_SETUP_MQTT ({int(mqtt is not None)}U)')
    lines.append(f'#define UTHOME_OCCUPANCY_LAB ({int(lab)}U)')
    lines.append(f'#define UTHOME_SETUP_TX_POWER ({tx_power}U)')
    if mqtt is not None:
        lines.append(f'#define UTHOME_SETUP_MQTT_PORT ({mqtt["port"]}U)')
    # String-literal escapes preserve UTF-8 bytes in char storage regardless
    # of compiler char signedness. Adjacent chunks bound generated line length.
    lines.append('static const char g_setup_ssid[33] =')
    for start in range(0,len(ssid_bytes),10):
        lines.append('    "'+''.join(f'\\x{byte:02x}' for byte in ssid_bytes[start:start+10])+'"')
    lines[-1]+=';'
    text_inputs=[('password',64,config['password'])]
    text_inputs += [(name,len(value)+1,value) for name,value in mqtt_text.items()]
    for name,size,value in text_inputs:
        data=value.encode('ascii')+b'\0'
        lines.append(f'static const char g_setup_{name}[{size}] = {{')
        for start in range(0,len(data),10):
            lines.append('    '+', '.join(f'0x{byte:02x}U' for byte in data[start:start+10])+',')
        lines.append('};')
    blobs['token']=config['token'].encode('ascii')+b'\0'
    for name,data in blobs.items():
        lines.append(f'static const uint8_t g_setup_{name}[] = {{')
        for start in range(0,len(data),10):
            lines.append('    '+', '.join(f'0x{byte:02x}U' for byte in data[start:start+10])+',')
        lines.append('};')
    lines+=['#endif','','/* End of file. */','','']
    header=format_header('\n'.join(lines).encode('utf-8'))
    record=dict(source=str(source),source_sha256=digest(raw),
                certificate_sha256=digest(blobs['certificate']),
                key_sha256=digest(blobs['key']),generated_sha256=digest(header))
    if mqtt is not None:
        record['mqtt_ca_sha256']=digest(blobs['mqtt_ca'])
    return header,record


def format_header(header):
    formatter=os.environ.get('BARR_CLANG_FORMAT','clang-format')
    try:
        version=subprocess.run([formatter,'--version'],capture_output=True,
                               check=True,text=True).stdout
        if not re.search(r'\b20\.1\.8\b',version):
            raise ValueError('pinned formatter required')
        assumed=Path(__file__).resolve().parents[2]/'firmware/app/netsetup.h'
        result=subprocess.run([formatter,'--style=file',f'--assume-filename={assumed}'],
                              input=header,capture_output=True,check=True).stdout
    except (OSError,subprocess.CalledProcessError) as error:
        raise ValueError('private setup formatter unavailable') from error
    return result.replace(b'\r\n',b'\n').rstrip(b'\n')+b'\n\n'


def stage(project,source):
    project=Path(project)
    header,record=generate(source)
    target=project/'app/netsetup.h'
    if target.exists():
        raise ValueError('private setup already staged')
    target.write_bytes(header)
    manifest_path=project/'source-manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    manifest['network_setup']=record
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


def verify(project,record):
    if not isinstance(record,dict) or not isinstance(record.get('source'),str):
        raise ValueError('invalid private setup manifest')
    header,current=generate(record['source'])
    if current!=record or (Path(project)/'app/netsetup.h').read_bytes()!=header:
        raise ValueError('private inputs changed')
