#!/usr/bin/env python3
"""PE CLI — PostEngineer 命令行工具

默认集成 PE Daemon 后台加速服务。首次使用时自动启动 daemon，
之后所有命令通过 daemon 调用（免启动开销，秒级响应）。
可通过 --no-daemon 标志回退到直接模式。

用法:
  pe status              # 查看引擎状态
  pe scene               # 列出场景所有对象
  pe types               # 列出所有对象类型
  pe debug start         # 启动调试（PEPlayer 渲染窗口，等价工具栏"启动调试"）
  pe debug stop          # 停止调试，关闭 PEPlayer
  pe create button -d '{"name":"btn1","text":"点击"}'
  pe effect set ao -d '{"enabled":true}'
  pe screenshot          # 截图
  pe toggle ao on|off    # 快捷开关
  pe --no-daemon status  # 绕过 daemon，直接模式
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.request
import urllib.error
from pathlib import Path
from typing import Any

# Windows 终端强制 UTF-8，避免中文乱码和 Unicode 字符报错
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

import click

# 终端标记：用 ASCII 替代 Unicode，兼容所有 Windows 终端
OK = "[OK]"
ERR = "[ERR]"

# ── Daemon 管理（默认加速，一键启动）──────────────────────

DAEMON_PORT = 9090
DAEMON_PORT_FILE = ".pe_daemon_port"
DAEMON_STARTUP_TIMEOUT = 3.0  # 等待 daemon 启动的最长时间（秒）

# 全局标志：是否绕过 daemon（由 --no-daemon 设置）
_force_direct = False


def _daemon_url() -> str | None:
    """检测 daemon 是否在运行，返回 URL 或 None"""
    # 1. 端口文件
    temp_dir = Path(os.environ.get("TEMP", "/tmp"))
    pf = temp_dir / DAEMON_PORT_FILE
    if pf.exists():
        try:
            port = int(pf.read_text().strip())
        except (ValueError, OSError):
            pass
        else:
            url = f"http://127.0.0.1:{port}"
            try:
                resp = urllib.request.urlopen(f"{url}/health", timeout=0.3)
                data = json.loads(resp.read())
                if data.get("success"):
                    return url
            except Exception:
                pass

    # 2. 默认端口
    try:
        resp = urllib.request.urlopen(f"http://127.0.0.1:{DAEMON_PORT}/health", timeout=0.3)
        data = json.loads(resp.read())
        if data.get("success"):
            return f"http://127.0.0.1:{DAEMON_PORT}"
    except Exception:
        pass

    return None


def _start_daemon() -> str | None:
    """启动 daemon 后台进程，等待就绪，返回 URL 或 None"""
    daemon_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pe_daemon.py")

    if not os.path.exists(daemon_path):
        return None

    try:
        kwargs = {}
        if sys.platform == "win32":
            kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
        subprocess.Popen(
            [sys.executable, daemon_path, "--quiet"],
            **kwargs,
        )
    except Exception:
        return None

    # 等待 daemon 就绪（最多 DAEMON_STARTUP_TIMEOUT 秒）
    deadline = time.time() + DAEMON_STARTUP_TIMEOUT
    while time.time() < deadline:
        url = _daemon_url()
        if url:
            return url
        time.sleep(0.15)

    return None


def _ensure_daemon() -> str | None:
    """确保 daemon 在运行：检测 → 自动启动 → 返回 URL"""
    # 先检测是否已在运行
    url = _daemon_url()
    if url:
        return url

    # 自动启动
    return _start_daemon()


def _daemon_call(method: str, **params) -> dict | None:
    """通过 daemon HTTP API 调用，失败返回 None"""
    url = _ensure_daemon()
    if not url:
        return None
    try:
        payload = json.dumps({"method": method, "params": params}).encode("utf-8")
        req = urllib.request.Request(
            f"{url}/cmd",
            data=payload,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        resp = urllib.request.urlopen(req, timeout=5)
        return json.loads(resp.read())
    except Exception:
        return None


# ── 输出格式化 ────────────────────────────────────────────


def _fmt(obj: Any, indent: int = 2) -> str:
    return json.dumps(obj, indent=indent, ensure_ascii=False, default=str)


def _print_result(result: dict, success_msg: str = ""):
    if result.get("success"):
        if success_msg:
            click.echo(f"OK {success_msg}")
        click.echo(_fmt(result))
    else:
        click.echo(f"ERR {result.get('error', '未知错误')}", err=True)
        sys.exit(1)


# ── 延迟导入 pe_client（仅 daemon 不可用时） ──────────────


_pe_client: Any = None


def _get_client():
    """延迟导入 pe_client（~0.8s），仅在 daemon 不可用时加载"""
    global _pe_client
    if _pe_client is None:
        import pe_client
        _pe_client = pe_client
    return _pe_client


def _call(method: str, **params) -> dict:
    """默认通过 daemon，--no-daemon 时回退直接模式"""
    if not _force_direct:
        result = _daemon_call(method, **params)
        if result is not None:
            return result
        # daemon 调用失败，静默回退到直接模式

    # 回退到直接模式
    pe = _get_client()
    func = getattr(pe, method, None)
    if func is None:
        return {"success": False, "error": f"未知方法: {method}"}
    try:
        return func(**params)
    except TypeError as e:
        return {"success": False, "error": f"参数错误: {e}"}


# ── CLI 命令 ──────────────────────────────────────────────


@click.group()
@click.option("--no-daemon", is_flag=True, default=False, help="绕过 daemon，强制使用直接模式")
@click.version_option(version="0.5.0", prog_name="pe")
def cli(no_daemon: bool = False):
    """PE CLI — PostEngineer 命令行工具

    \b
    默认集成 PE Daemon 后台加速服务。
    首次使用时自动启动 daemon，之后所有命令享受秒级响应。
    使用 --no-daemon 可绕过 daemon 使用直接模式。

    \b
    手动管理 daemon: pe daemon [status|stop]
    """
    global _force_direct
    _force_direct = no_daemon


@cli.command()
@click.argument("action", required=False, default="status")
def daemon(action: str):
    """管理 PE Daemon 后台加速服务

    \b
    pe daemon          # 查看 daemon 状态
    pe daemon status   # 同上
    pe daemon stop     # 停止 daemon
    """
    import subprocess

    url = _daemon_url()

    if action in ("status", ""):
        if url:
            try:
                resp = urllib.request.urlopen(f"{url}/health", timeout=3)
                data = json.loads(resp.read())
                click.echo("OK PE Daemon 运行中")
                click.echo(f"  地址:    {url}")
                click.echo(f"  运行时间: {data.get('uptime', '?')}s")
                # 检查引擎连接
                try:
                    cresp = urllib.request.urlopen(f"{url}/connect", timeout=3)
                    cdata = json.loads(cresp.read())
                    click.echo(f"  引擎:    {cdata.get('engine_url', '?')}")
                    click.echo(f"  引擎状态: {'OK 已连接' if cdata.get('success') else 'ERR 未连接'}")
                except Exception:
                    click.echo("  引擎:    (无法检测)")
            except Exception:
                click.echo("ERR Daemon 响应异常")
        else:
            click.echo("PE Daemon 未运行")
            click.echo()
            click.echo("下次执行任何 pe 命令时会自动启动 daemon。")
            click.echo("也可手动启动: pe daemon start")
        return

    if action == "start":
        if url:
            click.echo(f"OK PE Daemon 已在运行 → {url}")
        else:
            click.echo("正在启动 PE Daemon...")
            new_url = _start_daemon()
            if new_url:
                click.echo(f"OK PE Daemon 已启动 → {new_url}")
            else:
                click.echo("ERR 启动失败，请检查 pe_daemon.py 是否存在")
                sys.exit(1)
        return

    if action == "stop":
        if not url:
            click.echo("PE Daemon 未在运行")
            return
        try:
            resp = urllib.request.urlopen(f"{url}/shutdown", timeout=3)
            click.echo("OK PE Daemon 已停止")
        except Exception:
            # 暴力杀进程
            try:
                import signal
                temp_dir = Path(os.environ.get("TEMP", "/tmp"))
                pf = temp_dir / DAEMON_PORT_FILE
                if pf.exists():
                    pf.unlink()
            except Exception:
                pass
            click.echo("OK 已发送停止信号")
        return

    click.echo(f"ERR 未知操作: {action}。支持: status, start, stop", err=True)
    sys.exit(1)


@cli.command()
def connect():
    """显示连接状态"""
    result = _call("connect") if _daemon_url() else None
    if result is None:
        # 直接模式
        pe = _get_client()
        url = pe.get_engine_url()
        h = pe.client.engine_health()
        click.echo(f"模式:   直接模式 (无 daemon)")
        click.echo(f"目标:   {url}")
        click.echo(f"连接:   {'OK 已连接' if h.get('success') else 'ERR 无法连接'}")
    else:
        click.echo(f"模式:   Daemon 加速")
        click.echo(f"引擎:   {result.get('engine_url', '?')}")
        click.echo(f"连接:   {'OK 已连接' if result.get('success') else 'ERR 无法连接'}")


# ── 引擎状态 ──────────────────────────────────────────────

@cli.command()
def status():
    """查询 PE 引擎状态"""
    result = _call("status")
    if result.get("success"):
        click.echo("OK PE 引擎运行中")
        click.echo(_fmt(result))
    else:
        click.echo(f"ERR 引擎离线: {result.get('error', '无法连接')}")
        sys.exit(1)


@cli.command()
def health():
    """健康检查"""
    result = _call("health")
    if result.get("success"):
        click.echo(f"OK 引擎健康 (HTTP {result.get('status_code', '?')})")
    else:
        click.echo(f"ERR 无法连接: {result.get('error')}")
        sys.exit(1)


# ── 对象类型 ──────────────────────────────────────────────

@cli.command()
def types():
    """列出所有可用的对象类型"""
    result = _call("types")
    if result.get("success"):
        click.echo(f"{'类型':20s} 描述")
        click.echo("-" * 60)
        for name, info in result.get("object_types", {}).items():
            click.echo(f"{name:20s} {info['description']}")
    else:
        _print_result(result)


@cli.command()
@click.argument("obj_type")
def template(obj_type):
    """查看对象类型的字段模板"""
    result = _call("template", obj_type=obj_type)
    _print_result(result)


# ── 对象 CRUD ──────────────────────────────────────────────

@cli.command()
@click.argument("obj_type")
@click.option("--data", "-d", default="{}", help="JSON 格式的对象配置")
def create(obj_type, data):
    """创建引擎对象"""
    try:
        config = json.loads(data)
    except json.JSONDecodeError as e:
        click.echo(f"ERR JSON 解析错误: {e}", err=True)
        sys.exit(1)
    result = _call("create", obj_type=obj_type, data=config)
    _print_result(result, f"对象 '{obj_type}' 创建成功")


@cli.command("list")
@click.argument("obj_type")
def list_cmd(obj_type):
    """列出指定类型的所有对象"""
    result = _call("list", obj_type=obj_type)
    _print_result(result)


@cli.command()
@click.argument("obj_type")
@click.argument("name")
@click.option("--data", "-d", default="{}", help="JSON 格式的修改字段")
def modify(obj_type, name, data):
    """修改已有对象"""
    try:
        changes = json.loads(data)
    except json.JSONDecodeError as e:
        click.echo(f"ERR JSON 解析错误: {e}", err=True)
        sys.exit(1)
    result = _call("modify", obj_type=obj_type, name=name, data=changes)
    _print_result(result, f"对象 '{name}' 修改成功")


# ── 场景 ──────────────────────────────────────────────────

@cli.command()
@click.option("--json", "fmt_json", is_flag=True, help="输出 JSON 格式")
def scene(fmt_json):
    """列出当前场景中所有对象（快速）"""
    result = _call("scene")
    if not result.get("success"):
        _print_result(result)
        return

    total = result.get("total", 0)
    objects = result.get("objects", {})

    if fmt_json:
        click.echo(_fmt(result))
        return

    if not objects:
        click.echo("场景中没有对象")
        return

    click.echo(f"\n当前场景共 {total} 个对象：\n")
    for obj_type, info in sorted(objects.items()):
        desc = info.get("description", "")
        names = info.get("names", [])
        click.echo(f"  [{obj_type}] {desc} ({len(names)}个)")
        for name in names:
            click.echo(f"    · {name}")
        click.echo("")


# ── 输出 ──────────────────────────────────────────────────

@cli.command()
@click.option("--limit", "-n", default=0, help="返回最近 N 条")
def output(limit):
    """读取引擎输出窗口"""
    result = _call("output", limit=limit)
    if result.get("success"):
        lines = result.get("data", {}).get("lines", []) if isinstance(result.get("data"), dict) else result.get("lines", [])
        if lines:
            for line in lines:
                click.echo(line)
        else:
            click.echo("(无输出)")
    else:
        _print_result(result)


@cli.command("console")
@click.option("--limit", "-n", default=0, type=click.IntRange(min=0), help="返回最近 N 条，0 表示全部")
@click.option(
    "--channel", "-c",
    type=click.Choice(["auto", "output", "info", "debug"], case_sensitive=False),
    default="auto", show_default=True,
    help="底部控制台频道提示",
)
@click.option("--json", "as_json", is_flag=True, help="输出完整 JSON（含控件诊断信息）")
def console(limit, channel, as_json):
    """直接读取 PE 底部“输出/信息/调试”控制台。

    此命令读取 PostEngineer 的 Win32 控件，不依赖 /api/output/query，
    也不会激活 Tab 或改变当前 UI 状态。
    """
    from pe_console import read_bottom_console

    result = read_bottom_console(limit=limit, channel=channel.lower())
    if as_json:
        click.echo(_fmt(result))
    elif not result.get("success"):
        click.echo(f"ERR {result.get('error', '读取底部控制台失败')}", err=True)
        raise click.exceptions.Exit(1)
    else:
        lines = result.get("lines", [])
        if lines:
            for line in lines:
                click.echo(line)
        else:
            click.echo("(无输出)")


# ── PE 效果 ────────────────────────────────────────────────

@cli.group()
def ui():
    """PostEngineer 窗口/控件诊断与自动化。"""


@ui.command("inspect")
def ui_inspect():
    """列出 PE 主窗口及子控件，供自动化校准使用。"""
    try:
        from pe_ui_automation import inspect_pe
    except Exception as exc:
        raise click.ClickException(f"无法加载 UI 自动化模块: {exc}") from exc
    result = inspect_pe()
    click.echo(json.dumps(result, ensure_ascii=False, indent=2))
    if not result.get("success"):
        raise click.ClickException(result.get("error", "UI inspect failed"))


@cli.group()
def script():
    """PE 脚本绑定、启动和输出验证。"""


@script.command("deploy-and-run")
@click.option("--script-file", "script_file", required=True, type=click.Path(exists=True, dir_okay=False, path_type=str))
@click.option("--graph-title", default="", help="当前 PE 行为图文档标题，例如 alpha.rg。")
@click.option("--marker", default="", help="期望输出标记；默认从脚本文件名推导。")
@click.option("--timeout", default=20, show_default=True, type=int)
@click.option("--stop-after", is_flag=True, help="验证输出后停止 PEPlayer。")
def script_deploy_and_run(script_file, graph_title, marker, timeout, stop_after):
    """验证当前行为图绑定并启动原生调试，读取 PE/PEPlayer 输出。

    此命令不会修改 .rg 或 include.script；它用于已经通过 PE 图形编辑器
    完成脚本图元和根调用绑定的项目，作为稳定的全自动运行闭环。
    """
    import time as _time
    from pe_ui_automation import find_main_window, enum_children
    from pe_console import read_bottom_console
    from pe_client import debug_start, debug_stop

    if graph_title:
        root = find_main_window()
        if not root or graph_title.lower() not in root.title.lower():
            raise click.ClickException(
                f"当前 PE 行为图不是 {graph_title!r}；请先在 PE 中打开目标图。"
            )
    if not os.path.isfile(script_file):
        raise click.ClickException(f"脚本文件不存在: {script_file}")
    if not marker:
        marker = os.path.splitext(os.path.basename(script_file))[0]

    result = debug_start()
    if not result.get("success"):
        raise click.ClickException(result.get("error", "debug start failed"))

    deadline = _time.time() + max(1, timeout)
    observed = None
    while _time.time() < deadline:
        observed = read_bottom_console(limit=100, channel="auto")
        if marker in observed.get("text", ""):
            break
        _time.sleep(0.5)

    matched = bool(observed and marker in observed.get("text", ""))
    payload = {
        "success": matched,
        "script_file": os.path.abspath(script_file),
        "marker": marker,
        "debug_start": result,
        "console": observed,
    }
    if stop_after:
        payload["debug_stop"] = debug_stop()
    click.echo(json.dumps(payload, ensure_ascii=False, indent=2))
    if not matched:
        raise click.ClickException("启动调试后未在 PE 输出窗口找到期望标记")


@script.command("load-file")
@click.argument("filename", type=click.Path(exists=True, dir_okay=False, path_type=str))
def script_load_file(filename):
    """通过 PE 原生 CVsView::ReadScriptFromFile 加载/注册脚本文件。

    这是有副作用的加载动作：脚本函数会注册到当前 PE 主模块，但不会
    替代行为图中的根调用，也不会自动启动 PEPlayer。若项目启动图已经
    加载了同一脚本，再次调用可能产生函数重定义错误。
    """
    from pe_client import script_execute_file

    result = script_execute_file(os.path.abspath(filename))
    click.echo(json.dumps(result, ensure_ascii=False, indent=2))
    if not result.get("success"):
        raise click.ClickException(result.get("error", "native script load failed"))


@script.command("native-flow")
@click.option("--include-file", default="", type=click.Path(dir_okay=False, path_type=str),
              help="include.script；registration=native_file 时由 PE 原生加载")
@click.option("--registration", type=click.Choice(["project_startup", "native_file", "none"]),
              default="project_startup", show_default=True)
@click.option("--function-call", default="", help="可选：例如 helloworld();；仅在 --execute-function 时调用")
@click.option("--execute-function", is_flag=True, help="用 PE 原生 program 路径执行函数（默认关闭）")
@click.option("--graph-file", default="", type=click.Path(dir_okay=False, path_type=str),
              help="可选 .rg；只做原生解析验证，不修改行为图")
@click.option("--marker", default="", help="PE 输出窗口中必须出现的唯一标记")
@click.option("--wait", "wait_seconds", default=20.0, show_default=True, type=float)
@click.option("--keep-running", is_flag=True, help="验证后不调用原生 debug stop")
@click.option("--dry-run", is_flag=True, help="只检查参数和文件，不调用 PE")
def script_native_flow(include_file, registration, function_call, execute_function,
                       graph_file, marker, wait_seconds, keep_running, dry_run):
    """执行 PE 原生脚本生命周期：加载/注册→实例执行(可选)→启动调试→输出验证→终止。"""
    from pe_client import native_script_flow

    result = native_script_flow(
        include_file=os.path.abspath(include_file) if include_file else "",
        registration=registration,
        function_call=function_call,
        execute_function=execute_function,
        graph_file=os.path.abspath(graph_file) if graph_file else "",
        marker=marker,
        wait_seconds=wait_seconds,
        stop_after=not keep_running,
        dry_run=dry_run,
    )
    click.echo(json.dumps(result, ensure_ascii=False, indent=2))
    if not result.get("success"):
        raise click.ClickException(result.get("error", "native flow failed"))


@cli.group()
def effect():
    """PE 效果管理"""


@effect.command("list")
@click.option("--domain", "-d", default="", help="按域过滤")
def effect_list(domain):
    """列出所有 PE 效果"""
    result = _call("effect_list", domain=domain)
    if not result.get("success"):
        click.echo(f"ERR {result.get('error')}", err=True)
        sys.exit(1)

    pe = _get_client()
    labels = pe.registry.DOMAIN_LABELS

    click.echo(f"{'效果名':25s} {'域':15s} 描述")
    click.echo("-" * 80)
    for name, info in result.get("capabilities", {}).items():
        label = labels.get(info["domain"], info["domain"])
        click.echo(f"{name:25s} {label:15s} {info['description']}")


@effect.command("get")
@click.argument("effect_name")
def effect_get(effect_name):
    """查询场景级效果状态"""
    result = _call("effect_get", effect_name=effect_name)
    _print_result(result)


@effect.command("set")
@click.argument("effect_name")
@click.option("--data", "-d", default="{}", help="JSON 格式的参数")
def effect_set(effect_name, data):
    """设置场景级效果"""
    try:
        params = json.loads(data)
    except json.JSONDecodeError as e:
        click.echo(f"ERR JSON 解析错误: {e}", err=True)
        sys.exit(1)
    result = _call("effect_set", effect_name=effect_name, data=params)
    _print_result(result, f"效果 '{effect_name}' 设置成功")


@effect.command("batch")
@click.argument("items_json")
def effect_batch(items_json):
    """批量设置多个场景效果"""
    try:
        items = json.loads(items_json)
    except json.JSONDecodeError as e:
        click.echo(f"ERR JSON 解析错误: {e}", err=True)
        sys.exit(1)
    result = _call("effect_batch", items=items)
    if result.get("success"):
        click.echo(f"OK 批量设置 {result.get('count', 0)} 个效果成功")
    else:
        click.echo(f"ERR 失败: {result.get('error')}", err=True)
        sys.exit(1)


# ── 截图 ──────────────────────────────────────────────────

@cli.command()
@click.option("--label", "-l", default="", help="截图标签")
def screenshot(label):
    """截取当前渲染画面"""
    result = _call("screenshot", label=label)
    if result.get("success"):
        frame = result.get("frame", {})
        path = frame.get("path", "(引擎返回)")
        click.echo(f"OK 截图成功: {path}")
    else:
        click.echo(f"ERR 截图失败: {result.get('error', '未知错误')}", err=True)
        sys.exit(1)


# ── 最小化 ──────────────────────────────────────────────

@cli.command()
@click.argument("action", type=click.Choice(["toggle", "minimize", "restore", "status"]))
def minimize(action):
    """切换 UI 最小化状态: pe minimize toggle|minimize|restore|status"""
    import subprocess
    handler = os.path.join(os.path.dirname(os.path.abspath(__file__)), "minimize_handler.py")
    result = subprocess.run([sys.executable, handler, action], capture_output=True, text=True)
    if result.stdout:
        click.echo(result.stdout.rstrip())
    if result.stderr:
        click.echo(result.stderr.rstrip(), err=True)
    if result.returncode != 0:
        sys.exit(result.returncode)


# ── 调试 ──────────────────────────────────────────────────

START_EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bin", "Release-x64", "start.exe")


def _find_project_file() -> str | None:
    """通过当前 PE 窗口标题找到 .peproj 项目文件路径"""
    import subprocess
    try:
        result = subprocess.run(
            ["tasklist", "/v", "/fi", "imagename eq PostEngineer.exe", "/fo", "csv", "/nh"],
            capture_output=True, text=True, encoding="gbk", timeout=5,
        )
        for line in result.stdout.splitlines():
            if "PostEngineer.exe" in line and ".peproj" in line:
                # Extract project name from window title "PostEngineer - xxx.peproj"
                import re
                m = re.search(r"PostEngineer\s*-\s*(.+?\.peproj)", line)
                if m:
                    proj_name = m.group(1)
                    # Search for it under known project roots
                    for root in [
                        os.path.join(os.path.dirname(os.path.abspath(__file__)), "knowledge"),
                        os.path.join(os.path.dirname(os.path.abspath(__file__)), "PeProject"),
                        os.path.join(os.path.dirname(os.path.abspath(__file__)), "bin", "Release-x64", "Resource"),
                    ]:
                        for dirpath, dirs, files in os.walk(root):
                            if proj_name in files:
                                return os.path.join(dirpath, proj_name)
        return None
    except Exception:
        return None


# ═══════════════════════════════════════════════════════════════
# Debug 辅助函数
# ═══════════════════════════════════════════════════════════════

def _write_debug_config(bin_dir, proj_file):
    """写入 start.exe 所需的配置文件"""
    project_ini = os.path.join(bin_dir, "project.ini")
    with open(project_ini, "w") as f:
        f.write(f"project={proj_file}\n")
        f.write("mono=50\njitter=0\nshadow=0\n")
        f.write("setwideview=1\napp_fps_enable=0\napp_fps_kiki=0\nenablenavibox=0\n")

    videoplay_ini = os.path.join(bin_dir, "videoplay.ini")
    with open(videoplay_ini, "w") as f:
        f.write("[System]\n")
        f.write(f"project={proj_file}\n")
        f.write("setwideview=0\n")
        f.write("cameraSize=1\ncameraStep=5\ncameraFOV=45\n")
        f.write("shortStat=1\n")


def _wait_for_engine(timeout=15, exclude_port=None):
    """等待 PEPlayer HTTP API 就绪，自动探测端口。返回 URL 或 None"""
    import urllib.request
    import urllib.error
    import json
    import time

    start = time.time()
    # 探测端口范围：8080 起始
    ports_to_try = [8080, 8081, 8082, 8083, 8084, 9090]
    if exclude_port:
        ports_to_try = [p for p in ports_to_try if p != exclude_port]

    while time.time() - start < timeout:
        for port in ports_to_try:
            try:
                url = f"http://127.0.0.1:{port}"
                resp = urllib.request.urlopen(f"{url}/api/health", timeout=1)
                data = json.loads(resp.read())
                if data.get("success") and data.get("service") == "PEHelloMCP":
                    return url
            except Exception:
                pass
        time.sleep(0.5)
    return None


def _setup_debug_buttons(engine_url):
    """通过 PEPlayer HTTP API 创建关闭按钮并加载脚本"""
    import urllib.request
    import json

    def _post(path, payload):
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            f"{engine_url}{path}", data=data,
            headers={"Content-Type": "application/json"}, method="POST")
        resp = urllib.request.urlopen(req, timeout=5)
        return json.loads(resp.read())

    # 1. 创建右上角关闭按钮（先尝试带贴图，失败则用纯文本）
    btn_config = {
        "name": "close_btn",
        "text": "✕",
        "position": [0.93, 0.93, 1.0],
        "width": 0.04,
        "height": 0.04,
    }
    # 尝试添加三态贴图（PEPlayer 的路径解析可能不同，静默失败）
    for state, key in [("normal", "path_normal"), ("hover", "path_hover"), ("pressed", "path_pressed")]:
        for prefix in ["Resource/nicetry_btn/", "nicetry_btn/", ""]:
            path = f"{prefix}close_{state}.png"
            test_btn = dict(btn_config, **{key: path})
            test_btn["name"] = f"_imgtest_{state}"
            r_test = _post("/api/obj/data", {"obj_type": "button", "config": test_btn})
            if r_test.get("success"):
                btn_config[key] = path
                break

    r = _post("/api/obj/data", {"obj_type": "button", "config": btn_config})
    if not r.get("success"):
        click.echo(f"WARN 按钮创建失败: {r.get('message', '')}")

    # 2. 加载 OnObjectEvent 关闭脚本
    script = (
        'Script{\n'
        '    void OnObjectEvent(string objectName, int eventType, dword eventParam)\n'
        '    {\n'
        '        if(objectName == "close_btn" && eventType == 3)\n'
        '        {\n'
        '            closeProgram("start.exe");\n'
        '        }\n'
        '    }\n'
        '}'
    )
    r2 = _post("/api/script/execute", {"script": script, "script_name": "close_handler"})
    if r2.get("success"):
        click.echo("OK 关闭按钮已就绪")
    else:
        click.echo("WARN 脚本加载失败")


@cli.group()
def debug():
    """启动/停止 PE 项目调试"""


@cli.group()
def graph():
    """创建与检查 PE 原生行为图。"""


@graph.command("create")
@click.argument("name")
@click.option("--filename", "filename", required=True, type=click.Path(dir_okay=False, path_type=str),
              help="新 .rg 文件的完整路径（必须位于当前项目目录）。")
def graph_create_cmd(name: str, filename: str):
    """原生创建并注册行为图（项目管理器“添加行为”的数据链路）。"""
    result = _call("graph_create", name=name, filename=filename)
    if not result.get("success"):
        _print_result(result)
        return
    click.echo(f"OK 已创建并注册行为图 '{name}'")
    click.echo(_fmt(result))
    if not result.get("editor_open_requested"):
        click.echo("WARN 图文件与项目已创建；此 PE 版本的 ProjectMgr 编辑器打开入口仍未返回成功。")


@graph.command("read")
@click.argument("filename", type=click.Path(exists=True, dir_okay=False, path_type=str))
def graph_read_cmd(filename: str):
    """以 PE RelaGraph 原生解析器验证 .rg 文件。"""
    _print_result(_call("graph_read", filename=filename))


@graph.command("register")
@click.argument("filename", type=click.Path(exists=True, dir_okay=False, path_type=str))
@click.option("--name", required=True, help="PE 项目树中的行为图名称，例如 atomized.rg。")
def graph_register_cmd(filename: str, name: str):
    """注册已有 .rg；只写项目注册信息，不替换图文件内容。"""
    _print_result(_call("graph_register", name=name, filename=os.path.abspath(filename)))


@graph.command("create-boot")
@click.argument("project", type=click.Path(exists=True, file_okay=True, path_type=str))
@click.argument("script_path", type=click.Path(exists=True, dir_okay=False, path_type=str))
@click.option("--name", default="boot_graph", show_default=True, help="PE 项目中的行为图名称。")
@click.option("--filename", default="", type=click.Path(dir_okay=False, path_type=str),
              help="输出 .rg 路径；默认写入 project 目录的 built.rg。")
def graph_create_boot_cmd(project: str, script_path: str, name: str, filename: str):
    """创建 boot 节点脚本 + include.script 节点，并注册到 PE 项目。"""
    project_path = Path(project).resolve()
    project_dir = project_path if project_path.is_dir() else project_path.parent
    script_file = Path(script_path).resolve()
    try:
        include_file = os.path.relpath(script_file, project_dir).replace(os.sep, "\\")
    except ValueError as exc:
        raise click.ClickException(f"script path must be under project directory: {exc}")
    output_file = Path(filename).resolve() if filename else project_dir / "built.rg"
    result = _call("graph_build_boot", name=name, filename=str(output_file), include_file=include_file)
    _print_result(result, f"boot 行为图 '{name}' 已生成并注册")


@graph.command("node-create")
@click.argument("filename", type=click.Path(exists=True, dir_okay=False, path_type=str))
@click.option("--type", "node_type", required=True, help="PE behavior node type, e.g. boot.")
@click.option("--name", required=True, help="Node display name.")
@click.option("--x", default=0, type=int, show_default=True)
@click.option("--y", default=0, type=int, show_default=True)
def graph_node_create_cmd(filename: str, node_type: str, name: str, x: int, y: int):
    """Add a node using the native PE RelaGraph editor session."""
    _print_result(_call("graph_node_create", filename=filename, node_type=node_type, name=name, x=x, y=y))


@debug.command("start")
def debug_start():
    """启动调试 — 通过 PEHelloMCP 插件创建 PEDATA 并启动 PEPlayer"""
    try:
        from pe_client import debug_start as api_debug_start
    except ImportError:
        click.echo("ERR 无法导入 pe_client", err=True)
        sys.exit(1)

    try:
        result = api_debug_start()
    except Exception as e:
        click.echo(f"ERR 调用 /api/debug/start 失败: {e}", err=True)
        click.echo("     请确保 PostEngineer 已打开项目且 PEHelloMCP 插件已加载", err=True)
        sys.exit(1)

    if result.get("success"):
        proj = result.get("project", "?")
        click.echo(f"OK 调试已启动 — 项目: {proj}")
    else:
        click.echo(f"ERR {result.get('error', '未知错误')}", err=True)
        sys.exit(1)


@debug.command("stop")
def debug_stop():
    """停止调试 — 通过 PEHelloMCP 插件关闭 PEPlayer"""
    try:
        from pe_client import debug_stop as api_debug_stop
    except ImportError:
        click.echo("ERR 无法导入 pe_client", err=True)
        sys.exit(1)

    try:
        result = api_debug_stop()
    except Exception as e:
        # Fallback to taskkill
        import subprocess
        killed = False
        for proc_name in ["start.exe", "PEScenePlayer.exe", "PEPlayer.exe"]:
            r = subprocess.run(
                ["taskkill", "/F", "/IM", proc_name],
                capture_output=True, text=True, encoding="gbk", errors="replace",
            )
            if "没有" not in r.stdout and "not found" not in r.stdout.lower():
                killed = True
        if killed:
            click.echo("OK 调试已停止（taskkill 回退）")
        else:
            click.echo("调试窗口未在运行")
        return

    if result.get("success"):
        click.echo("OK 调试已停止")
    else:
        click.echo(f"WARN {result.get('error', '未知错误')}")


# ── 关闭 ──────────────────────────────────────────────────

@cli.command()
@click.option("--force", "-f", is_flag=True, help="强制关闭（不等待）")
def close(force):
    """关闭 PE 引擎"""
    import subprocess
    if not force:
        click.echo("正在关闭 PE 引擎...")
    cmd = ["taskkill", "/F", "/IM", "PostEngineer.exe"]
    result = subprocess.run(cmd, capture_output=True, text=True,
                           encoding="gbk", errors="replace")
    # 清理 filter
    filtered = "\n".join(
        line for line in result.stdout.splitlines()
        if "成功" in line or "SUCCESS" in line or "没有" in line or "not found" in line.lower()
    )
    if filtered:
        click.echo(filtered)
    if "没有" in result.stdout or "not found" in result.stdout.lower():
        click.echo("PE 引擎未在运行")
    else:
        click.echo("OK PE 引擎已关闭")


# ── 便捷别名 ──────────────────────────────────────────────

@cli.command()
@click.argument("effect_name")
@click.argument("enabled", type=click.Choice(["on", "off"]))
def toggle(effect_name, enabled):
    """快捷开关效果: pe toggle ao on"""
    flag = enabled == "on"
    result = _call("effect_set", effect_name=effect_name, data={"enabled": flag})
    _print_result(result, f"效果 '{effect_name}' 已{'开启' if flag else '关闭'}")


# ── 模板查看 ──────────────────────────────────────────────

@cli.command()
@click.argument("capability")
@click.option("--operation", "-o", default="set", help="操作类型")
def pe_template(capability, operation):
    """查看 PE 效果的参数模板"""
    result = _call("effect_template", capability=capability, operation=operation)
    _print_result(result)


# ======================================================================
# 入口
# ======================================================================

if __name__ == "__main__":
    cli()
