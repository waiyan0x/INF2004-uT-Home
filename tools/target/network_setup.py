"""Private development-only Wi-Fi/TLS build inputs, verified on rebuild.

No credentials are printed or placed in command arguments. The generated header,
ELF and flash image contain secrets and must stay private. This is immutable
development provisioning, not a second runtime configuration or save mechanism.
"""
import base64
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


def mqtt_ca_blob(pem):
    """Embed a single PEM certificate as DER; preserve native PEM bundle parsing."""
    single = re.fullmatch(rb'-----BEGIN CERTIFICATE-----\r?\n'
                          rb'((?:[A-Za-z0-9+/=]+\r?\n)+)'
                          rb'-----END CERTIFICATE-----(?:\r?\n)?', pem)
    if single:
        try:
            der = base64.b64decode(b''.join(single[1].splitlines()), validate=True)
        except ValueError:
            pass
        else:
            if der.startswith(b'\x30'):
                return der, True
    return pem + b'\0', False


def generate(source):
    source=Path(source).resolve()
    raw=source.read_bytes()
    if len(raw)>4096:
        raise ValueError('setup JSON exceeds bound')
    config=json.loads(raw.decode('utf-8'),object_pairs_hook=unique)
    required={'ssid','password'}
    if (not isinstance(config,dict) or not required<=config.keys() or
            config.keys()-required-{'mqtt','tx_power_quarter_dbm'}):
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
    password=config['password']
    if (not isinstance(password,str) or not 8<=len(password)<=63 or
            any(ord(char)<0x20 or ord(char)>0x7e for char in password)):
        raise ValueError('invalid setup password')
    blobs={}
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
        blobs['mqtt_ca'],ca_der=mqtt_ca_blob(ca)
    lines=['/** @file netsetup.h',
           ' * @brief Private generated development setup; never publish. */',
           '#ifndef UTHOME_NETSETUP_H','#define UTHOME_NETSETUP_H',
           '#include <stdint.h>']
    lines.append(f'#define UTHOME_SETUP_MQTT ({int(mqtt is not None)}U)')
    lines.append(f'#define UTHOME_SETUP_TX_POWER ({tx_power}U)')
    if mqtt is not None:
        lines.append(f'#define UTHOME_SETUP_MQTT_CA_DER ({int(ca_der)}U)')
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
    for name,data in blobs.items():
        lines.append(f'static const uint8_t g_setup_{name}[] = {{')
        for start in range(0,len(data),10):
            lines.append('    '+', '.join(f'0x{byte:02x}U' for byte in data[start:start+10])+',')
        lines.append('};')
    lines+=['#endif','','/* End of file. */','','']
    header=format_header('\n'.join(lines).encode('utf-8'))
    record=dict(source=str(source),source_sha256=digest(raw),
                generated_sha256=digest(header),
                phy_max_wifi_tx_power=20 if tx_power==0 else max(10,min(20,tx_power//4)))
    if mqtt is not None:
        record['mqtt_ca_sha256']=digest(ca+b'\0')
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
    defaults=project/'network-power.defaults'
    if target.exists() or defaults.exists():
        raise ValueError('private setup already staged')
    target.write_bytes(header)
    # The runtime cap is applied after radio initialization. Bound its startup
    # power too, using the pinned SDK's supported whole-dBm range (10..20).
    defaults.write_bytes(f'CONFIG_ESP_PHY_MAX_WIFI_TX_POWER={record["phy_max_wifi_tx_power"]}\n'.encode('ascii'))
    manifest_path=project/'source-manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    manifest['network_setup']=record
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


def verify(project,record,*,sdkconfig=None):
    if not isinstance(record,dict) or not isinstance(record.get('source'),str):
        raise ValueError('invalid private setup manifest')
    header,current=generate(record['source'])
    defaults=f'CONFIG_ESP_PHY_MAX_WIFI_TX_POWER={current["phy_max_wifi_tx_power"]}\n'.encode('ascii')
    if (current!=record or (Path(project)/'app/netsetup.h').read_bytes()!=header
            or (Path(project)/'network-power.defaults').read_bytes()!=defaults):
        raise ValueError('private inputs changed')
    if sdkconfig is not None:
        values=re.findall(r'^CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=(.*)$',
                          Path(sdkconfig).read_text(),re.MULTILINE)
        if values!=[str(current['phy_max_wifi_tx_power'])]:
            raise ValueError('compiled Wi-Fi startup power differs from private setup')
