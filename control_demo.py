#!/usr/bin/env python3
"""
control_demo.py

Simple DMX control demo for the Raspberry Pi Pico command protocol.

Usage:
    python control_demo.py
    python control_demo.py COM5
    python control_demo.py /dev/ttyACM0

If no port is supplied, the program scans available serial ports and selects
the first device that replies "OK" to a machine-mode "$" probe command.

Requires:
    pip install pyserial
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import Iterable

import serial
from serial import Serial
from serial.tools import list_ports


BAUD_RATE = 115200
READ_TIMEOUT_SECONDS = 1.0
WRITE_TIMEOUT_SECONDS = 1.0
PROBE_SETTLE_SECONDS = 0.25
COLOR_HOLD_SECONDS = 0.7

# RGB values sent to DMX channels 1, 2, and 3.
RAINBOW_COLORS: tuple[tuple[str, tuple[int, int, int]], ...] = (
    ("deep red",      (255,   0,   0)),
    ("warm red",      (255,  32,   0)),
    ("orange",        (255,  96,   0)),
    ("amber",         (255, 160,   0)),
    ("yellow",        (255, 255,   0)),
    ("yellow-green",  (128, 255,   0)),
    ("green",         (  0, 255,   0)),
    ("cyan",          (  0, 255, 255)),
    ("blue",          (  0,   0, 255)),
    ("indigo",        ( 75,   0, 130)),
    ("purple",        (160,   0, 255)),
    ("magenta",       (255,   0, 255)),
)


def open_port(port_name: str) -> Serial:
    """Open and configure a serial port for the Pico command interface."""
    return serial.Serial(
        port=port_name,
        baudrate=BAUD_RATE,
        timeout=READ_TIMEOUT_SECONDS,
        write_timeout=WRITE_TIMEOUT_SECONDS,
    )


def send_command(port: Serial, command: str) -> str:
    """
    Send one machine-mode command and return its single-line response.

    The supplied command should not include the leading '$' or line ending.
    """
    packet = f"${command}\r".encode("ascii")
    port.write(packet)
    port.flush()

    response = port.readline().decode("ascii", errors="replace").strip()

    if not response:
        raise TimeoutError("No response received from Pico")

    return response


def probe_port(port_name: str) -> Serial | None:
    """
    Try one serial port.

    Returns an open Serial object if the device replies 'OK' to '$'.
    Returns None if the port cannot be opened or does not reply correctly.
    """
    port: Serial | None = None

    try:
        port = open_port(port_name)

        # Opening a USB CDC port may expose old banner or prompt text that was
        # transmitted before the Python program connected.
        time.sleep(PROBE_SETTLE_SECONDS)
        port.reset_input_buffer()

        response = send_command(port, "")
        if response == "OK":
            return port

    except (serial.SerialException, TimeoutError, OSError, UnicodeError):
        pass

    if port is not None and port.is_open:
        port.close()

    return None


def find_port() -> Serial:
    """
    Scan all detected serial ports and return the first Pico that replies 'OK'.

    Raises RuntimeError if no compatible device is found.
    """
    ports = list(list_ports.comports())

    if not ports:
        raise RuntimeError("No serial ports were detected")

    print("Searching for the Pico DMX controller...")

    for port_info in ports:
        print(f"  Trying {port_info.device}...", end="", flush=True)
        port = probe_port(port_info.device)

        if port is not None:
            print(" found")
            return port

        print(" no response")

    print("Check the device is connected and that no other program is using the serial port.")
    raise RuntimeError("No Pico DMX controller was found")


def require_ok(port: Serial, command: str) -> None:
    """Send a command and raise an exception unless the response is exactly OK."""
    response = send_command(port, command)

    if response != "OK":
        raise RuntimeError(f"Command {command!r} failed: {response}")


def set_rgb(port: Serial, red: int, green: int, blue: int) -> None:
    """Set DMX channels 1, 2, and 3 to one RGB colour."""
    require_ok(port, f"set_chan 1 {red}")
    require_ok(port, f"set_chan 2 {green}")
    require_ok(port, f"set_chan 3 {blue}")


def run_demo(port: Serial, colors: Iterable[tuple[str, tuple[int, int, int]]]) -> None:
    """Continuously cycle through the supplied RGB colours."""
    print(f"Using serial port: {port.port}")
    print("Cycling channels 1-3 through rainbow colours.")
    print("Press Ctrl+C to stop.")

    while True:
        for name, (red, green, blue) in colors:
            set_rgb(port, red, green, blue)
            print(f"{name:13s}  R={red:3d} G={green:3d} B={blue:3d}")
            time.sleep(COLOR_HOLD_SECONDS)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Control a Pico DMX transmitter over USB serial."
    )
    parser.add_argument(
        "port",
        nargs="?",
        help="Serial port, for example COM5 or /dev/ttyACM0. "
             "If omitted, all detected ports are probed.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    port: Serial | None = None

    try:
        if args.port:
            print(f"Opening {args.port}...")
            port = probe_port(args.port)

            if port is None:
                raise RuntimeError(
                    f"{args.port} did not respond with OK to the '$' probe"
                )
        else:
            port = find_port()

        run_demo(port, RAINBOW_COLORS)

    except KeyboardInterrupt:
        print("\nStopping demo...")

        if port is not None and port.is_open:
            try:
                set_rgb(port, 0, 0, 0)
                print("Channels 1-3 set to zero.")
            except Exception as exc:
                print(f"Could not send blackout command: {exc}", file=sys.stderr)

    except (RuntimeError, serial.SerialException, OSError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    finally:
        if port is not None and port.is_open:
            port.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
