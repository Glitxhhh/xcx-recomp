// See cheats.h. Guest addresses are host addresses (memory.c), so a cheat reads and writes the game's memory directly
// and the game's own pointers can be followed as they are. The checks in guest_write keep a wrong address from ending
// the game: on the console a cheat's bad write stops at the kernel, here it would be an access violation.
//
// Layout, under the mods folder of romfs_mods.c (XCX_MOD_DIR, else "mods" beside xcx.exe):
//   <mod>/cheats/<build id>.txt   a mod's cheats for one build of main (the NSO header's module id, 16 hex digits)
//   cheats-enabled.txt            "<mod>\t<cheat>" per switched-on cheat; the built-in ones use CHEATS_BUILTIN
#include "cheats.h"
#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "romfs_mods.h"
#include "platform/platform.h"

#define PERIOD_MS 83   // 12 passes a second, as Atmosphere runs cheats and as they are written for
#define STATE_FILE "cheats-enabled.txt"
#define PATH_CAP 4200

// The port's own cheats, for the 2.0.0 update (build BUILTIN_BUILD). The game data object's pointer is at main +
// 0x1DC1408; it is null until a save is loaded, which the 1 opcode tests first. In that object Funds are a u32 at
// +0x1B5CC, which the game caps at 99,999,999; the value set here shows in the game's own menu. Tickets (+0x1B5D4) have
// no cheat: their cap is not known, and a save was seen holding 15,000, above the 9,999 it was thought to be.
#define BUILTIN_BUILD "EC4ED46BBC4A9EBB"
static const char builtin_text[] =
    "[Max Funds]\n"
    "18060000 01DC1408 00000000 00000000\n"   // if the game data pointer is not null
    "58000000 01DC1408\n"                     // r0 = the pointer
    "78000000 0001B5CC\n"                     // r0 += Funds
    "64000000 00000000 05F5E0FF\n"            // u32 [r0] = 99,999,999
    "20000000\n";

typedef struct {
    char *name;   // the mod folder, or CHEATS_BUILTIN
    CheatFile cheats;
} Source;

static PlatMutex lock = PLAT_MUTEX_INIT;   // sources and the enabled flags, against the cheat thread
static Source *sources;
static int nsources, other_builds, started;
static char build_id[17];
static char **state;   // the lines of cheats-enabled.txt
static int nstate;
static uint64_t code_end;   // main's code and read-only data end here
static _Atomic uint64_t keys;

// ---- cheats-enabled.txt
static char *state_path(char *out, size_t cap) {
    const char *dir = romfs_mods_dir();
    if (!dir || snprintf(out, cap, "%s/%s", dir, STATE_FILE) >= (int)cap) return NULL;
    return out;
}

static char *state_line(const char *source, const char *cheat) {
    size_t n = strlen(source) + strlen(cheat) + 2;
    char *s = malloc(n);
    if (!s) rt_fatal("cheats: out of memory");
    snprintf(s, n, "%s\t%s", source, cheat);
    return s;
}

static int state_find(const char *line) {
    for (int i = 0; i < nstate; i++)
        if (!strcmp(state[i], line)) return i;
    return -1;
}

static void state_push(char *line) {
    char **grown = realloc(state, (size_t)(nstate + 1) * sizeof *grown);
    if (!grown) rt_fatal("cheats: out of memory");
    state = grown;
    state[nstate++] = line;
}

static void state_read(void) {
    char path[PATH_CAP];
    if (!state_path(path, sizeof path)) return;
    uint64_t n = 0;
    char *text = (char *)plat_file_read(path, &n);
    if (!text) return;
    for (uint64_t i = 0; i < n;) {
        uint64_t e = i;
        while (e < n && text[e] != '\n') e++;
        uint64_t b = e;
        while (b > i && text[b - 1] == '\r') b--;
        if (b > i && text[i] != '#') {
            char *s = malloc(b - i + 1);
            if (!s) rt_fatal("cheats: out of memory");
            memcpy(s, text + i, b - i);
            s[b - i] = 0;
            state_push(s);
        }
        i = e + 1;
    }
    free(text);
}

static void state_write(void) {
    char path[PATH_CAP];
    if (!state_path(path, sizeof path)) return;
    plat_file_mkdir_p(romfs_mods_dir());   // the built-in cheats need no mods, so the folder may not exist yet
    size_t n = 0;
    for (int i = 0; i < nstate; i++) n += strlen(state[i]) + 1;
    char *text = malloc(n + 1), *p = text;
    if (!text) rt_fatal("cheats: out of memory");
    for (int i = 0; i < nstate; i++) p += sprintf(p, "%s\n", state[i]);
    if (!plat_file_write_atomic(path, text, n)) rt_log("cheats: cannot write %s", path);
    free(text);
}

static void state_set(const char *source, const char *cheat, int on) {
    char *s = state_line(source, cheat);
    int at = state_find(s);
    if (on && at < 0) {
        state_push(s);
    } else {
        if (!on && at >= 0) {
            free(state[at]);
            state[at] = state[--nstate];
        }
        free(s);
    }
    state_write();
}

// ---- the folder
typedef struct { char **names; int n; } Names;

static int collect(const char *name, void *ctx) {
    Names *l = ctx;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return 1;
    char **grown = realloc(l->names, (size_t)(l->n + 1) * sizeof *grown);
    char *copy = strdup(name);
    if (!grown || !copy) rt_fatal("cheats: out of memory");
    l->names = grown;
    l->names[l->n++] = copy;
    return 1;
}

static int name_order(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static Names list_sorted(const char *path) {
    Names l = {NULL, 0};
    plat_file_list(path, "", collect, &l);
    if (l.n) qsort(l.names, (size_t)l.n, sizeof *l.names, name_order);
    return l;
}

static void names_free(Names *l) {
    for (int i = 0; i < l->n; i++) free(l->names[i]);
    free(l->names);
}

static void read_build_id(void) {
    char path[PATH_CAP];
    uint8_t id[8];
    const char *exefs = rt_config()->exefs;
    if (!exefs || snprintf(path, sizeof path, "%s/main", exefs) >= (int)sizeof path) return;
    PlatFile *f = plat_file_open(path);
    if (!f) return;
    if (plat_file_pread(f, id, sizeof id, 0x40) == (int64_t)sizeof id)   // the NSO header's module id
        for (int i = 0; i < 8; i++) snprintf(build_id + i * 2, 3, "%02X", id[i]);
    plat_file_close(f);
}

static int is_this_build(const char *file) {
    if (!*build_id || strlen(file) != 20) return 0;
    for (int i = 16; i < 20; i++)
        if (tolower((unsigned char)file[i]) != ".txt"[i - 16]) return 0;
    for (int i = 0; i < 16; i++)
        if (toupper((unsigned char)file[i]) != build_id[i]) return 0;
    return 1;
}

static void add_source(const char *name, const char *text, size_t n) {
    Source *grown = realloc(sources, (size_t)(nsources + 1) * sizeof *grown);
    if (!grown) rt_fatal("cheats: out of memory");
    sources = grown;
    Source *s = &sources[nsources++];
    memset(s, 0, sizeof *s);
    s->name = strdup(name);
    if (!s->name || !cheat_parse(text, n, &s->cheats)) rt_fatal("cheats: out of memory");
    if (code_end) cheat_scan_code_writes(&s->cheats, code_end);
    for (int k = 0; k < s->cheats.count; k++) {
        Cheat *c = &s->cheats.cheats[k];
        char *line = state_line(s->name, c->name);
        c->enabled = state_find(line) >= 0;
        free(line);
    }
}

// The mod folder's cheat file for this build, if it has one.
static void load_mod(const char *dir, const char *mod) {
    char path[PATH_CAP];
    if (snprintf(path, sizeof path, "%s/%s/cheats", dir, mod) >= (int)sizeof path || !plat_dir_exists(path)) return;
    Names files = list_sorted(path);
    int found = 0;
    for (int i = 0; i < files.n && !found; i++) {
        if (!is_this_build(files.names[i])) continue;
        found = 1;
        char file[PATH_CAP + 32];
        snprintf(file, sizeof file, "%s/%s", path, files.names[i]);
        uint64_t n = 0;
        char *text = (char *)plat_file_read(file, &n);
        if (!text) {
            rt_log("cheats: cannot read %s", file);
            break;
        }
        add_source(mod, text, (size_t)n);
        free(text);
        rt_log("cheats: %s: %d for build %s", mod, sources[nsources - 1].cheats.count, build_id);
    }
    if (!found && files.n) {
        other_builds++;
        rt_log("cheats: %s: none for build %s", mod, build_id);
    }
    names_free(&files);
}

// Under lock.
static void load_all(void) {
    for (int i = 0; i < nsources; i++) {
        cheat_free(&sources[i].cheats);
        free(sources[i].name);
    }
    free(sources);
    sources = NULL;
    nsources = other_builds = 0;
    for (int i = 0; i < nstate; i++) free(state[i]);
    free(state);
    state = NULL;
    nstate = 0;
    state_read();
    if (!strcmp(build_id, BUILTIN_BUILD)) add_source(CHEATS_BUILTIN, builtin_text, sizeof builtin_text - 1);
    const char *dir = romfs_mods_dir();
    if (!dir || !plat_dir_exists(dir)) return;
    Names names = list_sorted(dir);
    for (int i = 0; i < names.n; i++) {
        char path[PATH_CAP];
        snprintf(path, sizeof path, "%s/%s", dir, names.names[i]);
        if (plat_dir_exists(path)) load_mod(dir, names.names[i]);
    }
    names_free(&names);
}

// ---- the cheat thread
static int guest_read(void *ctx, uint64_t addr, void *out, int n) {
    (void)ctx;
    return addr < RT_ADDRESS_LIMIT && plat_mem_read(addr, out, (size_t)n);
}

// What a page must be for a cheat to write it: read-write, or frozen, which is read-write to the game (the first write
// faults, memory.c thaw_fault thaws the region and the write goes through). The kernel and host-function windows stay
// out of reach: they are the port's own and hold no game data.
static int guest_write(void *ctx, uint64_t addr, const void *in, int n) {
    (void)ctx;
    const RtGuestLayout *l = mem_layout();
    if (addr >= RT_ADDRESS_LIMIT || addr + (uint64_t)n > RT_ADDRESS_LIMIT) return 0;
    if (addr + (uint64_t)n > l->kernel.base && addr < l->kernel.base + l->kernel.size) return 0;
    if (addr + (uint64_t)n > l->hostfn.base && addr < l->hostfn.base + l->hostfn.size) return 0;
    for (uint64_t p = addr & ~0xFFFull; p < addr + (uint64_t)n; p += 0x1000) {
        uint64_t end;
        int st = plat_mem_state(p, p + 0x1000, &end);
        if (st != PLAT_PROT_RW && !(st == PLAT_PROT_READ && mem_frozen(p))) return 0;
    }
    memcpy((void *)(uintptr_t)addr, in, (size_t)n);
    return 1;
}

static void guest_log(void *ctx, int id, uint64_t value) {
    rt_log("cheats: %s: log %d = %#llx", ((Source *)ctx)->name, id, (unsigned long long)value);
}

static void cheat_thread(void *arg) {
    (void)arg;
    const RtGuestLayout *l = mem_layout();
    CheatEnv env = {.base = {ldr_module(0)->base, l->heap.base, l->alias.base, l->aslr.base, 0},
                    .read = guest_read, .write = guest_write, .log = guest_log};
    for (;;) {
        plat_sleep_ms(PERIOD_MS);
        env.keys = keys;
        plat_mutex_lock(&lock);
        for (int i = 0; i < nsources; i++) {
            env.ctx = &sources[i];
            cheat_run(&sources[i].cheats, &env);
            for (int k = 0; k < sources[i].cheats.count; k++) {
                Cheat *c = &sources[i].cheats.cheats[k];
                if (!c->refused || c->refusal_logged) continue;
                c->refusal_logged = 1;
                rt_log("cheats: %s: %s writes to memory that cannot be written, skipped", sources[i].name, c->name);
            }
        }
        plat_mutex_unlock(&lock);
    }
}

void cheats_start(void) {
    if (started) return;
    started = 1;
    read_build_id();
    code_end = ldr_module(0)->data_off;   // .text and .rodata: the lifted C holds the code, and rodata is sealed
    plat_mutex_lock(&lock);
    load_all();
    plat_mutex_unlock(&lock);
    rt_log("cheats: %d sources, game build %s", nsources, *build_id ? build_id : "unknown");
    if (!plat_thread_spawn(cheat_thread, NULL, "cheats", 256 << 10)) rt_log("cheats: no thread, cheats stay off");
}

void cheats_keys(uint64_t buttons) { keys = buttons; }

int cheats_count(void) {
    int n = 0;
    for (int i = 0; i < nsources; i++) n += sources[i].cheats.count;
    return n;
}

static Cheat *cheat_at(int i, Source **owner) {
    for (int k = 0; k < nsources; i -= sources[k++].cheats.count)
        if (i < sources[k].cheats.count) {
            *owner = &sources[k];
            return &sources[k].cheats.cheats[i];
        }
    return NULL;
}

CheatRow cheats_row(int i) {
    Source *s = NULL;
    Cheat *c = cheat_at(i, &s);
    if (!c) return (CheatRow){"", "", 0, 0, CHEAT_MALFORMED, 0};
    return (CheatRow){s->name, c->name, c->enabled, c->master, c->state, c->refused};
}

void cheats_set(int i, int on) {
    Source *s = NULL;
    plat_mutex_lock(&lock);
    Cheat *c = cheat_at(i, &s);
    if (c) {
        c->enabled = on != 0;
        state_set(s->name, c->name, on);
        rt_log("cheats: %s: %s %s", s->name, c->name, on ? "on" : "off");
    }
    plat_mutex_unlock(&lock);
}

void cheats_reload(void) {
    if (!started) return;
    plat_mutex_lock(&lock);
    load_all();
    plat_mutex_unlock(&lock);
}

const char *cheats_build_id(void) { return build_id; }
int cheats_other_builds(void) { return other_builds; }
