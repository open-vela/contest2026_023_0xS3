"""演示环境使用的最小局域网 NTP 服务。"""

from __future__ import annotations

import argparse
import socket
import struct
import time


NTP_EPOCH_SECONDS = 2_208_988_800
NTP_PACKET_BYTES = 48


def build_response(request: bytes, unix_time: float) -> bytes:
    """根据客户端请求和当前 Unix 时间生成一个 NTPv4 响应。"""
    if len(request) < NTP_PACKET_BYTES:
        raise ValueError("NTP 请求不足 48 字节")

    ntp_time = unix_time + NTP_EPOCH_SECONDS
    seconds = int(ntp_time)
    fraction = int((ntp_time - seconds) * (1 << 32))
    timestamp = struct.pack("!II", seconds, fraction)

    response = bytearray(NTP_PACKET_BYTES)
    response[0] = 0x24  # 无闰秒、NTPv4、服务端模式
    response[1] = 2     # 局域网二级时间源
    response[2] = 4
    response[3] = (-20) & 0xFF
    response[4:8] = struct.pack("!I", 0x00001000)
    response[8:12] = struct.pack("!I", 0x00001000)
    response[12:16] = b"LOCL"
    response[16:24] = timestamp
    response[24:32] = request[40:48]
    response[32:40] = timestamp
    response[40:48] = timestamp
    return bytes(response)


def serve(host: str, port: int) -> None:
    """在前台持续提供 UDP NTP 响应。"""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((host, port))
        print(f"VelaOps LAN NTP listening on udp://{host}:{port}")
        while True:
            request, address = server.recvfrom(512)
            try:
                response = build_response(request, time.time())
            except ValueError:
                continue
            server.sendto(response, address)


def main() -> int:
    parser = argparse.ArgumentParser(description="VelaOps 局域网演示 NTP")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=40123)
    arguments = parser.parse_args()
    if arguments.port < 1024 or arguments.port > 65535:
        parser.error("端口必须在 1024 到 65535 之间")
    serve(arguments.host, arguments.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
