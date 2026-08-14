# PE CLI 命令实现原理

## 架构概述

```
┌──────────┐  HTTP POST    ┌───────────────┐  HTTP POST    ┌──────────────────┐
│ pe CLI   │ ────────────→ │  PE Daemon    │ ────────────→ │  PEHelloMCP 插件  │
│ (click)  │ ←──────────── │  (localhost:   │ ←──────────── │  (PostEngineer   │
│          │   JSON 响应    │   9090)       │   JSON 响应    │   内嵌 HTTP)     │
└──────────┘               └───────────────┘               └──────────────────┘
     │                            ▲
     │  --no-daemon 时             │ 首次使用时自动启动
     │  绕过 daemon               │
     └────────────────────────────┘
```

- **Daemon 加速模式**（默认）：pe CLI → Daemon (9090) → PEHelloMCP (8080)
- **直接模式**（`--no-daemon`）：pe CLI → PEHelloMCP (8080) 直连

---

## 各命令实现详解

### 1. `pe status` — 查询引擎状态
`POST /api/engine/status` → 返回 tick_alive, engine_ready, view_attached, 命令队列统计

### 2. `pe scene` — 列出场景对象
对 7 种对象类型各发一次 `/api/obj/query`，汇总展示

### 3. `pe types` — 对象类型列表
本地注册表查询，支持中文别名

### 4-13. 其他 CRUD/screenshot/effect 命令
通过 `/api/obj/data`, `/api/obj/modify`, `/api/effect/*`, `/api/frame/capture` 实现

---

### 14. `pe debug start` — 启动调试 ⭐ (v3.0 — PEHelloMCP 原生 API)

```
调用链: CLI → POST /api/debug/start → PEHelloMCP 插件 → 创建 PEDATA → 启动 start.exe /debug
```

v3.0 完全使用 PE 原生内部机制：

**PEHelloMCP 插件端**：
1. 从 PostEngineer 窗口标题获取当前项目 `.peproj` 路径
2. 派生 `.peo` 路径，读取二进制场景数据
3. 创建 `PEDATA` 命名共享内存（`CreateFileMappingW`）
4. 写入 `project.ini` / `videoplay.ini` 配置
5. 启动 `start.exe /debug`（`CreateProcess` + `DETACHED_PROCESS`）

**CLI 端**：
```python
result = pe_client.debug_start()  # → POST /api/debug/start
return "OK 调试已启动" if result["success"] else "ERR ..."
```

与工具栏"启动调试"按钮等价——同一套 PEDATA 序列化机制，代码在 `PEHelloMCP/HelloMCPDebug.cpp` 中统一维护。

### 15. `pe debug stop` — 停止调试

```
调用链: CLI → POST /api/debug/stop → PEHelloMCP 插件 → taskkill start.exe → 释放 PEDATA
```

回退方案：API 不可用时自动 fallback 到 `taskkill`。

---

## 依赖关系

| 模块 | 依赖 | 作用 |
|------|------|------|
| `pe_cli.py` | click, urllib | CLI 入口 |
| `pe_client/` | requests, pydantic | HTTP 传输层 |
| `pe_daemon.py` | Flask | 后台加速代理 |
| `start.exe` | project.ini, videoplay.ini | PEPlayer 渲染窗口 |

## 零 computer_use 依赖

所有命令使用纯 Windows 内置工具，不依赖任何 GUI 自动化。在所有 Agent 平台通用。