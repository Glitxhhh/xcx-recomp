param([switch]$Sanitize)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..\..')
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$testArgs = @('tests/menu/check.py')
if ($Sanitize) { $testArgs += '--sanitize' }
& python @testArgs
exit $LASTEXITCODE
