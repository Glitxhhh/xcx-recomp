// XCX_OBJECT_LOD_RANGE: metres added to the distance at which trees, bushes, rocks and the other map objects switch
// to their detailed models (0 = the game's distances).
//
// Map objects are instanced models placed by the map. The trees, bushes and rocks the player sees take their level of detail
// from main+0x9fa5a0 (reached from the map object renderer main+0x9ad0a0 through main+0x9f7740 and main+0x9fa110),
// after the spatial tree walk main+0x9fa400 has culled the nodes out of range. The pick tries the model's LOD records
// (0x24 bytes each, the switch distance at +0x1c) in order, and the first whose distance plus the margin covers the
// camera distance wins; past the last one the object is hidden.
//
// The margin is a float at +0x9ec of the map object settings, whose pointer is at main+0x27681e0 (main+0x96fd74
// returns it). The game adds it to every LOD distance and to every node's range (so nodes are not culled before their
// objects) but only ever sets it to 0, when the settings object is made. Three other routines read the same field (a
// similar walk and pick at main+0x9a1980 and main+0x9a2b60, a walk at main+0x9a3760 and one read in main+0x9fb4a0);
// taking the margin away from each of them in turn, with a test build, made no visible difference. So this writes the configured metres there at
// every present, and a change applies at once. The game's own distances run from under 5 m to about 50 m depending on
// the model, so a margin moves all of them by the same amount where a multiplier would leave the short ones short.
//
// The cost is mostly on the CPU: more objects at their detailed level means more draws to record. Measured on an RTX
// 4090 at a 90 fps cap: GPU time per frame flat standing still and +0.2 to +0.4 ms running a path, recording +0.3 ms at
// +300 m and +0.65 ms at +1000 m, no hitches and no extra video memory.
//
// The addresses are those of the 2.0.0 update (build EC4ED46BBC4A9EBB), so the setting is off for any other game and
// when the four instructions it relies on differ from what is checked below.
#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern const char rt_game_name[];

#define OBJ_SETTINGS_GLOBAL 0x27681e0u   // main + this: the map object settings' pointer
#define OBJ_LOD_MARGIN 0x9ecu            // settings + this: the LOD margin in metres (float)
#define OBJ_LOD_MAX 1000.0f

// The code that reads the margin, as 2.0.0 has it: main+0x96fd74 loads the settings pointer (add x8, x8, #0x1e0;
// ldr x0, [x8]), and the tree walk and the LOD pick read the margin right after calling it (ldr s0, [x0, #0x9ec] at
// main+0x9fa44c and main+0x9fa7cc).
static const struct { uint32_t off, word; } objlod_code[] = {
    {0x96fd78, 0x91078108}, {0x96fd7c, 0xf9400100}, {0x9fa44c, 0xbd49ec00}, {0x9fa7cc, 0xbd49ec00},
};

static int objlod_state;   // 0 not checked, 1 available, -1 not this game
static float objlod_logged = -1;
static uint64_t objlod_obj_logged;

// 1 when this game has the map object settings this setting writes (the 2.0.0 update): the menu shows its row only then.
int rt_objlod_available(void) {
    if (objlod_state) return objlod_state > 0;
    if (ldr_count() < 1) return 0;   // not loaded yet: ask again later
    objlod_state = -1;
    if (strcmp(rt_game_name, "2.0.0")) return 0;
    uint64_t base = ldr_module(0)->base;
    for (size_t i = 0; i < sizeof objlod_code / sizeof *objlod_code; i++) {
        uint32_t w = 0;
        if (!plat_mem_read(base + objlod_code[i].off, &w, sizeof w) || w != objlod_code[i].word) {
            rt_log("object lod: main+%#x holds %#010x, not %#010x: setting off", objlod_code[i].off, w, objlod_code[i].word);
            return 0;
        }
    }
    objlod_state = 1;
    return 1;
}

void rt_objlod_update(void) {
    if (!rt_objlod_available()) return;
    const char *v = cfg_get("XCX_OBJECT_LOD_RANGE");
    float m = v && *v ? strtof(v, NULL) : 0.0f;
    if (!(m >= 0.0f)) m = 0.0f;   // also catches NaN and text that is not a number
    if (m > OBJ_LOD_MAX) m = OBJ_LOD_MAX;
    uint64_t obj = 0;
    if (!plat_mem_read(ldr_module(0)->base + OBJ_SETTINGS_GLOBAL, &obj, sizeof obj) || !obj) return;   // not made yet
    if (!plat_mem_readable(obj + OBJ_LOD_MARGIN, sizeof(float))) return;
    volatile float *p = (volatile float *)(uintptr_t)(obj + OBJ_LOD_MARGIN);
    float was = *p;
    if (was != m) *p = m;
    if (m != objlod_logged || obj != objlod_obj_logged) {
        rt_log("object lod: +%.0f m on every map object LOD distance (settings %#llx, was %g)", m,
               (unsigned long long)obj, was);
        objlod_logged = m;
        objlod_obj_logged = obj;
    }
}
