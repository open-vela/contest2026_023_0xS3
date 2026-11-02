#!/usr/bin/env python3
"""Raw TTY helper for ESP32-S3 NSH/ai_agent console — SLOW/char-by-char send.

MUST be used for long commands that contain an API key or other long token:
a single-shot write over the native USB CDC serial drops trailing characters
(e.g. the last 4 chars of a 51-char MiMo key), corrupting the command.

Usage:
    python3 send_slow.py router_set mimo <api_key>
    PORT=/dev/ttyACM0 CHAR_DELAY=0.01 DRAIN=8 python3 send_slow.py <cmd...>
"""
import os, sys, termios, time, select

from serial_utils import command_notice, sanitize_serial_output

PORT = os.environ.get("PORT", "/dev/ttyACM0")
CHAR_DELAY = float(os.environ.get("CHAR_DELAY", "0.01"))
DRAIN = float(os.environ.get("DRAIN", "8"))

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

# Drain any pending output
time.sleep(0.3)
t0 = time.time()
while time.time() - t0 < 0.3:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            os.read(fd, 4096)
        except OSError:
            pass

arguments = sys.argv[1:]
cmd = " ".join(arguments)
sys.stderr.write(command_notice("SENT (char-by-char)", arguments) + "\n")
for ch in cmd:
    os.write(fd, ch.encode())
    time.sleep(CHAR_DELAY)
os.write(fd, b"\n")

out = bytearray()
t0 = time.time()
while time.time() - t0 < DRAIN:
    r, _, _ = select.select([fd], [], [], 0.3)
    if r:
        try:
            d = os.read(fd, 4096)
            if d:
                out += d
        except OSError:
            pass
decoded = out.decode(errors="replace")
sys.stdout.write(sanitize_serial_output(decoded, arguments))
