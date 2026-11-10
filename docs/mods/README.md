# Lua mods

You can make a mod with two text files and add your own settings, buttons and status text to F1/Esc → Mods. You can also request one shortcut in the game's Online menu, which opens that same Mods page. You don't need to compile anything for an ordinary Lua mod.

These features are experimental and tested on Linux. Native Windows validation is still pending. Lua can respond to startup, settings and button events, but it can't read players, move actors, change quests or edit game saves.

## Workflow

Start with [01 · Start Here](01-start-here.md), then use the small examples and reference links. If your mod needs a separately built native library, read the provider reference after trying the Lua example. You must enable that library explicitly, and you need a binary for each supported platform. The common Lua files can stay the same; the provider files go in the package's `native/linux-x86_64` or `native/windows-x86_64` folder.

## Reference

- [Settings API](../lua-mods.md)
- [Actions, status and provider commands](../provider-actions.md)
- [Menu registration v1](03-menu-registration.md)
- [Compatibility, capabilities and troubleshooting](04-compatibility.md)
- [Native provider loading](../native-providers.md)

The Online shortcut uses the game's existing menu row, but the page it opens is still the F1/Esc overlay. Game-native content windows are not available.
