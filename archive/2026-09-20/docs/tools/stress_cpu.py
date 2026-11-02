#!/usr/bin/env python3
"""把开发机 CPU 压满，用于演示 VelaOps 主动告警（CPU 阈值）全流程。

板端看板采集的是开发机 CPU（Proxy 从 /proc/stat 采样），超过 85% 会触发主动
事件 → LCD 弹闪烁告警框（本地摘要 CPU xx% HIGH）→ Agent 单轮 LLM 诊断 →
把结论回写到告警框。

用法：
    python3 docs/tools/stress_cpu.py                 # 全核压满 60s
    python3 docs/tools/stress_cpu.py --seconds 90    # 压 90s
    python3 docs/tools/stress_cpu.py --workers 8     # 只压 8 个进程

到点自动停止，避免影响随后的 LLM 诊断。Ctrl+C 亦可提前结束。
"""

from __future__ import annotations

import argparse
import multiprocessing as mp
import os
import time


def _burn_until(stop_at: float) -> None:
    while time.time() < stop_at:
        value = 0
        for index in range(200000):
            value += index * index
        if value < 0:  # 防止被优化掉
            print(value)


def main() -> int:
    parser = argparse.ArgumentParser(description="开发机 CPU 压测")
    parser.add_argument("--seconds", type=float, default=60.0,
                        help="持续秒数（默认 60）")
    parser.add_argument("--workers", type=int, default=os.cpu_count() or 4,
                        help="压测进程数（默认 CPU 核数）")
    args = parser.parse_args()

    stop_at = time.time() + max(1.0, args.seconds)
    workers = max(1, args.workers)
    print(f"[stress] {workers} 个进程，持续 {args.seconds:.0f}s "
          f"（Ctrl+C 提前结束）", flush=True)

    procs = [mp.Process(target=_burn_until, args=(stop_at,))
             for _ in range(workers)]
    for proc in procs:
        proc.start()
    try:
        for proc in procs:
            proc.join()
    except KeyboardInterrupt:
        for proc in procs:
            proc.terminate()
    print("[stress] 结束", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
