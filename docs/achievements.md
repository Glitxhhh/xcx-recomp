# Recomp Achievements

When [RecompLauncher](https://recomplauncher.com) starts the game, `xcx.exe` reports the game's own achievements to it
over the local channel of [Recomp Achievements](https://github.com/elripalda/RecompAchievements) (its `docs/SPEC.md`,
version 1). The launcher shows the toast, keeps the unlocks and decides what counts as Ranked. Started any other way,
the game does none of this. The client is our own code, `runtime/achievements.c`; the library's license lets any game
use the list format and the channel, and nothing of the library is copied.

## What the game sends

The list is a `native` list, the spec's kind for a game's own achievements: XCX has its own (the in-game Achievements
menu), and the port only passes on what the game awards. A thread reads the game's own data twice a second (no work on
the game's threads).

| `t` | When |
| --- | --- |
| `hello` | On connect: `console` `switch`, `game` `xcx-recomp`, the port's version |
| `native_list` | Right after `hello`, once the game has loaded its achievement tables (at boot): the 741 achievements the game's menu shows, each with its `id`, `title` and `description` read from the player's own copy of the game. No `gamerscore`: the game gives its achievements no points, so the hosted list sets them |
| `native` with `id` | The game awards its achievement with that ID |
| `native` with `id`, `earlier` | The loaded save already had it (sent once a session, 10 s after the load) |
| `event` `ach_<ID>` | With each `native` that is not `earlier`, for an `events` list naming the same achievements |
| `save_loaded` | At each load after the game data first appears |
| `casual` `cheats` | While a cheat is switched on (F1 > Cheats), so those unlocks are kept as Casual |

The spec lists `native` lists for the `ps3` and `x360` consoles only, so RecompLauncher has to take them for `switch`
too; until it does, the `ach_<ID>` events let an `events` list do the same. The launcher's `peek` (its dev tools'
memory viewer) is answered from guest memory at the guest address it names (main is at `0x7000000000`); `list` and
`quiet` need nothing from a native list.

An achievement counts as awarded now only when the game is seen without it first, with the values settled for 10 s
after the last save load; what a save already has is `earlier`. The game data object is filled with defaults for the
first moments after boot and then rewritten in place by the save the game reads at start, or by a load from the menu.
A load is seen when an earned achievement turns unearned, more than 8 achievements are earned in one look (half a
second), 3 or more characters' levels change in one look, a level or the scenario number goes down, or the scenario
number skips a chapter (play only ever moves it one step, or from one chapter's last step to the next chapter's first).
The levels and the scenario number serve only this. A load none of these catch (another save at nearly the same story
point and levels, with 8 or fewer achievements more) is not seen, and its extra achievements count as awarded now.

What the game reads, build `EC4ED46BBC4A9EBB` of update 2.0.0: the game data object's pointer at main + `0x1DC1408`;
the game's achievement records from `+0x91528`; 23 character slots `0x7F8` apart from the object, the level a `u16` at
`+0x172` in each; the scenario number a `u16` at `+0x7867A`; the tables `SCL_AchiveList` and `MNU_Achievement_ms`
(below). `XCX_ACH_TRACE=1` logs the top level, the scenario number and how many achievements are earned when they
change, each load, and each time the tables are looked for and not found.

### The game's own achievements

The game keeps its own list of achievements: the table `SCL_AchiveList` (sic), 844 rows, IDs 1 to 843 and a last empty
row `ACHIVE_END`. The `id` in `native`, `native_list` and the list is the row's ID. Each row names a statistic
(`StatsID`) and a goal (`Count`); when the game counts that statistic up to the goal, it awards the achievement (setting
the statistic by other means, a cheat for one, awards nothing: the game checks only when it counts). The row's `Disp` is
1 for the 741 the menu lists; the others are kept but never shown.

The result is a 16-byte record per ID at game data object + `0x91528` + ID × 16 (record 0 unused). The function the
menu asks, main + `0x4fa03c`, reads it at `0x17AB0` + ID × 16 into the block main + `0x4e4054` returns, which is the
object + `0x79A78`. The first `u32` holds flags: bit 1 is set when the achievement is awarded (and bit 0 with it), bit
2 once the menu has shown it as new, so an earned record reads 3 or 7. The third `u32` is when it was awarded, in
seconds since 1970. The records are saved with the rest of the object. A record with any other flag bit set means the
offsets do not fit the build, and the thread reads nothing.

The row IDs group by kind, from the row names: `ACHIVE_EV…` (1–54) the story parts of the prologue and chapters 1–12,
then `ACHIVE_QE…`, `ACHIVE_FLD…`, `ACHIVE_BTL…` and the rest, and `ACHIVE_STS_…` (790–843) the Definitive Edition's:
chapter 13's acts, the new party members and more. The story ones are one per part of a chapter, numbered as the menu
shows them:

| Part | IDs, in order | Last part |
| --- | --- | --- |
| Prologue | 1–5 | 5 |
| Chapter 1 | 6–10 | 10 |
| Chapter 2 | 11–14 | 14 |
| Chapter 3 | 15–18 | 18 |
| Chapter 4 | 19–22 | 22 |
| Chapter 5 | 23–27 | 27 |
| Chapter 6 | 28–31 | 31 |
| Chapter 7 | 32–35 | 35 |
| Chapter 8 | 36–38 | 38 |
| Chapter 9 | 39–42 | 42 |
| Chapter 10 | 43–45 | 45 |
| Chapter 11 | 46–49 | 49 |
| Chapter 12 | 50–54 | 54 |
| Chapter 13, act 1 | 790–796 | 796 |
| Chapter 13, act 2 | 797–805 | 805 |
| Chapter 13, act 3 | 806–811 | 811 |

Titles and descriptions are game text and stay out of this repository: the game reads them from the player's copy
while it runs and sends them in `native_list`. The row's `Title` and `Description` columns are row IDs in the text
table `MNU_Achievement_ms`, whose third column holds the text; the base texts are complete, and the `Title_replace_…`
and `Desc_replace_…` columns are not needed for them. Only rows with `Disp` 1 are listed; a hidden one the game awards
is still sent as `native`.

Both tables are BDAT version 4 tables the game keeps on its heap (the alias region; the address changes from run to
run, and no pointer from the modules' data leads to them), so the thread looks for them once, in one pass over the
alias region's committed pages (about a second), again every 20 s until both are found. A table starts with `BDAT`,
`0x04`, then `u32`s: columns, rows, the first row's ID, one unused, then the offsets from the table's start of the
column definitions (3 bytes each, type and name), the hashes, the rows, the row length, the strings and their length.
Between the 0x30-byte header and the column definitions the loaded table keeps a names block: every row's name, then
the table's, then its columns'. The list is found by its shape (14 columns, 844 rows of 26 bytes), the text table by
its name in that block. A row's name, such as `ACHIVE_EV000`, is stored in its `ID` column as the murmur3 hash (seed
0) of the name. Seen on saves: a New Los Angeles test save has 15 earned, IDs 1–6 among them.

### The scenario number

The story's progress is one number, the one the game's scripts read with `getScenario` and write with `setScenario`
(their natives at main + `0x3ed320` and `0x3ed370` call the flag store's get and set, main + `0xc96d0` and `0xc94a0`,
for 16-bit value 1). The flag store sits inside the game data object at `+0x72E78`; its 16-bit values start `0x5800`
into it, so value 1 is at `+0x7867A`. It is saved with the rest of the object.

It counts chapter × 1000 + step. The game's own conditions (the table `FLD_ConditionList_Scenario`, read from memory;
its row names are the murmur3 hashes in its ID column) give the steps:

| Part | Runs | Cleared at (condition) |
| --- | --- | --- |
| Prologue | 1–8 | 1001, chapter 1 starts (`COND_SCEN_START_M01`) |
| Chapter 1 | 1001–1011 | 1012 (`COND_SCEN_CLEAR_M01`) |
| Chapter 2 | 2001–2006 | 2007 (`CLEAR_M02`) |
| Chapter 3 | 3001–3009 | 3010 (`CLEAR_M03`) |
| Chapter 4 | 4001–4011 | 4012 (`CLEAR_M04`) |
| Chapter 5 | 5001–5014 | 5015 (`CLEAR_M05`) |
| Chapter 6 | 6002–6014 | 6015 (`CLEAR_M06`) |
| Chapter 7 | 7001–7007 | 7008 (`CLEAR_M07`) |
| Chapter 8 | 8001–8022 | 8023 (`CLEAR_M08`) |
| Chapter 9 | 9001–9011 | 9012 (`CLEAR_M09`) |
| Chapter 10 | 10001–10009 | 10010 (`CLEAR_M10`) |
| Chapter 11 | 11001–11010 | 11011 (`CLEAR_M11`) |
| Chapter 12 | 12001–12022 | 12023 (`CLEAR_M12`) |
| Chapter 13, act 1 (`M31`) | 13002–13020 | 13021 (`COND_SCEN_0009_CLEAR_M31`) |
| Chapter 13, act 2 (`M32`) | 13021–14036 | 14037 (`COND_SCEN_0010_CLEAR_M32`) |
| Chapter 13, act 3 (`M33`) | 14037–15025 | 15026 (`COND_SCEN_0011_CLEAR_M33`); 15027 is the last value |

Chapters 1–12 match the Wii U game's table. The chapter menu (`MNU_ChapterList`) names M31–M33 "Chapter 13, Act
1–3". Seen on saves: the prologue test saves read 8, the New Los Angeles ones 1001 and 1007.

## The list for RecompLauncher

A starting point to give the RecompLauncher team (they host and check the list, and set the points). Titles and
descriptions come from `native_list`, so the list only gives each achievement's `id` (the game's), `points` and `type`.
It names the story achievements: the last part of each chapter is `progression` and the last part of chapter 13, act
3 is `win`, which Beaten needs. The other 725 the game shows are up to the team; every one counts for Mastered unless
the list makes it `bonus`.

```json
{
  "format": 1,
  "game": "xcx-recomp",
  "source": "native",
  "console": "switch",
  "versions": ">=0.1.22",
  "voids": [],
  "achievements": [
    {"id": 5, "points": 10, "type": "progression"},
    {"id": 10, "points": 10, "type": "progression"},
    {"id": 14, "points": 10, "type": "progression"},
    {"id": 18, "points": 10, "type": "progression"},
    {"id": 22, "points": 10, "type": "progression"},
    {"id": 27, "points": 10, "type": "progression"},
    {"id": 31, "points": 10, "type": "progression"},
    {"id": 35, "points": 10, "type": "progression"},
    {"id": 38, "points": 10, "type": "progression"},
    {"id": 42, "points": 10, "type": "progression"},
    {"id": 45, "points": 10, "type": "progression"},
    {"id": 49, "points": 10, "type": "progression"},
    {"id": 54, "points": 10, "type": "progression"},
    {"id": 796, "points": 10, "type": "progression"},
    {"id": 805, "points": 10, "type": "progression"},
    {"id": 811, "points": 50, "type": "win"}
  ]
}
```

The spec does not have this list yet (native lists exist for `ps3` and `x360`); the shape follows its PS3 lists, minus
the titles that `native_list` brings.

## Testing without the launcher

`tests/achievements_fake_launcher.py` plays the launcher's side on Windows: it creates the pipe, starts the game with
`RL_SESSION` naming it and prints every line the game sends.

```sh
python tests/achievements_fake_launcher.py --seconds 120 -- <install>/xcx.exe --seconds 100
```

With `XCX_ACH_TRACE=1` and a cheat that writes a record's flags while a button is held (a mod's
`cheats/EC4ED46BBC4A9EBB.txt` with `80000200`, `58000000 01DC1408`, `78000000 <0x91528 + ID × 16>`,
`64000000 00000000 00000003`, `20000000`, pressed by `XCX_INPUT_SCRIPT`), on a copy of a New Los Angeles save (15
earned): `native_list` comes right after `hello` with 741 achievements, the load sends `native` with `earlier` for those
15, and the cheat sends `casual`, then `native` and `ach_<ID>` once.
