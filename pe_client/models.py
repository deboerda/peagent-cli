"""PE Client — 共享 Pydantic 数据模型

所有对象类型和 PE 效果的数据模型，用于校验和自动生成 schema。
与 mcpServer.py 中的定义完全一致。
"""

from __future__ import annotations

from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator


class _BaseData(BaseModel):
    """基类：忽略多余字段，不报错"""
    model_config = ConfigDict(extra="ignore")


class _PEBaseModel(BaseModel):
    """PE 模型基类：允许 partial update，忽略额外字段"""
    model_config = ConfigDict(extra="ignore")

    @staticmethod
    def _validate_vector(value: list[float] | None, size: int, label: str) -> list[float] | None:
        if value is not None and len(value) != size:
            raise ValueError(f"{label} must contain exactly {size} numbers")
        return value


# ======================================================================
# 1. 对象 Data Models
# ======================================================================


class VisData(_BaseData):
    """模型对象 — 引擎渲染对象"""
    path: str = Field(default="", description="模型路径，留空则不设置")


class ButtonData(_BaseData):
    """按钮对象 — 可交互的 UI 按钮"""
    name: str = Field(default="btn", description="按钮名称，引擎中唯一标识")
    text: str = Field(default="Button", description="按钮显示文本")
    position: list[float] = Field(
        default=[0.0, 0.0, 0.0],
        description="位置 [x, y, z], z 表示层级，数值越大越靠前",
    )
    width: float = Field(default=0.3, description="宽度")
    height: float = Field(default=0.1, description="高度")
    path_normal: str = Field(default="", description="常态图片路径，留空则不设置")
    path_normal_mask: str = Field(default="", description="常态图片掩膜路径，留空则不设置")
    path_hover: str = Field(default="", description="鼠标悬停图片路径，留空则不设置")
    path_hover_mask: str = Field(default="", description="鼠标悬停图片掩膜路径，留空则不设置")
    path_pressed: str = Field(default="", description="按下态图片路径，留空则不设置")
    path_pressed_mask: str = Field(default="", description="按下态图片掩膜路径，留空则不设置")


class ImageData(_BaseData):
    """图片对象 — 引擎中用来显示图片"""
    name: str = Field(default="image", description="图片名称，引擎中唯一标识")
    position: list[float] = Field(
        default=[0.0, 0.0, 0.0],
        description="位置 [x, y, z], z 表示层级，数值越大越靠前",
    )
    width: float = Field(default=0.3, description="宽度")
    height: float = Field(default=0.1, description="高度")
    img_path: str = Field(default="", description="主图片路径，留空则不设置")
    mask_path: str = Field(default="", description="蒙版图片路径，留空则不设置")


class VariableData(_BaseData):
    """变量对象 — 引擎中的数据变量"""
    name: str = Field(default="", description="变量名称，引擎中唯一标识，留空则自动生成")
    type: Literal["float", "double", "bool", "long", "string", "int"] = Field(
        default="float", description="变量类型"
    )
    arr_size: int = Field(default=0, description="维度数量，0=标量，1=一维数组，依此类推")
    dims: list[int] = Field(
        default_factory=lambda: [0, 0, 0, 0],
        description="各维度长度，如 [3,0,0,0] 表示长度 3 的一维数组",
    )


class BodyData(_BaseData):
    """刚体对象 — 绑定模型节点的刚体"""
    name: str = Field(default="", description="刚体名称，引擎中唯一标识")
    node_path: str = Field(default="", description="绑定的模型节点路径")


class ControlPointData(_BaseData):
    """控制点对象 — 绑定模型节点的操作控制点"""
    name: str = Field(default="", description="控制点名称，引擎中唯一标识")
    node_path: str = Field(default="", description="绑定的模型节点路径")


class SpinnerData(_BaseData):
    """螺旋体对象 — 绕轴旋转的螺旋件"""
    name: str = Field(default="", description="螺旋体名称，引擎中唯一标识")
    node_path: str = Field(default="", description="绑定的模型节点路径")
    rotation_axis: list[float] = Field(
        default=[0.0, 0.0, 1.0], description="旋转轴方向 [x, y, z]"
    )


class FontData(_BaseData):
    """图表字体设置"""
    size: int = Field(default=20, description="字号，映射为 LOGFONT 的负高度")
    bold: bool = Field(default=False, description="是否粗体")
    italic: bool = Field(default=False, description="是否斜体")
    charset: int = Field(default=134, description="字符集，默认 GB2312_CHARSET")


class RemarkData(_BaseData):
    """文字标签图表"""
    name: str = Field(default="remark", description="对象名称，引擎中唯一标识")
    text: str = Field(default="", description="显示文本")
    position: list[float] = Field(default=[0.0, 0.0, 0.0], description="位置 [x, y, z]")
    width: float = Field(default=0.5, description="宽度")
    height: float = Field(default=0.3, description="高度")
    color: list[float] = Field(default=[1.0, 0.0, 0.0], description="文字颜色 [r, g, b]")
    font: FontData = Field(default_factory=FontData, description="字体设置")
    value_variable: str = Field(default="", description="绑定的变量名")


class CurveChartData(_BaseData):
    """曲线图"""
    name: str = Field(default="curve", description="对象名称，引擎中唯一标识")
    position: list[float] = Field(default=[0.0, 0.0, 0.0], description="位置 [x, y, z]")
    width: float = Field(default=0.5, description="宽度")
    height: float = Field(default=0.3, description="高度")
    color: list[float] = Field(default=[1.0, 0.0, 0.0], description="曲线颜色 [r, g, b]")
    font: FontData = Field(default_factory=lambda: FontData(size=30, bold=True, italic=True), description="字体设置")
    line_width: int = Field(default=3, description="线宽")
    line_type: int = Field(default=0, description="线型")
    decimal_num: int = Field(default=3, description="小数位数")
    curve_type: int = Field(default=1, description="曲线类型")
    style: int = Field(default=1, description="样式")
    x_range: list[float] = Field(default=[-100.0, 90.0], description="X 轴范围 [min, max]")
    y_range: list[float] = Field(default=[0.0, 90.0], description="Y 轴范围 [min, max]")
    y_range2: list[float] = Field(default=[0.0, 0.1], description="第二 Y 轴范围 [min, max]")
    min_unit: list[float] = Field(default=[20.0, 11.0], description="最小刻度 [x, y]")
    value_variable: str = Field(default="", description="绑定的变量名")


class DashBoardChartData(_BaseData):
    """仪表盘"""
    name: str = Field(default="dashboard", description="对象名称，引擎中唯一标识")
    position: list[float] = Field(default=[0.0, 0.0, 0.0], description="位置 [x, y, z]")
    width: float = Field(default=0.5, description="宽度")
    height: float = Field(default=0.3, description="高度")
    decimal_num: int = Field(default=3, description="小数位数")
    style: int = Field(default=1, description="样式")
    range_min: float = Field(default=0.0, description="最小值")
    range_max: float = Field(default=100.0, description="最大值")
    value_variable: str = Field(default="", description="绑定的变量名")


class NumberChartData(_BaseData):
    """数字显示图表"""
    name: str = Field(default="number", description="对象名称，引擎中唯一标识")
    position: list[float] = Field(default=[0.0, 0.0, 0.0], description="位置 [x, y, z]")
    width: float = Field(default=0.5, description="宽度")
    height: float = Field(default=0.3, description="高度")
    color: list[float] = Field(default=[1.0, 0.0, 0.0], description="文字颜色 [r, g, b]")
    font: FontData = Field(default_factory=FontData, description="字体设置")
    value_variable: str = Field(default="", description="绑定的变量名")


class ProgressChartData(_BaseData):
    """进度条图表"""
    name: str = Field(default="progress", description="对象名称，引擎中唯一标识")
    position: list[float] = Field(default=[0.0, 0.0, 0.0], description="位置 [x, y, z]")
    width: float = Field(default=0.3, description="宽度")
    height: float = Field(default=0.02, description="高度")
    color: list[float] = Field(default=[1.0, 0.0, 0.0], description="进度颜色 [r, g, b]")
    range_min: float = Field(default=0.0, description="最小值")
    range_max: float = Field(default=100.0, description="最大值")
    decimal_num: int = Field(default=3, description="小数位数")
    style: int = Field(default=3, description="样式")
    value_variable: str = Field(default="", description="绑定的变量名")


class HistogramChartData(_BaseData):
    """柱状图"""
    name: str = Field(default="histogram", description="对象名称，引擎中唯一标识")
    position: list[float] = Field(default=[0.0, 0.0, 0.0], description="位置 [x, y, z]")
    width: float = Field(default=0.1, description="宽度")
    height: float = Field(default=0.4, description="高度")
    color: list[float] = Field(default=[1.0, 0.0, 0.0], description="柱状图颜色 [r, g, b]")
    range_min: float = Field(default=0.0, description="最小值")
    range_max: float = Field(default=100.0, description="最大值")
    orientation: int = Field(default=0, description="方向")
    value_variable: str = Field(default="", description="绑定的变量名")


class TableChartData(_BaseData):
    """属性表"""
    name: str = Field(default="table", description="对象名称，引擎中唯一标识")
    position: list[float] = Field(default=[0.0, 0.0, 0.0], description="位置 [x, y, z]")
    width: float = Field(default=0.4, description="宽度")
    height: float = Field(default=0.4, description="高度")
    title: str = Field(default="", description="表标题")
    title_color: list[float] = Field(default=[1.0, 0.0, 0.0], description="标题颜色 [r, g, b]")
    back_color: list[float] = Field(default=[1.0, 1.0, 0.0], description="背景颜色 [r, g, b]")
    client_color: list[float] = Field(default=[1.0, 1.0, 1.0], description="客户区颜色 [r, g, b]")
    item_color: list[float] = Field(default=[1.0, 0.5, 1.0], description="项目名颜色 [r, g, b]")
    value_color: list[float] = Field(default=[0.5, 0.5, 1.0], description="值颜色 [r, g, b]")
    line_num: int = Field(default=0, description="行数")
    item_strings: list[str] = Field(default_factory=list, description="每行标题")
    decimal_num: int = Field(default=3, description="小数位数")
    font: FontData = Field(default_factory=lambda: FontData(size=40, bold=True, italic=False), description="字体设置")
    value_variables: list[str] = Field(default_factory=list, description="按行绑定的变量名列表")


class GridChartData(_BaseData):
    """网格表"""
    name: str = Field(default="grid", description="对象名称，引擎中唯一标识")
    position: list[float] = Field(default=[0.0, 0.0, 0.0], description="位置 [x, y, z]")
    width: float = Field(default=0.9, description="宽度")
    height: float = Field(default=0.5, description="高度")
    style: int = Field(default=0, description="样式")
    value_color: list[float] = Field(default=[1.0, 1.0, 1.0], description="值颜色 [r, g, b]")
    back_color: list[float] = Field(default=[1.0, 0.5, 1.0], description="背景色 1 [r, g, b]")
    back_color2: list[float] = Field(default=[0.5, 0.5, 1.0], description="背景色 2 [r, g, b]")
    rows: int = Field(default=0, description="行数")
    cols: int = Field(default=0, description="列数")
    col_widths: list[float] = Field(default_factory=list, description="每列宽度")
    decimal_num: int = Field(default=3, description="小数位数")
    font: FontData = Field(default_factory=lambda: FontData(size=40, bold=True, italic=False), description="字体设置")
    value_variables: list[list[str]] = Field(default_factory=list, description="二维变量绑定 [row][col]")


# ======================================================================
# 2. PE Data Models
# ======================================================================


class ShadowSettings(_PEBaseModel):
    enabled: bool | None = Field(default=None, description="是否开启阴影")


class AOSettings(_PEBaseModel):
    enabled: bool | None = Field(default=None, description="是否开启 AO")
    factor: float | None = Field(default=None, description="AO 强度")
    radius: float | None = Field(default=None, ge=0, description="AO 半径")
    bent_normal: bool | None = Field(default=None, description="是否开启法线折弯")


class BloomSettings(_PEBaseModel):
    enabled: bool | None = Field(default=None, description="是否开启辉光")
    threshold: float | None = Field(default=None, description="辉光阈值")


class SSRSceneSettings(_PEBaseModel):
    enabled: bool | None = Field(default=None, description="是否开启场景级 SSR")
    max_distance: float | None = Field(default=None, ge=0, description="最大反射距离")


class SSRNodeSettings(_PEBaseModel):
    strength: float | None = Field(default=None, description="节点 SSR 反射强度")


class MirrorSurfaceSettings(_PEBaseModel):
    enabled: bool | None = Field(default=None, description="是否将当前节点设为镜面")
    origin: list[float] | None = Field(default=None, description="镜面中心点 [x, y, z]")
    normal: list[float] | None = Field(default=None, description="镜面法线 [x, y, z]")
    reflect_ratio: float | None = Field(default=None, ge=0, le=1, description="镜面反射率")

    @field_validator("origin")
    @classmethod
    def _validate_origin(cls, value: list[float] | None) -> list[float] | None:
        return cls._validate_vector(value, 3, "origin")

    @field_validator("normal")
    @classmethod
    def _validate_normal(cls, value: list[float] | None) -> list[float] | None:
        return cls._validate_vector(value, 3, "normal")


class MirrorParticipationSettings(_PEBaseModel):
    enabled: bool | None = Field(default=None, description="当前节点是否参与镜面反射")


class MirrorSceneSettings(_PEBaseModel):
    reflect_depth: float | None = Field(default=None, ge=0, description="镜面反射深度")


class ProbeFactorSettings(_PEBaseModel):
    factor: float | None = Field(default=None, description="光照探针的全局光强度")


class AirParticleDensitySettings(_PEBaseModel):
    density: float | None = Field(default=None, description="空气微粒浓度")


class AtmosphereSettings(_PEBaseModel):
    enabled: bool | None = Field(default=None, description="是否开启大气")
    density: float | None = Field(default=None, description="大气浓度")


class SunSettings(_PEBaseModel):
    enabled: bool | None = Field(default=None, description="是否开启阳光")
    exposure: float | None = Field(default=None, description="阳光曝光度")
    azimuth: float | None = Field(default=None, description="阳光方位角")
    zenith: float | None = Field(default=None, description="阳光天顶角")


class FogSettings(_PEBaseModel):
    enabled: bool | None = Field(default=None, description="是否开启雾")
    density: float | None = Field(default=None, description="雾浓度")
    color: list[float] | None = Field(default=None, description="雾颜色 [r, g, b]")
    height: float | None = Field(default=None, description="雾高度")

    @field_validator("color")
    @classmethod
    def _validate_color(cls, value: list[float] | None) -> list[float] | None:
        return cls._validate_vector(value, 3, "color")


class PostprocessSettings(_PEBaseModel):
    tone: float | None = Field(default=None, description="色调")
    saturation: float | None = Field(default=None, description="饱和度")
    contrast: float | None = Field(default=None, description="对比度")


class MaterialSettings(_PEBaseModel):
    diffuse_rgba: list[float] | None = Field(default=None, description="漫反射颜色 [r, g, b, a]")
    ambient_rgb: list[float] | None = Field(default=None, description="环境反射颜色 [r, g, b]")
    specular_rgb: list[float] | None = Field(default=None, description="镜面反射颜色 [r, g, b]")
    emission_rgb: list[float] | None = Field(default=None, description="自发光颜色 [r, g, b]")
    shininess: float | None = Field(default=None, description="高光系数")
    refraction: float | None = Field(default=None, description="折射系数")
    brightness: float | None = Field(default=None, description="材质亮度")
    transparency: float | None = Field(default=None, ge=0, le=1, description="透明度")
    geometry_color_rgba: list[float] | None = Field(
        default=None, description="点线几何颜色 [r, g, b, a]"
    )
    apply_children: bool = Field(default=False, description="是否应用到子节点")
    local_only: bool = Field(default=False, description="透明度是否仅应用到当前节点")
    shader_name: str | None = Field(default=None, description="要关联到节点的 shader 名称")

    @field_validator("diffuse_rgba", "geometry_color_rgba")
    @classmethod
    def _validate_rgba(cls, value: list[float] | None) -> list[float] | None:
        return cls._validate_vector(value, 4, "rgba field")

    @field_validator("ambient_rgb", "specular_rgb", "emission_rgb")
    @classmethod
    def _validate_rgb(cls, value: list[float] | None) -> list[float] | None:
        return cls._validate_vector(value, 3, "rgb field")


class TextureSettings(_PEBaseModel):
    filename: str | None = Field(default=None, description="纹理图片路径，由用户或系统提供")
    map_type: Literal["color", "normal", "specular", "environment", "ao", "emission", "template"] | None = Field(
        default=None,
        description="贴图类型：漫反射、法线、高光、环境反射、AO、发光、蒙板",
    )
    tex_matrix: list[float] | None = Field(default=None, description="纹理矩阵")
    decal: bool | None = Field(default=None, description="是否贴花模式")
    alpha: int | None = Field(default=None, ge=0, le=255, description="alpha 通道默认值")
    width: int | None = Field(default=None, ge=0, description="纹理宽度")
    height: int | None = Field(default=None, ge=0, description="纹理高度")
    component: int | None = Field(default=None, ge=1, le=4, description="纹理通道数量")
    texname: str | None = Field(default=None, description="纹理名")
    tex_type: Literal["rgba", "rgb", "alpha", "luminance", "luminance_alpha"] | None = Field(
        default=None, description="纹理数据类型"
    )
    face: int | None = Field(default=None, ge=0, description="纹理面序号")
    multi_tex_id: int | None = Field(default=None, ge=0, description="多重纹理序号")
    coord_index: int | None = Field(default=None, ge=0, description="纹理坐标通道")
    apply_children: bool = Field(default=True, description="是否应用到子节点")


class LightProbeCreate(_PEBaseModel):
    position: list[float] = Field(description="探针位置 [x, y, z]")
    exposure: float = Field(default=1.0, description="曝光强度")
    camera_length: float | None = Field(default=None, ge=0, description="探测相机长度")
    base_node_path: str | None = Field(default=None, description="探针相对节点路径")
    option: str | None = Field(default=None, description="探针选项")

    @field_validator("position")
    @classmethod
    def _validate_position(cls, value: list[float]) -> list[float]:
        return cls._validate_vector(value, 3, "position") or value


class LightProbeUpdate(_PEBaseModel):
    position: list[float] | None = Field(default=None, description="探针位置 [x, y, z]")
    exposure: float | None = Field(default=None, description="曝光强度")
    camera_length: float | None = Field(default=None, ge=0, description="探测相机长度")
    base_node_path: str | None = Field(default=None, description="探针相对节点路径")
    option: str | None = Field(default=None, description="探针选项")

    @field_validator("position")
    @classmethod
    def _validate_position(cls, value: list[float] | None) -> list[float] | None:
        return cls._validate_vector(value, 3, "position")


class SceneEffectBatchItem(_PEBaseModel):
    effect_name: str = Field(description="scene_effect capability name")
    data: dict[str, Any] = Field(default_factory=dict, description="partial update payload")