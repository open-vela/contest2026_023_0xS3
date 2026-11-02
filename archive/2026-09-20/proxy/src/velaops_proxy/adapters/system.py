"""操作系统基础能力适配器。"""

import time


class SystemClock:
    """返回 UTC Unix 秒；只用于认证时间窗，不用于持续时间测量。"""

    def now_seconds(self) -> int:
        return int(time.time())
