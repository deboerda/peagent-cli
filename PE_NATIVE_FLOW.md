# PE native script flow API

The agent-facing flow is exposed without screen coordinates or RelaGraph binary
editing:

```text
project_startup/native_file registration
  -> native program execution (optional)
  -> native Start Debug
  -> PE output query
  -> native Stop Debug
  -> output query again after PEPlayer closes
```

## CLI

When `include.script` is already attached to the project startup graph:

```powershell
python E:\PEagent\pe_cli.py script native-flow `
  --registration project_startup `
  --graph-file E:\PEagent\PeProject\cleantest\main\alpha.rg `
  --marker "hello world from agent script" `
  --wait 20
```

To load an include exactly once through PE's native
`CVsView::ReadScriptFromFile` path, use `--registration native_file` and pass
`--include-file`. Do not use that mode when the same include is loaded by the
startup graph, or PE will report duplicate functions.

`--execute-function --function-call "helloworld();"` opts into the native
`ReadProgramFromString`/`CVsProgram::Execute` path. It is intentionally not the
default: project startup behavior should own invocation timing.

## Python/MCP

```python
from pe_client import native_script_flow
result = native_script_flow(
    graph_file=r"E:\PEagent\PeProject\cleantest\main\alpha.rg",
    registration="project_startup",
    marker="hello world from agent script",
)
```

The same operation is available as the `pe_native_script_flow` MCP tool.
`dry_run=True` validates paths and mode without contacting PE.

## Safety contract

The flow only calls verified endpoints (`engine/status`, `graph/read`,
`script/execute_file`, `script/execute`, `debug/start`, `debug/stop`, and
`output/query`). It never writes `.rg` bytes and rejects unknown registration
modes. Graph node mutation remains disabled until a version-specific native
editor session is exposed.
