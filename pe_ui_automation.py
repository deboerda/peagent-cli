"""Deterministic PostEngineer UI automation primitives.

This module deliberately does not use screenshots or image matching.  It
locates the PE window and child controls by Win32 HWND/class/title, sends
window messages, and reports state before and after every operation.  Higher
level graph actions should be built on these primitives only after their
command/control contract has been calibrated for the installed PE build.
"""

from __future__ import annotations

import ctypes
import time
from ctypes import wintypes
from dataclasses import asdict, dataclass
from typing import Iterable


user32 = ctypes.WinDLL("user32", use_last_error=True)

WM_COMMAND = 0x0111
WM_SETTEXT = 0x000C
BM_CLICK = 0x00F5
WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
WM_USER = 0x0400
TB_BUTTONCOUNT = WM_USER + 24
TB_GETBUTTON = WM_USER + 23
TB_GETITEMRECT = WM_USER + 29
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004


@dataclass(frozen=True)
class WindowInfo:
    hwnd: int
    parent: int
    class_name: str
    title: str
    visible: bool
    enabled: bool

    def as_dict(self) -> dict:
        return asdict(self)


class _TBBUTTON64(ctypes.Structure):
    _fields_ = [
        ("iBitmap", ctypes.c_int),
        ("idCommand", ctypes.c_int),
        ("fsState", ctypes.c_ubyte),
        ("fsStyle", ctypes.c_ubyte),
        ("reserved", ctypes.c_ubyte * 2),
        ("dwData", ctypes.c_void_p),
        ("iString", ctypes.c_void_p),
    ]


class _RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]


def _remote_struct(hwnd: int, message: int, index: int, struct_type):
    """Read a toolbar structure from the target process safely."""
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(wintypes.HWND(hwnd), ctypes.byref(pid))
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    process = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    if not process:
        return None
    kernel32.VirtualAllocEx.restype = ctypes.c_void_p
    kernel32.ReadProcessMemory.argtypes = [
        wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
        ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
    ]
    kernel32.ReadProcessMemory.restype = wintypes.BOOL
    remote = kernel32.VirtualAllocEx(process, None, ctypes.sizeof(struct_type), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    if not remote:
        kernel32.CloseHandle(process)
        return None
    try:
        # HWND/LPARAM are pointer-sized on the x64 PE build.  Without
        # explicit prototypes ctypes truncates the remote address and the
        # toolbar probe fails before any UI action is attempted.
        user32.SendMessageW.argtypes = (
            wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM,
        )
        user32.SendMessageW.restype = wintypes.LPARAM
        user32.SendMessageW(wintypes.HWND(hwnd), message, index, remote)
        value = struct_type()
        got = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(process, ctypes.c_void_p(remote), ctypes.byref(value), ctypes.sizeof(value), ctypes.byref(got)):
            return None
        return value
    finally:
        kernel32.VirtualFreeEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
        kernel32.VirtualFreeEx.restype = wintypes.BOOL
        kernel32.VirtualFreeEx(process, remote, 0, MEM_RELEASE)
        kernel32.CloseHandle(process)


def toolbar_buttons(root: int, min_count: int = 1) -> list[dict]:
    """Return safe metadata for visible toolbar buttons in PE."""
    result: list[dict] = []
    for toolbar in enum_children(root):
        if "toolbar" not in toolbar.class_name.lower():
            continue
        count = int(user32.SendMessageW(toolbar.hwnd, TB_BUTTONCOUNT, 0, 0))
        if count < min_count:
            continue
        for index in range(count):
            button = _remote_struct(toolbar.hwnd, TB_GETBUTTON, index, _TBBUTTON64)
            rect = _remote_struct(toolbar.hwnd, TB_GETITEMRECT, index, _RECT)
            if button is None or rect is None:
                continue
            result.append({
                "toolbar_hwnd": toolbar.hwnd,
                "toolbar_title": toolbar.title,
                "index": index,
                "command_id": int(button.idCommand),
                "state": int(button.fsState),
                "style": int(button.fsStyle),
                "rect": {"left": rect.left, "top": rect.top, "right": rect.right, "bottom": rect.bottom},
            })
    return result


def _screen_point(hwnd: int, rect: dict) -> tuple[int, int]:
    window_rect = _RECT()
    user32.GetWindowRect(wintypes.HWND(hwnd), ctypes.byref(window_rect))
    return (
        int(window_rect.left + (rect["left"] + rect["right"]) / 2),
        int(window_rect.top + (rect["top"] + rect["bottom"]) / 2),
    )


def drag_toolbar_button_to_window(root: int, toolbar_hwnd: int, index: int, target_hwnd: int) -> dict:
    """Drag a calibrated toolbar tool into a target canvas window."""
    rect = _remote_struct(toolbar_hwnd, TB_GETITEMRECT, index, _RECT)
    if rect is None:
        return {"success": False, "error": "toolbar item rectangle unavailable"}
    source = _screen_point(toolbar_hwnd, {"left": rect.left, "top": rect.top, "right": rect.right, "bottom": rect.bottom})
    target_rect = _RECT()
    user32.GetWindowRect(wintypes.HWND(target_hwnd), ctypes.byref(target_rect))
    target = (int((target_rect.left + target_rect.right) / 2), int((target_rect.top + target_rect.bottom) / 2))
    return drag_toolbar_button_to_point(root, toolbar_hwnd, index, target)


def drag_toolbar_button_to_point(root: int, toolbar_hwnd: int, index: int, target: tuple[int, int]) -> dict:
    """Drag a calibrated toolbar tool to an explicit screen point."""
    rect = _remote_struct(toolbar_hwnd, TB_GETITEMRECT, index, _RECT)
    if rect is None:
        return {"success": False, "error": "toolbar item rectangle unavailable"}
    source = _screen_point(toolbar_hwnd, {"left": rect.left, "top": rect.top, "right": rect.right, "bottom": rect.bottom})
    user32.SetCursorPos(*source)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.15)
    steps = 12
    for step in range(1, steps + 1):
        x = int(source[0] + (target[0] - source[0]) * step / steps)
        y = int(source[1] + (target[1] - source[1]) * step / steps)
        user32.SetCursorPos(x, y)
        user32.mouse_event(MOUSEEVENTF_MOVE, 0, 0, 0, 0)
        time.sleep(0.03)
    time.sleep(0.15)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    return {"success": True, "source": source, "target": target, "toolbar_hwnd": toolbar_hwnd, "index": index}


def _text(hwnd: int, ansi: bool = True) -> str:
    if not hwnd:
        return ""
    if ansi:
        length = int(user32.GetWindowTextLengthA(hwnd))
        buf = ctypes.create_string_buffer(max(1, length + 1))
        user32.GetWindowTextA(hwnd, buf, len(buf))
        return buf.value.decode("gbk", errors="replace")
    length = int(user32.GetWindowTextLengthW(hwnd))
    buf = ctypes.create_unicode_buffer(max(1, length + 1))
    user32.GetWindowTextW(hwnd, buf, len(buf))
    return buf.value


def _class_name(hwnd: int) -> str:
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, len(buf))
    return buf.value


def _window_info(hwnd: int, parent: int = 0) -> WindowInfo:
    return WindowInfo(
        hwnd=int(hwnd),
        parent=int(parent or user32.GetParent(hwnd) or 0),
        class_name=_class_name(hwnd),
        title=_text(hwnd),
        visible=bool(user32.IsWindowVisible(hwnd)),
        enabled=bool(user32.IsWindowEnabled(hwnd)),
    )


def enum_windows(visible_only: bool = False) -> list[WindowInfo]:
    result: list[WindowInfo] = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def callback(hwnd, _lparam):
        if not visible_only or user32.IsWindowVisible(hwnd):
            result.append(_window_info(hwnd))
        return True

    user32.EnumWindows(callback, 0)
    return result


def enum_children(root: int, visible_only: bool = False) -> list[WindowInfo]:
    result: list[WindowInfo] = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def callback(hwnd, _lparam):
        if not visible_only or user32.IsWindowVisible(hwnd):
            result.append(_window_info(hwnd, root))
        return True

    user32.EnumChildWindows(wintypes.HWND(root), callback, 0)
    return result


def find_main_window(title_contains: str = "PostEngineer") -> WindowInfo | None:
    candidates = [
        item for item in enum_windows(visible_only=True)
        if title_contains.lower() in item.title.lower()
        or item.class_name == "PEMainWindowClass"
    ]
    return candidates[0] if candidates else None


def find_child(
    root: int,
    *,
    class_name: str | None = None,
    title: str | None = None,
    title_contains: str | None = None,
    visible_only: bool = False,
) -> WindowInfo | None:
    for item in enum_children(root, visible_only=visible_only):
        if class_name and item.class_name.lower() != class_name.lower():
            continue
        if title is not None and item.title != title:
            continue
        if title_contains is not None and title_contains.lower() not in item.title.lower():
            continue
        return item
    return None


def set_text(hwnd: int, value: str) -> bool:
    """Set an Edit/property value without keyboard simulation."""
    result = user32.SendMessageW(wintypes.HWND(hwnd), WM_SETTEXT, 0, value)
    return bool(result)


def click(hwnd: int) -> bool:
    """Invoke a standard Button control through BM_CLICK."""
    if not hwnd:
        return False
    user32.SendMessageW(wintypes.HWND(hwnd), BM_CLICK, 0, 0)
    return True


def post_command(root: int, command_id: int, source_hwnd: int = 0) -> bool:
    """Post a calibrated WM_COMMAND to the PE window."""
    wparam = int(command_id) & 0xFFFF
    if source_hwnd:
        wparam |= (0 << 16)
    return bool(user32.PostMessageW(wintypes.HWND(root), WM_COMMAND, wparam, source_hwnd))


def wait_for_child(
    root: int,
    *,
    class_name: str | None = None,
    title_contains: str | None = None,
    timeout: float = 5.0,
) -> WindowInfo | None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        item = find_child(root, class_name=class_name, title_contains=title_contains)
        if item:
            return item
        time.sleep(0.05)
    return None


def inspect_pe() -> dict:
    root = find_main_window()
    if not root:
        return {"success": False, "error": "PostEngineer main window not found"}
    children = enum_children(root.hwnd, visible_only=False)
    return {
        "success": True,
        "main_window": root.as_dict(),
        "children": [item.as_dict() for item in children],
    }
