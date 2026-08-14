"""PE Script Search — 函数搜索索引

原地搜索 1927 个 VS_API 函数，支持中文关键词 + 英文函数名混合搜索。
Agent 通过此模块按需检索函数信息，无需加载全部函数到 context。

设计原则：
- 零外部依赖，纯 Python 内存搜索
- 中文描述优先匹配（100% 函数有中文描述）
- 返回紧凑结果，节省 token

用法:
    from pe_client.script_search import search_functions, get_function, get_module_index

    results = search_functions("创建球体 设置颜色")
    # → [{name, module, description, score}, ...]

    detail = get_function("VsCreateSphere")
    # → {name, module, return_type, params, description, param_descriptions}
"""

from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import Any

# ── 加载 ──────────────────────────────────────────────────

_INDEX: dict[str, dict[str, Any]] | None = None
_MODULES: dict[str, list[dict[str, Any]]] | None = None
_MODULE_INDEX_TEXT: str | None = None


def _load_index() -> tuple[dict[str, dict[str, Any]], dict[str, list[dict[str, Any]]]]:
    """懒加载：从 api_functions.json 构建搜索索引"""
    global _INDEX, _MODULES

    if _INDEX is not None:
        return _INDEX, _MODULES

    # 查找 api_functions.json
    candidates = [
        Path(os.environ.get("PE_AGENT_ROOT", "")) / "函数接口 2026-05-22-11-45-32" / "2026-05-22-11-45-32" / "api_functions.json",
        Path(__file__).parent.parent / "函数接口 2026-05-22-11-45-32" / "2026-05-22-11-45-32" / "api_functions.json",
        Path("E:/PEagent/函数接口 2026-05-22-11-45-32/2026-05-22-11-45-32/api_functions.json"),
    ]
    json_path = None
    for p in candidates:
        if p.exists():
            json_path = p
            break

    if json_path is None:
        raise FileNotFoundError(
            "Cannot find api_functions.json. Searched:\n" +
            "\n".join(f"  - {p}" for p in candidates)
        )

    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    _INDEX = {}
    _MODULES = {}

    for module in data["modules"]:
        module_name = module["module"]
        _MODULES[module_name] = []
        for func in module["functions"]:
            name = func["name"]
            entry = {
                "name": name,
                "module": module_name,
                "return_type": func.get("return_type", "void").replace("VS_API ", ""),
                "params": func.get("params", ""),
                "description": func.get("description", ""),
                "param_descriptions": func.get("param_descriptions", []),
            }
            _INDEX[name] = entry
            _MODULES[module_name].append(entry)

    return _INDEX, _MODULES


# ── 搜索核心 ──────────────────────────────────────────────


def _tokenize(text: str) -> list[str]:
    """中文单字 + 英文单词 混合分词"""
    tokens = []
    # 英文单词
    for m in re.finditer(r"[a-zA-Z_][a-zA-Z0-9_]*", text):
        tokens.append(m.group().lower())
    # 中文单字
    for ch in text:
        if "\u4e00" <= ch <= "\u9fff":
            tokens.append(ch)
    return tokens


def _score_function(func: dict[str, Any], query_tokens: list[str]) -> float:
    """计算函数与查询的匹配度。

    计分规则：
    - 函数名精确匹配：+10
    - 函数名部分匹配（单词层级）：+5
    - 描述匹配：+3/token
    - 模块名匹配：+1
    - 参数描述匹配：+1/token
    """
    score = 0.0
    name_lower = func["name"].lower()

    # 函数名匹配
    name_tokens = set(_tokenize(func["name"]))
    for t in query_tokens:
        if t == name_lower:  # 精确匹配
            score += 10
        elif t in name_tokens:  # 单词匹配
            score += 5
        elif t in name_lower:  # 子串匹配
            score += 3

    # 描述匹配（中文）
    desc = func.get("description", "")
    for t in query_tokens:
        if t in desc:
            score += 3

    # 模块名匹配
    module = func.get("module", "")
    for t in query_tokens:
        if t in module.lower():
            score += 1

    # 参数描述匹配
    params_text = " ".join(func.get("param_descriptions", []))
    for t in query_tokens:
        if t in params_text:
            score += 1

    return score


def search_functions(
    query: str,
    module: str = "",
    limit: int = 10,
    min_score: float = 1.0,
) -> list[dict[str, Any]]:
    """搜索 PE 函数。

    Args:
        query: 搜索关键词（中英文混合，如 "创建球体 设置颜色"）
        module: 按模块名过滤（可选，如 "Camera"）
        limit: 返回结果数量上限
        min_score: 最低匹配分数

    Returns:
        [{name, module, description, params_preview, return_type, score}, ...]
        紧凑格式，不含完整参数描述以节省 token。
    """
    _INDEX, _MODULES = _load_index()
    query_tokens = _tokenize(query)

    if not query_tokens:
        return []

    scored = []
    target = _MODULES.get(module, []) if module else _INDEX.values()

    for func in target:
        score = _score_function(func, query_tokens)
        if score >= min_score:
            scored.append((score, func))

    scored.sort(key=lambda x: -x[0])

    results = []
    for score, func in scored[:limit]:
        params = func.get("params", "") or ""
        params_preview = params[:80] + ("..." if len(params) > 80 else "")
        results.append({
            "name": func["name"],
            "module": func["module"],
            "description": func.get("description", ""),
            "params_preview": params_preview,
            "return_type": func.get("return_type", "void"),
            "score": round(score, 1),
        })

    return results


def get_function(name: str) -> dict[str, Any] | None:
    """获取单个函数的完整信息。

    Args:
        name: 函数名（精确匹配，如 "VsCreateSphere"）

    Returns:
        {name, module, return_type, params, description, param_descriptions}
        若未找到返回 None。
    """
    _INDEX, _MODULES = _load_index()
    func = _INDEX.get(name)
    if func is None:
        return None
    return dict(func)  # 返回副本


def get_module_index() -> str:
    """获取模块索引（紧凑格式，~1,600 tokens）。

    用于 Agent 首次了解有哪些模块可用。每行一个模块：
    ModuleName (N funcs): func1, func2, func3 +M more

    Returns:
        模块索引文本
    """
    global _MODULE_INDEX_TEXT
    if _MODULE_INDEX_TEXT is not None:
        return _MODULE_INDEX_TEXT

    _INDEX, _MODULES = _load_index()
    lines = ["PE Script API — 模块索引\n"]

    for mod_name in sorted(_MODULES.keys()):
        funcs = _MODULES[mod_name]
        count = len(funcs)
        # 取前 5 个函数名作为样本
        samples = [f["name"] for f in funcs[:5]]
        sample_str = ", ".join(samples)
        if count > 5:
            sample_str += f" +{count - 5} more"
        lines.append(f"  {mod_name:15s} ({count:3d}) {sample_str}")

    _MODULE_INDEX_TEXT = "\n".join(lines)
    return _MODULE_INDEX_TEXT


def get_module_functions(module: str, limit: int = 20) -> list[dict[str, Any]]:
    """获取某个模块下的所有函数（紧凑格式）。

    Args:
        module: 模块名
        limit: 最多返回数量

    Returns:
        函数列表，紧凑格式
    """
    _INDEX, _MODULES = _load_index()
    funcs = _MODULES.get(module, [])
    results = []
    for f in funcs[:limit]:
        params = f.get("params", "") or ""
        params_preview = params[:80] + ("..." if len(params) > 80 else "")
        results.append({
            "name": f["name"],
            "module": f["module"],
            "description": f.get("description", ""),
            "params_preview": params_preview,
            "return_type": f.get("return_type", "void"),
        })

    if len(funcs) > limit:
        results.append({
            "name": f"... +{len(funcs) - limit} more functions",
            "module": module,
            "description": f"Use search_functions() to find specific functions",
            "params_preview": "",
            "return_type": "",
        })

    return results


# ── 批量查询（Agent 常用） ─────────────────────────────────

def get_functions(names: list[str]) -> dict[str, dict[str, Any] | None]:
    """批量获取多个函数的完整信息。

    Args:
        names: 函数名列表

    Returns:
        {name: detail_or_None}
    """
    _INDEX, _MODULES = _load_index()
    return {name: (dict(_INDEX[name]) if name in _INDEX else None) for name in names}