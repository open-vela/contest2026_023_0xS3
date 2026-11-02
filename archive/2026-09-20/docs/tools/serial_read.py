#!/usr/bin/env python3
"""Read raw bytes from the ESP32-S3 console for a duration, no input sent.

Use to capture boot output, panics, or long-running command output.

Usage:
    python3 serial_read.py [duration_seconds]
    PORT=/dev/ttyACM0 python3 serial_read.py 15
"""
import os, sys, termios, time, select

PORT = os.environ.get("PORT", "/dev/ttyACM0")
DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0

fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP |
          termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
a[1] &= ~termios.OPOST
a[2] &= ~(termios.CSIZE | termios.PARENB)
a[2] |= termios.CS8
a[3] &= ~(termios.ICANON | termios.ECHO | termios.ECHOE | termios.ISIG)
a[6][termios.VMIN] = 0
a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
os.set_blocking(fd, False)

out = bytearray()
t0 = time.time()
while time.time() - t0 < DURATION:
    r, _, _ = select.select([fd], [], [], 0.3)
    if r:
        try:
            d = os.read(fd, 4096)
            if d:
                out += d
        except OSError:
            pass
sys.stdout.write(out.decode(errors="replace"))
