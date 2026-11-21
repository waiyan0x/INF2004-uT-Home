"""GDB observers for USB parser release and the TLS X25519 generation call."""
import json
from pathlib import Path
import time

import gdb


def install(output):
    """Record ownership metadata to a fresh file on the qualified ESP image."""
    log = Path(output).open('x', encoding='utf-8')

    class OwnerEvent(gdb.Breakpoint):
        def __init__(self, symbol):
            super().__init__('*' + symbol, type=gdb.BP_HARDWARE_BREAKPOINT, internal=True)
            self.symbol = symbol

        def stop(self):
            def number(expression):
                return int(gdb.parse_and_eval(expression))

            row = dict(event=self.symbol, monotonic_ns=time.monotonic_ns(),
                       task=number('((struct task_control_block *)knl_ctxtsk)->tskid'),
                       mqtt=number("'tlsheap.c'::gh_owner"),
                       usb=number("'c3peer.c'::gb_views"),
                       crypto_owner=number("'c3peer.c'::gh_crypto"),
                       tls_stage=number("'tlsheap.c'::g_stage"),
                       parser_bytes=number("'cfgjson.c'::g_peak"))
            if number("'c3mqtt.c'::gp_tls"):
                state = number("'c3mqtt.c'::gp_tls->ssl.state")
                states = gdb.lookup_type('mbedtls_ssl_states').fields()
                row['state'] = next(field.name for field in states if field.enumval == state)
            log.write(json.dumps(row) + '\n')
            log.flush()
            return False

    return [OwnerEvent(symbol) for symbol in ('c3peer_close', 'c3peer_generate')]
