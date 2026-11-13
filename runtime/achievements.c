// Recomp Achievements: unlocks reported to RecompLauncher, which shows the toast, keeps them and decides what counts
// as Ranked. Our own client of the channel its spec describes (github.com/elripalda/RecompAchievements, docs/SPEC.md,
// version 1; its license lets any game use the format and channel); none of its library's code is here.
//
// RecompLauncher starts the game with RL_SESSION naming a local channel (a named pipe on Windows, a Unix socket
// elsewhere; platform.h plat_ipc_*). Without it this file does nothing. Both sides send one JSON object per line.
//
// The list is an "events" list: the launcher maps each event name below to an achievement, so events can be added to
// a list without changing the game, and an event no list names does nothing. A thread looks at the game's own data
// twice a second; nothing runs on the game's threads. What it reads, all from the game data object whose pointer is at
// main + GAME_DATA (runtime/cheats.c, the port's Max Funds, reads the same):
//   Funds        u32 at +FUNDS (the game caps them at 99,999,999)
//   Tickets      u32 at +TICKETS (cap 9,999)
//   characters   CHARACTERS slots CHARACTER_STRIDE apart from the object, the level a u16 at +LEVEL in each
// Events: level_<n> when any character reaches level n (10, 20, 30, 40, 50, 60), funds_1m, funds_10m, funds_max,
// tickets_max. The launcher unlocks an achievement once.
//
// A load must not fire anything: the object holds defaults (10,000 Funds) for the first moments after boot, then the
// save the game reads at start, and loading another save rewrites it in place (the pointer stays). A load is when the
// object first appears or changes to another one, when LOAD_LEVELS or more characters' levels change in one look, or
// when any level goes down. An event arms only once it was seen false with the values settled SETTLE_MS since the last
// load, and fires when it then holds: what a save already has never fires (as the spec's memory rules wait for a false
// frame). A load that changes only Funds or Tickets is not seen; their events could then fire on it.
//
// Also sent: hello on connect; save_loaded at each load after the object first appears (the save read at start is one);
// casual "cheats" while a cheat is switched on, so its unlocks are kept as Casual. From the launcher, peek (its dev
// tools' memory viewer) is answered from guest memory; list and quiet need nothing from an events list.
//
// XCX_ACH_TRACE=1 logs each value read when it changes, to check the offsets on another build.
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "cheats.h"
#include "host.h"
#include "platform/platform.h"

#define PERIOD_MS 500
#define RECONNECT_MS 5000
#define GAME_DATA 0x1DC1408u   // main + this: the game data object's pointer, 2.0.0 (build EC4ED46BBC4A9EBB)
#define FUNDS 0x1B5CCu
#define TICKETS 0x1B5D4u
#define CHARACTERS 23
#define CHARACTER_STRIDE 0x7F8u
#define LEVEL 0x172u
#define LEVEL_MAX 99            // a level read above it is not a level: the offsets do not fit this build
#define LOAD_LEVELS 3           // characters whose level changes in one look: a save loaded, not play
#define SETTLE_MS 10000

typedef struct {
    const char *name;
    int armed;   // seen false since the save loaded: it may fire
    int fired;   // sent this session
} Event;

static const int levels[] = {10, 20, 30, 40, 50, 60};
enum { LEVELS = sizeof levels / sizeof levels[0] };
static Event events[] = {{.name = "level_10"}, {.name = "level_20"}, {.name = "level_30"}, {.name = "level_40"}, {.name = "level_50"},
                         {.name = "level_60"},
                         {.name = "funds_1m"}, {.name = "funds_10m"}, {.name = "funds_max"}, {.name = "tickets_max"}};
enum { EVENTS = sizeof events / sizeof events[0] };

static PlatIpc *channel;
static char session[512];
static uint64_t frame;   // the spec's frame: the thread's ticks, only for ordering
static int trace;

static void send_line(const char *line) {
    if (!channel) return;
    size_t n = strlen(line);
    char buf[1100];
    if (n + 2 > sizeof buf) return;
    memcpy(buf, line, n);
    buf[n++] = '\n';
    if (!plat_ipc_write(channel, buf, n)) {
        rt_log("achievements: the launcher closed the channel");
        plat_ipc_close(channel);
        channel = NULL;
    }
}

// JSON string body: the names here are plain ASCII; anything else is dropped rather than escaped wrong.
static void json_text(char *out, size_t cap, const char *s) {
    size_t k = 0;
    for (; *s && k + 2 < cap; s++)
        if ((unsigned char)*s >= 0x20 && *s != '"' && *s != '\\' && (unsigned char)*s < 0x7f) out[k++] = *s;
    out[k] = 0;
}

static void say_hello(void) {
    char version[64], line[256];
    json_text(version, sizeof version, rt_port_version);
    snprintf(line, sizeof line,
             "{\"t\":\"hello\",\"sdk\":\"xcx-recomp-own-1\",\"console\":\"switch\",\"memory\":0,"
             "\"game\":\"xcx-recomp\",\"version\":\"%s\"}", version);
    send_line(line);
}

static void send_event(Event *e) {
    char line[160];
    snprintf(line, sizeof line, "{\"t\":\"event\",\"name\":\"%s\",\"frame\":%" PRIu64 "}", e->name, frame);
    send_line(line);
    rt_log("achievements: event %s", e->name);
}

// Where the value of a line's top-level field "key" starts (spaces around the colon allowed), NULL without it. The
// launcher's lines are flat objects for the messages read here, so a plain search for the quoted key is enough.
static const char *field(const char *line, const char *key) {
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    for (const char *p = strstr(line, pat); p; p = strstr(p + 1, pat)) {
        const char *q = p + strlen(pat);
        while (*q == ' ' || *q == '	') q++;
        if (*q != ':') continue;
        q++;
        while (*q == ' ' || *q == '	') q++;
        return q;
    }
    return NULL;
}

// A number field, -1 when it is not there.
static int64_t field_num(const char *line, const char *key) {
    const char *p = field(line, key);
    return p ? strtoll(p, NULL, 0) : -1;
}

// The message type "t" is this name.
static int is_type(const char *line, const char *name) {
    const char *p = field(line, "t");
    size_t n = strlen(name);
    return p && *p == '"' && !strncmp(p + 1, name, n) && p[1 + n] == '"';
}

// peek: up to 256 bytes of guest memory at address, as hex; unreadable memory answers an empty hex.
static void answer_peek(const char *line) {
    int64_t address = field_num(line, "address"), size = field_num(line, "size"), seq = field_num(line, "seq");
    if (address < 0 || size <= 0) return;
    if (size > 256) size = 256;
    uint8_t mem[256];
    char hex[520] = "", out[700];
    if ((uint64_t)address < RT_ADDRESS_LIMIT && plat_mem_read((uint64_t)address, mem, (size_t)size))
        for (int i = 0; i < size; i++) snprintf(hex + 2 * i, 3, "%02x", mem[i]);
    snprintf(out, sizeof out, "{\"t\":\"mem\",\"seq\":%" PRId64 ",\"hex\":\"%s\"}", seq < 0 ? 0 : seq, hex);
    send_line(out);
}

static void read_channel(void) {
    static char buf[16384];
    static size_t have;
    for (;;) {
        if (!channel) return;
        int64_t got = plat_ipc_read(channel, buf + have, sizeof buf - 1 - have);
        if (got < 0) {
            rt_log("achievements: the launcher closed the channel");
            plat_ipc_close(channel);
            channel = NULL;
            have = 0;
            return;
        }
        if (got == 0) return;
        have += (size_t)got;
        buf[have] = 0;
        char *start = buf, *nl;
        while ((nl = strchr(start, '\n'))) {
            *nl = 0;
            if (is_type(start, "peek")) answer_peek(start);
            else if (is_type(start, "list")) rt_log("achievements: the launcher sent its list");
            start = nl + 1;
        }
        have = (size_t)(buf + have - start);
        memmove(buf, start, have);
        if (have == sizeof buf - 1) have = 0;   // a line longer than the buffer: dropped
    }
}

static int read_u32(uint64_t addr, uint32_t *out) { return plat_mem_read(addr, out, sizeof *out); }

static uint16_t last_levels[CHARACTERS];

// One look at the game: the events that hold now and *loaded when the levels changed as only a load changes them; 0
// when the data does not fit.
static int look(uint64_t data, int hold[EVENTS], int *loaded) {
    uint32_t funds = 0, tickets = 0;
    if (!read_u32(data + FUNDS, &funds) || !read_u32(data + TICKETS, &tickets)) return 0;
    int top = 0, changed = 0, down = 0;
    uint16_t now[CHARACTERS];
    for (int i = 0; i < CHARACTERS; i++) {
        uint16_t level = 0;
        if (!plat_mem_read(data + i * (uint64_t)CHARACTER_STRIDE + LEVEL, &level, sizeof level)) return 0;
        if (level > LEVEL_MAX) return 0;
        if (level > top) top = level;
        changed += level != last_levels[i];
        down |= level < last_levels[i];
        now[i] = level;
    }
    *loaded = changed >= LOAD_LEVELS || down;
    memcpy(last_levels, now, sizeof now);
    for (int k = 0; k < LEVELS; k++) hold[k] = top >= levels[k];
    hold[LEVELS + 0] = funds >= 1000000u;
    hold[LEVELS + 1] = funds >= 10000000u;
    hold[LEVELS + 2] = funds >= 99999999u;
    hold[LEVELS + 3] = tickets >= 9999u;
    static int64_t last_top = -1, last_funds = -1, last_tickets = -1;
    if (trace && (top != last_top || funds != last_funds || tickets != last_tickets)) {
        rt_log("achievements: top level %d, funds %u, tickets %u", top, funds, tickets);
        last_top = top, last_funds = funds, last_tickets = tickets;
    }
    return 1;
}

static void achievements_thread(void *arg) {
    (void)arg;
    uint64_t main_base = ldr_module(0)->base, last_data = 0, next_connect = 0, settled_from = 0;
    int casual_sent = 0, bad_logged = 0, loads = 0;
    for (;;) {
        plat_sleep_ms(PERIOD_MS);
        frame++;
        uint64_t now = plat_now_ms();
        if (!channel && now >= next_connect) {
            next_connect = now + RECONNECT_MS;
            if ((channel = plat_ipc_connect(session))) {
                rt_log("achievements: connected to RecompLauncher");
                say_hello();
                casual_sent = 0;
                for (int k = 0; k < EVENTS; k++) events[k].fired = 0;
            }
        }
        read_channel();
        if (!channel) continue;
        if (!casual_sent && cheats_active()) {
            send_line("{\"t\":\"casual\",\"reason\":\"cheats\"}");
            casual_sent = 1;
        }
        uint64_t data = 0;
        if (!plat_mem_read(main_base + GAME_DATA, &data, sizeof data) || !data || data >= RT_ADDRESS_LIMIT) {
            last_data = 0;
            continue;
        }
        int hold[EVENTS], loaded = 0;
        if (!look(data, hold, &loaded)) {
            if (!bad_logged) rt_log("achievements: the game data does not fit this build's offsets; no events");
            bad_logged = 1;
            continue;
        }
        if (data != last_data || loaded) {   // a save loaded: every event waits to be seen false again, settled
            last_data = data;
            settled_from = now + SETTLE_MS;
            for (int k = 0; k < EVENTS; k++) events[k].armed = 0;
            if (loads++) {   // the first is the boot's defaults, or the save read at start
                char line[96];
                snprintf(line, sizeof line, "{\"t\":\"save_loaded\",\"frame\":%" PRIu64 "}", frame);
                send_line(line);
            }
            if (trace) rt_log("achievements: load %d", loads);
            continue;
        }
        int settled = now >= settled_from;
        for (int k = 0; k < EVENTS; k++) {
            Event *e = &events[k];
            if (!hold[k]) e->armed |= settled;
            else if (e->armed && !e->fired) {
                e->fired = 1;
                send_event(e);
            }
        }
    }
}

void achievements_start(void) {
    if (!plat_getenv("RL_SESSION", session, sizeof session) || !*session) return;
    char t[8];
    trace = plat_getenv("XCX_ACH_TRACE", t, sizeof t) && t[0] == '1';
    rt_log("achievements: RecompLauncher session channel given");
    if (!plat_thread_spawn(achievements_thread, NULL, "achievements", 128 << 10))
        rt_log("achievements: no thread, no achievements this session");
}
