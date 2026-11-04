"""Compatibility CLI for retained capture scripts; use ai.protocol.capture.

The study implementation lives in ai/protocol/. This entry point preserves
existing local capture commands without importing or opening a serial port.
"""
from ai.protocol.capture import main


if __name__ == "__main__":
    raise SystemExit(main())
