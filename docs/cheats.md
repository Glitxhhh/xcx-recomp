# Cheats

The **Cheats** tab of the in-game menu (F1) lists the recomp's built-in cheats and the cheats of every mod folder.
A cheat switched on applies at once and stays on after a restart.

Cheats change the running game, and the game saves what it finds in memory. Back up your saves first.

## Built-in cheats

For the 2.0.0 update (build `EC4ED46BBC4A9EBB`):

| Cheat | What it does |
|---|---|
| Max Funds | Sets Funds to 99,999,999, the game's cap. |

It acts only once a save is loaded. Switch it off and the value stays where it was.

## Cheat files

Cheats come in the Atmosphere text format, in the mods folder (`mods` next to `xcx.exe`, or `XCX_MOD_DIR`):

```
mods/
  My Cheats/
    cheats/
      EC4ED46BBC4A9EBB.txt   cheats for this build of the game
  cheats-enabled.txt         written by the menu: which cheats are on
```

A file has a `[Name]` line per cheat, followed by lines of eight-digit hex words. All opcodes are supported. `{Name}`
marks a master code, which always runs. Sections without code, such as credits, are dropped.

A cheat file is named after the build of the game it is for: the first 16 hex digits of the build ID of `main`. For
the 2.0.0 update that is `EC4ED46BBC4A9EBB`, which the Cheats tab also shows. Files for other builds don't load,
because their addresses point at something else in this one. The 1.0.1 cheats that circulate for this game are in
that group, so their offsets have to be found again in 2.0.0.

Cheats run 12 times a second, as they do under Atmosphere. The cheats of one file share registers within a pass.
Memory region 0 is `main`, and pointers read from the game can be followed as they are. The pause and resume opcodes
(`FF0`, `FF1`) do nothing. The debug log opcode (`FFF`) writes to `logs/xcx.log` when its value changes.

A write to memory that can't be written is skipped and the game keeps running; the cheat's description then says so.

**Reload cheats** reads the files again, for adding or editing a cheat while the game runs.

### Cheats that change code

They don't work, and the tab shows them greyed out. An emulator runs the game's ARM code, so changing those bytes
changes what runs. Here the code was compiled to x86-64 during setup and the ARM bytes never run.

## Checks

`xcx.exe --probe-cheats` runs the format and the interpreter against a buffer and needs no game files.
