# AI Agent 赛道要求检查表

> 依据工作区官方文档 `docs/zh-cn/contest_2026/` 与
> `packages/ai_agent/docs/` 整理。只记录赛道硬要求和本项目对应证据。

## 官方硬要求

- 作品在硬件上运行 openvela 与 ai_agent，并配置可用 LLM。
- 至少接入一种交互 Channel。
- 至少提供一个自定义 Markdown Skill。
- 至少演示一个主动触发场景和一个工具执行场景；纯聊天不符合要求。
- 队伍作品只修改队伍目录；上游公共目录的正式改动应走独立 PR。

## VelaOps 对应关系

- 硬件：ESP32-S3-EYE；ai_agent 与 MiMo 已完成实机基线对话。
- Channel：当前 Demo 使用设备 NSH CLI，后续可替换为图形或语音入口。
- Skill：`app/hello_app/skills/server-incident-response.md`。
- Tool：外部 Provider `velaops_check_resources`，只允许固定的
  `check_resources`，不接受目标、Action 或命令参数。
- 主动场景：首次成功只读工具调用后启动生命周期安全的后台巡检；资源阈值异常经
  去抖、去重后自动注入 Agent，恢复事件同样注入并要求重新取证。
- 执行场景：固定无参数 Tool `velaops_restart_service` 等待实体 BOOT 长按，调用
  白名单修复并使用新 request ID 独立复核；LLM 不提供服务、命令或批准字段。

## 当前验收边界

- 已通过 Skill 契约、Provider 边界、HTTP 信号中断恢复等主机测试。
- 已实机确认自定义 Skill 被索引，自然请求按 `read_file` → 只读 Tool 的顺序执行。
- 已多次实机完成自然请求 → Skill → Tool → 单个严格 JSON；MiMo 公网存在间歇超时。
- Agent 消息总线自动入队 `opened`/`recovered` 已真机验收，`opened` 已观察到开始
  处理。实体批准的设备闭环已真机通过：不按键安全超时且无变更审计，长按后仅执行
  一次重启并独立复核恢复。自然语言两步请求已真实调用
  `velaops_restart_service`，完成唯一服务重启和独立复核。
- Debug GUI 已通过文件队列 Channel 触发 MiMo 调用
  `velaops_show_message({"text":"TEST-OK"})`；ESP32-S3-EYE LCD 真机弹窗和 GUI
  成功标记均已验收。MiMo 通过仅绑定开发机局域网地址、仅允许板端 IP 的受限转发器
  访问，避免向同网段其他客户端开放主机侧 API Key。
