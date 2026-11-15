"""Host-only ctypes driver for the pinned TLS peer; all C calls are serialized."""
import ctypes as c
import socket
import time


class TLSError(RuntimeError):
    def __init__(self, code, description):
        self.code = code
        super().__init__(description)


class Peer:
    def __init__(self, library):
        self.lib = c.CDLL(str(library))
        signatures = {
            'start': (c.c_int, [c.c_bool, c.c_char_p, c.c_char_p, c.c_char_p, c.c_char_p]),
            'close': (None, []), 'handshake': (c.c_int, []),
            'read': (c.c_int, [c.c_void_p, c.c_size_t]),
            'write': (c.c_int, [c.c_void_p, c.c_size_t]),
            'feed': (c.c_bool, [c.c_void_p, c.c_size_t]),
            'drain': (c.c_size_t, [c.c_void_p, c.c_size_t]),
            'verify': (c.c_uint32, []), 'limit': (c.c_size_t, [c.c_bool]),
            'protocol': (c.c_char_p, []), 'cipher': (c.c_char_p, []),
            'error': (None, [c.c_int, c.c_void_p, c.c_size_t])}
        for name, (result, arguments) in signatures.items():
            function = getattr(self.lib, 'tlspeer_' + name)
            function.restype, function.argtypes = result, arguments
        self.socket = None

    def start(self, connection, folder, *, server=False, name='127.0.0.1', ca=None):
        self.socket = connection
        self.wire = bytearray()
        self.records = []
        path = lambda value: str(value).encode()
        self.check(self.lib.tlspeer_start(server, path(ca or folder/'ca.pem'),
                                         path(folder/'server.pem'), path(folder/'server.key'), name.encode()))

    def check(self, result):
        if result < 0:
            text = c.create_string_buffer(256)
            self.lib.tlspeer_error(result, text, len(text))
            # This SDK aliases these SSL errors to PSA codes. Its legacy
            # strerror formatter interprets them as unrelated combined errors.
            description = {
                -135: 'PSA_ERROR_INVALID_ARGUMENT / MBEDTLS_ERR_SSL_BAD_INPUT_DATA',
                -138: 'PSA_ERROR_BUFFER_TOO_SMALL / MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL'
            }.get(result, text.value.decode())
            raise TLSError(result, description)
        return result

    def call(self, function, *arguments):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            result = function(*arguments)
            outgoing = c.create_string_buffer(32768)
            size = self.lib.tlspeer_drain(outgoing, len(outgoing))
            if size:
                self.socket.settimeout(max(0.01, deadline-time.monotonic()))
                self.socket.sendall(outgoing.raw[:size])
            # Pinned SDK MBEDTLS_ERR_SSL_WANT_READ / WANT_WRITE.
            if result not in (-0x6900, -0x6880):
                return self.check(result)
            if result == -0x6900:
                self.socket.settimeout(max(0.01, deadline-time.monotonic()))
                data = self.socket.recv(16384)
                if not data:
                    raise EOFError('TLS peer closed before completion')
                self.wire.extend(data)
                while len(self.wire) >= 5:
                    size = int.from_bytes(self.wire[3:5], 'big')
                    if len(self.wire) < size + 5:
                        break
                    self.records.append({'type': self.wire[0], 'wire_payload_bytes': size})
                    del self.wire[:size+5]
                assert self.lib.tlspeer_feed(data, len(data)), 'host transport queue overflow'
        raise TimeoutError('TLS peer operation')

    def handshake(self):
        assert self.call(self.lib.tlspeer_handshake) == 0
        protocol = self.lib.tlspeer_protocol().decode()
        cipher = self.lib.tlspeer_cipher().decode()
        assert protocol == 'TLSv1.2'
        assert cipher in ('TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256',
                          'TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256')
        return dict(protocol=protocol, cipher=cipher)

    def read(self, size=128):
        data = c.create_string_buffer(size)
        count = self.call(self.lib.tlspeer_read, data, size)
        if not count:
            raise EOFError('TLS peer returned EOF')
        return data.raw[:count]

    def write(self, data):
        while data:
            # Stable bytes/length throughout WANT_READ/WRITE retries.
            count = self.call(self.lib.tlspeer_write, data, len(data))
            assert count > 0
            data = data[count:]

    def close(self):
        self.lib.tlspeer_close()
        if self.socket is not None:
            self.socket.close()
            self.socket = None


def packet(header, body):
    """Encode a bounded MQTT 3.1.1 test packet, not a production MQTT client."""
    size = len(body)
    assert size < 16384
    encoded = bytearray([header])
    while True:
        encoded.append((size % 128) | (128 if size >= 128 else 0))
        size //= 128
        if not size:
            return bytes(encoded) + body


def text_field(value):
    value = value.encode()
    return len(value).to_bytes(2, 'big') + value


def receive_packet(peer):
    def exact(count):
        result = b''
        while len(result) < count:
            result += peer.read(count-len(result))
        return result
    header = exact(1)[0]
    size = 0
    for index in range(4):
        digit = exact(1)[0]
        size += (digit & 127) << (index*7)
        assert size < 16384
        if digit < 128:
            return header, exact(size)
    raise AssertionError('invalid MQTT remaining length')
