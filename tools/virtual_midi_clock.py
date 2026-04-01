#!/usr/bin/env python3
"""Minimal virtual MIDI clock source for ALSA/JACK/PipeWire MIDI routing.

Creates a virtual MIDI output port and sends MIDI realtime clock ticks (24 PPQN).
Useful when you want exTracker transport sync without external hardware or a DAW.
"""

from __future__ import annotations

import argparse
import signal
import sys
import time


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send MIDI clock from a virtual output port (24 PPQN)."
    )
    parser.add_argument(
        "--bpm",
        type=float,
        default=125.0,
        help="Tempo in BPM (default: 125.0)",
    )
    parser.add_argument(
        "--port-name",
        default="exTracker Virtual Clock",
        help="Virtual MIDI output port name",
    )
    parser.add_argument(
        "--run-seconds",
        type=float,
        default=0.0,
        help="Optional duration in seconds (0 = run until Ctrl+C)",
    )
    parser.add_argument(
        "--no-start",
        action="store_true",
        help="Do not send MIDI Start when clock begins",
    )
    parser.add_argument(
        "--no-stop",
        action="store_true",
        help="Do not send MIDI Stop when clock ends",
    )
    parser.add_argument(
        "--list-outputs",
        action="store_true",
        help="List visible MIDI output ports and exit",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.bpm <= 0.0:
        print("Error: --bpm must be > 0", file=sys.stderr)
        return 2

    try:
        import mido
    except Exception as exc:
        print("Error: missing dependency. Install with:", file=sys.stderr)
        print("  pip install mido python-rtmidi", file=sys.stderr)
        print(f"Details: {exc}", file=sys.stderr)
        return 3

    if args.list_outputs:
        names = mido.get_output_names()
        if not names:
            print("No MIDI outputs visible.")
        else:
            print("Visible MIDI outputs:")
            for name in names:
                print(f"  {name}")
        return 0

    pulse_interval = 60.0 / (args.bpm * 24.0)
    should_stop = False

    def request_stop(_sig: int, _frame: object) -> None:
        nonlocal should_stop
        should_stop = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    with mido.open_output(args.port_name, virtual=True) as port:
        print(f"Opened virtual MIDI clock output: {args.port_name}")
        print(f"Tempo: {args.bpm:.3f} BPM")
        print("Connect it to exTracker input with aconnect.")

        if not args.no_start:
            port.send(mido.Message("start"))
            print("Sent: START")

        started = time.monotonic()
        next_pulse = started

        while not should_stop:
            now = time.monotonic()
            if args.run_seconds > 0.0 and (now - started) >= args.run_seconds:
                break

            if now >= next_pulse:
                port.send(mido.Message("clock"))
                next_pulse += pulse_interval

                # Re-anchor if scheduler lagged heavily to avoid burst catch-up.
                if now - next_pulse > 0.5:
                    next_pulse = now + pulse_interval
            else:
                time.sleep(min(0.001, next_pulse - now))

        if not args.no_stop:
            port.send(mido.Message("stop"))
            print("Sent: STOP")

    print("Virtual MIDI clock stopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
