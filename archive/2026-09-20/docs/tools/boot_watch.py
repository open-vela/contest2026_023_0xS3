#!/usr/bin/env python3
"""持续监听串口：出现 BOOT 批准提示时立即写标志文件；全部输出落到日志。

用法：
    PORT=/dev/ttyACM0 nohup python3 boot_watch.py >/dev/null 2>&1 &
    提示出现后 /tmp/boot_prompt.flag 会被创建（内容为出现时刻）。
"""
import os
import termios
import time

PORT = os.environ.get("PORT", "/dev/ttyACM0")
LOG = "/tmp/boot_watch.log"
FLAG = "/tmp/boot_prompt.flag"

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

flagged = False
buf = b""
log = open(LOG, "ab", buffering=0)
deadline = time.monotonic() + 600  # 最长监听 10 分钟
while time.monotonic() < deadline:
    try:
        chunk = os.read(fd, 4096)
    except BlockingIOError:
        chunk = b""
    except OSError:
        break
    if chunk:
        log.write(chunk)
        buf = (buf + chunk)[-4096:]
        if not flagged and b"\xe9\x95\xbf\xe6\x8c\x89 BOOT" in buf:  # "长按 BOOT"
            with open(FLAG, "w") as f:
                f.write(time.strftime("%H:%M:%S"))
            flagged = True
    else:
        time.sleep(0.05)
log.close()
os.close(fd)
