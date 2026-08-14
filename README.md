# peagent-cli

PostEngineer MCP and CLI integration for native behavior-graph and script workflows.

This repository contains only the MCP/CLI implementation and the related plugin/native graph tools. It does not contain PostEngineer, the PE SDK, user projects, license files, or the PE runtime DLLs.

## What is included

- `pe_cli.py` — general command-line client for the local PE engine.
- `pe_graph_cli.py` — atomic behavior-graph CLI: create a session, add one node, set text, connect nodes, materialize a native `.rg`, and register an existing `.rg`.
- `mcpServer.py` — FastMCP server exposing the PE client operations.
- `pe_daemon.py` and `pe_client/` — local HTTP daemon and typed client library.
- `PEHelloMCP/` — Visual Studio plugin source implementing the local HTTP bridge and native ProjectMgr/RelaGraph integration.
- `artifacts/PEHelloMCP.plu` — convenience Release build of the bridge plugin; the matching PostEngineer runtime is still required.
- `tools/` — native Release RelaGraph graph builders and their sources.
- `docs/` — design, MVP, graph, and native integration notes.

## Prerequisites

For CLI/MCP use:

- Windows 10/11
- Python 3.11 or newer
- A running PostEngineer installation with the matching `PEHelloMCP.plu` loaded

For rebuilding the plugin or native tools:

- Visual Studio C++ x64 toolchain
- The matching private PE SDK and PostEngineer release binaries
- The matching `RelaGraph.dll`, `ProjectMgr.plu`, and SDK libraries

The native graph builder uses Release-specific internal RVAs. It must be used with the exact compatible Release `RelaGraph.dll`; it is not a general-purpose parser for arbitrary PE versions.

## Install on another computer

```powershell
git clone https://github.com/deboerda/peagent-cli.git
cd peagent-cli
powershell -ExecutionPolicy Bypass -File .\scripts\setup.ps1
```

The setup script creates `.venv` and installs `requirements.txt`. Activate it with:

```powershell
.\.venv\Scripts\Activate.ps1
```

Before using the commands, start PostEngineer, open a project, and load `artifacts/PEHelloMCP.plu` (or build the plugin from `PEHelloMCP/`). The matching PostEngineer runtime is still required. The plugin's HTTP bridge normally listens on `http://127.0.0.1:8080`; set `PE_MCP_URL` if the port is different.

## CLI examples

Check the engine:

```powershell
python .\pe_cli.py health
```

Create a graph session and compose it one operation at a time:

```powershell
python .\pe_graph_cli.py session-new --output .\out\hello.rg
python .\pe_graph_cli.py node-create --session .\out\hello.rg.session.json --type boot --id 2 --x 120 --y 120
python .\pe_graph_cli.py node-create --session .\out\hello.rg.session.json --type hub --id 3 --x 260 --y 120
python .\pe_graph_cli.py node-create --session .\out\hello.rg.session.json --type script --id 4 --x 400 --y 120 --text 'main\\Script\\include.script'
python .\pe_graph_cli.py link --session .\out\hello.rg.session.json --from 2 --to 3
python .\pe_graph_cli.py link --session .\out\hello.rg.session.json --from 3 --to 4
python .\pe_graph_cli.py register --name hello.rg .\out\hello.rg
```

`session.json` is the editable composition layer. Node `1` is the invisible native graph root; the visible red startup triangle is the explicit `boot` node. The `.rg` file is always materialized through the native RelaGraph writer, so the on-disk format is produced by the PE engine rather than by handwritten serialization.

The native builder and DLL can be overridden when they are installed elsewhere:

```powershell
python .\pe_graph_cli.py --builder C:\PETools\build_graph_from_plan.exe --dll C:\PostEngineer\RelaGraph.dll session-new --output C:\work\hello.rg
```

## MCP server

```powershell
python .\mcpServer.py
```

The MCP server talks to the local PE plugin over HTTP. It does not replace PostEngineer and cannot operate without the running PE bridge for native operations.

## Build notes

The checked-in `tools/*.exe` files are convenience builds for the verified PE Release layout. Rebuild them only with the matching SDK and Release DLL. The `PEHelloMCP` Visual Studio project intentionally references SDK paths outside this repository; configure those paths on the target machine rather than committing proprietary SDK files.

See [`docs/pe-graph-cli.md`](docs/pe-graph-cli.md) for the operation model and registration semantics.
