"""Version-safe native PE script lifecycle orchestration.

This module deliberately composes only PEHelloMCP endpoints that are already
verified in the installed build.  It does not edit ``.rg`` files or infer
private RelaGraph object layouts.
"""

from __future__ import annotations

import os
import time
from typing import Any

from .client import (
    debug_start,
    debug_stop,
    engine_status,
    graph_read,
    output_query,
    script_execute,
    script_execute_file,
)


def _failed(step: str, result: dict[str, Any], steps: list[dict[str, Any]]) -> dict[str, Any]:
    steps.append({"step": step, "success": False, "result": result})
    return {
        "success": False,
        "failed_step": step,
        "steps": steps,
        "error": result.get("error", f"native flow failed at {step}"),
    }


def native_script_flow(
    *,
    include_file: str = "",
    function_call: str = "",
    graph_file: str = "",
    registration: str = "project_startup",
    wait_seconds: float = 20.0,
    marker: str = "",
    stop_after: bool = True,
    execute_function: bool = False,
    dry_run: bool = False,
) -> dict[str, Any]:
    """Run the verified native script lifecycle.

    ``registration`` is explicit to prevent duplicate function definitions:

    * ``project_startup``: the behavior graph already loads the include;
      no second ``ReadScriptFromFile`` call is made.
    * ``native_file``: load the include once with PE's native file reader.
    * ``none``: do not load an include (useful when the caller only wants to
      execute an already registered function).

    ``execute_function`` invokes the native ``program`` path before debug
    start.  It is opt-in because a project-startup graph should normally own
    the invocation timing.
    """
    steps: list[dict[str, Any]] = []
    registration = (registration or "project_startup").strip().lower()
    if registration not in {"project_startup", "native_file", "none"}:
        return {"success": False, "error": "registration must be project_startup, native_file, or none", "steps": steps}
    if registration == "native_file" and not include_file:
        return {"success": False, "error": "include_file is required for registration=native_file", "steps": steps}
    if include_file and not os.path.isfile(include_file):
        return {"success": False, "error": f"include_file does not exist: {include_file}", "steps": steps}
    if graph_file and not os.path.isfile(graph_file):
        return {"success": False, "error": f"graph_file does not exist: {graph_file}", "steps": steps}

    if dry_run:
        return {
            "success": True,
            "dry_run": True,
            "registration": registration,
            "include_file": os.path.abspath(include_file) if include_file else "",
            "graph_file": os.path.abspath(graph_file) if graph_file else "",
            "function_call": function_call,
            "steps": steps,
        }

    status = engine_status()
    if not status.get("success") or not status.get("engine_ready", True):
        return _failed("engine_status", status, steps)
    steps.append({"step": "engine_status", "success": True, "result": status})

    if graph_file:
        graph = graph_read(os.path.abspath(graph_file))
        if not graph.get("success") or not graph.get("parse_verified", False):
            return _failed("graph_read", graph, steps)
        steps.append({"step": "graph_read", "success": True, "result": graph})

    if registration == "native_file":
        loaded = script_execute_file(os.path.abspath(include_file))
        if not loaded.get("success"):
            return _failed("include_load", loaded, steps)
        steps.append({"step": "include_load", "success": True, "result": loaded})
    else:
        steps.append({
            "step": "include_load",
            "success": True,
            "skipped": True,
            "mode": registration,
            "message": "include is owned by the PE project startup graph",
        })

    if execute_function:
        if not function_call:
            return _failed("program_execute", {"success": False, "error": "function_call is required when execute_function=true"}, steps)
        program = script_execute(function_call, script_name="native_flow_program", execution_mode="program")
        if not program.get("success"):
            return _failed("program_execute", program, steps)
        steps.append({"step": "program_execute", "success": True, "result": program})

    started = debug_start()
    if not started.get("success"):
        return _failed("debug_start", started, steps)
    steps.append({"step": "debug_start", "success": True, "result": started})

    deadline = time.monotonic() + max(0.0, float(wait_seconds))
    observed: dict[str, Any] = {"success": True, "text": "", "lines": []}
    matched = False
    while time.monotonic() <= deadline:
        observed = output_query(limit=200)
        text = observed.get("text", "") if isinstance(observed, dict) else ""
        if marker and marker in text:
            matched = True
            break
        if not marker and text:
            matched = True
            break
        time.sleep(0.5)

    steps.append({"step": "output_verify", "success": matched, "marker": marker, "result": observed})
    stopped: dict[str, Any] | None = None
    if stop_after:
        stopped = debug_stop()
        steps.append({"step": "debug_stop", "success": bool(stopped.get("success")), "result": stopped})
        # PEHelloMCP intentionally hides the editor output while PEPlayer is
        # alive.  Query once more after the native stop so the same flow can
        # verify the actual output without UI automation.
        if not matched:
            after_stop = output_query(limit=200)
            after_text = after_stop.get("text", "") if isinstance(after_stop, dict) else ""
            matched = bool((marker and marker in after_text) or (not marker and after_text))
            # Some PE builds lose the output-pane caption after PEPlayer exits.
            # The existing Win32 ListBox reader is still a non-visual fallback
            # and keeps verification independent of screen coordinates/UIA.
            if not matched:
                try:
                    from pe_console import read_bottom_console
                    local_console = read_bottom_console(limit=200, channel="auto")
                    local_text = local_console.get("text", "") if isinstance(local_console, dict) else ""
                    matched = bool((marker and marker in local_text) or (not marker and local_text))
                    if local_console:
                        after_stop = {"api": after_stop, "local": local_console}
                        observed = after_stop
                except Exception as exc:
                    after_stop = {"api": after_stop, "local_error": str(exc)}
            if after_stop:
                observed = after_stop
            steps.append({"step": "output_verify_after_stop", "success": matched, "marker": marker, "result": after_stop})

    result: dict[str, Any] = {
        "success": matched,
        "registration": registration,
        "include_file": os.path.abspath(include_file) if include_file else "",
        "graph_file": os.path.abspath(graph_file) if graph_file else "",
        "function_call": function_call,
        "marker": marker,
        "debug_start": started,
        "console": observed,
        "steps": steps,
    }
    if stopped is not None:
        result["debug_stop"] = stopped
    if not matched:
        result["error"] = "native debug completed without the requested output marker"
    return result
