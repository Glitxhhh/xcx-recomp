# 01 · Start Here

You need a working executable built with Lua mod support, a plain-text editor and a separate installation for testing. Use the bundled `examples/mods/hello` files or copy the complete example in the next guide. Your package and saved preferences have separate folders, so you can update a mod without replacing your profile.

## Pick what you want to make

| I want to… | Go to |
| --- | --- |
| Run a complete first mod | [02 · First mod](02-first-mod.md) |
| Add settings, actions and status | [02 · First mod](02-first-mod.md#settings-and-actions) and [API reference](../provider-actions.md) |
| Request a native Online shortcut | [03 · Menu registration](03-menu-registration.md) |
| Check available capabilities or diagnose a rejected package | [04 · Compatibility](04-compatibility.md) |
| Add an optional native library | [Native providers](../native-providers.md) |

Follow the first-mod example before adding a native provider. UI and startup callbacks let you build a page; they are not a general game scripting API. New engine locations or gameplay operations need a separately implemented, version-checked shared adapter.
