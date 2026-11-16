"""Run the local configurator; connecting and deploying are explicit UI actions."""
import argparse
from pathlib import Path
from .web import serve
from .runtime import Runtime

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8080, help="Local HTTP port")
    parser.add_argument('--broker', help='Enable runtime transactions for this MQTT broker')
    parser.add_argument('--mqtt-port', type=int, default=8883)
    parser.add_argument('--ca', type=Path)
    parser.add_argument('--node')
    parser.add_argument('--state', type=Path, help='Persistent per-node transaction journal')
    parser.add_argument('--home-assistant', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    if not 1 <= args.mqtt_port <= 65535:
        parser.error('mqtt-port must be between 1 and 65535')
    runtime = None
    if any((args.broker, args.ca, args.node, args.state, args.home_assistant)):
        if not all((args.broker, args.ca, args.node, args.state)):
            parser.error('Runtime transactions require --broker, --ca, --node and --state')
        from tools.transaction.client import Mqtt
        from tools.transaction.controller import Controller
        from tools.transaction.homeassistant import Display
        runtime = Runtime(lambda: Controller(args.state, args.node,
            lambda: Mqtt(args.broker, args.mqtt_port, args.ca, args.node)),
            (lambda: Display(args.broker, args.mqtt_port, args.ca, args.node)) if args.home_assistant else None,
            target=dict(node=args.node, broker=args.broker, port=args.mqtt_port))
    try:
        serve(args.port, runtime=runtime)
    finally:
        if runtime is not None:
            runtime.close()

if __name__ == "__main__":
    main()
