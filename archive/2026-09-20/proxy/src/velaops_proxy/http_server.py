"""基于 Python 标准库的受限 HTTP 服务器适配器。"""

from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ssl
from typing import TypeAlias

from .http_api import ApiRequest, ApiResponse, ProxyApi, error_response
from .protocol import ErrorCode


MAX_REQUEST_BODY_BYTES = 16 * 1024
SOCKET_TIMEOUT_SECONDS = 10

ServerAddress: TypeAlias = tuple[str, int]


class VelaOpsHttpServer(ThreadingHTTPServer):
    """每个请求使用独立线程，关闭时不等待失联客户端。"""

    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: ServerAddress, api: ProxyApi):
        self.api = api
        super().__init__(address, VelaOpsRequestHandler)


class VelaOpsRequestHandler(BaseHTTPRequestHandler):
    """只负责 HTTP 边界校验和 ApiRequest/ApiResponse 转换。"""

    protocol_version = "HTTP/1.1"
    server: VelaOpsHttpServer

    def setup(self) -> None:
        super().setup()
        self.connection.settimeout(SOCKET_TIMEOUT_SECONDS)

    def do_GET(self) -> None:
        self._dispatch()

    def do_POST(self) -> None:
        self._dispatch()

    def do_PUT(self) -> None:
        self._dispatch()

    def do_DELETE(self) -> None:
        self._dispatch()

    def do_PATCH(self) -> None:
        self._dispatch()

    def _dispatch(self) -> None:
        try:
            request = self._read_request()
            response = self.server.api.handle(request)
        except RequestBoundaryError as exc:
            response = error_response(exc.status, exc.code, exc.message)
        except Exception:
            # 详细异常后续进入受保护审计；绝不把堆栈返回设备端。
            response = error_response(
                500, ErrorCode.INTERNAL_ERROR, "Proxy 内部错误"
            )
        self._write_response(response)

    def _read_request(self) -> ApiRequest:
        if not self.path.startswith("/") or len(self.path) > 2048:
            raise RequestBoundaryError(
                400, ErrorCode.INVALID_REQUEST, "请求目标格式不合法"
            )

        raw_headers = list(self.headers.raw_items())
        lowered = [name.lower() for name, _ in raw_headers]
        if len(lowered) != len(set(lowered)):
            raise RequestBoundaryError(
                400, ErrorCode.INVALID_REQUEST, "不允许重复请求头"
            )

        headers = {name.lower(): value for name, value in raw_headers}
        transfer_encoding = headers.get("transfer-encoding")
        if transfer_encoding:
            raise RequestBoundaryError(
                400, ErrorCode.INVALID_REQUEST, "不支持分块请求体"
            )

        content_length_text = headers.get("content-length", "0")
        if not content_length_text.isascii() or not content_length_text.isdecimal():
            raise RequestBoundaryError(
                400, ErrorCode.INVALID_REQUEST, "Content-Length 不合法"
            )
        content_length = int(content_length_text)
        if content_length > MAX_REQUEST_BODY_BYTES:
            raise RequestBoundaryError(
                413, ErrorCode.PAYLOAD_TOO_LARGE, "请求体超过 16 KiB"
            )

        body = self.rfile.read(content_length) if content_length else b""
        if len(body) != content_length:
            raise RequestBoundaryError(
                400, ErrorCode.INVALID_REQUEST, "请求体长度不足"
            )
        return ApiRequest(self.command, self.path, headers, body)

    def _write_response(self, response: ApiResponse) -> None:
        body = response.body_bytes()
        self.send_response(response.status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Connection", "close")
        for name, value in (response.extra_headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def log_message(self, format: str, *args: object) -> None:
        # 默认 access log 可能包含敏感查询串；结构化审计接入前保持静默。
        return


class RequestBoundaryError(ValueError):
    def __init__(self, status: int, code: ErrorCode, message: str):
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message


def create_server(
    address: ServerAddress,
    api: ProxyApi,
    tls_context: ssl.SSLContext | None = None,
) -> VelaOpsHttpServer:
    server = VelaOpsHttpServer(address, api)
    if tls_context is not None:
        try:
            server.socket = tls_context.wrap_socket(server.socket, server_side=True)
        except Exception:
            server.server_close()
            raise
    return server
