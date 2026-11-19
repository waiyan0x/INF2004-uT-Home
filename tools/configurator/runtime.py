"""One transaction owner for the local Desk; HTTP handlers only queue actions."""
from copy import deepcopy
from pathlib import Path
import queue
import sqlite3
import threading
import time

from tools.transaction import description
from tools.transaction.homeassistant import project

DESCRIPTIONS = Path(__file__).resolve().parents[2] / 'datasheets'


def catalogue():
    """Compile reviewed choices without a controller, MQTT or peripheral I/O."""
    items = []
    for path in sorted(DESCRIPTIONS.glob('*.md')):
        if path.name == 'README.md':
            continue
        document, digest = description.load_description(path)
        operations = []
        for name, operation in document['operations'].items():
            rules, reason = {}, operation.get('reason', '')
            if operation['kind'] != 'unsupported':
                for address in document['addresses'] or [None]:
                    key = '' if address is None else str(address)
                    rules[key] = description.compile_rule(document, name, 1, address)
            operations.append(dict(name=name, notes=operation['notes'], pages=operation['pages'],
                                   rules=rules, reason=reason))
        items.append(dict(id=path.name, device=document['device'], sha256=digest,
                          source=document['source'], addresses=document['addresses'], operations=operations))
    return items


def selected_path(command):
    # The browser selects a repository entry, never an arbitrary local path.
    paths = {path.name: path for path in DESCRIPTIONS.glob('*.md') if path.name != 'README.md'}
    if command['description'] not in paths:
        raise ValueError('Choose a reviewed description')
    path = paths[command['description']]
    document, digest = description.load_description(path)
    if digest != command['sha256']:
        raise ValueError('Description changed; reload the page and review it again')
    if document['addresses'] and command['address'] is None:
        raise ValueError('Choose a documented I2C address')
    description.compile_rule(document, command['operation'], 1, command['address'])
    return path


class Runtime:
    def __init__(self, factory=None, display_factory=None, *, target=None):
        self.factory, self.display_factory = factory, display_factory
        self.target = deepcopy(target)
        self.commands = queue.Queue(maxsize=1)
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.busy = self.enabled = False
        self.error = None
        self.view = None
        self.sampled = time.monotonic()
        self.ha = dict(enabled=display_factory is not None, connected=False, error=None)
        self.thread = None
        if factory is not None:
            self.thread = threading.Thread(target=self._run, name='desk-transaction-owner')
            self.thread.start()

    def snapshot(self):
        with self.lock:
            view = deepcopy(self.view)
            elapsed = int((time.monotonic() - self.sampled) * 1000)
            result = dict(configured=self.factory is not None, target=deepcopy(self.target),
                          enabled=self.enabled, busy=self.busy, error=self.error,
                          home_assistant=deepcopy(self.ha))
        if view is not None:
            data = view['display']
            if data['age_ms'] is not None:
                data['age_ms'] += elapsed
                if data['age_ms'] >= 5000:
                    data['available'], data['raw_hex'] = False, None
            # A blocked query must not keep an old copied snapshot live.
            if elapsed >= 2000:
                view['status_fresh'] = view['ready'] = False
                data['available'], data['raw_hex'] = False, None
                result['home_assistant']['connected'] = False
            if result['busy'] or not view['status_fresh']:
                view['can_load'] = view['can_execute'] = False
            result['display'] = project(view, invalidate=result['busy'])
        else:
            result['display'] = None
        result['state'] = view
        return result

    def dispatch(self, command):
        if not isinstance(command, dict):
            raise ValueError('Expected a runtime action')
        action = command.get('action')
        if action == 'snapshot' and set(command) == {'action'}:
            return self.snapshot()
        if action == 'load':
            if (set(command) != {'action', 'description', 'operation', 'address', 'sha256'}
                    or not all(isinstance(command[key], str) for key in ('description', 'operation', 'sha256'))
                    or (command['address'] is not None and type(command['address']) is not int)):
                raise ValueError('Load requires a reviewed description, operation, address and hash')
        elif action not in ('connect', 'disconnect', 'execute', 'status', 'recover') or set(command) != {'action'}:
            raise ValueError('Unknown runtime action or unexpected fields')
        with self.lock:
            if self.thread is None:
                raise ValueError('Start the Desk with MQTT connection settings first')
            if self.stop.is_set() or not self.thread.is_alive():
                raise ValueError('Runtime stopped; restart the Desk')
            if self.busy:
                raise ValueError('Another runtime action is pending')
            self.busy, self.error = True, None
            self.commands.put_nowait(deepcopy(command))
        return self.snapshot()

    def _run(self):
        controller = display = None
        enabled = False
        try:
            while not self.stop.is_set():
                try:
                    command = self.commands.get(timeout=0.1)
                except queue.Empty:
                    command = None
                try:
                    if command:
                        action = command['action']
                        if action == 'connect':
                            if controller is None:
                                controller = self.factory()
                            if display is None and self.display_factory:
                                display = self.display_factory()
                            enabled = True
                            controller.connect()
                        elif action == 'disconnect':
                            enabled = False
                            if display is not None:
                                display.close()
                                display = None
                            if controller is not None:
                                controller.close()
                                with self.lock:
                                    self.view = controller.snapshot()
                                controller = None
                        else:
                            if controller is None or not enabled:
                                raise ValueError('Connect to the runtime session first')
                            if action in ('load', 'execute', 'recover') and display is not None:
                                display.poll(controller.snapshot(), invalidate=True)
                            if action == 'load':
                                path = selected_path(command)
                                controller.select(path, command['operation'], command['address'])
                                controller.load()
                            else:
                                getattr(controller, {'execute': 'execute', 'status': 'refresh',
                                                     'recover': 'recover'}[action])()
                    if enabled:
                        view = controller.poll()
                        if display is not None:
                            display.poll(view)
                except (OSError, RuntimeError, ValueError, sqlite3.Error) as error:
                    with self.lock:
                        self.error = str(error)
                finally:
                    with self.lock:
                        if controller is not None:
                            self.view = controller.snapshot()
                        self.sampled = time.monotonic()
                        self.enabled = enabled
                        self.ha = dict(enabled=self.display_factory is not None,
                                       **(display.status() if display else dict(connected=False, error=None)))
                        if command:
                            self.busy = False
        finally:
            try:
                if display is not None:
                    display.close()
            finally:
                if controller is not None:
                    controller.close()
                with self.lock:
                    self.view = None
                    self.enabled = self.busy = False
                    self.ha['connected'] = False

    def close(self):
        self.stop.set()
        if self.thread is not None:
            self.thread.join()
