# PE 原生脚本链路：授权开发版逆向核验记录

本文只记录在本机开发版 SDK、PostEngineer 进程和 PEHelloMCP 插件上的
可验证分析。目标是还原“脚本文件 → include 注册 → 图节点引用 →
模块程序注册/实例化 → 根调用 → PEPlayer 输出”的生命周期，不修改
PostEngineer、RelaGraph、Tongyuan 保护或授权逻辑。

## 1. CSDN 文章能提供什么

给定文章是通用的 MFC 逆向方法：从窗口句柄和 `WM_COMMAND` 消息映射定位
UI 处理器，再用动态调试断点确认调用链。它不是 PE 技术文档，文中的
注册码绕过、NOP 和永久解锁部分不适用于本项目，也不应使用。

对 PE 有用的部分只有“静态符号 + 动态对比”方法：记录一次人工成功操作，
再记录一次失败/空操作，比较文件访问、线程、消息和 SDK 调用的差异。

## 2. 已验证的 PE SDK 入口

从当前 `ScriptReader.dll` 导出表确认：

```text
ScrReadScriptFromString
ScrReadProgramFromString
ScrReadProgramFromSourceFile
ScrReadFunctionFromString
ScrReadFunctionFromSourceFile
ScrFindIncludeFilename
```

从当前 `VsMVO.dll` 导出表确认：

```text
CVsModule::AddUserProgram
CVsModule::CreateUserPrograms
CVsModule::FindUserProgram
CVsModule::FindProgram
CVsModule::FindFunction
CVsProgram::ExecuteCreate
CVsProgram::Execute
CVsProgram::ExecuteAsyn
CVsView::ReadScriptFromFile
CVsView::ReadScriptFromString
CVsView::ReadProgramFromString
CVsView::MainModule
CVsView::SetDebugging
CVsView::Tick
```

这些是符号层面的事实，不等于每个函数都可以在一个新建的空
`CVsModule` 上直接调用。此前的独立 probe 已证明：脱离 PE 已初始化的
`CVsView/CVsModule` 生命周期直接调用解析函数会访问冲突；因此正式入口
必须使用 PE 当前 view 的主模块。

## 3. 当前项目的静态证据

`cleantest/main/alpha.rg` 通过原生 `graph_read` 解析，返回
`native=true`、`parse_verified=true`。对其二进制字符串扫描得到：

```text
main\\Script\\include.script
helloworld();
boot
```

这说明行为图保存的是 include 文件引用和调用表达式。它没有把
`helloworld` 函数体复制到 `.rg` 中。当前 `include.script` 的函数体为：

```text
Script{
    void helloworld()
    {
        outputMessage("hello world from agent script");
    }
}
```

## 4. 当前最可信的生命周期模型

```text
图编辑器“脚本”节点 + 文件选择
    ↓ 保存为 RelaGraph 节点属性
alpha.rg ──保存 include.script 路径和 helloworld() 调用表达式
    ↓ 项目打开/调试准备
CVsView / 主 CVsModule
    ↓
CVsView::ReadScriptFromFile
    ↓（include 解析）
ScrReadScriptFromString / ScrReadFunctionFromSourceFile
    ↓
CVsModule::AddUserProgram
    ↓
CVsModule::CreateUserPrograms
    ↓ 启动图的根节点解析 helloworld()
CVsModule::FindFunction / CVsProgram::FindFunction
    ↓
CVsProgram::ExecuteCreate（实例生命周期）
    ↓
CVsProgram::Execute 或 ExecuteAsyn
    ↓
outputMessage → PE 输出窗口
```

上图中“调用了哪一个具体函数、调用顺序、线程”仍需动态追踪确认；在
追踪完成前，不把它作为已完成的原生接口实现。

## 5. 动态追踪实施顺序

### A. 建立人工成功基线

在 `cleantest` 中保持现有 `alpha.rg`，不改图文件。记录：

1. 启动调试前后的进程和线程；
2. `include.script` 的文件打开事件；
3. `alpha.rg` 的读取事件；
4. 输出窗口的唯一标记；
5. 每个事件的时间戳、线程 ID 和返回值。

### B. 设置 SDK 符号断点/调用日志

只对当前开发版进程做调试附加，重点观察：

```text
CVsView::ReadScriptFromFile
ScrReadScriptFromSourceFile
ScrReadFunctionFromSourceFile
CVsModule::AddUserProgram
CVsModule::CreateUserPrograms
CVsModule::FindFunction
CVsProgram::ExecuteCreate
CVsProgram::Execute
CVsProgram::ExecuteAsyn
```

每个断点记录参数中的文件名、函数名、模块指针、返回指针和线程 ID。
不修改指令、不绕过校验、不写回二进制。

### C. 做两个差分实验

* 成功样本：现有 `alpha.rg`，脚本节点和根调用均存在。
* 失败样本：只在副本中移除根调用连线，include 仍保留。

如果两个样本都调用 `ReadScriptFromFile`，但只有成功样本进入
`FindFunction → Execute*`，就能证明“include 负责注册，根调用负责执行”。
如果连 `ReadScriptFromFile` 都没有，则说明图节点保存的是另一种延迟加载
记录，需要继续追踪图运行时的节点处理器。

## 6. CLI 实现门槛

只有满足以下条件，才把它封装成新的 `pe script native-run`：

1. 在真实 PE 主模块上确认解析入口和参数；
2. 确认用户程序注册和实例化的实际调用顺序；
3. 确认根调用使用的执行 API；
4. CLI 运行后，PE 输出窗口出现唯一标记；
5. 重复运行不会让 `tick_alive=false` 或积压命令。

在此之前，现有 `pe script deploy-and-run` 只负责对“已经由 PE 图形编辑器
绑定好的图”进行原生启动和输出验证，不冒充脚本导入/实例化接口。

## 7. 结论

目前已经证明“原生脚本加载器和程序运行时存在独立 API”，也证明
`.rg` 保存了 include 路径和调用表达式；尚未证明可以脱离图运行时安全地
直接构造完整启动程序。下一步应做动态差分追踪，而不是继续猜测消息 ID、
修改行为图或直接编辑 `.rg` 二进制。
