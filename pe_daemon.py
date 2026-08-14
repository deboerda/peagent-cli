#!/usr/bin/env python3
"""PE Daemon — 常驻后台进程，CLI 通过本地 HTTP 调用，避免每次启动 Python 的开销。

启动:  python pe_daemon.py
或:    python pe_daemon.py --port 9090
自动:  pe_cli.py 首次调用时自动启动（--quiet 模式）

CLI 默认集成 daemon，无需手动启动。
"""

from __future__ import annotations

import json
import os
import sys
import time
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path
from typing import Any

# 添加当前目录到 path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pe_client import (
    list_types,
    get_template,
    create_object,
    list_objects,
    modify_object,
    get_output,
    get_status,
    list_capabilities,
    get_capability_template,
    get_scene_effect,
    set_scene_effect,
    set_scene_effects,
    capture_frame,
    graph_create,
    graph_build_boot,
    graph_register,
    graph_read,
    graph_node_create,
    client,
    registry,
    resolver,
    discover_engine_url,
    write_port_file,
)

DAEMON_PORT = 9090
DAEMON_PORT_FILE = ".pe_daemon_port"

# ── 命令路由表 ────────────────────────────────────────────

ROUTES: dict[str, callable] = {
    # 引擎
    "status":    get_status,
    "health":    client.engine_health,
    # 对象
    "types":     list_types,
    "template":  get_template,
    "create":    create_object,
    "list":      list_objects,
    "modify":    modify_object,
    "output":    get_output,
    # 效果
    "effect_list":   list_capabilities,
    "effect_get":    get_scene_effect,
    "effect_set":    set_scene_effect,
    "effect_batch":  set_scene_effects,
    "effect_template": get_capability_template,
    # 截图
    "screenshot": capture_frame,
    # 行为图
    "graph_create": graph_create,
    "graph_build_boot": graph_build_boot,
    "graph_register": graph_register,
    "graph_read": graph_read,
    "graph_node_create": graph_node_create,
    # 场景
    "scene": None,  # 特殊处理
}


def _handle_scene() -> dict:
    """查询场景中所有对象"""
    obj_types = ['button','image','variable','body','remark','curve','progress','histogram','dashBoard','grid']
    results: dict[str, dict] = {}
    total = 0
    for t in obj_types:
        r = list_objects(t)
        if not r.get('success'):
            continue
        fp = r.get('file_path', '')
        if r.get('count', 0) == 0 or not fp or not os.path.exists(fp):
            continue
        with open(fp, 'r', encoding='utf-8') as fh:
            data = json.load(fh)
        if isinstance(data, list):
            names = [o.get('name', '?') for o in data if isinstance(o, dict)]
            if names:
                desc = registry.OBJECT_REGISTRY.get(t, {}).get("description", "")
                results[t] = {"description": desc, "count": len(names), "names": names}
                total += len(names)
    return {"success": True, "total": total, "objects": results}


class DaemonHandler(BaseHTTPRequestHandler):
    """处理 CLI 请求的 HTTP handler"""

    # 类变量：引用 server 用于 shutdown
    http_server: HTTPServer | None = None

    def _send_json(self, data: dict, status: int = 200):
        body = json.dumps(data, ensure_ascii=False, default=str).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == '/health':
            self._send_json({"success": True, "service": "PE-Daemon", "uptime": round(time.time() - START_TIME, 1)})
        elif self.path == '/connect':
            url = discover_engine_url() or f"http://127.0.0.1:{client.DEFAULT_PORT}"
            h = client.engine_health()
            self._send_json({"success": h.get("success", False), "engine_url": url, "engine_health": h})
        elif self.path == '/shutdown':
            self._send_json({"success": True, "message": "shutting down"})
            # 在独立线程中关闭，避免阻塞当前响应
            import threading
            def _shutdown():
                time.sleep(0.2)
                if DaemonHandler.http_server:
                    DaemonHandler.http_server.shutdown()
            threading.Thread(target=_shutdown, daemon=True).start()
        else:
            self._send_json({"success": False, "error": f"unknown GET path: {self.path}"}, 404)

    def do_POST(self):
        # 读取请求体
        length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(length) if length else b'{}'

        try:
            payload = json.loads(body) if body else {}
        except json.JSONDecodeError:
            self._send_json({"success": False, "error": "invalid JSON"}, 400)
            return

        method = payload.get('method', '')
        params = payload.get('params', {})

        # 路由
        if self.path == '/cmd':
            if method == 'scene':
                self._send_json(_handle_scene())
            elif method == 'connect':
                url = discover_engine_url() or f"http://127.0.0.1:{client.DEFAULT_PORT}"
                h = client.engine_health()
                self._send_json({"success": h.get("success", False), "engine_url": url, "engine_health": h})
            elif method in ROUTES and ROUTES[method] is not None:
                try:
                    result = ROUTES[method](**params)
                    self._send_json(result if isinstance(result, dict) else {"success": True, "data": result})
                except TypeError as e:
                    self._send_json({"success": False, "error": f"参数错误: {e}", "expected_params": _get_params(method)}, 400)
                except Exception as e:
                    self._send_json({"success": False, "error": str(e)}, 500)
            else:
                self._send_json({"success": False, "error": f"unknown method: {method}"}, 404)
        else:
            self._send_json({"success": False, "error": f"unknown path: {self.path}"}, 404)

    def log_message(self, format, *args):
        """静默日志"""
        pass


def _get_params(method: str) -> dict:
    """获取方法的参数签名"""
    import inspect
    func = ROUTES.get(method)
    if func is None:
        return {}
    sig = inspect.signature(func)
    return {name: str(param.annotation) for name, param in sig.parameters.items()}


def _write_daemon_port(port: int):
    try:
        temp_dir = Path(os.environ.get("TEMP", "/tmp"))
        (temp_dir / DAEMON_PORT_FILE).write_text(str(port))
    except OSError:
        pass


def _cleanup_daemon_port():
    try:
        temp_dir = Path(os.environ.get("TEMP", "/tmp"))
        pf = temp_dir / DAEMON_PORT_FILE
        if pf.exists():
            pf.unlink()
    except OSError:
        pass


START_TIME = time.time()


def main():
    import argparse
    parser = argparse.ArgumentParser(description="PE Daemon — CLI 后台加速服务")
    parser.add_argument("--port", type=int, default=DAEMON_PORT, help=f"监听端口 (默认 {DAEMON_PORT})")
    parser.add_argument("--foreground", action="store_true", help="前台运行（不 daemonize）")
    parser.add_argument("--quiet", action="store_true", help="安静模式：不打印启动信息（CLI 自动启动时使用）")
    args = parser.parse_args()

    port = args.port
    quiet = args.quiet

    # 检查是否已有 daemon 在运行
    try:
        import urllib.request
        resp = urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=1)
        data = json.loads(resp.read())
        if not quiet:
            print(f"PE Daemon 已在运行 (端口 {port}, 运行 {data.get('uptime', '?')}s)")
            print(f"无需重复启动。CLI 会自动连接。")
        return
    except Exception:
        pass

    # 写入端口文件
    _write_daemon_port(port)

    server = HTTPServer(('127.0.0.1', port), DaemonHandler)
    DaemonHandler.http_server = server  # 注入引用，供 /shutdown 使用

    if not quiet:
        print(f"PE Daemon 已启动 → http://127.0.0.1:{port}")
        print(f"CLI 已自动加速，直接使用 pe 命令即可")
        print(f"按 Ctrl+C 停止")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        if not quiet:
            print("\nPE Daemon 已停止")
    finally:
        server.server_close()
        _cleanup_daemon_port()


if __name__ == "__main__":
    main()
