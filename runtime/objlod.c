// XCX_OBJECT_LOD_RANGE: metres added to the distance at which trees, bushes, rocks and the other map objects switch
// to their detailed models (0 = the game's distances). XCX_OBJECT_LOD_MODE: auto (the default) treats that as a
// ceiling and keeps the frame within the frame budget by moving the range below it (auto_step); fixed uses it as set.
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

// XCX_OBJECT_LOD_MODE=auto (the default) treats the range as a ceiling and moves the margin between 0 and it so
// the frame fits the frame budget (the frame limit, or the display's refresh rate without one). It watches each
// frame's busy time, its interval minus the time it waited for pacing (perf.c perf_last_frame), as a median over half
// a second:
// - over 103% of the budget for a second (two windows in a row), or over 125% of it for one window: down by 8% plus
//   one and a half times the overrun (3% over: 12.5%, 10%: 23%, 25%: 45%, 70% at most), at least 10 m; no
//   step up for 1 s, and for 5 s no step back up to the value that was too much; the window after a drop is not
//   judged, as its frames mostly came before the drop took effect (without that pause a big overrun cut again every
//   half second, all the way to 0);
// - under 92% of the budget for a window: up by a share of the value in effect (at least 10 m) that grows with the
//   time to spare: 8% just under 92%, 15% at 80%, 30% under 70%. So it creeps near 0 and near the edge of the budget
//   and strides when there is plenty of room (leaving the city).
// Windows whose frames take 100 ms or more (loading, a pause) are skipped. fixed keeps the range as set.
#define AUTO_WINDOW_NS 500000000ull
#define AUTO_SAMPLES 512
static struct {
    float now, max, busy_ms, budget_ms;   // the margin in effect, the ceiling, the last window's median busy time, budget
    int on, bad, settle;                  // Auto in effect; windows in a row over the budget; skip the next window
    uint64_t window_at, up_after, trouble_until, seq;
    float trouble;                        // the value that was too much, until trouble_until
    float busy[AUTO_SAMPLES], interval[AUTO_SAMPLES];
    int n;
} lod_auto = {.now = -1};

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static float auto_step(float max) {
    uint64_t now_ns = plat_now_ns(), interval, pace;
    uint64_t seq = perf_last_frame(&interval, &pace);
    if (lod_auto.now < 0 || lod_auto.max != max) {   // first use, or a new ceiling: start at it / keep within it
        if (lod_auto.now < 0 || lod_auto.now > max) lod_auto.now = max;
        lod_auto.max = max;
        lod_auto.trouble_until = 0;
    }
    if (seq != lod_auto.seq && interval) {
        lod_auto.seq = seq;
        if (lod_auto.n < AUTO_SAMPLES) {
            lod_auto.busy[lod_auto.n] = (float)((interval > pace ? interval - pace : 0) / 1e6);
            lod_auto.interval[lod_auto.n] = (float)(interval / 1e6);
            lod_auto.n++;
        }
    }
    if (!lod_auto.window_at) lod_auto.window_at = now_ns;
    if (now_ns - lod_auto.window_at < AUTO_WINDOW_NS) return lod_auto.now;
    lod_auto.window_at = now_ns;
    int n = lod_auto.n;
    lod_auto.n = 0;
    if (lod_auto.settle) {   // the window after a drop: its frames show the value before it
        lod_auto.settle = 0;
        return lod_auto.now;
    }
    double hz = frame_step_limit_hz();
    if (n < 10 || hz <= 0) return lod_auto.now;
    qsort(lod_auto.interval, n, sizeof(float), cmp_float);
    if (lod_auto.interval[n / 2] >= 100.0f) return lod_auto.now;   // loading or paused
    qsort(lod_auto.busy, n, sizeof(float), cmp_float);
    float busy = lod_auto.busy[n / 2], budget = (float)(1000.0 / hz);
    lod_auto.busy_ms = busy;
    lod_auto.budget_ms = budget;
    float room = 1.0f - busy / budget;   // the share of the budget to spare
    float share = room >= 0.3f ? 0.3f : room >= 0.2f ? 0.15f + (room - 0.2f) * 1.5f : room >= 0.08f ? 0.08f + (room - 0.08f) * 0.58f : 0.08f;
    float up = lod_auto.now * share > 10 ? lod_auto.now * share : 10, down_min = 10;
    if (busy > budget * 1.03f) {
        if (++lod_auto.bad >= 2 || busy > budget * 1.25f) {   // a second over the budget, or far over it
            lod_auto.bad = 0;
            lod_auto.up_after = now_ns + 1000000000ull;
            if (lod_auto.now > 0) {
                lod_auto.trouble = lod_auto.now;
                lod_auto.trouble_until = now_ns + 5000000000ull;
                float cut = 0.08f + (busy / budget - 1.0f) * 1.5f;
                if (cut > 0.7f) cut = 0.7f;
                float d = lod_auto.now * cut > down_min ? lod_auto.now * cut : down_min;
                lod_auto.now = lod_auto.now > d ? lod_auto.now - d : 0;
                lod_auto.settle = 1;
            }
        }
    } else if (busy < budget * 0.92f) {
        lod_auto.bad = 0;
        if (now_ns >= lod_auto.up_after && lod_auto.now < max) {
            float cap = max;
            if (now_ns < lod_auto.trouble_until && lod_auto.trouble - up < cap) cap = lod_auto.trouble - up;
            if (lod_auto.now < cap) lod_auto.now = lod_auto.now + up < cap ? lod_auto.now + up : cap;
        }
    } else {
        lod_auto.bad = 0;
    }
    return lod_auto.now;
}

int rt_objlod_status(float *now_m, float *max_m, float *busy_ms, float *budget_ms) {
    if (!lod_auto.on) return 0;
    *now_m = lod_auto.now;
    *max_m = lod_auto.max;
    *busy_ms = lod_auto.busy_ms;
    *budget_ms = lod_auto.budget_ms;
    return 1;
}

void rt_objlod_update(void) {
    if (!rt_objlod_available()) return;
    const char *v = cfg_get("XCX_OBJECT_LOD_RANGE");
    float m = v && *v ? strtof(v, NULL) : 0.0f;
    if (!(m >= 0.0f)) m = 0.0f;   // also catches NaN and text that is not a number
    if (m > OBJ_LOD_MAX) m = OBJ_LOD_MAX;
    const char *mode = cfg_value("XCX_OBJECT_LOD_MODE");
    int automatic = m > 0 && !(mode && !strcmp(mode, "fixed"));
    if (automatic != lod_auto.on) {
        rt_log("object lod: %s", automatic ? "auto, the range follows the frame budget" : "fixed range");
        lod_auto.on = automatic;
        lod_auto.now = -1;   // Auto starts again at the ceiling
    }
    float ceiling = m;
    if (automatic) m = auto_step(ceiling);
    uint64_t obj = 0;
    if (!plat_mem_read(ldr_module(0)->base + OBJ_SETTINGS_GLOBAL, &obj, sizeof obj) || !obj) return;   // not made yet
    if (!plat_mem_readable(obj + OBJ_LOD_MARGIN, sizeof(float))) return;
    volatile float *p = (volatile float *)(uintptr_t)(obj + OBJ_LOD_MARGIN);
    float was = *p;
    if (was != m) *p = m;
    if (ceiling != objlod_logged || obj != objlod_obj_logged) {   // Auto: when the ceiling changes, not at each step
        rt_log(automatic ? "object lod: auto, up to +%.0f m on every map object LOD distance (settings %#llx, was %g)"
                         : "object lod: +%.0f m on every map object LOD distance (settings %#llx, was %g)",
               ceiling, (unsigned long long)obj, was);
        objlod_logged = ceiling;
        objlod_obj_logged = obj;
    }
}
