#!/usr/bin/env python3
"""演示用目标服务：带状态页的 HTTP 服务（默认端口 28791）。

用途：演示"实体批准 → 服务重启 → 独立复核"时，给观众一个**肉眼可证**的界面。
页面（浏览器打开 http://127.0.0.1:28791/ ）显示 PID / 启动时间 / 运行时长 /
请求计数和醒目的 UP 指示；服务被重启后这些值会归零，一眼可见"真的重启了"。
`GET /status` 返回同样的 JSON，便于脚本或截图。

用法：
    python3 docs/tools/demo_target_app.py [PORT]     # 默认 28791
"""

from __future__ import annotations

import http.server
import json
import os
import socketserver
import sys
import time

START = time.time()
PID = os.getpid()
COUNT = 0

PAGE = """<!doctype html><html><head><meta charset="utf-8">
<title>VelaOps Demo Service</title>
<meta http-equiv="refresh" content="1">
<style>
body{{font-family:system-ui,sans-serif;background:#0f1115;color:#eaeaea;
      text-align:center;padding:48px 16px}}
.big{{font-size:72px;font-weight:800;color:{color};letter-spacing:4px}}
.row{{margin-top:24px}}
.card{{display:inline-block;background:#1b1e26;border:1px solid #2a2f3a;
       border-radius:14px;padding:18px 32px;margin:8px}}
.k{{color:#8a90a0;font-size:13px;letter-spacing:1px}}
.v{{font-size:30px;font-weight:700;margin-top:6px}}
.hint{{color:#8a90a0;margin-top:32px;font-size:13px}}
</style></head><body>
<div class="big">{state}</div>
<div class="row">
  <div class="card"><div class="k">PID</div><div class="v">{pid}</div></div>
  <div class="card"><div class="k">UPTIME</div><div class="v">{uptime:.0f}s</div></div>
  <div class="card"><div class="k">REQUESTS</div><div class="v">{count}</div></div>
  <div class="card"><div class="k">STARTED</div><div class="v">{started}</div></div>
</div>
<p class="hint">VelaOps demo target · 批准重启后 PID/uptime/requests 会归零</p>
</body></html>"""


class Handler(http.server.BaseHTTPRequestHandler):
    def _send(self, body: str, ctype: str = "text/html; charset=utf-8") -> None:
        data = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:  # noqa: N802
        global COUNT
        COUNT += 1
        if self.path.startswith("/status"):
            self._send(json.dumps({
                "pid": PID,
                "uptime": round(time.time() - START, 1),
                "requests": COUNT,
            }), "application/json")
            return
        self._send(PAGE.format(
            color="#37d67a", state="UP", pid=PID,
            uptime=time.time() - START, count=COUNT,
            started=time.strftime("%H:%M:%S", time.localtime(START))))

    def log_message(self, *args) -> None:  # 静默，避免刷屏
        pass


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> int:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 28791
    print(f"[demo-target] serving on 127.0.0.1:{port} pid={PID}", flush=True)
    Server(("127.0.0.1", port), Handler).serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
