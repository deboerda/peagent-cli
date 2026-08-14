"""Read the PostEngineer bottom console with an explicit GBK boundary."""

from __future__ import annotations

import argparse
import ctypes
import json
import sys
from ctypes import wintypes


WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
LB_GETCOUNT = 0x018B
LB_GETTEXT = 0x0189
LB_GETTEXTLEN = 0x018A
LB_ERR = -1


def _empty(error: str = "") -> dict:
    result = {
        "success": not bool(error), "available": False,
        "source": "win32_bottom_console", "panel": "", "control_class": "",
        "line_count": 0, "returned_count": 0, "lines": [], "text": "",
    }
    if error:
        result["error"] = error
    return result


def read_bottom_console(limit: int = 0, channel: str = "auto") -> dict:
    """Read an ANSI PE ListBox/Edit control and return UTF-8 Python strings.

    PE's MFC controls are GBK/ANSI.  ``SendMessageW`` corrupts their text, so
    this intentionally uses the A APIs and decodes only at this boundary.
    """
    if limit < 0:
        return _empty("limit must be >= 0")
    if channel not in {"auto", "output", "info", "debug"}:
        return _empty("channel must be one of: auto, output, info, debug")
    if sys.platform != "win32":
        return _empty("PE console reading is only available on Windows")

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    for name in ("SendMessageA", "SendMessageW"):
        fn = getattr(user32, name)
        fn.argtypes = (wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
        fn.restype = wintypes.LPARAM

    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def ansi_text(hwnd: int) -> str:
        size = user32.GetWindowTextLengthA(hwnd)
        if size:
            buffer = ctypes.create_string_buffer(size + 1)
            user32.GetWindowTextA(hwnd, buffer, len(buffer))
            return buffer.value.decode("gbk", errors="replace")
        buffer_w = ctypes.create_unicode_buffer(1)
        user32.GetWindowTextW(hwnd, buffer_w, 1)
        return buffer_w.value

    def class_name(hwnd: int) -> str:
        buffer = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, buffer, len(buffer))
        return buffer.value

    roots: list[int] = []

    @callback
    def collect_root(hwnd, _):
        title = ansi_text(hwnd)
        if user32.IsWindowVisible(hwnd) and (
            title.startswith("PostEngineer") or title.startswith("PEPlayer")
        ):
            roots.append(hwnd)
        return True

    user32.EnumWindows(collect_root, 0)
    if not roots:
        return _empty("PostEngineer main window was not found")

    aliases = {
        "auto": ("\u8f93\u51fa", "\u4fe1\u606f", "\u8c03\u8bd5", "output", "info", "debug"),
        "output": ("\u8f93\u51fa", "output"),
        "info": ("\u4fe1\u606f", "info"),
        "debug": ("\u8c03\u8bd5", "debug"),
    }[channel]
    candidates: list[tuple[int, int, str, str]] = []

    for root in roots:
        root_rect = wintypes.RECT()
        user32.GetWindowRect(root, ctypes.byref(root_rect))

        @callback
        def collect_child(hwnd, _):
            cls = class_name(hwnd)
            lower = cls.lower()
            if lower != "listbox" and "edit" not in lower:
                return True
            rect = wintypes.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
            if rect.right <= rect.left or rect.bottom <= rect.top:
                return True
            names: list[str] = []
            parent = hwnd
            while parent and parent != root:
                value = ansi_text(parent)
                if value:
                    names.append(value)
                parent = user32.GetParent(parent)
            panel = " / ".join(reversed(names))
            semantic = 100 if any(x.lower() in panel.lower() for x in aliases) else 0
            bottom = int(50 * (rect.top - root_rect.top) / max(1, root_rect.bottom - root_rect.top))
            line_hint = 0
            if lower == "listbox":
                try:
                    line_hint = max(0, int(user32.SendMessageA(hwnd, LB_GETCOUNT, 0, 0)))
                except Exception:
                    line_hint = 0
            candidates.append((semantic + bottom + (20 if lower == "listbox" else 0) + min(line_hint, 100), hwnd, cls, panel))
            return True

        user32.EnumChildWindows(root, collect_child, 0)

    if not candidates:
        return _empty("no ListBox/Edit/RichEdit console control was found")
    candidates.sort(reverse=True, key=lambda item: item[0])
    score, hwnd, cls, panel = candidates[0]
    lines: list[str] = []
    if cls.lower() == "listbox":
        count = int(user32.SendMessageA(hwnd, LB_GETCOUNT, 0, 0))
        if count == LB_ERR:
            return _empty("failed to read the PE console ListBox")
        for index in range(max(0, count - limit) if limit else 0, count):
            length = int(user32.SendMessageA(hwnd, LB_GETTEXTLEN, index, 0))
            if length == LB_ERR:
                continue
            buffer = ctypes.create_string_buffer(length + 1)
            user32.SendMessageA(hwnd, LB_GETTEXT, index, ctypes.addressof(buffer))
            lines.append(buffer.value.decode("gbk", errors="replace"))
        total = count
    else:
        length = int(user32.SendMessageA(hwnd, WM_GETTEXTLENGTH, 0, 0))
        buffer = ctypes.create_string_buffer(length + 1)
        user32.SendMessageA(hwnd, WM_GETTEXT, len(buffer), ctypes.addressof(buffer))
        all_lines = buffer.value.decode("gbk", errors="replace").splitlines()
        total = len(all_lines)
        lines = all_lines[-limit:] if limit else all_lines

    return {
        "success": True, "available": True, "source": "win32_bottom_console",
        "panel": panel, "control_class": cls, "match_score": score,
        "line_count": total, "returned_count": len(lines), "lines": lines,
        "text": "\n".join(lines),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Read the PostEngineer bottom console")
    parser.add_argument("--limit", "-n", type=int, default=0)
    parser.add_argument("--channel", "-c", choices=("auto", "output", "info", "debug"), default="auto")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    result = read_bottom_console(limit=args.limit, channel=args.channel)
    print(json.dumps(result, ensure_ascii=False, indent=2) if args.json else result.get("text", result.get("error", "")))
    if not result["success"]:
        raise SystemExit(1)
