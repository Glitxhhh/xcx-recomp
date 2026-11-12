# Recomp Achievements

When [RecompLauncher](https://recomplauncher.com) starts the game, `xcx.exe` reports achievement events to it over the
local channel of [Recomp Achievements](https://github.com/elripalda/RecompAchievements) (its `docs/SPEC.md`, version
1). The launcher shows the toast, keeps the unlocks and decides what counts as Ranked. Started any other way, the game
does none of this. The client is our own code, `runtime/achievements.c`; the library's license lets any game use the
list format and the channel, and nothing of the library is copied.

## Events

The list is an `events` list: RecompLauncher hosts it and maps each event name to an achievement, so achievements can
be added or changed without a game update, and an event no list names does nothing. A thread reads the game's own data
twice a second (no work on the game's threads):

| Event | When |
| --- | --- |
| `level_10`, `level_20`, `level_30`, `level_40`, `level_50`, `level_60` | Any character reaches that level |
| `funds_1m`, `funds_10m` | Funds reach 1,000,000 or 10,000,000 |
| `funds_max` | Funds reach the cap, 99,999,999 |
| `tickets_max` | Tickets reach the cap, 9,999 |

An event fires only when the game is seen without it first, with the values settled for 10 s after the last save load.
A save that already has a level or the Funds never fires anything when it loads. The game data object is filled with
defaults for the first moments after boot and then rewritten in place by the save the game reads at start, or by a
load from the menu. A load is seen when 3 or more characters' levels change in one look, or a level goes down. A load
that changes only Funds or Tickets is not seen, so their events could fire on it.

What the game reads, build `EC4ED46BBC4A9EBB` of update 2.0.0: the game data object's pointer at main + `0x1DC1408`;
Funds a `u32` at `+0x1B5CC`, Tickets a `u32` at `+0x1B5D4`; 23 character slots `0x7F8` apart from the object, the level
a `u16` at `+0x172` in each. `XCX_ACH_TRACE=1` logs the values read when they change, and each load.

Also sent: `hello` on connect (`console` `switch`, `game` `xcx-recomp`, the port's version); `save_loaded` at each load
after the object first appears; `casual` with reason `cheats` while a cheat is switched on (F1 > Cheats), so those
unlocks are kept as Casual. The launcher's `peek` (its dev tools' memory viewer) is answered from guest memory at the
guest address it names (main is at `0x7000000000`). `list` and `quiet` need nothing from an events list.

## The list for RecompLauncher

A starting point to give the RecompLauncher team (they host and check the list; titles and points are theirs to set):

```json
{
  "format": 1,
  "game": "xcx-recomp",
  "source": "events",
  "versions": ">=0.1.22",
  "voids": [],
  "achievements": [
    {"id": 1, "title": "Rookie", "description": "Reach level 10 with any character", "points": 5, "type": "progression", "event": "level_10"},
    {"id": 2, "title": "Seasoned", "description": "Reach level 30 with any character", "points": 10, "event": "level_30"},
    {"id": 3, "title": "Veteran", "description": "Reach level 50 with any character", "points": 25, "event": "level_50"},
    {"id": 4, "title": "Master of Mira", "description": "Reach level 60 with any character", "points": 50, "event": "level_60"},
    {"id": 5, "title": "Millionaire", "description": "Hold 1,000,000 Funds", "points": 10, "event": "funds_1m"},
    {"id": 6, "title": "Tycoon", "description": "Hold 99,999,999 Funds", "points": 50, "type": "missable", "bonus": true, "event": "funds_max"}
  ]
}
```

Story achievements (chapters cleared, the ending: the spec's `win` type, needed for Beaten) need the game's scenario
progress, which is not found yet.

## Testing without the launcher

`tests/achievements_fake_launcher.py` plays the launcher's side on Windows: it creates the pipe, starts the game with
`RL_SESSION` naming it and prints every line the game sends.

```sh
python tests/achievements_fake_launcher.py --seconds 120 -- <install>/xcx.exe --seconds 100
```

With `XCX_ACH_TRACE=1` and a cheat that sets Funds while a button is held (a mod's `cheats/EC4ED46BBC4A9EBB.txt`,
pressed by `XCX_INPUT_SCRIPT`), `funds_1m`, `funds_10m` and `funds_max` fire once each, after `casual`.
