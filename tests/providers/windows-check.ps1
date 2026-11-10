# Native Windows standalone gate. No game or proprietary inputs are required.
param([string]$Python = "python", [switch]$Sanitize)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..\..')
$tool = 'C:\msys64\ucrt64\bin'
$env:PATH = "$tool;$env:PATH"
$argsList = @('-B', 'tests/providers/check.py', '--cc', "$tool\clang.exe", '--cxx', "$tool\clang++.exe")
if ($Sanitize) { $argsList += '--sanitize' }
& $Python @argsList
if ($LASTEXITCODE) { exit $LASTEXITCODE }
