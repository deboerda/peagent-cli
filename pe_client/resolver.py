"""PE Client — 名称解析 + 数据校验

提供类型名/效果名/域名的模糊匹配和别名解析，
以及 Pydantic 模型的 partial/full 校验。
"""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel, ValidationError

from .registry import OBJECT_REGISTRY, PE_REGISTRY, PE_DOMAIN_ALIASES


def _normalize_name(value: str) -> str:
    return value.strip().casefold()


def resolve_obj_type(obj_type: str) -> str | None:
    """解析对象类型名（支持中文别名）"""
    normalized = _normalize_name(obj_type)
    for key, reg in OBJECT_REGISTRY.items():
        if normalized == _normalize_name(key):
            return key
        if any(normalized == _normalize_name(alias) for alias in reg.get("aliases", [])):
            return key
    return None


def resolve_capability(capability: str) -> str | None:
    """解析 PE capability 名称（支持中文别名）"""
    normalized = _normalize_name(capability)
    for key, reg in PE_REGISTRY.items():
        if normalized == _normalize_name(key):
            return key
        if any(normalized == _normalize_name(alias) for alias in reg.get("aliases", [])):
            return key
    return None


def resolve_domain(domain: str) -> str | None:
    """解析 domain 名称（支持中文别名）"""
    if not domain:
        return ""
    return PE_DOMAIN_ALIASES.get(_normalize_name(domain))


def resolve_capability_for_domains(
    capability: str, allowed_domains: set[str]
) -> tuple[str | None, dict[str, Any] | None, dict | None]:
    """解析 capability 并检查 domain 权限"""
    resolved = resolve_capability(capability)
    if not resolved:
        return None, None, {"success": False, "error": f"未知 PE capability: {capability}"}

    reg = PE_REGISTRY[resolved]
    if reg["domain"] not in allowed_domains:
        return (
            None, None,
            {"success": False, "error": f"{resolved} 属于 {reg['domain']} 域，不能在当前工具中调用"},
        )
    return resolved, reg, None


def validate_partial(model: type[BaseModel], data: dict[str, Any]) -> tuple[dict[str, Any] | None, dict | None]:
    """Partial update 校验：只返回提供的字段"""
    try:
        validated = model.model_validate(data)
        changes = validated.model_dump(exclude_unset=True, exclude_none=True)
    except ValidationError as exc:
        return None, {"success": False, "error": f"data 格式错误: {exc}"}
    if not changes:
        return None, {"success": False, "error": "data 不能为空，至少提供一个可修改字段"}
    return changes, None


def validate_full(model: type[BaseModel], data: dict[str, Any]) -> tuple[dict[str, Any] | None, dict | None]:
    """Full create 校验：返回所有字段（含默认值）"""
    try:
        validated = model.model_validate(data)
        return validated.model_dump(exclude_none=True), None
    except ValidationError as exc:
        return None, {"success": False, "error": f"data 格式错误: {exc}"}


def build_model_template(model: type[BaseModel]) -> dict[str, Any]:
    """从 Pydantic 模型生成字段模板（schema + 默认值 + 描述）"""
    default_example: dict[str, Any] = {}
    for name, field in model.model_fields.items():
        if field.is_required():
            continue
        default_value = field.get_default(call_default_factory=True)
        if default_value is not None:
            default_example[name] = default_value

    return {
        "schema": model.model_json_schema(),
        "default_example": default_example,
        "field_descriptions": {
            name: field.description or ""
            for name, field in model.model_fields.items()
        },
    }


def build_query_input(target_kind: str, operation: str) -> dict[str, str]:
    """根据 target_kind 和 operation 生成查询参数描述"""
    if target_kind == "scene":
        return {}
    if target_kind == "node":
        return {"node_path": "必填，模型节点路径"}
    if target_kind == "probe":
        if operation == "list":
            return {}
        if operation == "get":
            return {"probe_key": "必填，要查询的探针 key"}
        if operation == "delete":
            return {"probe_key": "单删时填写；delete_all=true 时可留空", "delete_all": "true 时删除全部探针"}
        if operation == "render":
            return {"probe_key": "填写时只渲染单个探针；留空则渲染全部", "exclude_reflection": "仅在渲染全部时有意义"}
        if operation == "modify":
            return {"probe_key": "必填，要修改的探针 key"}
    return {}


def require_node_path(node_path: str) -> dict | None:
    """校验 node_path 非空"""
    if node_path.strip():
        return None
    return {"success": False, "error": "node_path 不能为空，必须是有效的模型节点路径"}