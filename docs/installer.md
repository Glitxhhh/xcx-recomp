# Installer

A public release that turns the player's own game files into a playable XCX folder. The release carries no game
data and no code derived from the game: the installer builds the game on the player's machine. The installed folder
holds only the game; the installer and its tools stay outside it.

## What the player does

1. Downloads `XCX-Setup-<version>-windows.zip` from GitHub Releases (none is published yet), unpacks it anywhere and
   starts `XCX-Setup.exe`.
2. Picks:
   - the base game (XCI or NSP) and the 1.2.1 or 1.4.3 update (NSP), or the same compressed (XCZ, NSZ);
   - their `prod.keys`;
   - the game language and the install folder.
3. Waits for the setup, one time. The window shows each step and its progress.
4. Plays `xcx.exe` from the install folder. The installer and the source NSP or XCI can be deleted.

The install folder (`main.c` `portable_paths()` reads this layout; it stays portable):

```
<install folder>/
  xcx.exe, SDL3.dll, libopus-0.dll, xcx_xlate.exe
  nvngx_dlss.dll                 NVIDIA DLSS, downloaded by the setup's DLSS step when the player keeps it ticked
  xcx.ini                       language; the other keys as commented defaults (xcx.ini.example)
  exefs/, romfs/                 extracted from the player's files
  profile/                       saves
  logs/                          xcx.log of the last run, xcx.1.log to xcx.4.log of the runs before it
  shader and pipeline caches     prepared by the setup
  xcx-install.json              what built this folder: release version, input hashes, finished steps
```

## Updating the port

The player runs a newer installer on the same folder. It reads `xcx-install.json`, keeps `romfs/`, `exefs/`,
`profile/` and `xcx.ini` after checking their hashes, rebuilds `xcx.exe` and re-prepares the shaders. A newer
release that changes nothing game-derived skips the rebuild.

Given another update file than the recorded one (1.2.1, then 1.4.3), the installer extracts and builds again from it and
keeps `profile/`. Saves carry over both ways: 1.4.3 converts a 1.2.1 save on load, and 1.2.1 loads the saves 1.4.3 wrote.

## Mods

**Mods** (on the first screen and the finish screen) opens the mod manager for the install folder in the form
(`installer/gui_mods.py` over `installer/mods.py`; `python -m installer.mods --dest DIR ...` is the same from a
console). It lists the mod library, [jmars125/xcx-recomp-mods](https://github.com/jmars125/xcx-recomp-mods), next to
what is in `mods/`:

- Lua mods are hosted in the library; each file is checked against the size and SHA-256 in its `index.json`. They
  are on when listed in `mods/enabled.txt` and apply when the game starts.
- Game-file mods are listed by link: the manager downloads the zip from the author's page (GameBanana), checks its
  size and SHA-256, and unpacks its `romfs/` (or `romfs/mod/`) folder into `mods/<id>/romfs/`; `exefs/` is left out.
  **Add zip...** does the same for a zip the player downloaded. They are on when listed in `mods/romfs-enabled.txt`,
  which the game reads again twice a second (`runtime/romfs_mods.c`), so they switch while it runs.

Saves are never touched. The game uses a game-file mod's copy the next time it loads the file, so data read at
start-up (the bdat tables) needs a restart.

The game's F1 menu has the same manager on its **Mods** tab (`runtime/mod_library.c`: WinHTTP downloads on Windows,
curl on Linux, `runtime/mod_zip.c` unpacks), with the same checks and folder layout. Both write
`mods/<id>/library.ini` (name, author, version, kind), so each sees the other's installs and updates.

Setup's look (`installer/theme.py`) takes the colours of the F1 menu (`runtime/ui/menu.cpp`, `style_init`).

## Setup steps

1. **Check** before writing anything: the files open with the keys, the update is 1.2.1 or 1.4.3 (by hash, as
   `tools/extract.py`), the keys are new enough for the update, enough space is free on the target disk (about 36 GiB at the peak of a
   first install, 20 GiB after it), the CPU has
   AVX2 (`-march=x86-64-v3`), a Vulkan 1.3 GPU exists. Each failure is one clear message.
2. **Extract** ExeFS and RomFS into the install folder and verify the hashes.
3. **Lift** the ExeFS to C (`recomp/gen.py`, `recomp/patches-v121.txt` or `patches-v143.txt`) into a temp folder next
   to the installer.
4. **Compile** the generated C, `modules.c`, `romfs.c`, `patches.c` and `ov_zstd.c` (they bake the per-install layout, RomFS
   hashes and the game version's patch page) and link
   them with the prebuilt runtime library into `xcx.exe` (`build_game.py --runtime-lib`; about 3 min on 24 cores).
5. **Prepare** the RomFS patches and the shaders (`tools/make_romfs_patches.py`, `tools/prepare_shaders.py`,
   `xcx.exe --prepare-shaders`) in the install folder.
6. **DLSS** (the window's tick box, on by default, or `--dlss`; Windows with an NVIDIA RTX GPU): download
   `nvngx_dlss.dll` from NVIDIA's own DLSS SDK repository (github.com/NVIDIA/DLSS, a pinned tag, NVIDIA RTX SDK
   license) into the install folder and check its size and SHA-256 (`steps.py`, `Dlss`). The release does not ship
   the library; NGX loads it from the folder of xcx.exe for `upscaler = dlss`. A DLSS library already in the folder (a
   newer one the player put there) is kept. Without an RTX GPU the step only logs and does nothing; a failed download
   warns, the game runs without DLSS, and the next run of the setup tries again.
7. **Clean up** the temp folder (generated C and objects, about 1.3 GB).

A cancelled or failed setup resumes from the last finished step (`xcx-install.json`).

## Command line

`installer/` (stdlib only; `installer/layout.py` maps the release folder and, in a repository, the dev toolchain):

```
python -m installer --base FILE --update FILE --keys FILE --lang CODE --dest DIR [--root DIR] [--temp DIR] [--verify]
```

- On a folder with `xcx-install.json` only `--dest` is needed; the game files only when the extraction must run again.
- For a launcher or a script (RecompLauncher passes them, `recomplauncher.json`): `--game PATH` takes the game file or
  a folder holding the base game, the update and the add-on content in place of `--base`, `--update` and `--dlc`,
  and sorts the files by content (`installer/game_files.py`); `--lang auto` keeps the language already in `xcx.ini`;
  `--dlss-file PATH` copies a DLSS library, only NVIDIA's release the download takes (`DLSS_SHA256`), instead of
  downloading one; `--progress-lines` prints `progress 0.4 Text` lines instead of JSON events. Empty values
  (`--dlc=`, `--update=`, a launcher's optional input left empty) count as not given.
- NSZ and XCZ (`installer/nsz.py`): the checks read their NCA headers in place; the extraction first rebuilds each as a
  plain NSP in the temp folder (every NCA checked against its own name, a SHA-256), then goes on as with an NSP.
- An extracted game (`installer/extracted.py`): `--game FOLDER`, or a folder as `--base`, holding `exefs/` of the 2.0.0
  update and its RomFS as an image (`romfs.bin` or `romfs/romfs.bin`, hard-linked into the install when it can be) or as
  loose files (`romfs/`, an emulator's "Dump RomFS" with the update installed). No update file or keys are needed. The
  ExeFS and an image are checked by the same hashes as an extraction. Loose files are checked one by one against
  `tools/data/romfs-files-xcx-<version>.json` (the SHA-256 of each path, size and SHA-256 of each file, written from a
  verified extraction by `tools/romfs_files.py`) and packed into a new `romfs.bin`: `xcx.exe` reads the RomFS only at the
  offsets of its index, so the image need not be the cartridge's byte for byte.
- stdout: one JSON event per line, types `start`, `plan`, `step` (started, done, skipped), `progress` (step, phase,
  done/total/unit), `space` (bytes needed and free on the install disk and the temp disk), `warning`, `error`, `end` (`installer/events.py`). Messages are ids with parameters;
  `installer/text/en.json` holds their English text. The human-readable log goes to `<temp>/logs/`.
- Exit codes: 0 done, 1 failed, 2 bad command line, 3 cancelled. The window cancels with Ctrl+Break to the
  installer's process group.
- Each step records a key over its inputs (runner.py). A step with the recorded key and intact output is skipped, so
  resume and update are one rule; output kept from another release is re-hashed first.
- Peak space on the install disk is about 36 GiB, not 22: the extraction copies both Program NCAs (16.3 GiB) next to
  the merged RomFS (15.9 GiB) before it deletes them. The check computes the exact figure.
- Window (`python -m installer.gui`, `installer/gui.py` draws, `installer/guilogic.py` decides): form, step list with
  progress, space line, collapsible log, Cancel (Ctrl+Break to the CLI's process group, then it waits for the exit),
  finish screen with Open folder. English or Russian, chosen from the Windows display language, switchable in the
  window; every sentence is an id in `installer/text/<en|ru>.json`. A folder with `xcx-install.json` needs only the
  folder. `--selftest [--shots DIR]` builds every screen in both languages and closes itself.
- `tests/installer_gui_test.py`: event model, form, catalogs, cancel, the self-closing window and one end-to-end run
  of the CLI in `--verify` mode (needs `build/install-test` and `build/extracted/v121`; see its docstring).
- `tests/installer_test.py`: bookkeeping with fake steps. `tests/installer_e2e.py`: a dev-mode run with the files of
  `config.local.toml`.

## Release layout

The unpacked release; `tools/make_release.py` assembles it and `--zip` packs it:

```
XCX-Setup.exe        starts python/pythonw.exe -m installer.gui (installer/launcher/launcher.c, no console)
installer/            the installer: steps, CLI, window, en/ru text catalog
python/               Python 3.14.9 embeddable + numpy, lz4, cryptography, cffi, pycparser (+ tkinter for the window)
recomp/, tools/       the lifter and the build, extraction and shader scripts the steps run (copied from the repo)
llvm/                 llvm-mingw, x86_64-only subset (Apache-2.0 with LLVM exception; mingw-w64 parts permissive)
runtime/              libxcxrt.a and the headers the generated code and modules.c/romfs.c/patches.c include
bin/                  xcx_xlate.exe, hactool.exe, SDL3.dll, libopus-0.dll, ninja.exe
data/                 shader_list.tsv (MacNativeGames, MIT)
THIRD_PARTY_NOTICES.md
xcx.ini.example, VERSION  the default ini; the release string (the repository's VERSION plus the commit)
```

- SDL3 comes from the upstream release, not MSYS2: MSYS2's DLL imports the LGPL libiconv.
- Every bundled third-party piece gets a row in `THIRD_PARTY_NOTICES.md`.
- The release contains nothing game-derived but the execution profile `tools/data/pgo/pgo-upd.profdata.xz` (run counts
  of the lifted functions, named by guest address; docs/building.md, "Profile-guided build"). `make_release.py` runs
  `tools/check_content.py` over the folder before it finishes, and that check lets the profile through only as an xz
  stream holding an LLVM indexed profile.
- On Windows the Compile step builds the game with that profile at -O2 (`installer/layout.py` pgo); Linux has none,
  since the profile belongs to the LLVM of the Windows toolchain.

## Linux release

`tools/make_release_linux.py` builds the same setup for Linux and packs it as `XCX-Setup-<version>-linux.AppImage` (the static
type-2 runtime: no libfuse2 needed on the host). The player makes it executable and starts it; on a Steam Deck, from
Desktop Mode. Building the release also needs Git, `make`, `pkg-config` and `file` (used by appimagetool).
Use Xvfb for the installer selftests on a headless machine. The bundled Tcl/Tk 9.0.4 is rebuilt with
Xft against the sniper sysroot so Russian text uses system TrueType fonts. The setup builds the game into the folder they pick, as on Windows, and the game is `xcx` there with
`libSDL3.so.0` beside it. The folder inside the AppImage:

```
AppRun                starts python/bin/python3 -m installer.gui (no bytecode written into the read-only mount)
xcx-setup.desktop, xcx-setup.png
installer/, recomp/, tools/, data/, licenses/, THIRD_PARTY_NOTICES.md, xcx.ini.example, VERSION   as on Windows
python/               python-build-standalone 3.14 (with tkinter) + the installer's packages; a .pth puts the folder,
                      tools/ and recomp/ on sys.path
llvm/                 LLVM 23 (clang, lld, llvm-ar) and llvm/sysroot: the files of the Steam Runtime 3 "sniper" SDK the
                      game's compile and link read (about 130 files)
runtime/              libxcxrt.a, headers, the install-side sources, xcx.png; deps/ with the Vulkan headers, VMA and a
                      static zstd
bin/                  xcx_xlate, hactool, ninja, libSDL3.so.0
```

- What we compile is built against the sniper sysroot (glibc 2.31): the game, hostcheck, hactool and the translator
  need no newer glibc. The game links Opus, libvpx, zstd and libstdc++ statically and needs only glibc, the system's
  Vulkan loader and the SDL3 beside it. The setup itself needs glibc 2.34 (Ubuntu 22.04, Debian 12, Fedora 35,
  SteamOS 3): the official LLVM binaries and the `cryptography` wheel are built against it. `make_release_linux.py`
  checks every ELF file in the folder against that floor (`check_glibc`), and the setup's check step stops with one
  message on an older system.
- The setup's temp folder is `.setup-temp` inside the install folder (`--temp` changes it): the AppImage is mounted
  read-only, and a Steam Deck's `/home` is often too full for the generated C while the game goes to the SD card.
- The game keeps Mesa's shader cache in `mesa-cache` beside `xcx` unless `MESA_SHADER_CACHE_DIR` is set, for the
  same reason: RADV fills its default folder in `~/.cache` up to 1 GB.
- The window follows `LANG` for English or Russian, cancels through SIGTERM to the CLI's process group (the CLI handles
  it as Ctrl+Break on Windows) and opens the finished folder with `xdg-open`.
- Requirements beyond Windows': Linux 6.7 or later (the write watch, `docs/building.md`) and glibc 2.34.

## Making a release

`.github/workflows/release.yml` builds both packages with no game files. `libxcxrt.a` takes `MOD_COUNT` and
`MOD_MAIN_BASE`, all the runtime reads of the module layout, from `tools/runtime-layout/layout.h`; nothing game-derived
reaches a runner or a package except the committed execution profile, which also builds the Windows `libxcxrt.a`.

Each run builds `XCX-Setup-<version>-windows.zip` on Windows and `XCX-Setup-<version>-linux.AppImage` on Linux, with
`check_content`, the check-only installer pass and the window's `--selftest` inside `tools/make_release.py` and
`tools/make_release_linux.py`. Runners have no GPU, so the host check runs on Mesa's CPU Vulkan driver
(`.github/actions/software-vulkan`). A second runner then unpacks each package and repeats those checks
(`tools/check_release.py`). Downloads are cached from `toolchain/release-dl/`.

For a test build, push a tag `test-<name>` on the commit to try (`git tag test-issue123 && git push origin
test-issue123`), or start the Release workflow on a branch from the Actions tab (`gh workflow run release.yml --ref
<branch>`). Either only builds and checks: no draft release is made. The packages are the run's artifacts, named
`XCX-Setup-test-<name>-windows.zip` and `XCX-Setup-test-<name>-linux.AppImage` for a tag (the `VERSION` for a run
started by hand); link the run in the issue for the reporter.

For a release:

1. On the release branch, bump `VERSION` and push.
2. Tag that commit `v<VERSION>` and push the tag. A tag that differs from `VERSION` stops the run. A passing run
   creates a draft release with both packages and generated notes.
3. Prove the packages locally. `--prove` reads `generated/upd`, `build/extracted/upd` and `build/upd/shaders`, which are
   game-derived, so it never runs on a runner. Its shader probes need a census: make one once, from the shaders a local
   2.0.0 build met in play (`build/upd/shader-programs.bin`). Download both packages from the draft, then:

   ```sh
   .venv-py314\Scripts\python tools\shader_census.py --version upd --played
   # Windows, with the zip unpacked to build/release/XCX-Setup
   .venv-py314\Scripts\python tools\make_release.py --out build/release/XCX-Setup --prove
   # Linux or WSL: run the AppImage with --appimage-extract, move squashfs-root to build/release/XCX-Setup-linux
   .venv/bin/python tools/make_release_linux.py --out build/release/XCX-Setup-linux --prove
   ```

4. Install from both packages with real game files and play the build.
5. Publish the draft (a person, on the release page or with `gh release edit v<VERSION> --draft=false --latest`), then
   merge the release branch.

Without Actions, the same two scripts build the packages locally; on Windows run `tools/build_hactool.py` and
`tools/build_xlate.py` first (`tools/make_release.py` copies their output).

## Constraints

- The installer steps contain no Win32 calls: Linux runs the same steps with its own compiler ("Linux release").
- The keys file path is passed to the tools; key values never appear in the UI, logs or files.
- The window is a front end over the CLI (`python -m installer --base ... --update ... --keys ... --lang ... --dest
  ...`), which prints one JSON progress event per line. Tests and CI use the CLI.
- Paths may hold any Unicode characters (a Cyrillic user name puts the temp and install folders under it). Every exe
  the setup runs or builds reads its arguments and paths as UTF-8 through an `activeCodePage` manifest: hactool
  (our patch, [building.md](building.md)), xcx.exe (`runtime/xcx.rc`), ninja (upstream). The Python steps write
  their files and logs as UTF-8, and `build_game.py` quotes paths with spaces on its Ninja command lines.
- A game file whose data fails its hash checks or ends early stops the setup with `check.input_damaged`, which names
  the file and asks to copy it again (`extract.DamagedFile`). A failed merge checks the base Program NCA alone to tell
  which file is damaged.
- Unsigned exe: SmartScreen warns on first start.

## Later

- Re-warm the driver cache on start after a move, GPU or driver change.
