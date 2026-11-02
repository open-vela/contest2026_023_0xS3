"""`python -m velaops_proxy` 命令行入口。"""

from __future__ import annotations

import argparse
import signal
import sys

from .application import build_runtime


def _arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="VelaOps Proxy")
    parser.add_argument("--config", required=True, help="权限为 0400/0600 的 JSON 配置")
    return parser.parse_args()


def main() -> int:
    arguments = _arguments()
    runtime = build_runtime(arguments.config)

    def stop(_signum: int, _frame: object) -> None:
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    host, port = runtime.server.server_address
    scheme = "https" if runtime.config.tls is not None else "http"
    print(f"VelaOps Proxy listening on {scheme}://{host}:{port}", file=sys.stderr)
    try:
        runtime.server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        return 0
    finally:
        runtime.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
