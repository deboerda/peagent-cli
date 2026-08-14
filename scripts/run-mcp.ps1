$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$python = Join-Path $repo '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { $python = 'python' }
& $python (Join-Path $repo 'mcpServer.py') @args
