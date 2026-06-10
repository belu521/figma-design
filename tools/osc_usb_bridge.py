#!/usr/bin/env python3
"""OSC-over-USB bridge for the grandMA3 controller.

Listens for OSC datagrams over UDP (sent by ma3_plugin/ColorFeedback.lua via
the MA3 OSC output pointed at 127.0.0.1) and forwards each datagram to the
ESP32-S3 over its native USB CDC serial port, framed with SLIP (RFC 1055,
the OSC 1.1 serial transport). No WiFi required.

Usage:
    python osc_usb_bridge.py --serial-port COM5
    python osc_usb_bridge.py --serial-port /dev/ttyACM0 --listen-port 8000

Requires: pyserial (pip install pyserial)

Run `python -m serial.tools.list_ports -v` to find the ESP32-S3 port.
The bridge reconnects automatically if the serial port disappears.
"""

import argparse
import socket
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

SLIP_END = b"\xc0"
SLIP_ESC = b"\xdb"
SLIP_ESC_END = b"\xdb\xdc"
SLIP_ESC_ESC = b"\xdb\xdd"


def slip_encode(packet: bytes) -> bytes:
    """Encode one packet as a SLIP frame (END-terminated, END-prefixed)."""
    body = packet.replace(SLIP_ESC, SLIP_ESC_ESC).replace(SLIP_END, SLIP_ESC_END)
    return SLIP_END + body + SLIP_END


def open_serial(port: str, baud: int) -> serial.Serial:
    while True:
        try:
            connection = serial.Serial(port, baud, timeout=0, write_timeout=1)
            print(f"[bridge] serial connected: {port} @ {baud}")
            return connection
        except serial.SerialException as exc:
            print(f"[bridge] serial open failed ({exc}); retrying in 2s...")
            time.sleep(2)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Forward UDP OSC datagrams to an ESP32-S3 as SLIP frames over USB serial."
    )
    parser.add_argument(
        "--serial-port",
        required=True,
        help="ESP32-S3 USB CDC serial port (e.g. COM5 or /dev/ttyACM0)",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="serial baud rate (default: 115200, must match OSC_USB_BAUD in the firmware)",
    )
    parser.add_argument(
        "--listen-host",
        default="127.0.0.1",
        help="UDP listen address (default: 127.0.0.1)",
    )
    parser.add_argument(
        "--listen-port",
        type=int,
        default=8000,
        help="UDP listen port for OSC from MA3 (default: 8000)",
    )
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.listen_host, args.listen_port))
    print(f"[bridge] listening for OSC on udp://{args.listen_host}:{args.listen_port}")

    connection = open_serial(args.serial_port, args.baud)
    try:
        while True:
            packet, _ = sock.recvfrom(2048)
            if not packet:
                continue
            frame = slip_encode(packet)
            while True:
                try:
                    connection.write(frame)
                    break
                except serial.SerialException as exc:
                    print(f"[bridge] serial write failed ({exc}); reconnecting...")
                    try:
                        connection.close()
                    except serial.SerialException:
                        pass
                    connection = open_serial(args.serial_port, args.baud)
    except KeyboardInterrupt:
        print("\n[bridge] stopped")
    finally:
        connection.close()
        sock.close()


if __name__ == "__main__":
    main()
