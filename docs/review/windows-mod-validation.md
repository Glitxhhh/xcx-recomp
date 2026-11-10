# Native Windows validation

Status: pending. This procedure has been prepared and its shared headless installation runner was exercised on Linux. It has not been executed on Windows. Wine, cross-compilation and a successful Linux build do not establish native Windows acceptance.

Use a native Windows x64 machine or VM, MSYS2 UCRT64 Clang/Ninja, and Python 3.14. Follow [building](../building.md) for the compiler and library prerequisites; do not use cl.exe. Use a new checkout or the supplied source test bundle at `C:\xcx-lua-review`, without spaces in that test path. The bundle contains source/tests and a hash manifest, not an executable, provider DLL, game files or saves.

## Source and standalone gates

From PowerShell, in the source root:

```powershell
$ErrorActionPreference = 'Stop'
Set-Location C:\xcx-lua-review
$py = 'C:\Python314\python.exe' # change to your Python 3.14 executable
$env:PYTHONUTF8 = '1'
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
& $py -m pip install -r tools/requirements-installer.txt
if ($LASTEXITCODE) { throw 'Python dependencies failed' }
# When using the supplied source bundle, verify every included file before testing.
& $py -c "import json,hashlib,pathlib; m=json.load(open('source-test-manifest.json')); assert all(hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest()==h for n,h in m['files'].items()); print('Source bundle hashes match')"
if ($LASTEXITCODE) { throw 'Source hash mismatch' }
& powershell -ExecutionPolicy Bypass -File tests/release/windows-check.ps1 -Python $py
if ($LASTEXITCODE) { throw 'Native Windows source/runtime gate failed' }
```

A normal Git checkout does not have `source-test-manifest.json`; use its reviewed commit and local diff instead of the bundle verification command. The script records Windows identity in `build/windows-platform.json`, runs C/C++ ABI, loader, service/Lua, menu/example and release-copy regressions, and builds a native runtime archive with two jobs. It never launches the game. Keep the command output and report skipped tests or failures without treating them as passes.

The standalone Windows runners use a no-write persistence adapter for some fixtures. They do not prove Windows settings persistence. The next gate uses the real linked Windows file and thread implementations. The Linux-only coordinator/filesystem suite is not a substitute. Windows sanitizer execution is a separate pending check; don't change the provider dependency policy to make a test DLL load.

## Build with your own game inputs

You need your own verified XCX DE 2.0.0 extracted ExeFS and RomFS. Nothing in the test bundle supplies these. Choose new output/profile paths; don't point these commands at a normal installation or existing profile.

```powershell
$exefs = 'D:\XCX-dump\upd\exefs' # your owned, extracted inputs
$romfs = 'D:\XCX-dump\upd\romfs'
& $py recomp/gen.py --exefs $exefs --out generated/windows-review `
    --patches recomp/patches-upd.txt --mod-menu-hook --jobs 2
if ($LASTEXITCODE) { throw 'Checked generation failed' }
& $py tools/build_game.py --version upd --gen generated/windows-review `
    --out build/windows-host --runtime-lib build/windows-runtime/libxcxrt.a `
    --expected tools/data/expected-xcx-2.0.0.json --exefs $exefs --romfs $romfs `
    --profile build/windows-disposable-profile --jobs 2
if ($LASTEXITCODE) { throw 'Native link failed' }
& .\build\windows-host\xcx.exe --selftest
if ($LASTEXITCODE) { throw 'Selftest failed' }
& $py -B tests/release/check_installed.py --build build/windows-host `
    --out build/windows-first-mod-01 --provider build/actions/regular/c0.dll `
    --ninja C:/msys64/ucrt64/bin/ninja.exe
if ($LASTEXITCODE) { throw 'Installed runtime check failed' }
```

`windows-first-mod-01` must not already exist; choose a new numbered directory for another run. The runner links a private entry around the existing native build. It reserves memory and runs the real mod loader, settings writer and provider lifecycle, but never boots the game, draws a window or loads a profile. It checks startup-only discovery, enable/disable/re-enable, actions/status, settings across process restart, preservation of rejected oversized/directory settings, script updates without rebuilding the executable and native provider shutdown before Lua closure. Read `build/windows-first-mod-01/result.json`; it must report the provider check and clean shutdown as true, unchanged before/after executable hashes, and native_platform `nt`. Keep the OS record too; that JSON alone cannot distinguish Wine from a native Windows run.

Reparse-point/ACL-specific cases and sanitizer behavior still need native results beyond these ordinary disk/directory/oversize checks. A full Windows release ZIP/toolchain proof and its native installer run remain release gates; this source procedure does not claim they passed.

## One visible check, after coordination

No visible check is needed again on Linux for unchanged settings/navigation. Windows has no accepted visible result yet. After the headless gates pass, arrange one title-screen check using the newly built disposable host. Copy the unchanged `examples/mods/hello` folder into `build/windows-host/mods/hello`, and write `hello` on its own line in both `catalog.txt` and `enabled.txt` in that mods folder. Use plain UTF-8 without BOM. Keep settings and profiles outside package replacement operations.

The exact launch from the source root is:

```powershell
& .\build\windows-host\xcx.exe --exefs $exefs --romfs $romfs `
    --profile C:/xcx-lua-review/build/windows-visible-profile-01
```

Only run that command after coordination. At the title screen, open F1/Esc → Mods → Hello workshop. Confirm the page is listed, Say hello changes the status, Show hints saves, and normal window close exits. Reopen once to confirm the checkbox. The headless test already checks restart-only activation and provider cleanup; don't repeat gameplay or name-entry persistence for this gate. If checking the optional Online shortcut later, use the documented registration snippet: it opens the same Mods overlay, not a game-native content window. Record focus/Back/held-input results separately rather than calling every device accepted.
