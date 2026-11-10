# Run on native Windows. This records the OS and builds/tests source; it never opens a game.
param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Use a native Windows host or VM, not Wine.' }
Set-Location (Join-Path $PSScriptRoot '..\..')
$env:PYTHONUTF8 = '1'
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
New-Item -ItemType Directory -Force build | Out-Null
Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, OSArchitecture |
    ConvertTo-Json | Out-File build\windows-platform.json -Encoding utf8
foreach ($runner in @('tools/prepare_lua.py', 'tests/providers/check.py', 'tests/actions/check.py', 'tests/menu/check.py', 'tests/menu/check_docs.py')) {
    & $Python -B $runner
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
& $Python -B -m unittest discover -s tests/release -p 'test_*.py' -v
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $Python -B tools/build_game.py --make-runtime-lib --version upd --gen tools/runtime-layout `
    --expected tools/data/expected-xcx-2.0.0.json --out build/windows-runtime --jobs 2
exit $LASTEXITCODE
