"""PE Client — 注册表

单一真实来源：所有对象类型和 PE 效果的定义。
CLI 和 MCP 共用同一份注册表，新增能力只需在这里添加。
"""

from __future__ import annotations

from typing import Any

from . import models

# ======================================================================
# 对象注册表
# ======================================================================

OBJECT_REGISTRY: dict[str, dict[str, Any]] = {
    "vis": {
        "description": "模型对象 — 引擎渲染对象",
        "aliases": ["vis模型"],
        "data_model": models.VisData,
    },
    "button": {
        "description": "按钮对象 — 可交互的 UI 按钮",
        "aliases": ["按钮"],
        "data_model": models.ButtonData,
    },
    "image": {
        "description": "图片对象 — 引擎中用来显示图片",
        "aliases": ["图片"],
        "data_model": models.ImageData,
    },
    "variable": {
        "description": "变量对象 — 引擎中的数据变量",
        "aliases": ["变量"],
        "data_model": models.VariableData,
    },
    "body": {
        "description": "刚体对象 — 绑定模型节点的刚体",
        "aliases": ["刚体"],
        "data_model": models.BodyData,
    },
    "control_point": {
        "description": "控制点对象 — 绑定模型节点的操作控制点",
        "aliases": ["控制点"],
        "data_model": models.ControlPointData,
    },
    "spinner": {
        "description": "螺旋体对象 — 绕轴旋转的螺旋件",
        "aliases": ["螺旋体"],
        "data_model": models.SpinnerData,
    },
    "remark": {
        "description": "文字标签图表",
        "aliases": ["文字", "文字标签", "remark"],
        "data_model": models.RemarkData,
    },
    "curve": {
        "description": "曲线图表",
        "aliases": ["曲线", "曲线图", "折线图", "polyline", "chart_polyline"],
        "data_model": models.CurveChartData,
    },
    "number": {
        "description": "数字显示图表",
        "aliases": ["数字", "数字显示", "number"],
        "data_model": models.NumberChartData,
    },
    "progress": {
        "description": "进度条图表",
        "aliases": ["进度条", "progress"],
        "data_model": models.ProgressChartData,
    },
    "histogram": {
        "description": "柱状图表",
        "aliases": ["柱状图", "bar", "histogram", "chart_bar"],
        "data_model": models.HistogramChartData,
    },
    "table": {
        "description": "属性表图表",
        "aliases": ["属性表", "table", "property", "chart_property"],
        "data_model": models.TableChartData,
    },
    "dashBoard": {
        "description": "仪表盘图表",
        "aliases": ["仪表盘", "dashboard"],
        "data_model": models.DashBoardChartData,
    },
    "grid": {
        "description": "网格表图表",
        "aliases": ["网格表", "grid", "chart_grid"],
        "data_model": models.GridChartData,
    },
}

# ======================================================================
# PE 效果注册表
# ======================================================================

PE_REGISTRY: dict[str, dict[str, Any]] = {
    "shadow": {
        "domain": "node_effect",
        "description": "节点阴影开关",
        "aliases": ["阴影", "shadowvolume"],
        "target_kind": "node",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.ShadowSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "shadow",
    },
    "ao": {
        "domain": "scene_effect",
        "description": "环境遮挡（AO）",
        "aliases": ["环境遮挡", "AO", "ssao"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.AOSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "ssao",
    },
    "bloom": {
        "domain": "scene_effect",
        "description": "辉光",
        "aliases": ["辉光"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.BloomSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "bloom",
    },
    "ssr_scene": {
        "domain": "scene_effect",
        "description": "场景级屏幕空间反射（SSR）",
        "aliases": ["SSR场景", "场景SSR", "屏幕空间反射场景"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.SSRSceneSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "ssr_scene",
    },
    "ssr_node": {
        "domain": "node_effect",
        "description": "节点级 SSR 反射强度",
        "aliases": ["节点SSR", "SSR节点", "屏幕空间反射节点"],
        "target_kind": "node",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.SSRNodeSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "ssr_node",
    },
    "mirror_surface": {
        "domain": "node_effect",
        "description": "镜面表面设置",
        "aliases": ["镜面表面", "mirror_surface"],
        "target_kind": "node",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.MirrorSurfaceSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "mirror_surface",
    },
    "mirror_participation": {
        "domain": "node_effect",
        "description": "节点是否参与镜面反射",
        "aliases": ["镜面参与反射", "mirror_participation"],
        "target_kind": "node",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.MirrorParticipationSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "mirror_participation",
    },
    "mirror_scene": {
        "domain": "scene_effect",
        "description": "镜面场景参数",
        "aliases": ["镜面场景", "镜面反射深度", "mirror_scene"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.MirrorSceneSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "mirror_scene",
    },
    "probe_factor": {
        "domain": "scene_effect",
        "description": "全局光强度",
        "aliases": ["全局光强度", "光照探针全局光强度", "probefactor"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.ProbeFactorSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "probefactor",
    },
    "air_particle_density": {
        "domain": "scene_effect",
        "description": "空气微粒浓度",
        "aliases": ["空气微粒浓度", "airparticledensity"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.AirParticleDensitySettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "airparticledensity",
    },
    "atmosphere": {
        "domain": "scene_effect",
        "description": "大气效果",
        "aliases": ["大气", "atmosphere"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.AtmosphereSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "atmosphere",
    },
    "sun": {
        "domain": "scene_effect",
        "description": "阳光效果",
        "aliases": ["阳光", "sun"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.SunSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "sun",
    },
    "fog": {
        "domain": "scene_effect",
        "description": "雾效果",
        "aliases": ["雾", "fog"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.FogSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "fog",
    },
    "material": {
        "domain": "appearance",
        "description": "节点材质与 shader",
        "aliases": ["材质", "material", "shader"],
        "target_kind": "node",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.MaterialSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "material",
    },
    "texture": {
        "domain": "appearance",
        "description": "节点纹理",
        "aliases": ["纹理", "texture"],
        "target_kind": "node",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.TextureSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "texture",
    },
    "light_probe": {
        "domain": "light_probe",
        "description": "光照探针的创建、查询、更新、删除和渲染",
        "aliases": ["光照探针", "probe", "lightprobe"],
        "target_kind": "probe",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.LightProbeUpdate,
        "models": {
            "create": models.LightProbeCreate,
            "modify": models.LightProbeUpdate,
        },
        "operations": ["create", "list", "get", "modify", "delete", "render"],
        "response_keys": ["success", "error", "effect_name", "target", "state", "probes"],
        "engine_effect_name": "light_probe",
    },
    "postprocess": {
        "domain": "scene_effect",
        "description": "后处理色调参数",
        "aliases": ["后处理", "postprocess"],
        "target_kind": "scene",
        "supports_get": True,
        "supports_set": True,
        "set_model": models.PostprocessSettings,
        "response_keys": ["success", "error", "effect_name", "target", "state"],
        "engine_effect_name": "postprocess",
    },
}

# ======================================================================
# Domain 别名映射
# ======================================================================

PE_DOMAIN_ALIASES: dict[str, str] = {
    "scene_effect": "scene_effect",
    "scene": "scene_effect",
    "scene-effect": "scene_effect",
    "场景": "scene_effect",
    "场景效果": "scene_effect",
    "node_effect": "node_effect",
    "node": "node_effect",
    "node-effect": "node_effect",
    "节点": "node_effect",
    "节点效果": "node_effect",
    "appearance": "appearance",
    "外观": "appearance",
    "节点外观": "appearance",
    "material": "appearance",
    "texture": "appearance",
    "light_probe": "light_probe",
    "probe": "light_probe",
    "light-probe": "light_probe",
    "光照探针": "light_probe",
}

# 人类可读的 domain 标签
DOMAIN_LABELS: dict[str, str] = {
    "scene_effect": "场景效果",
    "node_effect": "节点效果",
    "appearance": "外观",
    "light_probe": "光照探针",
}