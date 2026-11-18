// Cheats in the Atmosphere format (docs/cheats.md): the port's built-in ones and each mod folder's
// cheats/<build id>.txt. Cheats that change data run 12 times a second on a thread of their own, in the interpreter
// of cheat_vm.c; cheats that change code were built into the game by the setup (tools/cheats.py) as patch sets, which
// switching them on or off flips (runtime/cheats.c).
#pragma once
#include <stdint.h>
#include "cheat_vm.h"

typedef struct {
    const char *source;   // the mod folder's name, or CHEATS_BUILTIN
    const char *name;
    int enabled, master;
    CheatState state;
    uint64_t refused;     // writes the cheat aimed at memory that cannot be written
    uint32_t buttons;     // a patched cheat that acts only while these buttons are held (nn::hid bits), else 0
    const char *note;     // CHEAT_UNSUPPORTED: why, from the setup ("" none)
} CheatRow;

#define CHEATS_BUILTIN "Built-in"

// Reads the cheats and starts their thread. Called once the game's modules are loaded.
void cheats_start(void);
int cheats_active(void);   // cheats are on and at least one is switched on (or a master code runs)
// The buttons the game was handed this poll (nn::hid bits), for the cheats that wait for a button.
void cheats_keys(uint64_t buttons);

// For the menu, from its thread. The rows stay valid until cheats_reload.
int cheats_count(void);
CheatRow cheats_row(int i);
void cheats_set(int i, int on);   // kept in cheats-enabled.txt of the mods folder
void cheats_reload(void);         // reads the cheat files again
const char *cheats_build_id(void);   // 16 hex digits, "" when main was not read
int cheats_other_builds(void);       // mod folders whose cheat files are all for another build
