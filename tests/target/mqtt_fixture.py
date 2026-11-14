"""Authenticated loopback TLS broker and C-owner bridge for host acceptance."""
import asyncio
import ctypes as c
from datetime import datetime, timedelta, timezone
import ipaddress
from pathlib import Path
import queue
import secrets
import ssl
import threading
import time

from amqtt.broker import Broker
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
from passlib.hash import sha512_crypt
import paho.mqtt.client as mqtt

NODE = 'uthome-0123456789ab'
BASE = 'uthome/v1/' + NODE + '/transaction/'


def certificates(folder):
    """Fresh test-only trust root and an IP-scoped loopback server certificate."""
    now = datetime.now(timezone.utc)
    ca_key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'uT-Home software fixture')])
    ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
          .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
          .not_valid_before(now - timedelta(minutes=1)).not_valid_after(now + timedelta(days=1))
          .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
          .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
          .add_extension(x509.SubjectKeyIdentifier.from_public_key(ca_key.public_key()), critical=False)
          .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()), critical=False)
          .sign(ca_key, hashes.SHA256()))
    key = ec.generate_private_key(ec.SECP256R1())
    server_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'loopback software broker')])
    cert = (x509.CertificateBuilder().subject_name(server_name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - timedelta(minutes=1)).not_valid_after(now + timedelta(days=1))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()), critical=False)
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]), critical=False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            .add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), critical=True)
            .sign(ca_key, hashes.SHA256()))
    (folder / 'ca.pem').write_bytes(ca.public_bytes(serialization.Encoding.PEM))
    (folder / 'server.pem').write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    (folder / 'server.key').write_bytes(key.private_bytes(serialization.Encoding.PEM,
                                                        serialization.PrivateFormat.PKCS8,
                                                        serialization.NoEncryption()))


class LocalBroker:
    def __init__(self, folder):
        self.folder = Path(folder)
        certificates(self.folder)
        self.ca = self.folder / 'ca.pem'
        self.passwords = {name: secrets.token_hex(16) for name in ('controller-test', 'fixture-test')}
        (self.folder / 'users.txt').write_text(''.join(name + ':' + sha512_crypt.using(rounds=5000).hash(value) + '\n'
                                                    for name, value in self.passwords.items()))
        self.port = 0
        self.thread = None
        self.error = None

    def start(self):
        self.stop = threading.Event()
        self.ready = threading.Event()
        self.error = None
        self.thread = threading.Thread(target=self._run, name='loopback-broker')
        self.thread.start()
        if not self.ready.wait(10):
            raise TimeoutError('loopback broker startup')
        if self.error:
            raise self.error

    def _run(self):
        async def serve():
            broker = Broker(dict(listeners={'default': dict(type='tcp', bind=f'127.0.0.1:{self.port}',
                ssl=True, certfile=str(self.folder/'server.pem'), keyfile=str(self.folder/'server.key'),
                max_connections=4)}, timeout_disconnect_delay=0, plugins={
                    'amqtt.plugins.authentication.FileAuthPlugin': {'password_file': str(self.folder/'users.txt')},
                    'amqtt.plugins.topic_checking.TopicAccessControlListPlugin': {
                        'acl': {'controller-test': [BASE+'result'], 'fixture-test': [BASE+'request']},
                        'publish_acl': {'controller-test': [BASE+'request'], 'fixture-test': [BASE+'result']}}}))
            await broker.start()
            self.port = broker._servers['default'].instance.sockets[0].getsockname()[1]
            self.ready.set()
            try:
                while not self.stop.is_set():
                    await asyncio.sleep(0.02)
            finally:
                # Simulate broker loss, including peers that are not pumping TLS.
                # These internals belong to the pinned, test-only amqtt version.
                for _, handler in list(broker._sessions.values()):
                    if handler.writer is not None:
                        handler.writer._writer.transport.abort()
                await broker.shutdown()
        try:
            asyncio.run(serve())
        except Exception as error:
            self.error = error
            self.ready.set()

    def close(self):
        if self.thread is not None:
            self.stop.set()
            self.thread.join(10)
            if self.thread.is_alive():
                raise TimeoutError('loopback broker cleanup')
            self.thread = None
            if self.error:
                raise self.error


class DeviceBridge:
    """One thread runs all fixture C calls and its real TLS MQTT connection."""
    def __init__(self, lib, broker):
        self.lib, self.broker = lib, broker
        self.ready, self.stop = threading.Event(), threading.Event()
        self.control = queue.Queue(maxsize=4)
        self.online = False
        self.drop = False
        self.error = None
        self.client = mqtt.Client(client_id='uthome-C-fixture', clean_session=True)
        self.client.username_pw_set('fixture-test', broker.passwords['fixture-test'])
        self.client.tls_set_context(ssl.create_default_context(cafile=str(broker.ca)))
        self.client.on_connect = self._connected
        self.client.on_disconnect = self._disconnected
        self.client.on_subscribe = lambda *args: self.ready.set()
        self.client.on_message = self._message
        self.thread = threading.Thread(target=self._run, name='C-owner-bridge')
        self.thread.start()
        if not self.ready.wait(10):
            raise TimeoutError('C bridge subscription')
        if self.error:
            raise self.error

    def _connected(self, connection, userdata, flags, code):
        if code:
            raise RuntimeError('fixture broker login rejected')
        self.online = True
        connection.subscribe(BASE+'request', qos=0)

    def _disconnected(self, *args):
        self.online = False
        self.ready.clear()

    def _message(self, connection, userdata, message):
        if message.topic != BASE+'request' or message.retain or message.qos or len(message.payload) != 96:
            raise ValueError('unexpected fixture request')
        if self.lib.txptest_send(bytes(message.payload), 96) != 0:
            raise RuntimeError('fixture request queue busy')

    def _run(self):
        try:
            self.client.connect('127.0.0.1', self.broker.port, keepalive=5)
            output = c.create_string_buffer(96)
            now = 0
            while not self.stop.is_set():
                self.client.loop(timeout=0.01)
                now += 1
                self.lib.txptest_pump(now, self.online, True)
                if self.lib.txptest_reply(output) == 0:
                    if self.drop and output.raw[3] == 2:
                        self.drop = False
                    elif self.online:
                        sent = self.client.publish(BASE+'result', output.raw, qos=0, retain=False)
                        if sent.rc:
                            raise ConnectionError('fixture reply queue failed')
                try:
                    action, event, result = self.control.get_nowait()
                except queue.Empty:
                    continue
                if action == 'drop':
                    self.drop = True
                elif action == 'reconnect':
                    self.ready.clear()
                    self.client.reconnect()
                else:
                    result.append(self.lib.txptest_observed(action))
                event.set()
        except Exception as error:
            self.error = error
            self.ready.set()
        finally:
            self.client.disconnect()
            self.client.loop(timeout=0.01)

    def command(self, action):
        if self.error:
            raise self.error
        event, result = threading.Event(), []
        self.control.put((action, event, result), timeout=2)
        if not event.wait(3):
            raise TimeoutError('C fixture control')
        return result[0] if result else None

    def reconnect(self):
        self.command('reconnect')
        if not self.ready.wait(5):
            raise TimeoutError('C bridge resubscription')
        if self.error:
            raise self.error

    def close(self):
        self.stop.set()
        self.thread.join(5)
        if self.thread.is_alive():
            raise TimeoutError('C bridge cleanup')
        if self.error:
            raise self.error
