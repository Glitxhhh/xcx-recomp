# Native Windows service/Lua standalone gate; no game launch or proprietary inputs.
param([string]$Python = 'python', [switch]$Sanitize)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..\..')
$actionsTool = 'C:\msys64\ucrt64\bin'
$env:PATH = "$actionsTool;$env:PATH"
foreach ($runner in @('tests/providers/check.py', 'tests/actions/check.py')) {
    $actionsArgs = @('-B', $runner, '--cc', "$actionsTool\clang.exe", '--cxx', "$actionsTool\clang++.exe")
    if ($Sanitize) { $actionsArgs += '--sanitize' }
    & $Python @actionsArgs
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
