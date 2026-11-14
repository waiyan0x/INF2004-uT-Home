"""Explicit local TLS broker bench; real C owner/HAL doubles, no ESP/RF proof.

Requires the declared MQTT client, openssl and mosquitto executables. Creates
only loopback listeners and temporary test credentials, then stops the broker.
"""
from test_txjob import BOOT
import argparse
import ctypes as c
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT));sys.path.insert(0,str(Path(__file__).parent))
from tools.transaction.client import Mqtt, request, result
from test_tx_owner import OwnerTests


def run(args):
    import paho.mqtt.client as mqtt
    OwnerTests.setUpClass()
    owner=OwnerTests('test_i2c_rules_select_actual_registers_and_release_claims')
    owner.setUp()
    node='uthome-010203040506'
    incoming=queue.Queue(maxsize=1); stopped=threading.Event(); ready=threading.Event()
    failures=[]; results=[]
    link=None; device=None; process=None; thread=None
    previous={key:os.environ.get(key) for key in ('UTHOME_MQTT_USER','UTHOME_MQTT_PASSWORD')}
    with tempfile.TemporaryDirectory(prefix='uthome-mqtt-loopback-') as temp:
        folder=Path(temp)
        subprocess.run([args.openssl,'req','-x509','-newkey','rsa:2048','-nodes',
                        '-keyout',str(folder/'key.pem'),'-out',str(folder/'cert.pem'),
                        '-days','1','-subj','/CN=localhost','-addext',
                        'subjectAltName=DNS:localhost,IP:127.0.0.1'],
                       check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        passwords=folder/'passwords'
        passwords.write_text('controller:loopback-only\nnode:loopback-only\n')
        passwords.chmod(0o600)
        subprocess.run([args.passwd,'-U',str(passwords)],check=True,capture_output=True)
        with socket.socket() as probe:
            probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
        config=folder/'broker.conf'
        config.write_text(f'listener {port} 127.0.0.1\nallow_anonymous false\n'
                          f'password_file {passwords}\ncertfile {folder/"cert.pem"}\n'
                          f'keyfile {folder/"key.pem"}\npersistence false\n')
        log=(folder/'broker.log').open('w')
        try:
            process=subprocess.Popen([args.mosquitto,'-c',str(config)],stdout=log,stderr=log)
            deadline=time.monotonic()+5
            while True:
                if process.poll() is not None: raise RuntimeError('local broker failed to start')
                try:
                    with socket.create_connection(('127.0.0.1',port),timeout=.1):break
                except OSError:
                    if time.monotonic()>=deadline:raise TimeoutError('local broker startup timeout')
                    time.sleep(.02)
            device=mqtt.Client(client_id=node,clean_session=True)
            device.username_pw_set('node','loopback-only')
            device.tls_set_context(ssl.create_default_context(cafile=str(folder/'cert.pem')))
            topic=f'uthome/v1/{node}/transaction/request'
            reply_topic=f'uthome/v1/{node}/transaction/result'
            def connected(client,userdata,flags,code):
                if code: failures.append('device connection rejected')
                else:client.subscribe(topic,qos=0)
            def subscribed(client,userdata,mid,qos):
                if list(qos)==[0]:ready.set()
            def message(client,userdata,msg):
                if msg.topic!=topic or msg.retain or len(msg.payload)!=96:
                    failures.append('invalid broker payload');return
                try:incoming.put_nowait(bytes(msg.payload))
                except queue.Full:failures.append('bounded command inbox overflow')
            device.on_connect=connected;device.on_subscribe=subscribed;device.on_message=message
            device.connect('127.0.0.1',port);device.loop_start()
            if not ready.wait(5):raise TimeoutError('device SUBACK timeout')
            def worker():
                feed=None; protocol=0; rx=0
                output=c.create_string_buffer(96)
                try:
                    while not stopped.is_set():
                        try:
                            payload=incoming.get_nowait()
                            if payload[3]==1 and payload[51]<=32:
                                protocol=payload[40];rx=payload[51]
                            if owner.lib.txptest_send(payload,96):raise RuntimeError('owner inbox busy')
                            if payload[3]==2 and protocol==3:feed=bytes(range(rx))
                        except queue.Empty:pass
                        owner.pump()
                        if feed is not None and owner.lib.txptest_feed(feed,len(feed))==0:feed=None
                        if owner.lib.txptest_reply(output)==0:
                            device.publish(reply_topic,output.raw,qos=0,retain=False)
                        stopped.wait(.001)
                except Exception as error:
                    failures.append(str(error));stopped.set()
            thread=threading.Thread(target=worker,name='transaction-owner',daemon=True);thread.start()
            os.environ['UTHOME_MQTT_USER']='controller';os.environ['UTHOME_MQTT_PASSWORD']='loopback-only'
            link=Mqtt('127.0.0.1',port,folder/'cert.pem',node,timeout=10)
            rid=0;last=None
            examples=ROOT/'configs/transactions'
            for name in ('bmp-id','bmp-pressure','oled-off','oled-on','radar-raw16','radar-raw32'):
                spec=json.loads((examples/(name+'.json')).read_text())
                rid+=1
                update=result(link.exchange(request(spec,rid,4,BOOT)),request_id=rid,generation=4)
                if update['status']!='ok':raise AssertionError(update)
                rid+=1
                raw=request(spec,rid,4,BOOT,execute=True)
                answer=result(link.exchange(raw),request_id=rid,generation=4)
                if answer['status']!='ok':raise AssertionError(answer)
                results.append(dict(example=name,**answer));last=spec
            rid+=1;bad=bytearray(request(last,rid,4,BOOT));bad[51]=33
            rejected=result(link.exchange(bytes(bad)),request_id=rid,generation=4)
            if rejected['status']!='invalid':raise AssertionError(rejected)
            rid+=1;execute=request(last,rid,4,BOOT,execute=True)
            answer=result(link.exchange(execute),request_id=rid,generation=4)
            before=owner.lib.txptest_observed(2)
            duplicate=result(link.exchange(execute),request_id=rid,generation=4)
            if duplicate!=answer or before!=owner.lib.txptest_observed(2):raise AssertionError('retry repeated I/O')
            expected=['50','00 00 00','','',bytes(range(16)).hex(' ').upper(),bytes(range(32)).hex(' ').upper()]
            if [row['raw_hex'] for row in results]!=expected:raise AssertionError(results)
            if failures:raise AssertionError(failures)
            record=dict(passed=True,real_tls_broker=True,simulated_hal=True,hardware_executed=False,
                        examples=results,rejected_update_preserved=True,exact_retry_no_io=True,
                        note='Real MQTT TLS transport and compiled C owner; no physical ESP/peripherals or RF test.')
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(json.dumps(record,indent=2)+'\n')
            print(json.dumps(dict(passed=True,examples=len(results),hardware_executed=False,report=str(args.output))))
        finally:
            stopped.set()
            if thread:thread.join(2)
            if link:link.close()
            if device:device.disconnect();device.loop_stop()
            if process:
                process.terminate()
                try:process.wait(5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
            log.close()
            for key,value in previous.items():
                if value is None:os.environ.pop(key,None)
                else:os.environ[key]=value
            OwnerTests.doClassCleanups()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mosquitto',default=shutil.which('mosquitto'))
    parser.add_argument('--passwd',default=shutil.which('mosquitto_passwd'))
    parser.add_argument('--openssl',default=shutil.which('openssl'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if not all((args.mosquitto,args.passwd,args.openssl)):parser.error('provide broker/password/openssl executables')
    run(args)

if __name__=='__main__':main()
