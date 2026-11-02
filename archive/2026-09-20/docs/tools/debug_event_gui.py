#!/usr/bin/env python3
"""VelaOps 联调 Debug 控制台（浏览器版，零第三方依赖）。

启动后自动打开浏览器，页面上两个大按钮：
  - 「触发 DEBUG 事件」：向设备 Agent 发送联调 debug ask，模型会调用
    velaops_show_message 在板载 LCD 弹出提示框，短按 BOOT 关闭。
  - 「例行资源巡检」：发送只读巡检 ask，验证当前链路。

页面下方实时滚动串口输出；检测到弹窗工具调用时顶部横幅变色提醒。
串口为独占资源：运行本工具期间不要再使用其他串口脚本。

用法：
    PORT=/dev/ttyACM0 python3 docs/tools/debug_event_gui.py
"""
import json
import os
import select
import sys
import termios
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from serial_utils import sanitize_serial_output  # noqa: E402

PORT = os.environ.get("PORT", "/dev/ttyACM0")
HTTP_PORT = int(os.environ.get("GUI_PORT", "8765"))
CHAR_DELAY = float(os.environ.get("CHAR_DELAY", "0.015"))
LOG_LIMIT = 4000

DEBUG_ASK = (
    "ask show TEST-OK using velaops_show_message tool"
)
PATROL_ASK = "ask check using velaops_check_resources tool"

# 智能体的 LLM 缓存只哈希消息前 64 个字符（llm_cache.c），编号必须放在
# 开头才能改变缓存键；同时缓存命中时会跳过工具调用，必须避开。
_seq_lock = threading.Lock()
_seq = 0


def next_event_ask(base_ask):
    global _seq
    with _seq_lock:
        _seq += 1
        seq = _seq
    # 缓存键只看前 64 字符，把时间戳也放进开头，避免 GUI 重启后编号复用
    # 撞上旧缓存（缓存命中会跳过工具调用，弹窗就不出现）。
    stamp = int(time.time()) % 100000
    if base_ask.startswith("ask "):
        return "ask E%d-%d %s" % (seq, stamp, base_ask[4:])
    return "E%d-%d %s" % (seq, stamp, base_ask)

PAGE = """<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>VelaOps 联调 Debug 控制台</title>
<style>
 body{font-family:sans-serif;background:#111;color:#eee;margin:24px}
 h1{font-size:20px}
 .btns{display:flex;gap:20px;margin:18px 0}
 button{font-size:22px;padding:18px 34px;border:0;border-radius:12px;
        cursor:pointer;font-weight:bold}
 #btnDebug{background:#d33;color:#fff}
 #btnPatrol{background:#2a7;color:#fff}
 button:disabled{opacity:.45;cursor:wait}
 #banner{padding:10px 14px;border-radius:8px;background:#222;margin-bottom:12px}
 #banner.hit{background:#d33;animation:blink .5s steps(2) infinite}
 @keyframes blink{50%{opacity:.35}}
 #log{background:#000;color:#9f9;font-family:monospace;font-size:12px;
      height:420px;overflow-y:auto;padding:10px;border-radius:8px;
      white-space:pre-wrap;word-break:break-all}
 .hint{color:#888;font-size:13px}
</style></head><body>
<h1>VelaOps 联调 Debug 控制台 <span class="hint">串口：__PORT__</span></h1>
<div id="banner">等待事件…</div>
<div class="btns">
 <button id="btnDebug" onclick="fire('debug')">🚨 触发 DEBUG 事件</button>
 <button id="btnPatrol" onclick="fire('patrol')">📊 例行资源巡检</button>
</div>
<p class="hint">弹窗出现后，在板子上短按 BOOT 键即可关闭并恢复资源看板。</p>
<div id="log"></div>
<script>
let lastSeq = 0;
function fire(kind){
  document.getElementById('btnDebug').disabled = true;
  document.getElementById('btnPatrol').disabled = true;
  fetch('/trigger/' + kind).then(r => r.json()).then(j => {
    setBanner(j.notice || '已发送，等待设备响应…', false);
  });
}
function setBanner(text, hit){
  const b = document.getElementById('banner');
  b.textContent = text;
  b.className = hit ? 'hit' : '';
}
async function poll(){
  try{
    const r = await fetch('/events?from=' + lastSeq);
    const j = await r.json();
    const log = document.getElementById('log');
    for (const ev of j.events){
      lastSeq = ev.seq;
      log.textContent += ev.text;
      if (ev.hit) setBanner(ev.hit, true);
    }
    if (j.events.length){ log.scrollTop = log.scrollHeight; }
    if (j.idle) {
      document.getElementById('btnDebug').disabled = false;
      document.getElementById('btnPatrol').disabled = false;
    }
  }catch(e){}
  setTimeout(poll, 800);
}
poll();
</script></body></html>
"""


class SerialConsole:
    """独占串口：后台持续读取输出入环形缓冲；发送用逐字符慢写。"""

    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                      termios.ISTRIP | termios.INLCR | termios.IGNCR |
                      termios.ICRNL | termios.IXON)
        attrs[1] &= ~termios.OPOST
        attrs[2] &= ~(termios.CSIZE | termios.PARENB)
        attrs[2] |= termios.CS8
        attrs[3] &= ~(termios.ICANON | termios.ECHO | termios.ECHOE |
                      termios.ISIG)
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        os.set_blocking(self.fd, False)
        self.lock = threading.Lock()
        self.events = deque(maxlen=LOG_LIMIT)
        self.seq = 0
        self.hit_notice = None
        self.busy_until = 0.0
        self.recent = bytearray()
        self.last_rx = time.time()
        threading.Thread(target=self._reader, daemon=True).start()

    def _push(self, text):
        hit = None
        # 只用工具执行结果特征判定。ask 文本本身含 velaops_show_message
        # 字样，不能只匹配工具名，否则自己发出的命令回显会误报。
        with self.lock:
            marker_window = (bytes(self.recent[-64:]).decode(errors="replace")
                             + text)
            if (self.hit_notice is None and
                    ("lcd_popup" in marker_window or
                     "Executed velaops tool: velaops_show_message"
                     in marker_window)):
                hit = "📺 检测到弹窗工具调用：设备 LCD 应已出现提示框！短按 BOOT 关闭。"
            self.seq += 1
            self.events.append({"seq": self.seq, "text": text, "hit": hit})
            if hit:
                self.hit_notice = hit
            self.recent.extend(text.encode(errors="replace"))
            del self.recent[:-2048]

    def _reader(self):
        while True:
            ready, _, _ = select.select([self.fd], [], [], 0.3)
            if not ready:
                continue
            try:
                data = os.read(self.fd, 4096)
            except OSError:
                data = b""
            if data:
                self.last_rx = time.time()
                self._push(data.decode(errors="replace"))

    def _quiet(self, need=0.6, timeout=12.0):
        # 等串口安静（巡检日志间隔约 5s，中间有安静窗口），
        # 此时发送才不容易被 nsh 的阻塞读抢走。
        deadline = time.time() + timeout
        while time.time() < deadline:
            time.sleep(0.1)
            with self.lock:
                silent = time.time() - self.last_rx
            if silent >= need:
                return True
        return False

    def send_slow(self, command):
        with self.lock:
            self.busy_until = time.time() + 240
            self.hit_notice = None
        self._quiet()
        self._push(f"\n>>> {command}\n")
        # ESP32-S3 原生 USB CDC 对长命令的一次性 write 会丢尾部字节；
        # 中文 UTF-8 更容易因此被截断。与 send_slow.py 保持一致，按字符
        # 写入（每个 Unicode 字符一次完整编码），确保 ask-inject 可解析。
        for char in command:
            os.write(self.fd, char.encode())
            time.sleep(CHAR_DELAY)
        os.write(self.fd, b"\n")

    def events_since(self, last):
        with self.lock:
            events = [e for e in self.events if e["seq"] > last]
            idle = time.time() > self.busy_until
            return events, idle


CONSOLE = SerialConsole(PORT)


def worker(command, notice):
    # 队列通道：nsh 执行 velaops ask-inject 写 /tmp/vela-ask.txt，
    # 设备侧 agent 的 ask_queue 线程读走投递，避开 nsh/vela 串口抢占。
    for attempt in range(3):
        with CONSOLE.lock:
            start_seq = CONSOLE.seq
        CONSOLE.send_slow(command)
        time.sleep(4)
        with CONSOLE.lock:
            reply = "".join(e["text"] for e in CONSOLE.events
                            if e["seq"] > start_seq)
        if "command not found" in reply:
            CONSOLE._push("\n[GUI] ask-inject 执行失败，重试 (%d/3)…\n"
                          % (attempt + 1))
            continue
        break
    deadline = time.time() + 220
    while time.time() < deadline:
        time.sleep(2)
        with CONSOLE.lock:
            blob = bytes(CONSOLE.recent).decode(errors="replace")
        if ("lcd_popup" in blob or "shown" in blob or
                "Executed velaops tool: velaops_show_message" in blob):
            CONSOLE._push("\n[GUI] 弹窗确认已出现。\n")
            break
    with CONSOLE.lock:
        CONSOLE.busy_until = 0.0
        if CONSOLE.hit_notice is None:
            CONSOLE.hit_notice = notice


def inject_command(base_ask):
    # 去掉 "ask " 前缀，改走 velaops ask-inject 队列文件通道。
    text = next_event_ask(base_ask)
    if text.startswith("ask "):
        text = text[4:]
    return "velaops ask-inject " + text


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _json(self, obj):
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/" or self.path.startswith("/index"):
            body = PAGE.replace("__PORT__", PORT).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path.startswith("/trigger/"):
            kind = self.path.split("/trigger/")[1]
            if kind == "debug":
                threading.Thread(
                    target=worker,
                    args=(inject_command(DEBUG_ASK),
                          "DEBUG 事件已发送完毕（220 秒内未见弹窗确认）。"),
                    daemon=True).start()
                self._json({"notice": "🚨 DEBUG 事件已发送，等待智能体响应…"})
            elif kind == "patrol":
                threading.Thread(
                    target=worker,
                    args=(inject_command(PATROL_ASK), "巡检 ask 已发送。"),
                    daemon=True).start()
                self._json({"notice": "📊 巡检请求已发送，等待智能体响应…"})
            else:
                self._json({"notice": "未知操作"})
            return
        if self.path.startswith("/events"):
            try:
                last = int(self.path.split("from=")[1])
            except (IndexError, ValueError):
                last = 0
            events, idle = CONSOLE.events_since(last)
            self._json({"events": events, "idle": idle})
            return
        self.send_response(404)
        self.end_headers()


def main():
    server = ThreadingHTTPServer(("127.0.0.1", HTTP_PORT), Handler)
    url = f"http://127.0.0.1:{HTTP_PORT}/"
    print(f"[gui] 联调控制台已启动: {url} （Ctrl+C 退出）")
    try:
        webbrowser.open(url)
    except Exception:
        pass
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
