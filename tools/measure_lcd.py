#!/usr/bin/env python3
"""Capture a bounded LCD performance run using PlatformIO's pyserial install."""
import argparse
from pathlib import Path
import time

import serial

parser = argparse.ArgumentParser()
parser.add_argument("--port", default="/dev/cu.usbmodem1101")
parser.add_argument("--seconds", type=float, default=45)
parser.add_argument("--log", type=Path, required=True)
args = parser.parse_args()

device = serial.Serial()
device.port = args.port
device.baudrate = 115200
device.timeout = 0.25
device.dtr = False
device.rts = False
with device, args.log.open("wb") as log:
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        data = device.readline()
        if not data:
            continue
        log.write(data)
        log.flush()
        text = data.decode(errors="replace")
        if "lcd:" in text or "health:" in text or "Error" in text or "panic" in text:
            print(text, end="", flush=True)
print(f"Serial log: {args.log}")
