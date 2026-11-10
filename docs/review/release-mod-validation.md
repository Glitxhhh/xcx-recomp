# Mod support: remaining release validation

Use a fresh test checkout and new output directories. Release assembly replaces its output folder, and `check_release.py` replaces `build/release-verify`. Keep your normal installation, packages, settings and profiles elsewhere. These commands do not publish a release or boot a game.

## Linux package

Install the prerequisites in [Making a release](../installer.md#making-a-release), including .NET 10, Python 3.14 with Pillow, Git, make, pkg-config, file and Xvfb. With Python 3.14, these environment settings limit Python build helpers and .NET to two processors:

```sh
export PYTHON_CPU_COUNT=2
export DOTNET_PROCESSOR_COUNT=2
xvfb-run -a python tools/make_release_linux.py \
  --out build/release-mod-review/XCX-Setup-linux \
  --appimage build/release-mod-review/XCX-Setup-linux.AppImage
xvfb-run -a python tools/check_release.py \
  build/release-mod-review/XCX-Setup-linux.AppImage
```

For a CPU-only Vulkan check, set `VK_DRIVER_FILES` to your installed lavapipe ICD before both commands. Its path depends on the distribution. Don't substitute a system compiler or sysroot and call that a pinned release-toolchain result.

Expected: both runtime archives build; package content checks pass; bundled Python/installer and font/UI selftests pass; AppImage creation succeeds; extraction and the same checks pass again. The check-only installer should report that game inputs are needed. That expected stop is checked by the script; it isn't a failed package check. Verify `licenses/lua-LICENSE.txt`, `docs/mods/`, `examples/mods/hello/` and both provider ABI headers in the extracted folder. Development tests/review documents, pristine Lua sources, game data and user profiles must be absent.

For the game-derived proof, supply your own checked 2.0.0 inputs, generated code and played shader census at the locations in [Making a release, step 3](../installer.md#making-a-release). Generation must include `--mod-menu-hook` to exercise the supported Online shortcut. Then run:

```sh
python tools/make_release_linux.py \
  --out build/release-mod-review/XCX-Setup-linux --prove
```

Expected: the packaged compiler/sysroot links the generated game, selftest and every listed probe exit zero. Keep the linked executable's GLIBC requirements and probe results. Missing played shaders are a missing prerequisite, not shader-probe success. A packaged-toolchain build on a newer distribution does not prove execution on SteamOS or a CPU limited to x86-64-v2. Check those targets separately before claiming them supported by this candidate.


To check mod installation around that packaged build without booting the game, use the private source-checkout runner:

```sh
python tests/release/check_installed.py --build build/release-linux-check/game \
  --build-root build/release-mod-review/XCX-Setup-linux \
  --ninja "$PWD/build/release-mod-review/XCX-Setup-linux/bin/ninja" \
  --out build/linux-packed-mods-01
```

Expected: `result.json` reports restart-only discovery, enable/disable/re-enable, real settings persistence, rejected-file preservation, script replacement and clean shutdown, with unchanged executable hashes. For the inert provider gate, first run `python tests/actions/check.py`, then add `--provider build/actions/regular/c0.so` using a new output directory. `--build-root` matters because the package's Ninja paths are relative to the unpacked package, not the source checkout.

## Native Windows

Linux, Wine and cross-compilation do not establish native Windows acceptance. Use a native Windows x64 machine or VM and the exact source/runtime/installed-mod commands in [Native Windows validation](windows-mod-validation.md). That procedure checks real disk persistence and an inert provider in a private headless executable, then gives one coordinated visible check. Preserve the Windows OS record and full output; skipped cases remain pending.

For the full ZIP path, use a two-vCPU VM if you need to enforce the same two-job limit throughout the Windows release helpers. Prepare the MSVC NativeAOT and MSYS2 prerequisites from [building](../building.md), then run from a fresh checkout in PowerShell:

```powershell
python tools/build_hactool.py
if ($LASTEXITCODE) { throw 'hactool build failed' }
python tools/build_xlate.py
if ($LASTEXITCODE) { throw 'translator build failed' }
python tools/make_release.py --out build/release-mod-review/XCX-Setup `
  --zip build/release-mod-review/XCX-Setup-windows.zip
if ($LASTEXITCODE) { throw 'Windows release assembly failed' }
python tools/check_release.py build/release-mod-review/XCX-Setup-windows.zip
if ($LASTEXITCODE) { throw 'Packed Windows validation failed' }
```

After the linked native Windows source procedure has built its inert provider fixture, and with your own generated/input/census prerequisites in the documented locations, also run:

```powershell
python tools/make_release.py --out build/release-mod-review/XCX-Setup --prove
if ($LASTEXITCODE) { throw 'Windows packaged-toolchain proof failed' }
$bundle = (Resolve-Path build/release-mod-review/XCX-Setup).Path
python tests/release/check_installed.py --build build/release-check `
  --build-root $bundle --ninja "$bundle/bin/ninja.exe" `
  --out build/windows-packed-mods-01 --provider build/actions/regular/c0.dll
if ($LASTEXITCODE) { throw 'Windows packaged mod check failed' }
```

Expected: the ZIP unpacks and its own Python/installer selftests pass; the packaged toolchain links the game; all listed probes exit zero. Then perform the source guide's private installed-mod check and coordinated title-screen Mods check on native Windows. Record reparse/junction/ACL and failed-save retry behavior, restricted DLL search/import cases, worker failure/stop/join and sanitizer availability separately. The shared runners do not cover every Windows-specific filesystem/input path; unavailable results remain release gates. Don't widen the DLL policy to make a fixture pass.

## Evidence for this branch

The finalization report records which commands actually ran, their source/artifact hashes and any remaining machine-specific checks. A successful Linux AppImage/package check must not be described as native Windows, SteamOS, gameplay or native-content-window acceptance. No runtime API change or owner gameplay repetition is required for this documentation.
