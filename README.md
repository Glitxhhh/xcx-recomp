# XCX Recomp

<a href="https://recomplauncher.com/game/xcx-recomp"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

**XCX Recomp** is a static recompilation of *Xenoblade Chronicles X: Definitive Edition* (Nintendo Switch, 2025) into a native Windows x86-64 application ([docs/architecture.md](docs/architecture.md)). It is a fork of [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) by Valiantsin Dzerakh.

> [!IMPORTANT]
> **No game files are included.** You need your own legally obtained copy of *Xenoblade Chronicles X: Definitive Edition*, dumped from your own console: the base game and the 2.0.0 update (the add-on content is optional). Instructions for obtaining those files will not be provided.
>
> The game is built from your files on your machine, so the result contains the game. It is for your own use. Do not share it.

## Status

Early development. New Game, cutscenes and gameplay run on Windows with sound, text and the menu, at 60 fps by
default (30 to 360 in the settings; above 60 is experimental).

Report problems as issues, following [CONTRIBUTING.md](CONTRIBUTING.md).

## Features

- Native code, not emulation
- Update 2.0.0 and the Exploration Support Package add-on content
- XCX-Setup: builds the game from your own NSP files and `prod.keys`
- In-game settings menu (F1) and performance overlay (F10)
- HDR10 output with peak, game and HUD brightness settings
- Keyboard, mouse and gamepad

## Experimental source mods

This feature branch supports explicitly enabled Lua source packages, settings/actions in F1/Esc → Mods, optional trusted native providers and one checked Online-menu shortcut to the existing Mods page. Start with the [mod-author guide](docs/mods/README.md). Linux candidate tests are recorded in the [reviewer guide](docs/review/lua-mod-support.md); native Windows acceptance for these additions is pending. This is separate from emulator asset mods and does not expose gameplay APIs or game-native content windows.

## Planned

- Linux and the Steam Deck
- Mods made for emulators: unpack a mod into a `mods` folder
- Custom aspect ratios, including ultrawide
- VRR

## Installing

Requirements: Windows 10 or later, an x86-64 CPU with AVX2, a GPU with Vulkan 1.3 and `VK_EXT_external_memory_host`, about 36 GiB free during setup (20 GiB after).

1. Download `XCX-Setup-<version>-windows.zip` from [Releases](../../releases/latest) and unpack it.
2. Start `XCX-Setup.exe`. If SmartScreen warns about an unsigned app, choose *More info*, then *Run anyway*.
3. Pick your game files, the language and an install folder, and press *Start*. The setup builds the game once.
4. Play `xcx.exe` from the install folder.

To update, run a newer `XCX-Setup.exe` on the same folder; saves and settings are kept. Settings are in the in-game menu and in `xcx.ini` ([xcx.ini.example](xcx.ini.example) lists every key). Controls: [docs/controls.md](docs/controls.md).

## Credits

XCX Recomp builds on the projects below. What was taken from each, with its license, is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

- [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) by Valiantsin Dzerakh, the project XCX Recomp is forked from
- [LAS Recomp](https://github.com/peiikai/LASRecomp)
- [MacNativeGames](https://github.com/ly05010419/MacNativeGames)
- [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) and [nxnative](https://github.com/seedds/nxnative)
- [SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp)
- [Ryujinx](https://github.com/Ryujinx/Ryujinx)
- [deko3d](https://github.com/devkitPro/deko3d)
- [hactool](https://github.com/SciresM/hactool)
- [SDL 3](https://libsdl.org), [Dear ImGui](https://github.com/ocornut/imgui), [AMD FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR), [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [libopus](https://opus-codec.org), [libvpx](https://chromium.googlesource.com/webm/libvpx), [Zstandard](https://github.com/facebook/zstd)
- [Audune Prompts](https://audune.itch.io/audune-prompts) by Audune, the Xbox and PlayStation button artwork of the button icon styles. It's licensed CC BY-SA 4.0, so those images keep that license and aren't covered by this project's MIT license: [assets/appendage-glyph/audune/NOTICE.txt](assets/appendage-glyph/audune/NOTICE.txt) has the credit line and what we changed
- [Kenney Input Prompts](https://kenney.nl/assets/input-prompts) by Kenney, the keyboard, mouse and Steam Controller button artwork of the button icon styles (CC0)
- The [60FPS mod for XCX DE 1.0.1](https://github.com/Fl4sh9174/Switch-Emulator-Ultrawide-FPS-Mods) by Fl4sh_ and StevensND
- [Switchbrew](https://switchbrew.org) and the Switch homebrew community

## License

The project's own code is under the [MIT License](LICENSE); third-party code keeps its own license ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). The license grants nothing for Nintendo's game.

XCX Recomp is an independent, non-commercial fan project, not affiliated with or endorsed by Nintendo. *Xenoblade Chronicles X* and *Nintendo Switch* are trademarks of Nintendo; Xenoblade Chronicles X is developed by Monolith Soft.

## Building from source

[docs/building.md](docs/building.md) lists the toolchain and the steps. Everything the build writes stays under `build\` and `generated\`, which git ignores; run `tools/check_content.py` before a commit. Testers: [docs/tester-guide.md](docs/tester-guide.md).

Most of the code and documentation is written by AI coding agents (Claude Code) under the maintainer's direction; the maintainer plays every build and decides what is merged. [AGENTS.md](AGENTS.md) holds the rules they work under.
