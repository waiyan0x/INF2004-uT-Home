"""One durable command owner shared by future UI and display integrations.

    Connection recovery queries status only. Recovering a cached completion is
    an explicit operation and is allowed only after status proves that exact ID
    completed in the same boot and generation. An unseen write is never resent.
"""
from copy import deepcopy
import re
import secrets
import sqlite3
import time

from . import client, description
from .journal import Journal

MAX_ID = 0xffffffff
SETTINGS = ('address', 'mode', 'dc_pin', 'dc_level', 'cs_pin', 'rate', 'timeout_ms')


class StateError(RuntimeError):
    """An explicit operation is unavailable in the observed session."""


def _packet(command):
    return client.request(command['rule'], command['id'], command['generation'],
                          command['boot_id'], execute=command['kind'] == 'execute')


def _reply(command, wire):
    rule = command['rule']
    result = client.result(wire, request_id=command['id'], boot_id=command['boot_id'],
                           generation=command['generation'], device=rule['device'],
                           revision=rule['revision'], kind=1 if command['kind'] == 'update' else 2)
    if result['status'] == 'ok':
        if result['protocol'] != client.PROTOCOLS[rule['protocol']] or any(result[k] != rule[k] for k in SETTINGS):
            raise ValueError('response selected a different transaction')
        if command['kind'] == 'execute' and (result['tx_accepted'] > len(bytes.fromhex(rule['write_hex']))
                                            or result['rx_received'] != rule['read_length']):
            raise ValueError('response counts do not match the requested transfer')
    return result


def _command_valid(command):
    if not isinstance(command, dict) or set(command) != {'id', 'boot_id', 'generation', 'kind', 'rule', 'packet'}:
        raise ValueError('invalid journal command')
    if command['kind'] not in ('update', 'execute') or _packet(command).hex() != command['packet']:
        raise ValueError('journal packet does not match its command')


def _state_valid(state, node):
    fields = {'version', 'node', 'boot_id', 'generation', 'high_id', 'seen_id',
              'revision', 'selected', 'loaded', 'pending', 'last'}
    if not isinstance(state, dict) or set(state) != fields or state['node'] != node:
        raise ValueError('journal version or node does not match')
    client.integer(state['version'], 1, 1, 'journal version')
    client.integer(state['boot_id'], 0, 0xffffffffffffffff, 'journal boot')
    for key in ('generation', 'high_id', 'seen_id', 'revision'):
        client.integer(state[key], 0, MAX_ID, 'journal ' + key)
    if state['seen_id'] > state['high_id']:
        raise ValueError('invalid journal command history')
    for key in ('selected', 'loaded'):
        if state[key] is not None:
            if not isinstance(state[key], dict) or set(state[key]) != {'rule', 'source'}:
                raise ValueError('invalid journal rule selection')
            client.validate(state[key]['rule'])
            if not isinstance(state[key]['source'], dict):
                raise ValueError('invalid journal provenance')
    pending = state['pending']
    if pending is not None:
        _command_valid(pending)
        if (pending['id'] != state['high_id'] or pending['boot_id'] != state['boot_id']
                or pending['generation'] != state['generation']):
            raise ValueError('pending command does not match journal session')
    last = state['last']
    if last is not None:
        if not isinstance(last, dict) or set(last) != {'command', 'reply', 'reason'}:
            raise ValueError('invalid journal outcome')
        _command_valid(last['command'])
        if not isinstance(last['reason'], str):
            raise ValueError('invalid journal outcome reason')
        if last['reply'] is not None:
            _reply(last['command'], bytes.fromhex(last['reply']))
    if pending is not None and (last is None or last['command'] != pending or last['reply'] is not None):
        raise ValueError('pending command has an inconsistent outcome')


class Controller:
    """Call from one serialized owner. Only load/execute/recover send commands."""

    def __init__(self, state_path, node, connector, *, freshness_ms=5000, clock=time.monotonic):
        if not re.fullmatch(r'uthome-[0-9a-f]{12}', node):
            raise ValueError('invalid node identity')
        client.integer(freshness_ms, 100, 60000, 'freshness_ms')
        self.clock, self.connector, self.freshness_ms = clock, connector, freshness_ms
        self.link = None
        self.observed = None
        self.observed_at = 0
        self.query_started = 0
        self.next_connect = 0
        self.backoff = 1
        self.nonce = secrets.randbelow(MAX_ID) + 1
        self.issue = None
        self.error = None
        self.fault = False
        self.journal = Journal(state_path)
        try:
            self.state = self.journal.read()
            if self.state is None:
                self.state = dict(version=1, node=node, boot_id=0, generation=0,
                                  high_id=0, seen_id=0, revision=0, selected=None,
                                  loaded=None, pending=None, last=None)
                self.journal.write(self.state)
            _state_valid(self.state, node)
        except Exception:
            self.journal.close()
            raise

    def _save(self, state):
        if self.fault:
            raise StateError('journal unavailable; close and reopen the controller')
        if state != self.state:
            try:
                self.journal.write(state)
            except (OSError, sqlite3.Error):
                self.fault = True
                self._disconnect('journal unavailable; no further commands can be sent')
                raise
            self.state = state

    def _disconnect(self, error=None):
        link, self.link = self.link, None
        self.observed = None
        self.error = error
        self.next_connect = self.clock() + self.backoff
        self.backoff = min(30, self.backoff * 2)
        if link is not None:
            try:
                link.close()
            except OSError:
                pass  # Losing transport cleanup cannot erase the durable command.

    def connect(self):
        if self.fault:
            raise StateError('journal unavailable; reopen the controller')
        self._disconnect()
        try:
            self.link = self.connector()
            self.refresh()
            self.backoff = 1
            return self.snapshot()
        except (OSError, RuntimeError, ValueError):
            self._disconnect('connection unavailable; pending outcome is unchanged')
            raise

    def refresh(self):
        """Observe a session without replaying a command or adopting its rule."""
        if self.link is None:
            raise StateError('connect before querying status')
        self.observed = None
        self.query_started = self.clock()
        self.nonce = self.nonce % MAX_ID + 1
        try:
            status = client.status_result(self.link.exchange(client.query(self.nonce)), request_id=self.nonce)
        except (OSError, RuntimeError, ValueError):
            self._disconnect('status unavailable')
            raise
        state = deepcopy(self.state)
        self.issue = None
        if status['boot_id'] != state['boot_id']:
            if status['last_request_id'] or status['loaded'] or status['busy']:
                self.issue = 'unowned_session: retain its journal or establish a fresh ESP boot'
            else:
                if state['pending'] is not None:
                    state['last']['reason'] = 'ESP restarted; previous outcome remains unknown'
                state.update(boot_id=status['boot_id'], generation=status['generation'],
                             high_id=0, seen_id=0, revision=0, loaded=None, pending=None)
        elif status['last_request_id'] > state['high_id']:
            self.issue = 'foreign_command: another command owner used this session'
        elif status['last_request_id'] < state['seen_id']:
            self.issue = 'history_regressed: establish a fresh ESP boot'
        else:
            state['seen_id'] = status['last_request_id']
            if status['generation'] != state['generation']:
                if state['pending'] is not None:
                    state['last']['reason'] = 'plan changed; previous outcome remains unknown'
                state.update(generation=status['generation'], loaded=None, pending=None)
        if self.issue is None and state['pending'] is None:
            if not status['loaded']:
                state['loaded'] = None
            elif state['loaded'] is None or not self._active_matches(state['loaded']['rule'], status):
                self.issue = 'unknown_active_rule: its accepted bytes are not in this journal'
        self._save(state)
        self.observed, self.observed_at = status, self.clock()
        self.error = None
        return self.snapshot()

    @staticmethod
    def _active_matches(rule, status):
        return (status['device'] == rule['device'] and status['revision'] == rule['revision']
                and status['protocol'] == client.PROTOCOLS[rule['protocol']]
                and status['write_length'] == len(bytes.fromhex(rule['write_hex']))
                and status['read_length'] == rule['read_length']
                and all(status[key] == rule[key] for key in SETTINGS))

    def select(self, path, operation, address=None):
        obj, digest = description.load_description(path)
        rule = description.compile_rule(obj, operation, self.state['revision'] + 1, address)
        return self.select_rule(rule, source=dict(description_sha256=digest, operation=operation,
                                                 source=deepcopy(obj['source']),
                                                 pages=obj['operations'][operation]['pages'],
                                                 source_pdf_verified=False))

    def select_rule(self, rule, *, source=None):
        """Preview only. Execution always names the separately acknowledged active rule."""
        rule = deepcopy(client.validate(rule))
        rule['write_hex'] = bytes.fromhex(rule['write_hex']).hex(' ').upper()
        rule['revision'] = client.integer(self.state['revision'] + 1, 1, MAX_ID, 'next revision')
        state = deepcopy(self.state)
        state['selected'] = dict(rule=rule, source=deepcopy(source or {}))
        self._save(state)
        return self.snapshot()

    def _ready(self):
        self.refresh()
        if not self.snapshot()['ready']:
            raise StateError(self.issue or 'idle plan, known session and resolved outcome required')
        client.integer(self.state['high_id'] + 1, 1, MAX_ID, 'next command ID')

    def load(self):
        self._ready()
        if self.state['selected'] is None:
            raise StateError('select and preview a rule first')
        selection = deepcopy(self.state['selected'])
        selection['rule']['revision'] = client.integer(self.state['revision'] + 1, 1, MAX_ID, 'next revision')
        return self._send(selection, 'update')

    def execute(self):
        self._ready()
        if self.state['loaded'] is None:
            raise StateError('successfully load a rule before execution')
        return self._send(deepcopy(self.state['loaded']), 'execute')

    def _send(self, selection, kind):
        state = deepcopy(self.state)
        command = dict(id=state['high_id'] + 1, boot_id=state['boot_id'],
                       generation=state['generation'], kind=kind, rule=selection['rule'])
        command['packet'] = _packet(command).hex()
        state['high_id'] = command['id']
        if kind == 'update':
            state['revision'] = selection['rule']['revision']
            state['selected'] = selection
        state['pending'] = command
        state['last'] = dict(command=command, reply=None, reason='outcome unknown until a matching reply is saved')
        self._save(state)  # Must finish before publication, even for an update.
        self.observed = None
        try:
            wire = self.link.exchange(bytes.fromhex(command['packet']))
            return self._finish(command, wire)
        except (OSError, RuntimeError, ValueError):
            self._disconnect('command outcome unknown; use status and explicit recovery')
            raise

    def _finish(self, command, wire):
        answer = _reply(command, wire)
        state = deepcopy(self.state)
        state['pending'] = None
        state['last'] = dict(command=command, reply=wire.hex(), reason='')
        if answer['generation'] != state['generation']:
            state['loaded'] = None
        elif command['kind'] == 'update' and answer['status'] == 'ok':
            source = state['selected']['source'] if state['selected'] and state['selected']['rule'] == command['rule'] else {}
            state['loaded'] = dict(rule=deepcopy(command['rule']), source=deepcopy(source))
        self._save(state)
        return deepcopy(answer)

    def recover(self):
        """Fetch only a proven cached completion. Never resend unseen/pending work."""
        self.refresh()
        command = self.state['pending']
        if command is None:
            return self.snapshot()
        status = self.observed
        if (self.issue or status['busy'] or status['last_completed_id'] != command['id']
                or status['last_kind'] != command['kind']
                or status['last_generation'] != command['generation']):
            raise StateError('completion is not recoverable; outcome remains unknown')
        try:
            wire = self.link.exchange(bytes.fromhex(command['packet']))
            answer = _reply(command, wire)
            if (answer['generation'] != command['generation'] or answer['status'] != status['last_status']
                    or answer['completed_ms'] != status['last_completed_ms']):
                raise ValueError('completion changed during recovery; outcome remains unknown')
            return self._finish(command, wire)
        except (OSError, RuntimeError, ValueError):
            self._disconnect('cached completion unavailable; outcome remains unknown')
            raise

    def poll(self):
        """Maintain transport and observe status; automatic reconnect never executes."""
        if self.fault:
            return self.snapshot()
        try:
            if self.link is None:
                if self.clock() >= self.next_connect:
                    self.connect()
            else:
                self.link.poll()
                if self.observed is None or self.clock() - self.observed_at >= 1:
                    self.refresh()
        except (OSError, RuntimeError, ValueError):
            if self.link is not None:
                self._disconnect('connection unavailable')
        return self.snapshot()

    def snapshot(self):
        state = self.state
        recent = (self.observed is not None and self.link is not None
                  and 0 <= (self.clock() - self.query_started) * 1000 < self.freshness_ms)
        ready = bool(recent and not self.fault and not self.issue
                     and self.observed['ready'] and state['pending'] is None)
        last = state['last']
        outcome = None
        if last is not None:
            outcome = dict(outcome='completed' if last['reply'] is not None else 'unknown',
                           reason=last['reason'], command=deepcopy(last['command']), result=None)
            if last['reply'] is not None:
                outcome['result'] = _reply(last['command'], bytes.fromhex(last['reply']))
        raw, age = None, None
        if recent and not self.issue and outcome and outcome['result']:
            result = outcome['result']
            if (result['kind'] == 'execute' and result['status'] == 'ok'
                    and result['boot_id'] == self.observed['boot_id']
                    and result['generation'] == self.observed['generation']
                    and self.observed['last_completed_id'] == result['request_id']):
                age = self.observed['now_ms'] - result['completed_ms']
                if age >= 0:
                    # Query round-trip time is included: conservatively overestimate age.
                    age += int((self.clock() - self.query_started) * 1000)
                    if age < self.freshness_ms:
                        raw = result['raw_hex']
        return dict(node=state['node'], connected=self.link is not None, status_fresh=recent,
                    ready=ready, can_load=ready and state['selected'] is not None and state['high_id'] < MAX_ID and state['revision'] < MAX_ID,
                    can_execute=ready and state['loaded'] is not None and state['high_id'] < MAX_ID,
                    issue=self.issue, error=self.error, journal_fault=self.fault,
                    observed=deepcopy(self.observed), selected=deepcopy(state['selected']),
                    active=deepcopy(state['loaded']), pending=deepcopy(state['pending']),
                    last=outcome, display=dict(available=raw is not None, raw_hex=raw, age_ms=age))

    def close(self):
        try:
            self._disconnect()
        finally:
            self.journal.close()
