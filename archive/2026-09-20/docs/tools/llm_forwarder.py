#!/usr/bin/env python3
"""LLM 转发器：设备侧 TLS 直连外网不稳定时，由主机代转发。

设备 agent 把 LLM 后端配置成 http://<主机>:28792/（velaops ask-inject
配套的 set_llm 指令），本进程原样转发 POST 请求体到
https://api.xiaomimimo.com/v1/chat/completions 并回传响应。

用法:
    MIMO_API_KEY=sk-... python3 llm_forwarder.py [port]

默认仅监听 127.0.0.1。供开发板访问时必须显式设置 BIND_HOST，并建议同时
设置 ALLOWED_CLIENT 为开发板 IPv4，避免局域网其他设备借用主机侧密钥。
"""

import json
import os
import sys
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UPSTREAM = "https://api.xiaomimimo.com/v1/chat/completions"
API_KEY = os.environ.get("MIMO_API_KEY", "")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 28792
BIND_HOST = os.environ.get("BIND_HOST", "127.0.0.1")
ALLOWED_CLIENT = os.environ.get("ALLOWED_CLIENT", "")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print("[fwd] %s" % (fmt % args), flush=True)

    def do_POST(self):
        if ALLOWED_CLIENT and self.client_address[0] != ALLOWED_CLIENT:
            self.send_error(403, "client not allowed")
            return

        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)

        # 设备可能带自己的 key，统一换成主机侧密钥，保证转发可用。
        try:
            payload = json.loads(body)
        except ValueError:
            payload = None
        if isinstance(payload, dict):
            payload.setdefault("stream", False)

        request = urllib.request.Request(
            UPSTREAM,
            data=body if payload is None else json.dumps(payload).encode(),
            headers={
                "Content-Type": "application/json",
                "Authorization": "Bearer %s" % API_KEY,
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                data = response.read()
                self.send_response(response.status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
        except urllib.error.HTTPError as error:
            data = error.read()
            print("[fwd] upstream HTTP %d: %.1000s" %
                  (error.code, data.decode(errors="replace")), flush=True)
            self.send_response(error.code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except Exception as error:  # noqa: BLE001
            message = json.dumps({"error": str(error)}).encode()
            self.send_response(502)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(message)))
            self.end_headers()
            self.wfile.write(message)

    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"llm-forwarder ok\n")


def main():
    if not API_KEY:
        print("[fwd] 缺少 MIMO_API_KEY 环境变量", file=sys.stderr)
        sys.exit(1)
    server = ThreadingHTTPServer((BIND_HOST, PORT), Handler)
    print("[fwd] LLM 转发器已启动: %s:%d -> %s (client=%s)" %
          (BIND_HOST, PORT, UPSTREAM, ALLOWED_CLIENT or "any"),
          flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
