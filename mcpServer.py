"""PE MCP Server — 引擎对象工具 + PE 效果工具

基于 pe_client 共享库，通过 FastMCP 暴露为 MCP tools。
所有业务逻辑（HTTP 调用、校验、注册表）在 pe_client 中。
"""

from __future__ import annotations

from typing import Any

from fastmcp import FastMCP

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
    models,
    registry,
    resolver,
    client,
    search_functions,
    get_function,
    get_module_index,
    get_module_functions,
    get_functions,
)

mcp = FastMCP("pe-engine")

# ======================================================================
# 对象 MCP Tools
# ======================================================================


@mcp.tool(
    description=(
        "【第1步】列出所有可用的引擎对象类型及描述。"
        "返回精简列表（类型名 + 一句话描述），不包含字段详情。"
        "需要了解某类型的详细字段时，请调用 engine_get_template。"
    )
)
def engine_list_types() -> dict:
    return list_types()


@mcp.tool(
    description=(
        "【第2步】获取指定对象类型的字段模板。"
        "返回所有字段名、类型、默认值、描述。"
        "创建对象前应先调用此工具了解有哪些可填字段及其默认值。"
        "也支持传入中文别名（如 '按钮'）作为 obj_type。"
    )
)
def engine_get_template(obj_type: str) -> dict:
    return get_template(obj_type)


@mcp.tool(
    description=(
        "【第3步】创建引擎对象。"
        "请按照 engine_get_template 返回的模板填写 data 字典。"
        "重要规则：\n"
        "1. name 在引擎中唯一，重名会创建失败（引擎返回错误）\n"
        "2. 不确定的字段不要猜测，使用默认值或留空\n"
        "3. 路径类字段（node_path, path_normal 等）留空表示不设置，不要编造路径\n"
    )
)
def engine_create_object(obj_type: str, data: dict[str, Any]) -> dict:
    return create_object(obj_type, data)


@mcp.tool(
    description=(
        "按对象类型导出该类型全部对象，并返回导出的 JSON 文件路径。"
        "必须指定 obj_type（和 create 一致）。"
        "返回结果包含 file_path 和 count，AI 可按路径继续读取对象详情。"
    )
)
def engine_list_objects(obj_type: str) -> dict:
    return list_objects(obj_type)


@mcp.tool(
    description=(
        "读取 PostEngineer 输出窗口中的引擎输出。"
        "重要限制：只有当 PEPlayer 顶层窗口没有运行时，这些输出才可读取。"
        "limit=0 表示返回全部输出；limit>0 表示只返回最近 limit 条。"
    )
)
def engine_get_output(limit: int = 0) -> dict:
    return get_output(limit)


@mcp.tool(
    description=(
        "查询 PE HTTP 服务、活动引擎视图、命令队列和最近一次 Tick 状态。"
        "当其他引擎工具连接失败或超时时，应先调用此工具区分服务器未启动和引擎视图不可用。"
    )
)
def engine_get_status() -> dict:
    return get_status()


@mcp.tool(
    description=(
        "修改引擎中已有对象的属性。"
        "obj_type 和 name 必填：obj_type 和 create 一致，"
        "name 必须是查询到的名字（不能修改名字本身）。"
        "data 字典只包含要修改的属性，未列出的属性保持不变。"
        "修改前请先用 engine_list_objects 确认对象存在。"
    )
)
def engine_modify_object(obj_type: str, name: str, data: dict[str, Any]) -> dict:
    return modify_object(obj_type, name, data)


# ======================================================================
# PE MCP Tools
# ======================================================================


@mcp.tool(
    description=(
        "【PE 第1步】列出所有可用的 PE capability。"
        "domain 可选：scene_effect / node_effect / appearance / light_probe。"
        "拿到 capability 名称后，再调用 pe_get_template 查看参数模板。"
    )
)
def pe_list_capabilities(domain: str = "") -> dict:
    return list_capabilities(domain)


@mcp.tool(
    description=(
        "【PE 第2步】获取 PE capability 的参数模板。"
        "普通效果默认 operation='set'；"
        "light_probe 支持 create / list / get / modify / delete / render。"
    )
)
def pe_get_template(capability: str, operation: str = "set") -> dict:
    return get_capability_template(capability, operation)


@mcp.tool(
    description=(
        "【PE 第3步】查询场景级效果状态。"
        "effect_name 必须是 scene_effect 域的 capability。"
        "建议先用 pe_list_capabilities 或 pe_get_template 确认 capability 名称。"
    )
)
def pe_get_scene_effect(effect_name: str) -> dict:
    return get_scene_effect(effect_name)


@mcp.tool(
    description=(
        "【PE 第4步】设置场景级效果。"
        "effect_name 必须是 scene_effect 域的 capability。"
        "data 只传要修改的字段，MCP 会按 partial-update 语义校验。"
    )
)
def pe_set_scene_effect(effect_name: str, data: dict[str, Any]) -> dict:
    return set_scene_effect(effect_name, data)


@mcp.tool(
    description=(
        "截图当前活动渲染画面。"
        "用于在多次调参之后单独请求一张当前画面，而不是每一步都返回截图。"
    )
)
def pe_capture_current_frame(label: str = "") -> dict:
    return capture_frame(label)


@mcp.tool(
    description=(
        "批量设置多个 scene_effect。"
        "每个 item 包含 effect_name 和 data。"
        "该工具只负责批量调参，不会自动截图；如需画面请单独调用 pe_capture_current_frame。"
    )
)
def pe_set_scene_effects(items: list[models.SceneEffectBatchItem]) -> dict:
    return set_scene_effects([item.model_dump() for item in items])


# ======================================================================
# PE Script Agent Tools
# ======================================================================


@mcp.tool(
    description=(
        "【脚本搜索】搜索 PE 引擎的 1927 个原生脚本函数。\n"
        "支持中英文混合搜索，如 \"创建球体 设置颜色\"、\"camera move fog\"。\n"
        "返回紧凑列表（函数名、模块、描述、参数预览），不包含完整参数详情。\n"
        "找到候选函数后，用 pe_script_get_function 获取完整签名和参数说明。\n"
        "也可先用 pe_script_module_index 了解有哪些模块。"
    )
)
def pe_script_search_functions(query: str, module: str = "", limit: int = 10) -> dict:
    results = search_functions(query, module=module, limit=limit)
    return {
        "success": True,
        "query": query,
        "count": len(results),
        "functions": results,
        "hint": "Use pe_script_get_function(name) to get full signature and parameter details for any function above.",
    }


@mcp.tool(
    description=(
        "【脚本函数详情】获取单个 PE 脚本函数的完整信息：返回值、参数列表、描述、参数说明。\n"
        "name 必须是 pe_script_search_functions 返回的精确函数名。\n"
    )
)
def pe_script_get_function(name: str) -> dict:
    func = get_function(name)
    if func is None:
        return {"success": False, "error": f"Function not found: {name}"}
    return {"success": True, **func}


@mcp.tool(
    description=(
        "【脚本模块索引】列出 PE 引擎所有 63 个 API 模块及其函数数量。\n"
        "用于初步了解有哪些模块可用（如 Camera、Light、Animation、Math 等）。\n"
        "每行格式：ModuleName (N funcs) func1, func2, func3 +M more\n"
        "返回约 1,600 tokens 的紧凑文本。"
    )
)
def pe_script_module_index() -> dict:
    index = get_module_index()
    return {"success": True, "index": index}


@mcp.tool(
    description=(
        "【脚本模块展开】获取某个模块下的函数列表（紧凑格式，每行一个函数名+描述）。\n"
        "module 必须是 pe_script_module_index 返回的模块名之一。\n"
        "limit 默认 20，超出部分会提示 +N more。"
    )
)
def pe_script_module_functions(module: str, limit: int = 20) -> dict:
    funcs = get_module_functions(module, limit=limit)
    return {"success": True, "module": module, "count": len(funcs), "functions": funcs}


@mcp.tool(
    description=(
        "【脚本执行】执行 PE 原生脚本字符串。\n"
        "脚本使用 PE DSL 语法（module/begin/end）。\n"
        "生成脚本前请先用 pe_script_search_functions 和 pe_script_get_function 确认函数签名。\n"
        "需要 PE 引擎运行且 PEHelloMCP 插件已加载。"
    )
)
def pe_script_execute(script: str, script_name: str = "") -> dict:
    from pe_client.client import script_execute as _exec
    return _exec(script, script_name)


@mcp.tool(
    description=(
        "【脚本验证】验证 PE 脚本语法，返回是否有效。\n"
        "执行前建议先用此工具检查语法错误。"
    )
)
def pe_script_validate(script: str) -> dict:
    from pe_client.client import script_validate as _validate
    return _validate(script)


# ======================================================================
# 入口
# ======================================================================

if __name__ == "__main__":
    mcp.run(transport="stdio")