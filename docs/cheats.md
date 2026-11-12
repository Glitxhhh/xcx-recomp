# Cheats

Turn on **Cheats** in the Game tab of the in-game menu (F1) and a **Cheats** tab appears. It lists the recomp's
built-in cheats, grouped as Adventure, Combat, Items, Item Drops, Weapon Type and Weather, then the cheats of every mod
folder. A cheat switched on applies at once and stays on after a restart. Turning **Cheats** off stops every cheat and
hides the tab; the ones chosen come back when it is turned on again (`cheats` in `xcx.ini`).

Cheats change the running game, and the game saves what it finds in memory. Back up your saves first.

## Built-in cheats

For the 2.0.0 update (build `EC4ED46BBC4A9EBB`) the recomp carries the community cheat list for 1.0.2, whose
addresses 2.0.0 keeps: codes by pspmaster, TomSwitch, ranma99vn, Nic87, zzpong and WiduraGoez, collected by Muezaa.
The names were cleaned up for the menu; the codes are unchanged, except Max Funds, which is the recomp's own: it sets
Funds to 99,999,999, the game's cap, once a save is loaded. The list is `tools/data/cheats/EC4ED46BBC4A9EBB.txt`.
The runtime embeds it and every install builds its code cheats into the game, so they need no files and no extra
setup run. Values a cheat set stay where they are when it is switched off.

A few of them cannot be built in yet and show as unavailable, with the reason: see "Code cheats" below.

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

Most cheats for this game change its code (opcode `04000000` into `main`). On an emulator that works because it runs
the game's ARM code. Here that code was compiled to x86-64 during setup, so the setup builds such cheats into the game
instead: `tools/cheats.py` turns each one into a patch set that the Cheats tab switches on and off while the game runs.

- Put the cheat file in `mods/<name>/cheats/`, then run the setup again (Install or Update). It rebuilds the game with
  the cheats in; that takes a few minutes. A cheat added or edited later shows "needs the setup again" until then.
- Cheats that branch into a code cave (a "MUST BE ON" section that writes code past `main`) are supported: the cave's
  code is built in at the cheat's site.
- Button cheats (`8` opcode around the code writes) act only while their buttons are held.
- Some cheats can't be built in yet, and the tab greys them out with the reason: caves with their own branches
  (if/else or loops inside), and cheats that mix code changes with data written by the interpreter.
- Two cheats that change the same instruction (such as hp inf and tp inf) can't both act: the first one in the file
  wins.
- The setup lists what it built in `build-cache/cheats.status` next to `xcx.exe`.

The 1.0.2 cheats work on 2.0.0: their addresses are the same. Rename the file to `EC4ED46BBC4A9EBB.txt`.

## Checks

`xcx.exe --probe-cheats` runs the format and the interpreter against a buffer and needs no game files.
