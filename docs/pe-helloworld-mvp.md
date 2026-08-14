# PE Hello World 行为图 MVP 说明

## 1. MVP 目标

验证以下完整链路可以在 PostEngineer Release 版本中稳定工作：

```text
PE 脚本函数
  → include.script 注册
  → 行为图脚本节点加载
  → 程序节点调用函数
  → 项目注册
  → 原生启动调试
  → PE 输出窗口显示结果
```

本 MVP 的最终可观察结果为：PE 启动调试后，输出窗口显示 `helloworld`。

## 2. 已验证项目

项目目录：

```text
E:\PEagent\PeProject\final
```

关键文件：

```text
final\main\Script\helloworld.script
final\main\Script\include.script
final\main\helloworld.rg
```

脚本文件使用 PE 兼容的 GBK/ANSI 编码和 CRLF 换行。

## 3. 脚本结构

### 3.1 函数脚本

`main\Script\helloworld.script`：

```cpp
Script{
void helloworld(){
    outputMessage("helloworld");
}
}
```

该文件只定义功能函数，不定义额外的 `main()` 或生命周期入口。

### 3.2 include 注册

`main\Script\include.script`：

```cpp
Script{
    include{helloworld.script};
}
```

行为图负责启动时机，`include.script` 负责函数注册。

## 4. 行为图结构

`helloworld.rg` 由 Release 版 `RelaGraph.dll` 内部 API 创建，包含：

| 图元 | 作用 |
|---|---|
| root | 行为图根对象 |
| boot | 启动节点 |
| Hub | 启动流程连接节点 |
| curve | 行为流程连线 |
| script | 加载 `main\\Script\\include.script` |
| program | 执行 `helloworld();` |

Hub 的端点缓存设置为：

```text
previous id = 2  (boot)
next id     = 7  (program)
previous count = 1
next count     = 1
```

这些字段保证保存后的行为图能够被 PE 正确识别和执行。

## 5. 原生生成与注册

图生成器：

```text
E:\PEagent\tools\build_boot_graph.exe
```

实现源码：

```text
E:\PEagent\tools\build_boot_graph.cpp
```

生成器通过匹配当前 Release PE 版本的 RelaGraph 内部函数完成：

- 按类型创建图元
- 设置脚本路径和程序文本
- 设置节点位置
- 建立节点关系
- 构建 Hub 端点信息
- 调用原生 `WriteToBuffer` 保存 `.rg`

生成后使用 Release RelaGraph 原生解析器重新读取，确认图结构有效。

之后由 PE 插件调用项目模块的原生注册接口：

```cpp
CVsModule::AddGraphInfo(
    "helloworld.rg",
    "E:\\PEagent\\PeProject\\final\\main\\helloworld.rg",
    0
);
```

最后发送项目保存消息，行为图出现在 `final` 项目树中。

## 6. 调试验证

执行过程：

1. 打开 `final.peproj`。
2. 确认项目树中出现 `helloworld.rg`。
3. 打开行为图。
4. 停止已有 PEPlayer 调试进程。
5. 执行 PE 原生 Start Debug。
6. `start.exe` / PEPlayer 加载 `final` 项目。
7. 启动行为图加载 `include.script`。
8. 程序节点调用 `helloworld();`。
9. `outputMessage("helloworld")` 输出到 PE 输出窗口。

实测结果：

```text
PE 输出窗口：helloworld
```

## 7. MVP 验收标准

以下条件全部满足时，视为 MVP 完成：

- `helloworld.script` 存在且语法有效。
- `include.script` 正确引用函数脚本。
- `.rg` 能被 Release RelaGraph 原生解析。
- 行为图已注册到目标 PE 项目。
- 项目保存成功。
- PEPlayer 能够启动。
- 输出窗口能够看到 `helloworld`。

本次 `final` 项目已满足以上标准。

## 8. 当前限制

- 图生成器依赖当前 PE Release 版本的内部 RVA，版本升级后需要重新验证。
- Debug 版和 Release 版不能直接混用 RelaGraph DLL 或 RVA。
- 当前已验证的是生成、保存、注册和执行链路；ProjectMgr 编辑器会话尚未完全绑定。
- 因此后续复杂节点编辑暂时应通过已验证的原生图构造流程扩展。

## 9. 后续开发方向

### 9.1 MCP 集成

将图生成器正式封装为 MCP action：

```text
graph_build_boot
```

返回：

- graph name
- graph file
- native parse result
- registered status
- project save result
- editor session status

### 9.2 CLI 支持

增加类似命令：

```powershell
python pe_cli.py graph create-boot `
  --name helloworld.rg `
  --filename E:\PEagent\PeProject\final\main\helloworld.rg `
  --include-file main\Script\include.script
```

### 9.3 版本适配

- 自动识别 PE Debug/Release 版本。
- 校验 RelaGraph 导出符号和模块版本。
- 使用导出符号、RTTI 和函数特征辅助重建 RVA。
- 不匹配时拒绝执行，避免生成损坏图文件。

### 9.4 行为图扩展

- 多程序节点。
- 条件分支。
- 定时节点。
- 多脚本节点。
- 变量、对象和命令图元。
- 更完整的连线和端点管理。

## 10. 后续应用方式

新增功能时沿用以下模板：

```text
创建 Feature.script
  → 在 include.script 中 include
  → 在行为图中添加脚本节点
  → 在程序节点中调用 Feature()
  → 注册并保存行为图
  → 启动 PEPlayer 验证输出
```

例如：

```cpp
Script{
void feature(){
    outputMessage("feature executed");
}
}
```

在程序节点中调用：

```cpp
feature();
```

这样可以将每个功能拆分为独立脚本，再通过行为图组合为完整的 PE 自动化逻辑。
