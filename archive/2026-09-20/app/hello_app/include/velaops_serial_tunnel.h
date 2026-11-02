/****************************************************************************
 * Contest 2026 team 023 - VelaOps 串口 LLM 隧道（板端）。
 *
 * 设备侧 ai_agent 的 LLM 后端指向 127.0.0.1:18080，本模块把该端口上的
 * HTTP 请求经 USB 串口转发给开发机 relay，再把 relay 注入的响应回写给
 * ai_agent。这样带 36 个工具、~12KB 的 LLM 请求不再走 WiFi，规避
 * ESP32-S3 WiFi 在突发大包时掉线的问题。
 ****************************************************************************/

#ifndef __CONTEST2026_023_VELAOPS_SERIAL_TUNNEL_H
#define __CONTEST2026_023_VELAOPS_SERIAL_TUNNEL_H

/* 隧道统一入口：设备侧所有业务 HTTP 都指向该地址，开发机 relay 再按路径
 * 转发到真实 Proxy / LLM 转发器。 */
#define VELAOPS_TUNNEL_HOST "127.0.0.1"
#define VELAOPS_TUNNEL_PORT 18080
#define VELAOPS_TUNNEL_PORT_STR "18080"

#ifdef __NuttX__

/* 运行隧道主循环（常驻任务），不返回。 */
int velaops_serial_tunnel_run(void);

#else

#define velaops_serial_tunnel_run() (-1)

#endif

#endif /* __CONTEST2026_023_VELAOPS_SERIAL_TUNNEL_H */
