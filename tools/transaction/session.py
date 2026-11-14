"""Run one persistent controller; accept explicit JSON commands on standard input."""
import argparse
import json
from pathlib import Path
import queue
import sqlite3
import sys
import threading

from . import client
from .controller import Controller


def dispatch(controller, command):
    if not isinstance(command, dict) or not isinstance(command.get('action'), str):
        raise ValueError('expected an action object')
    action = command['action']
    if action == 'select':
        if (not {'action', 'path', 'operation'} <= set(command)
                or set(command) - {'action', 'path', 'operation', 'address'}
                or not isinstance(command['path'], str) or not isinstance(command['operation'], str)):
            raise ValueError('select requires path, operation and optional address')
        return controller.select(command['path'], command['operation'], command.get('address'))
    if action == 'select_rule':
        if set(command) != {'action', 'rule'}:
            raise ValueError('select_rule requires one rule object')
        return controller.select_rule(command['rule'])
    operations = {'status': controller.refresh, 'load': controller.load,
                  'execute': controller.execute, 'recover': controller.recover,
                  'reconnect': controller.connect}
    if set(command) != {'action'} or action not in operations:
        raise ValueError('unknown action or unexpected fields')
    return operations[action]()


def run(factory, incoming, outgoing):
    """Input parsing cannot call MQTT; a single worker owns all controller state."""
    commands = queue.Queue(maxsize=8)
    idle, eof = object(), object()
    initialized = threading.Event()
    stop = threading.Event()
    state = {'failed': False, 'fatal': False}

    def emit(value):
        outgoing.write(json.dumps(value, allow_nan=False) + '\n')
        outgoing.flush()

    def owner():
        controller = None
        try:
            controller = factory()
            controller.poll()
            emit(dict(event='started', state=controller.snapshot()))
            initialized.set()
            previous = None
            while not stop.is_set():
                try:
                    command = commands.get(timeout=0.1)
                except queue.Empty:
                    command = idle
                if command is eof:
                    break
                if command is not idle:
                    try:
                        if isinstance(command, Exception):
                            raise command
                        result = dispatch(controller, command)
                        if isinstance(result, dict) and result.get('status', 'ok') != 'ok':
                            state['failed'] = True
                        emit(dict(event='command', action=command['action'], result=result,
                                  state=controller.snapshot()))
                    except (ValueError, OSError, RuntimeError, sqlite3.Error) as error:
                        state['failed'] = True
                        emit(dict(event='error', error=str(error), state=controller.snapshot()))
                view = controller.poll()
                signature = (view['connected'], view['status_fresh'], view['ready'],
                             view['issue'], view['error'], view['display']['available'],
                             view['observed'])
                if signature != previous:
                    emit(dict(event='status', state=view))
                    previous = signature
        except Exception as error:
            state['failed'] = state['fatal'] = True
            emit(dict(event='fatal', error=str(error)))
        finally:
            if controller is not None:
                controller.close()
            initialized.set()

    worker = threading.Thread(target=owner, name='transaction-owner')
    worker.start()
    initialized.wait()
    def enqueue(value):
        while worker.is_alive():
            try:
                commands.put(value, timeout=0.1)
                return True
            except queue.Full:
                pass
        return False
    try:
        if not state['fatal']:
            while not state['fatal']:
                line = incoming.readline(65537)
                if not line:
                    break
                if len(line) > 65536:
                    enqueue(ValueError('command line exceeds 64 KiB; session closed'))
                    state['failed'] = True
                    break
                try:
                    command = json.loads(line, object_pairs_hook=client.unique)
                except ValueError as error:
                    command = error
                if command == {'action': 'quit'}:
                    break
                if not enqueue(command):
                    break
    except KeyboardInterrupt:
        stop.set()
    finally:
        if not stop.is_set():
            enqueue(eof)
        worker.join()
    return 1 if state['failed'] else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--broker', required=True)
    parser.add_argument('--port', type=int, default=8883)
    parser.add_argument('--ca', type=Path, required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--state', type=Path, required=True, help='local persistent journal, normally under build/')
    parser.add_argument('--timeout', type=float, default=10)
    parser.add_argument('--freshness-ms', type=int, default=5000)
    args = parser.parse_args(argv)
    if not 0 < args.timeout <= 30:
        parser.error('--timeout must be greater than zero and at most 30 seconds')
    client.integer(args.port, 1, 65535, 'port')
    def factory():
        return Controller(args.state, args.node,
                          lambda: client.Mqtt(args.broker, args.port, args.ca, args.node, args.timeout),
                          freshness_ms=args.freshness_ms)
    return run(factory, sys.stdin, sys.stdout)


if __name__ == '__main__':
    raise SystemExit(main())
