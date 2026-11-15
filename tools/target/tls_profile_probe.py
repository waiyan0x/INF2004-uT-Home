"""Build pinned host TLS peers and compare record compatibility over loopback.

Real handshakes, certificate checks and encryption; host software crypto/heap,
not ESP sockets, hardware, static-allocation acceptance or configuration apply.
Generated keys, logs and results must stay in a new ignored build directory.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
import hashlib
import ipaddress
import json
import platform
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import threading

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
import paho.mqtt.client as mqtt

from .network_config import request
from .qualify_kernel import SDK_REVISION, pinned_checkout
from .tls_profile_peer import Peer, TLSError, packet, receive_packet, text_field

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tests/target'))
from mqtt_fixture import BASE, LocalBroker, certificates

MBEDTLS_REVISION = 'a2b32072ea898afc1ed5b6caf6931e36028c91d6'
BOOT = '0000000000000001'
TOKEN = 'test-only-local-token'


def large_chain(folder):
    """Valid RSA-2048 leaf and two intermediate certificates, below 8192 PEM bytes."""
    now = datetime.now(timezone.utc)
    certs, keys = [], []
    for index in range(4):
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, f'local TLS chain {index}')])
        builder = (x509.CertificateBuilder().subject_name(subject)
                   .issuer_name(certs[-1].subject if certs else subject)
                   .public_key(key.public_key()).serial_number(x509.random_serial_number())
                   .not_valid_before(now-timedelta(minutes=1)).not_valid_after(now+timedelta(days=1))
                   .add_extension(x509.BasicConstraints(ca=index < 3, path_length=2-index if index < 3 else None), True)
                   .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), False)
                   .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(
                       keys[-1].public_key() if keys else key.public_key()), False)
                   .add_extension(x509.KeyUsage(index == 3, False, False, False, False,
                                               index < 3, index < 3, False, False), True))
        if index == 3:
            builder = (builder.add_extension(x509.SubjectAlternativeName([
                x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]), False)
                .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), False))
        certs.append(builder.sign(keys[-1] if keys else key, hashes.SHA256()))
        keys.append(key)
    pem = serialization.Encoding.PEM
    (folder/'ca.pem').write_bytes(certs[0].public_bytes(pem))
    chain = b''.join(cert.public_bytes(pem) for cert in reversed(certs[1:]))
    assert len(chain) <= 8192
    (folder/'server.pem').write_bytes(chain)
    (folder/'server.key').write_bytes(keys[-1].private_bytes(
        pem, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    return {'pem_bytes': len(chain), 'der_bytes': sum(len(cert.public_bytes(serialization.Encoding.DER))
                                                   for cert in certs[1:])}


def build(output, sdk, compiler, incoming, outgoing):
    directory = output/f'in{incoming}-out{outgoing}'
    command = ['cmake', '-S', str(ROOT/'tests/target/tls_profile'), '-B', str(directory),
               '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
               f'-DCMAKE_C_COMPILER={compiler}', f'-DMBEDTLS_SOURCE={sdk}/components/mbedtls/mbedtls',
               f'-DTLS_INPUT={incoming}', f'-DTLS_OUTPUT={outgoing}']
    with (output/f'build-{incoming}-{outgoing}.log').open('x') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['cmake', '--build', str(directory), '--parallel', '8'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    library = directory/('libtlspeer.dll' if sys.platform == 'win32' else 'libtlspeer.so')
    return Peer(library), {'input': incoming, 'output': outgoing,
                           'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(),
                           'compile_commands_sha256': hashlib.sha256(
                               (directory/'compile_commands.json').read_bytes()).hexdigest()}


def broker_case(peer, broker, *, name='127.0.0.1', ca=None, exchange=False):
    result = {'handshake': False, 'mqtt_echo_bytes': 0}
    companion = None
    try:
        if exchange:
            ready = threading.Event()
            companion = mqtt.Client(client_id='tls-profile-echo', clean_session=True)
            companion.username_pw_set('fixture-test', broker.passwords['fixture-test'])
            companion.tls_set_context(ssl.create_default_context(cafile=str(broker.ca)))
            companion.on_connect = lambda client, userdata, flags, code: client.subscribe(BASE+'request', 0)
            companion.on_subscribe = lambda *args: ready.set()
            companion.on_message = lambda client, userdata, message: client.publish(BASE+'result', message.payload, 0)
            companion.connect('127.0.0.1', broker.port)
            companion.loop_start()
            assert ready.wait(5), 'MQTT echo subscription'
        connection = socket.create_connection(('127.0.0.1', broker.port), timeout=5)
        peer.start(connection, broker.folder, name=name, ca=ca)
        result.update(peer.handshake())
        result.update(handshake=True, verify_flags=peer.lib.tlspeer_verify())
        assert result['verify_flags'] == 0
        if exchange:
            body = (text_field('MQTT') + b'\x04\xc2\x00\x14' + text_field('tls-profile-mbed')
                    + text_field('controller-test') + text_field(broker.passwords['controller-test']))
            peer.write(packet(0x10, body))
            assert receive_packet(peer) == (0x20, b'\x00\x00')
            peer.write(packet(0x82, b'\x00\x01'+text_field(BASE+'result')+b'\x00'))
            assert receive_packet(peer) == (0x90, b'\x00\x01\x00')
            payload = bytes(range(96))
            peer.write(packet(0x30, text_field(BASE+'request')+payload))
            assert receive_packet(peer) == (0x30, text_field(BASE+'result')+payload)
            peer.write(b'\xe0\x00')
            result['mqtt_echo_bytes'] = len(payload)
    except TLSError as error:
        result.update(tls_error=error.code, description=str(error), verify_flags=peer.lib.tlspeer_verify())
    finally:
        result['received_records'] = peer.records
        peer.close()
        if companion is not None:
            companion.disconnect()
            companion.loop_stop()
    return result


def response():
    # Deliberately report rejection: this fixture never runs configuration apply.
    data = json.dumps(dict(version=1, boot_id=BOOT, request_id=1, controller_status=3,
                           active_revision=0, saved_revision=0, active_crc=0, saved_crc=0,
                           flags=0, historical=False)).encode()
    return (b'HTTP/1.1 422 Unprocessable Content\r\nContent-Type: application/json\r\n'
            + f'Content-Length: {len(data)}\r\nConnection: close\r\n\r\n'.encode()+data)


def upload(origin, folder, body, segmented):
    if not segmented:
        return request(origin, 'apply', TOKEN, str(folder/'ca.pem'), request_id=1,
                       revision=1, expected=0, boot=BOOT, request_token='local-test', profile=body)
    # Experimental client, not a change to the maintained client or firmware.
    context = ssl.create_default_context(cafile=str(folder/'ca.pem'))
    port = int(origin.rsplit(':', 1)[1])
    with socket.create_connection(('127.0.0.1', port), timeout=5) as raw:
        with context.wrap_socket(raw, server_hostname='127.0.0.1') as secure:
            headers = (f'POST /v1/configuration HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n'
                       f'Authorization: Bearer {TOKEN}\r\nContent-Type: application/json\r\n'
                       f'Content-Length: {len(body)}\r\nX-Request-ID: 1\r\nX-Request-Token: local-test\r\n'
                       f'X-Revision: 1\r\nX-Expected-Revision: 0\r\nX-Expected-Boot-ID: {BOOT}\r\n'
                       'Connection: close\r\n\r\n').encode()
            secure.sendall(headers)
            for offset in range(0, len(body), 1024):
                secure.sendall(body[offset:offset+1024])
            received = b''
            expected = response()
            while len(received) < len(expected):
                data = secure.recv(512)
                if not data:
                    break
                received += data
            assert received == expected
            return {'outcome': 'rejected', 'saved_at_completion': False}


def https_case(peer, folder, size, segmented=False):
    body = b'{}' + b' '*(size-2)
    result = {'body_bytes': size, 'segmented_test_client': segmented,
              'body_received': 0, 'handshake': False, 'application_executed': False}
    with socket.socket() as listener, ThreadPoolExecutor(max_workers=1) as worker:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(5)
        origin = f'https://127.0.0.1:{listener.getsockname()[1]}'
        future = worker.submit(upload, origin, folder, body, segmented)
        try:
            connection, _ = listener.accept()
            peer.start(connection, folder, server=True)
            result.update(peer.handshake())
            result['handshake'] = True
            received = b''
            while b'\r\n\r\n' not in received:
                received += peer.read()
                assert len(received) < 2048
            header, received = received.split(b'\r\n\r\n', 1)
            assert f'Content-Length: {size}'.lower().encode() in header.lower()
            result['body_received'] = len(received)
            while len(received) < size:
                received += peer.read()
                result['body_received'] = len(received)
            assert received == body
            peer.write(response())
        except TLSError as error:
            result.update(tls_error=error.code, description=str(error))
        finally:
            result['received_records'] = peer.records
            peer.close()
        try:
            reply = future.result(timeout=5)
            assert reply['outcome'] == 'rejected' and not reply['saved_at_completion']
            result['reply_received'] = True
        except (OSError, ValueError) as error:
            result.update(reply_received=False, client_error=type(error).__name__)
            # Only fresh local fixtures are contacted; no deployed credentials.
            result['client_reason'] = str(getattr(error, 'reason', error))
    return result


def verify_cases(cases, incoming, outgoing):
    assert cases['mqtt_small']['mqtt_echo_bytes'] == 96
    for name in ('wrong_ca', 'wrong_name'):
        assert not cases[name]['handshake'] and cases[name]['verify_flags'] != 0
        assert cases[name]['tls_error'] == -0x2700, cases[name]
    assert cases['https_small']['body_received'] == 512 and cases['https_small']['reply_received']
    assert cases['https_segmented']['body_received'] == 8192 and cases['https_segmented']['reply_received']
    if incoming == 16384:
        assert cases['https_full']['body_received'] == 8192 and cases['https_full']['reply_received']
    else:
        assert cases['https_full']['handshake'] and not cases['https_full']['reply_received']
        assert cases['https_full']['body_received'] == 0 and cases['https_full']['tls_error'] == -135
    assert cases['mqtt_chain']['handshake'] == (incoming >= 4096)
    assert cases['https_chain']['reply_received'] == (outgoing >= 4096)
    if incoming < 4096:
        assert cases['mqtt_chain']['tls_error'] == -135
    if outgoing < 4096:
        assert cases['https_chain']['tls_error'] == -138


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    if not __debug__:
        parser.error('assertions must remain enabled')
    output = args.output.resolve()
    if not output.is_relative_to(ROOT/'build') or output == ROOT/'build':
        parser.error('output must be a new directory below the ignored root build directory')
    sdk = args.sdk.resolve()
    pinned_checkout(sdk, SDK_REVISION)
    pinned_checkout(sdk/'components/mbedtls/mbedtls', MBEDTLS_REVISION)
    output.mkdir(parents=True, exist_ok=False)
    small, large, wrong = (output/name for name in ('small-certificate', 'chain-certificate', 'wrong-ca'))
    for folder in (small, large, wrong):
        folder.mkdir()
    brokers = [LocalBroker(small), LocalBroker(large)]
    chain = large_chain(large)
    certificates(wrong)
    report = {'kind': 'host-tls-record-compatibility', 'sdk_commit': SDK_REVISION,
              'mbedtls_commit': MBEDTLS_REVISION, 'python': sys.version,
              'host': platform.platform(), 'machine': platform.machine(),
              'openssl': ssl.OPENSSL_VERSION, 'chain': chain, 'profiles': [],
              'hardware_tested': False, 'static_allocation_qualified': False,
              'configuration_apply_tested': False, 'expectations_passed': False,
              'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_sha256': {str(path.relative_to(ROOT)).replace('\\', '/'):
                                hashlib.sha256(path.read_bytes()).hexdigest() for path in (
                                    *sorted((ROOT/'tests/target/tls_profile').glob('*')),
                                    Path(__file__), ROOT/'tools/target/tls_profile_peer.py',
                                    ROOT/'tools/target/network_config.py', ROOT/'tests/target/mqtt_fixture.py')}}
    try:
        for broker in brokers:
            broker.start()
        for incoming, outgoing in ((16384, 4096), (4096, 2048), (2048, 1024)):
            peer, identity = build(output, sdk, args.cc, incoming, outgoing)
            assert peer.lib.tlspeer_limit(True) == incoming
            assert peer.lib.tlspeer_limit(False) == outgoing
            cases = {}
            report['profiles'].append(dict(identity, cases=cases))
            cases['mqtt_small'] = broker_case(peer, brokers[0], exchange=True)
            cases['wrong_ca'] = broker_case(peer, brokers[0], ca=wrong/'ca.pem')
            cases['wrong_name'] = broker_case(peer, brokers[0], name='wrong.invalid')
            cases['mqtt_chain'] = broker_case(peer, brokers[1])
            cases['https_small'] = https_case(peer, small, 512)
            cases['https_full'] = https_case(peer, small, 8192)
            cases['https_segmented'] = https_case(peer, small, 8192, segmented=True)
            cases['https_chain'] = https_case(peer, large, 512)
            verify_cases(cases, incoming, outgoing)
            print(f'{incoming}/{outgoing}: compatibility expectations passed', flush=True)
        report['expectations_passed'] = True
    finally:
        for broker in brokers:
            broker.close()
        (output/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
