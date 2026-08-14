$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$venv = Join-Path $repo '.venv'

$launcher = $null
foreach ($candidate in @('py', 'python')) {
    if (-not (Get-Command $candidate -ErrorAction SilentlyContinue)) { continue }
    & $candidate --version *> $null
    if ($LASTEXITCODE -eq 0) { $launcher = $candidate; break }
}
if (-not $launcher) {
    throw 'A working Python 3.11+ executable is required. Install Python from python.org and reopen PowerShell.'
}

if (-not (Test-Path -LiteralPath $venv)) {
    if ($launcher -eq 'py') {
        & py -3 -m venv $venv
    } else {
        & $launcher -m venv $venv
    }
}

$python = Join-Path $venv 'Scripts\python.exe'
& $python -m pip install --upgrade pip
& $python -m pip install -r (Join-Path $repo 'requirements.txt')
Write-Host "Ready. Activate with: $venv\Scripts\Activate.ps1"
