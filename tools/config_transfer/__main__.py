"""Explicit USB configuration commands. No board reset, flash or automatic retry."""
import argparse
from dataclasses import asdict
import json
from pathlib import Path
import sys

from .client import Client, TransferError
from .protocol import Command, MAX_PROFILE_BYTES, encode_request


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("probe", "query", "apply", "save", "recover", "telemetry", "capabilities"))
    parser.add_argument("--port", required=True, help="Explicit serial port, for example COM8")
    parser.add_argument("--revision", type=int, default=0)
    parser.add_argument("--profile", type=Path, help="Reviewed node JSON for apply")
    args = parser.parse_args(argv)
    command = Command[args.command.upper()]
    stream = None
    try:
        if (command == Command.APPLY) != (args.profile is not None):
            raise ValueError("only apply requires --profile")
        profile = b""
        if args.profile is not None:
            with args.profile.open("rb") as source:
                profile = source.read(MAX_PROFILE_BYTES + 1)
        # Validate command bounds before even importing/opening the serial port.
        encode_request(command, 1, args.revision, profile)
        import serial
        stream = serial.Serial(port=None, baudrate=115200, timeout=0.05,
                               write_timeout=0.2, rtscts=False, dsrdtr=False)
        stream.dtr = False
        stream.rts = False
        stream.port = args.port
        stream.open()
        client = Client(stream)
        if command == Command.CAPABILITIES:
            reply = client.capabilities()
        elif command == Command.TELEMETRY:
            reply = client.telemetry()
        elif command == Command.PROBE:
            reply = client.exchange(command, 1)
        else:
            reply = client.query()
            if command != Command.QUERY:
                if reply.request_id == 0xFFFFFFFF:
                    raise TransferError("request IDs exhausted; no mutation sent")
                reply = client.exchange(command, reply.request_id + 1,
                                        args.revision, profile)
        print(json.dumps(asdict(reply), indent=2))
        if command == Command.TELEMETRY:
            return 0 if reply.result == 0 else 2
        return 0 if command in (Command.PROBE, Command.CAPABILITIES) or reply.status in (0, 1, 8) else 2
    except (ImportError, OSError, ValueError, TransferError) as error:
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if stream is not None:
            stream.close()


if __name__ == "__main__":
    raise SystemExit(main())

