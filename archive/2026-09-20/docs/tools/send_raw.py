#!/usr/bin/env python3
"""Raw TTY helper for ESP32-S3 NSH/ai_agent console.

Sends a command as one write. Use for SHORT commands only.
For long commands (e.g. router_set with API key) use send_slow.py.

Usage:
    python3 send_raw.py <command...>          # e.g. python3 send_raw.py 'wapi sense wlan0'
    PORT=/dev/ttyACM0 DRAIN=10 python3 send_raw.py <cmd>

Avoids touching DTR/RTS so the board does NOT reset.
"""
import os, sys, termios, time, select

from serial_utils import command_notice, sanitize_serial_output

PORT = os.environ.get("PORT", "/dev/ttyACM0")
DRAIN = float(os.environ.get("DRAIN", "6"))

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
sys.stderr.write(command_notice("SENDING", arguments) + "\n")
os.write(fd, (cmd + "\n").encode())

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
