// XCX_CROWD_DISTANCE: a multiplier on how far from the player the crowd people spawn and fade out (1, 1.5, 2, 3 or
// 4).
//
// The crowd people (the walkers of NLA and the other ambient "mob" groups) come in groups placed by the map. Each
// group spawns its people while the player is within its spawn distance and fades them out past its leave distance,
// both 25 m in NLA. Set "crowd" (recomp/patches-upd.txt) multiplies both by the float at patch page +0x120 where the
// spawn and fade checks compare them with the player's distance, so a change applies at once to every group: after
// every present rt_crowd_update writes the menu's value there and turns the set on (off at 1, the game's own code).
//
// The cap is 4: at 8, with that many more people, the game crashed in the physics step of its characters.
#include "cfg.h"
#include "host.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { CROWD_SCALE_PAGE = 0x120 };
_Static_assert(CROWD_SCALE_PAGE + sizeof(float) <= 0x1000, "the crowd scale must fit on the patch page");

static int crowd_set = -2;

// 1 when this build has the crowd patch set (the 2.0.0 update): the menu shows its row only then.
int rt_crowd_available(void) {
    if (crowd_set == -2) crowd_set = rt_patch_set("crowd");
    return crowd_set >= 0;
}

static float crowd_applied = -1.0f;   // the scale on the page, -1 before the first update

// At boot and after every present: the value of XCX_CROWD_DISTANCE (1 to 4), applied when it changed. The scale is
// stored before the set is turned on, so a site that sees the set on reads the new scale.
void rt_crowd_update(void) {
    uint8_t *page = rt_patch_page();
    if (!rt_crowd_available() || !page) return;
    const char *v = cfg_get("XCX_CROWD_DISTANCE");
    float scale = v && *v ? strtof(v, NULL) : 1.0f;
    if (!(scale >= 1.0f)) scale = 1.0f;   // also catches NaN and text that is not a number
    if (scale > 4.0f) scale = 4.0f;
    if (scale == crowd_applied) return;
    uint32_t word;
    memcpy(&word, &scale, sizeof word);
    __atomic_store_n((uint32_t *)(page + CROWD_SCALE_PAGE), word, __ATOMIC_RELEASE);
    rt_patch_switch(crowd_set, scale != 1.0f);
    crowd_applied = scale;
    rt_log("crowd: spawn and leave distances x%.2f", scale);
}

void rt_crowd_init(void) { rt_crowd_update(); }
