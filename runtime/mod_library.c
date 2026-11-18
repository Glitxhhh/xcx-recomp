// The mod library in game: the same index, folder layout and checks as XCX Setup's Mods window (installer/mods.py),
// so a mod installed in either one shows in the other.
//
//   mods/<id>/romfs/...      a game-file mod (runtime/romfs_mods.c), on when listed in romfs-enabled.txt
//   mods/<id>/mod.ini ...    a Lua mod (runtime/mod_package.c), listed in catalog.txt, on when in enabled.txt
//   mods/<id>/library.ini    name, author, version and kind of a mod either manager installed
//   mods/<anything else>     a mod unpacked there by hand, moved to mods/<id>/romfs when the library is read (normalise)
//
// Jobs run one at a time on a thread of their own: the index download, an install (download, size and SHA-256
// check, unpack into mods/.install-<id>, then a rename to mods/<id>) or a removal. The menu reads a copy of the
// state under the lock and never waits on the network.
//
// XCX_MOD_INDEX: the index to read instead of the library's, an https URL or a local file. XCX_MOD_TEST_FILES=1 also
// lets the index give file: URLs, so tests install from zips on disk. XCX_MODLIB_SCRIPT="install <id>; on <id>; off
// <id>; remove <id>; ...": steps the library thread runs, in order, once the index was first read (the Mods tab starts
// that), each logged with its outcome; for tests that cannot click.
#include "mod_library.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "host.h"
#include "mod_fetch.h"
#include "mod_zip.h"
#include "platform/platform.h"
#include "romfs_mods.h"
#include "sha256.h"

#define LIBRARY "https://raw.githubusercontent.com/jmars125/xcx-recomp-mods/main/"
#define INDEX_FORMAT 2
#define INDEX_MAX (8u << 20)
#define ZIP_MAX ((uint64_t)1 << 30)   // a download the game unpacks in memory
#define LUA_FILES 8

typedef struct { char name[32]; char sha256[65]; uint64_t size; } LuaFile;
typedef struct {
    ModLibRow row;
    char url[400], sha256[65], path[128];
    int nfiles;
    LuaFile files[LUA_FILES];
} Entry;

static PlatMutex lock = PLAT_MUTEX_INIT;
static Entry index_entries[MODLIB_ROWS];
static int index_count;
static ModLibView view;          // the published view (rows rebuilt by rescan)
static int started, busy;

static void publish_locked(void) { view.generation++; }

static void set_message(int error, const char *fmt, const char *a, const char *b) {
    plat_mutex_lock(&lock);
    snprintf(view.message, sizeof view.message, fmt, a ? a : "", b ? b : "");
    view.message_error = error;
    publish_locked();
    plat_mutex_unlock(&lock);
}

static int valid_id(const char *id) {
    size_t n = strlen(id);
    if (!n || n > 64 || !islower((unsigned char)id[0])) return 0;
    for (size_t i = 0; i < n; i++)
        if (!islower((unsigned char)id[i]) && !isdigit((unsigned char)id[i]) && !strchr("._-", id[i])) return 0;
    return 1;
}

static int valid_sha(const char *s) {
    if (strlen(s) != 64) return 0;
    for (int i = 0; i < 64; i++)
        if (!isdigit((unsigned char)s[i]) && !(s[i] >= 'a' && s[i] <= 'f')) return 0;
    return 1;
}

// ---- JSON: just enough to read the index (objects, arrays, strings, numbers, true/false/null)

typedef struct { const char *p, *end; int bad; } Json;

static void ws(Json *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t')) j->p++;
}

static int eat(Json *j, char c) {
    ws(j);
    if (j->p < j->end && *j->p == c) {
        j->p++;
        return 1;
    }
    return 0;
}

static int hex4(const char *p, unsigned *v) {
    *v = 0;
    for (int i = 0; i < 4; i++) {
        int c = p[i], d = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return 0;
        *v = *v << 4 | (unsigned)d;
    }
    return 1;
}

// A string into out (UTF-8, cut at cap - 1 bytes on a character boundary); out may be NULL to skip it.
static int string(Json *j, char *out, size_t cap) {
    if (!eat(j, '"')) return j->bad = 1, 0;
    size_t n = 0;
    while (j->p < j->end && *j->p != '"') {
        unsigned c = (unsigned char)*j->p++;
        char enc[4];
        int len = 1;
        enc[0] = (char)c;
        if (c == '\\') {
            if (j->p >= j->end) break;
            char e = *j->p++;
            if (e == 'u') {
                unsigned u;
                if (j->end - j->p < 4 || !hex4(j->p, &u)) return j->bad = 1, 0;
                j->p += 4;
                if (u >= 0xd800 && u < 0xdc00 && j->end - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
                    unsigned lo;
                    if (hex4(j->p + 2, &lo) && lo >= 0xdc00 && lo < 0xe000) {
                        u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
                        j->p += 6;
                    }
                }
                if (u < 0x80) enc[0] = (char)u;
                else if (u < 0x800) { enc[0] = (char)(0xc0 | u >> 6); enc[1] = (char)(0x80 | (u & 63)); len = 2; }
                else if (u < 0x10000) {
                    enc[0] = (char)(0xe0 | u >> 12); enc[1] = (char)(0x80 | (u >> 6 & 63));
                    enc[2] = (char)(0x80 | (u & 63)); len = 3;
                } else {
                    enc[0] = (char)(0xf0 | u >> 18); enc[1] = (char)(0x80 | (u >> 12 & 63));
                    enc[2] = (char)(0x80 | (u >> 6 & 63)); enc[3] = (char)(0x80 | (u & 63)); len = 4;
                }
            } else {
                enc[0] = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'b' ? '\b' : e == 'f' ? '\f' : e;
            }
        } else if (c < 0x20) {
            return j->bad = 1, 0;
        }
        if (out && n + (size_t)len < cap) {
            memcpy(out + n, enc, (size_t)len);
            n += (size_t)len;
        }
    }
    if (out) {
        while (n && ((unsigned char)out[n - 1] & 0xc0) == 0x80 && n + 4 > cap) n--;   // a cut character
        out[n] = 0;
    }
    if (j->p >= j->end) return j->bad = 1, 0;
    j->p++;
    return 1;
}

static int number(Json *j, uint64_t *v) {
    ws(j);
    if (j->p >= j->end || !isdigit((unsigned char)*j->p)) return 0;
    uint64_t x = 0;
    while (j->p < j->end && isdigit((unsigned char)*j->p)) x = x * 10 + (uint64_t)(*j->p++ - '0');
    while (j->p < j->end && strchr(".eE+-0123456789", *j->p)) j->p++;
    *v = x;
    return 1;
}

static void skip(Json *j, int depth) {
    ws(j);
    if (j->p >= j->end || depth > 32) { j->bad = 1; return; }
    char c = *j->p;
    if (c == '"') { string(j, NULL, 0); return; }
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        j->p++;
        if (eat(j, close)) return;
        do {
            if (c == '{') {
                if (!string(j, NULL, 0) || !eat(j, ':')) { j->bad = 1; return; }
            }
            skip(j, depth + 1);
        } while (!j->bad && eat(j, ','));
        if (!eat(j, close)) j->bad = 1;
        return;
    }
    while (j->p < j->end && !strchr(",}] \n\r\t", *j->p)) j->p++;   // a number, true, false or null
}

static void lua_files(Json *j, Entry *e) {
    if (!eat(j, '[')) { skip(j, 1); return; }
    if (eat(j, ']')) return;
    do {
        LuaFile f = {0};
        if (!eat(j, '{')) { j->bad = 1; return; }
        if (!eat(j, '}')) {
            do {
                char key[32];
                if (!string(j, key, sizeof key) || !eat(j, ':')) { j->bad = 1; return; }
                if (!strcmp(key, "name")) string(j, f.name, sizeof f.name);
                else if (!strcmp(key, "sha256")) string(j, f.sha256, sizeof f.sha256);
                else if (!strcmp(key, "size")) { if (!number(j, &f.size)) skip(j, 2); }
                else skip(j, 2);
            } while (!j->bad && eat(j, ','));
            if (!eat(j, '}')) { j->bad = 1; return; }
        }
        if (e->nfiles < LUA_FILES) e->files[e->nfiles++] = f;
        else e->nfiles = LUA_FILES + 1;   // too many: the entry is refused
    } while (!j->bad && eat(j, ','));
    if (!eat(j, ']')) j->bad = 1;
}

static void entry(Json *j, Entry *e) {
    memset(e, 0, sizeof *e);
    if (!eat(j, '{')) { j->bad = 1; return; }
    if (eat(j, '}')) return;
    do {
        char key[32];
        if (!string(j, key, sizeof key) || !eat(j, ':')) { j->bad = 1; return; }
        ModLibRow *r = &e->row;
#define STR(k, field) else if (!strcmp(key, k)) { ws(j); if (j->p < j->end && *j->p == '"') string(j, field, sizeof field); else skip(j, 2); }
        if (0) {}
        STR("id", r->id) STR("kind", r->kind) STR("name", r->name) STR("author", r->author)
        STR("description", r->description) STR("notes", r->notes) STR("homepage", r->homepage)
        STR("version", r->version) STR("url", e->url) STR("sha256", e->sha256) STR("path", e->path)
        else if (!strcmp(key, "size")) { if (!number(j, &r->size)) skip(j, 2); }
        else if (!strcmp(key, "files")) lua_files(j, e);
        else skip(j, 2);
#undef STR
    } while (!j->bad && eat(j, ','));
    if (!eat(j, '}')) j->bad = 1;
}

// Whether an entry is complete enough to install, by the rules of installer/mods.py.
static int usable(const Entry *e) {
    const ModLibRow *r = &e->row;
    if (!valid_id(r->id)) return 0;
    if (!strcmp(r->kind, "romfs"))
        return valid_sha(e->sha256) && r->size && (!strncmp(e->url, "https://", 8) ||
               (!strncmp(e->url, "file:", 5) && getenv("XCX_MOD_TEST_FILES")));
    if (!strcmp(r->kind, "lua")) {
        if (!e->nfiles || e->nfiles > LUA_FILES || !e->path[0] || strstr(e->path, "..")) return 0;
        static const char *const allowed[] = {"mod.ini", "main.lua", "info.ini", "README.md", "LICENSE", "LICENSE.txt"};
        for (int i = 0; i < e->nfiles; i++) {
            int ok = valid_sha(e->files[i].sha256);
            int known = 0;
            for (size_t k = 0; k < sizeof allowed / sizeof *allowed; k++) known |= !strcmp(e->files[i].name, allowed[k]);
            if (!ok || !known) return 0;
        }
        return 1;
    }
    return 0;
}

// Parses an index; the count of usable entries, or -1 when the format is unknown.
static int parse_index(const char *text, size_t n, Entry *out, int cap) {
    Json j = {text, text + n, 0};
    uint64_t format = 0;
    int count = 0, saw_mods = 0;
    if (!eat(&j, '{')) return -1;
    if (!eat(&j, '}')) {
        do {
            char key[32];
            if (!string(&j, key, sizeof key) || !eat(&j, ':')) return -1;
            if (!strcmp(key, "format")) { if (!number(&j, &format)) skip(&j, 1); }
            else if (!strcmp(key, "mods")) {
                saw_mods = 1;
                if (!eat(&j, '[')) return -1;
                if (!eat(&j, ']')) {
                    do {
                        Entry e;
                        entry(&j, &e);
                        if (j.bad) return -1;
                        int dup = 0;
                        for (int i = 0; i < count; i++) dup |= !strcmp(out[i].row.id, e.row.id);
                        if (usable(&e) && !dup && count < cap) {
                            e.row.in_library = 1;
                            out[count++] = e;
                        }
                    } while (eat(&j, ','));
                    if (!eat(&j, ']')) return -1;
                }
            } else skip(&j, 1);
        } while (!j.bad && eat(&j, ','));
        if (!eat(&j, '}')) return -1;
    }
    return !j.bad && saw_mods && format == INDEX_FORMAT ? count : -1;
}

// ---- the mods folder

static const char *root(void) { return romfs_mods_dir(); }

static void join(char *out, size_t cap, const char *a, const char *b) { snprintf(out, cap, "%s/%s", a, b); }

// The ids of a list file (enabled.txt, catalog.txt, romfs-enabled.txt) in order; the count.
static int read_list(const char *file, char (*ids)[65], int cap) {
    char path[4200];
    join(path, sizeof path, root(), file);
    uint64_t size = 0;
    char *text = (char *)plat_file_read(path, &size);
    int n = 0;
    for (char *p = text, *e; p && *p && n < cap; p = e ? e + 1 : NULL) {
        e = strchr(p, '\n');
        if (e) *e = 0;
        size_t len = strlen(p);
        while (len && (p[len - 1] == '\r' || p[len - 1] == ' ')) p[--len] = 0;
        if (p[0] == '#' || !valid_id(p)) continue;
        int dup = 0;
        for (int i = 0; i < n; i++) dup |= !strcmp(ids[i], p);
        if (!dup) snprintf(ids[n++], 65, "%s", p);
    }
    free(text);
    return n;
}

// Writes a list with id first (on) or without it (off), through a rename so the game never reads half a list.
static int write_list(const char *file, const char *id, int on) {
    char ids[64][65];
    int n = read_list(file, ids, 64);
    char text[64 * 66 + 66];
    size_t len = 0;
    if (on) len += (size_t)snprintf(text, sizeof text, "%s\n", id);
    for (int i = 0; i < n; i++)
        if (strcmp(ids[i], id)) len += (size_t)snprintf(text + len, sizeof text - len, "%s\n", ids[i]);
    char path[4200];
    plat_file_mkdir_p(root());
    join(path, sizeof path, root(), file);
    return plat_file_write_atomic(path, text, len);
}

static int in_list(const char *file, const char *id) {
    char ids[64][65];
    int n = read_list(file, ids, 64);
    for (int i = 0; i < n; i++)
        if (!strcmp(ids[i], id)) return 1;
    return 0;
}

static void ini_value(const char *text, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key);
    for (const char *p = text; p && *p;) {
        while (*p == ' ' || *p == '\t') p++;
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len > kl && !strncmp(p, key, kl)) {
            const char *v = p + kl, *end = p + len;
            while (v < end && (*v == ' ' || *v == '\t')) v++;
            if (v < end && *v == '=') {
                for (v++; v < end && (*v == ' ' || *v == '\t'); v++) {}
                while (end > v && (end[-1] == '\r' || end[-1] == ' ')) end--;
                if (end > v && (size_t)(end - v) < cap) {
                    memcpy(out, v, (size_t)(end - v));
                    out[end - v] = 0;
                }
                return;
            }
        }
        if (!e) break;
        p = e + 1;
    }
}

// ---- mods put in the folder by hand
//
// A player unpacks a mod's download into mods/ as it comes: a folder named after the mod ("Unburger Patch"), the
// Switch SD card layout (atmosphere/contents/<title id>/romfs, <title id>/romfs, contents/<title id>/romfs) or a bare
// romfs/. normalise moves each such folder to mods/<id>/romfs, the one layout romfs_mods.c reads, and keeps a line
// for the Mods tab on each folder it moved or left alone. installer/mods.py, normalise, follows the same rules, so
// XCX Setup and the game agree on the result.

#define TITLE_ID "0100453019aa8000"   // Xenoblade Chronicles X: Definitive Edition
#define SEARCH_DEPTH 4                 // folders below the top one in which a romfs folder is looked for
#define TOP_NAMES 256                  // entries of mods/ looked at

static ModLibNote notes[MODLIB_NOTES];
static int nnotes;

static void note(int error, const char *fmt, const char *a, const char *b) {
    if (nnotes < MODLIB_NOTES) {
        snprintf(notes[nnotes].text, sizeof notes[nnotes].text, fmt, a, b ? b : "");
        notes[nnotes++].error = error;
    }
    char line[sizeof notes[0].text];
    snprintf(line, sizeof line, fmt, a, b ? b : "");
    rt_log("mod library: %s", line);
}

static int is_title_id(const char *s, size_t n) {
    if (n != 16) return 0;
    for (size_t i = 0; i < n; i++)
        if (!isxdigit((unsigned char)s[i])) return 0;
    return 1;
}

// The mod id made from a folder name: ASCII letters and digits lowercased, '.' and '_' kept, anything else a '-'
// (one for a run), 'mod-' in front when it does not start with a letter, at most 64 characters. installer/mods.py,
// slug, is the same.
static void slug(const char *text, char out[65]) {
    char s[300];
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p && n < sizeof s - 1; p++) {
        char c = *p < 0x80 && (isalnum(*p) || *p == '.' || *p == '_') ? (char)tolower(*p) : '-';
        if (c == '-' && n && s[n - 1] == '-') continue;
        s[n++] = c;
    }
    s[n] = 0;
    char *b = s;
    while (*b && strchr("-._", *b)) b++;
    size_t len = strlen(b);
    while (len && strchr("-._", b[len - 1])) b[--len] = 0;
    char t[310];
    if (len && islower((unsigned char)b[0])) snprintf(t, sizeof t, "%s", b);
    else snprintf(t, sizeof t, len ? "mod-%s" : "mod", b);
    t[64] = 0;
    len = strlen(t);
    while (len && strchr("-._", t[len - 1])) t[--len] = 0;
    memcpy(out, t, len + 1);
}

// The romfs folders nearest below a top folder: best is their depth, count how many, found the first one's path.
typedef struct { char path[4200]; size_t base; int best, count; char found[4200]; } Find;

static void find_in(Find *f);
static int find_entry(const char *name, int kind, uint64_t size, void *ctx) {
    (void)size;
    Find *f = ctx;
    if (kind != PLAT_FS_DIR) return 1;
    size_t len = strlen(f->path), n = strlen(name);
    if (len + n + 2 >= sizeof f->path) return 1;
    int depth = 0;   // folders below the top one, this one included
    for (const char *p = f->path + f->base; *p; p++) depth += *p == '/';
    depth++;
    f->path[len] = '/';
    memcpy(f->path + len + 1, name, n + 1);
    if (!strcasecmp(name, "romfs")) {
        if (depth < f->best) { f->best = depth; f->count = 0; }
        if (depth == f->best && f->count++ == 0) snprintf(f->found, sizeof f->found, "%s", f->path);
    } else if (depth < f->best && depth < SEARCH_DEPTH) {
        find_in(f);
    }
    f->path[len] = 0;
    return 1;
}
static void find_in(Find *f) { plat_fs_list(f->path, find_entry, f); }

typedef struct { int n; char *name[TOP_NAMES]; } Tops;

static int top_entry(const char *name, int kind, uint64_t size, void *ctx) {
    (void)kind; (void)size;
    Tops *t = ctx;
    if (name[0] == '.') return 1;   // .install-, .old-: the managers' own
    return t->n < TOP_NAMES && (t->name[t->n] = strdup(name)) != NULL ? (t->n++, 1) : 0;
}

static int by_str(const void *a, const void *b) {
    const char *const *x = a, *const *y = b;
    return strcmp(*x, *y);
}

typedef struct { int n, mod; char first[260]; } Count;
static int count_entry(const char *name, int kind, uint64_t size, void *ctx) {
    (void)size;
    Count *c = ctx;
    if (!c->n++) snprintf(c->first, sizeof c->first, "%s", name);
    c->mod = c->n == 1 && kind == PLAT_FS_DIR && !strcasecmp(name, "mod");
    return 1;
}

// Whether romfs holds just a folder named mod: the XCXDE mod loader's layout, romfs/mod/<game path>.
static int is_loader(const char *romfs, char first[260]) {
    Count c = {0};
    plat_fs_list(romfs, count_entry, &c);
    if (first) memcpy(first, c.first, sizeof c.first);
    return c.n == 1 && c.mod;
}

static int exists(const char *path) { return plat_fs_kind(path) != PLAT_FS_MISSING; }

// base, or base-2, base-3 ...: the first id not taken in the mods folder. The folder itself (same name but for
// case) does not count, so on a file system that ignores case "Uncensor" becomes "uncensor".
static void free_id(const char *base, const char *folder, char out[65]) {
    char path[4300];
    snprintf(out, 65, "%s", base);
    for (int n = 2; n < 1000; n++) {
        snprintf(path, sizeof path, "%s/%s", root(), out);
        if (!exists(path) || (folder && !strcasecmp(out, folder))) return;
        char suffix[8];
        int sl = snprintf(suffix, sizeof suffix, "-%d", n);
        snprintf(out, 65, "%.*s%s", 64 - sl, base, suffix);
    }
}

static int move(const char *from, const char *to) { return plat_fs_rename(from, to, 1) == PLAT_FS_OK; }

// One entry of the mods folder; see the comment above.
static void normalise_one(const char *name, int kind) {
    char top[4200], romfs[4200], path[4300], first[260];
    join(top, sizeof top, root(), name);
    size_t rl = strlen(root());
    if (kind == PLAT_FS_FILE) {
        size_t n = strlen(name);
        if (n > 4 && !strcasecmp(name + n - 4, ".zip"))
            note(1, "%s in the mods folder is not read: unpack it there, or add it with Add zip in XCX Setup.%s",
                 name, NULL);
        return;
    }
    if (kind != PLAT_FS_DIR) return;
    snprintf(path, sizeof path, "%s/mod.ini", top);
    if (plat_fs_kind(path) == PLAT_FS_FILE) {
        if (!valid_id(name))
            note(1, "\"%s\" in the mods folder is not read: a Lua mod's folder name may have only lowercase letters, "
                    "digits, '.', '_' and '-'.%s", name, NULL);
        return;
    }
    int bare = !strcasecmp(name, "romfs");
    if (bare) {
        snprintf(romfs, sizeof romfs, "%s", top);
    } else {
        Find f = {.best = SEARCH_DEPTH + 1};
        snprintf(f.path, sizeof f.path, "%s", top);
        f.base = strlen(top);
        find_in(&f);
        if (!f.count) {
            note(1, "\"%s\" in the mods folder is not read: it has no romfs folder or mod.ini.%s", name, NULL);
            return;
        }
        if (f.count > 1) {
            note(1, "\"%s\" in the mods folder is not read: it has more than one romfs folder. Put each mod in a "
                    "folder of its own.%s", name, NULL);
            return;
        }
        snprintf(romfs, sizeof romfs, "%s", f.found);
    }
    const char *rel = romfs + rl + 1;   // name/.../romfs
    for (const char *p = rel; *p;) {
        size_t n = strcspn(p, "/");
        char part[17];
        if (is_title_id(p, n) && (memcpy(part, p, 16), part[16] = 0, strcasecmp(part, TITLE_ID))) {
            note(1, "\"%s\" in the mods folder is not read: it is made for another game (title ID %s).", name, part);
            return;
        }
        p += n + (p[n] == '/');
    }
    const char *inner = strchr(rel, '/');   // the romfs below the top folder: "/.../romfs", or NULL when bare
    if (valid_id(name) && inner && !strcmp(inner, "/romfs") && !is_loader(romfs, NULL)) return;   // already right
    int generic = bare || !strcasecmp(name, "atmosphere") || !strcasecmp(name, "contents") ||
                  is_title_id(name, strlen(name));
    char id[65], base[65], final[4200], dst[4300];
    if (generic) free_id("romfs-mod", NULL, id);
    else if (valid_id(name)) snprintf(id, sizeof id, "%s", name);
    else { slug(name, base); free_id(base, name, id); }
    join(final, sizeof final, root(), id);
    snprintf(dst, sizeof dst, "%s/romfs", final);
    int made = 0, ok;
    if (bare) {
        ok = (made = plat_fs_mkdir(final) == PLAT_FS_OK) && move(top, dst);
    } else if (generic) {
        snprintf(path, sizeof path, "%s/%s", final, name);
        char from[4400];
        snprintf(from, sizeof from, "%s/%s", final, rel);
        ok = (made = plat_fs_mkdir(final) == PLAT_FS_OK) && move(top, path) && move(from, dst);
    } else {
        ok = !strcmp(id, name) || move(top, final);
        if (ok && strcmp(inner, "/romfs")) {
            snprintf(path, sizeof path, "%s%s", final, inner);
            ok = move(path, dst);
        }
    }
    if (ok && is_loader(dst, first)) {
        char tmp[4300], mod[4600];
        snprintf(tmp, sizeof tmp, "%s/.romfs-loader", final);
        snprintf(mod, sizeof mod, "%s/%s", tmp, first);
        ok = move(dst, tmp) && move(mod, dst) && plat_fs_rmdir(tmp) == PLAT_FS_OK;
    }
    if (!ok) {
        char detail[32];
        snprintf(detail, sizeof detail, "error %u", plat_fs_os_error());
        if (made) plat_fs_rmdir(final);   // only while still empty
        note(1, "\"%s\" in the mods folder could not be moved to the layout the game reads: %s", name, detail);
        return;
    }
    snprintf(path, sizeof path, "%s/library.ini", final);
    if (!exists(path)) {
        char text[400];
        int n = snprintf(text, sizeof text, "name = %.90s\nauthor = \nversion = \nkind = romfs\nsource = folder\n",
                         generic ? id : name);
        plat_file_write(path, text, (size_t)n);
    }
    note(0, "\"%s\" in the mods folder is now the mod %s. Turn it on to use it.", name, id);
}

// Runs on the library thread, which alone writes notes; the view gets a copy.
static void normalise(void) {
    nnotes = 0;
    Tops t = {0};
    if (root()) plat_fs_list(root(), top_entry, &t);
    qsort(t.name, (size_t)t.n, sizeof t.name[0], by_str);   // the order installer/mods.py goes in
    for (int i = 0; i < t.n; i++) {
        char path[4200];
        join(path, sizeof path, root(), t.name[i]);
        normalise_one(t.name[i], plat_fs_kind(path));
        free(t.name[i]);
    }
    plat_mutex_lock(&lock);
    memcpy(view.notes, notes, sizeof notes);
    view.nnotes = nnotes;
    publish_locked();
    plat_mutex_unlock(&lock);
}

typedef struct { ModLibRow *rows; int n, cap; } Scan;

static int scan_entry(const char *name, int kind, uint64_t size, void *ctx) {
    (void)size;
    Scan *s = ctx;
    if (kind != PLAT_FS_DIR || !valid_id(name) || s->n >= s->cap) return 1;
    char dir[4200], path[4300];
    join(dir, sizeof dir, root(), name);
    ModLibRow r = {0};
    snprintf(r.id, sizeof r.id, "%s", name);
    snprintf(path, sizeof path, "%s/romfs", dir);
    if (plat_fs_kind(path) == PLAT_FS_DIR) strcpy(r.kind, "romfs");
    snprintf(path, sizeof path, "%s/mod.ini", dir);
    if (!r.kind[0] && plat_fs_kind(path) == PLAT_FS_FILE) strcpy(r.kind, "lua");
    if (!r.kind[0]) return 1;
    snprintf(r.name, sizeof r.name, "%s", name);
    snprintf(path, sizeof path, "%s/library.ini", dir);
    uint64_t sz = 0;
    char *ini = (char *)plat_file_read(path, &sz);
    if (ini) {
        ini_value(ini, "name", r.name, sizeof r.name);
        ini_value(ini, "author", r.author, sizeof r.author);
        ini_value(ini, "version", r.version, sizeof r.version);
        free(ini);
    }
    r.installed = 1;
    r.enabled = in_list(!strcmp(r.kind, "romfs") ? "romfs-enabled.txt" : "enabled.txt", name);
    s->rows[s->n++] = r;
    return 1;
}

static int by_name(const void *a, const void *b) {
    const ModLibRow *x = a, *y = b;
    return strcasecmp(x->name, y->name);
}

// The view's rows from the index and the folder: library entries first, then mods only on disk.
static void rescan(void) {
    static ModLibRow have[MODLIB_ROWS];
    Scan s = {have, 0, MODLIB_ROWS};
    if (root()) plat_fs_list(root(), scan_entry, &s);
    static ModLibRow rows[MODLIB_ROWS];
    int n = 0;
    plat_mutex_lock(&lock);
    for (int i = 0; i < index_count && n < MODLIB_ROWS; i++) {
        ModLibRow r = index_entries[i].row;
        for (int k = 0; k < s.n; k++)
            if (!strcmp(have[k].id, r.id)) {
                r.installed = 1;
                r.enabled = have[k].enabled;
                r.update = strcmp(have[k].version, r.version) != 0;
                have[k].id[0] = 0;   // joined
            }
        rows[n++] = r;
    }
    for (int k = 0; k < s.n && n < MODLIB_ROWS; k++)
        if (have[k].id[0]) rows[n++] = have[k];
    qsort(rows, (size_t)n, sizeof *rows, by_name);
    memcpy(view.rows, rows, (size_t)n * sizeof *rows);
    view.count = n;
    publish_locked();
    plat_mutex_unlock(&lock);
}

// Deletes a directory tree below the mods folder; links are removed, never followed.
static int rm_entry(const char *name, int kind, uint64_t size, void *ctx);
static void rm_tree(const char *path) {
    int kind = plat_fs_kind(path);
    if (kind == PLAT_FS_DIR) {
        plat_fs_list(path, rm_entry, (void *)path);
        plat_fs_rmdir(path);
    } else if (kind != PLAT_FS_MISSING) {
        plat_fs_unlink(path);
    }
}
static int rm_entry(const char *name, int kind, uint64_t size, void *ctx) {
    (void)kind; (void)size;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return 1;
    char path[4200];
    join(path, sizeof path, (const char *)ctx, name);
    rm_tree(path);
    return 1;
}

// ---- jobs

static void progress(uint64_t done, uint64_t total, void *ctx) {
    (void)ctx;
    plat_mutex_lock(&lock);
    view.done = done;
    view.total = total;
    publish_locked();
    plat_mutex_unlock(&lock);
}

// A file: URL of the test hook copied to path; NULL on success.
static const char *fetch_any(const char *url, const char *path, uint64_t max) {
    if (!strncmp(url, "file:", 5) && getenv("XCX_MOD_TEST_FILES")) {
        uint64_t size = 0;
        uint8_t *data = plat_file_read(url + 5, &size);
        const char *err = !data ? "the test file is missing" : size > max ? "the file is larger than the library says"
                        : !plat_file_write(path, data, size) ? "the file could not be written" : NULL;
        free(data);
        return err;
    }
    return mod_fetch(url, path, max, progress, NULL);
}

// The SHA-256 of a file and its size; 0 when it cannot be read.
static int file_sha(const char *path, char out[65], uint64_t *size) {
    PlatFile *f = plat_file_open(path);
    if (!f) return 0;
    static __thread uint8_t buf[1 << 16];
    Sha256 s;
    sha256_init(&s);
    uint64_t off = 0;
    for (;;) {
        int64_t got = plat_file_pread(f, buf, sizeof buf, off);
        if (got < 0) { plat_file_close(f); return 0; }
        if (!got) break;
        sha256_update(&s, buf, (size_t)got);
        off += (uint64_t)got;
    }
    plat_file_close(f);
    uint8_t d[32];
    sha256_final(&s, d);
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
    *size = off;
    return 1;
}

// Downloads url to path and checks it against the library's size and SHA-256; NULL on success.
static const char *fetch_checked(const char *url, const char *path, uint64_t size, const char *sha) {
    plat_fs_unlink(path);
    const char *err = fetch_any(url, path, size);
    if (err) return err;
    char got[65];
    uint64_t got_size = 0;
    if (!file_sha(path, got, &got_size)) return "the download could not be read back";
    if (got_size != size || strcmp(got, sha))
        return "the download is not the file the library lists (size or SHA-256 differ); the author may have uploaded "
               "a new version";
    return NULL;
}

static const char *install_job(const Entry *e, const char *staged) {
    const ModLibRow *r = &e->row;
    char path[4300];
    if (!strcmp(r->kind, "romfs")) {
        if (r->size > ZIP_MAX) return "the mod is too large to install in game; use XCX Setup's Mods button";
        snprintf(path, sizeof path, "%s.zip", staged);
        const char *err = fetch_checked(e->url, path, r->size, e->sha256);
        if (!err) {
            uint64_t size = 0;
            uint8_t *zip = plat_file_read(path, &size);
            err = !zip ? "the download could not be read back" : mod_zip_unpack_romfs(zip, (size_t)size, staged);
            free(zip);
        }
        plat_fs_unlink(path);
        return err;
    }
    for (int i = 0; i < e->nfiles; i++) {
        char url[600];
        snprintf(url, sizeof url, "%s%s/%s", LIBRARY, e->path, e->files[i].name);
        snprintf(path, sizeof path, "%s/%s", staged, e->files[i].name);
        const char *err = fetch_checked(url, path, e->files[i].size, e->files[i].sha256);
        if (err) return err;
    }
    return NULL;
}

static void write_library_ini(const char *staged, const ModLibRow *r) {
    char text[512], name[96], author[64];
    // one line each: a newline in the index would start a key of its own
    snprintf(name, sizeof name, "%s", r->name);
    snprintf(author, sizeof author, "%s", r->author);
    for (char *p = name; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
    for (char *p = author; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
    int n = snprintf(text, sizeof text, "name = %s\nauthor = %s\nversion = %s\nkind = %s\nsource = %s\n", name, author,
                     r->version, r->kind, r->homepage[0] ? r->homepage : "library");
    char path[4300];
    snprintf(path, sizeof path, "%s/library.ini", staged);
    plat_file_write(path, text, (size_t)n);
}

typedef struct { int type; char id[65]; int on; } Job;
enum { JOB_INDEX, JOB_INSTALL, JOB_REMOVE };

static void load_index(void) {
    const char *source = getenv("XCX_MOD_INDEX");
    char *text = NULL;
    uint64_t size = 0;
    const char *err = NULL;
    if (source && *source && strncmp(source, "https://", 8)) {
        text = (char *)plat_file_read(source, &size);
        if (!text) err = "the index file is missing";
    } else {
        char path[4200];
        plat_file_mkdir_p(root());
        join(path, sizeof path, root(), ".index.json");
        plat_fs_unlink(path);
        err = mod_fetch(source && *source ? source : LIBRARY "index.json", path, INDEX_MAX, NULL, NULL);
        if (!err) text = (char *)plat_file_read(path, &size);
        plat_fs_unlink(path);
        if (!err && !text) err = "the index could not be read back";
    }
    static Entry parsed[MODLIB_ROWS];
    int n = err ? -1 : parse_index(text, (size_t)size, parsed, MODLIB_ROWS);
    free(text);
    plat_mutex_lock(&lock);
    if (n >= 0) {
        memcpy(index_entries, parsed, (size_t)n * sizeof *parsed);
        index_count = n;
        view.index_ok = 1;
    }
    plat_mutex_unlock(&lock);
    if (err) {
        set_message(1, "The mod library could not be reached (%s); installed mods are listed and can still be "
                       "switched.%s", err, "");
        rt_log("mod library: %s", err);
    } else if (n < 0) {
        set_message(1, "The mod library has a format this build does not know; a newer XCX Recomp release reads it.%s%s",
                    NULL, NULL);
    } else {
        rt_log("mod library: %d mods", n);
    }
}

static void remove_job(const char *id) {
    char dir[4200], old[4300];
    join(dir, sizeof dir, root(), id);
    if (plat_fs_kind(dir) != PLAT_FS_DIR) {
        set_message(1, "%s is not installed.%s", id, NULL);
        return;
    }
    write_list("romfs-enabled.txt", id, 0);
    write_list("enabled.txt", id, 0);
    if (in_list("catalog.txt", id)) write_list("catalog.txt", id, 0);
    romfs_mods_reload();
    // renamed first, so a file the game still has open cannot keep a half-deleted mod under its own name
    snprintf(old, sizeof old, "%s/.old-%s-%llu", root(), id, (unsigned long long)plat_now_ms());
    if (plat_fs_rename(dir, old, 0) != PLAT_FS_OK) {
        set_message(1, "%s could not be removed: the game may still be reading it. Turn it off, restart the game "
                       "and remove it then.%s", id, NULL);
        return;
    }
    rm_tree(old);   // what the game still holds open stays until the next start's cleanup
    char name[96];
    snprintf(name, sizeof name, "%s", id);
    plat_mutex_lock(&lock);
    for (int i = 0; i < view.count; i++)
        if (!strcmp(view.rows[i].id, id) && view.rows[i].name[0]) snprintf(name, sizeof name, "%s", view.rows[i].name);
    plat_mutex_unlock(&lock);
    set_message(0, "Removed %s.%s", name, NULL);
    rt_log("mod library: removed %s", id);
}

static void install(const char *id) {
    Entry e;
    int found = 0;
    plat_mutex_lock(&lock);
    for (int i = 0; i < index_count; i++)
        if (!strcmp(index_entries[i].row.id, id)) { e = index_entries[i]; found = 1; }
    plat_mutex_unlock(&lock);
    if (!found) { set_message(1, "%s is not in the mod library.%s", id, NULL); return; }
    char staged[4200], dir[4200], old[4300];
    snprintf(staged, sizeof staged, "%s/.install-%s", root(), id);
    join(dir, sizeof dir, root(), id);
    rm_tree(staged);
    plat_file_mkdir_p(staged);
    const char *err = install_job(&e, staged);
    if (err) {
        rm_tree(staged);
        set_message(1, "%s was not installed: %s.", e.row.name, err);
        rt_log("mod library: %s not installed: %s", id, err);
        return;
    }
    write_library_ini(staged, &e.row);
    snprintf(old, sizeof old, "%s/.old-%s-%llu", root(), id, (unsigned long long)plat_now_ms());
    int had = plat_fs_kind(dir) == PLAT_FS_DIR;
    if (had && plat_fs_rename(dir, old, 0) != PLAT_FS_OK) {
        rm_tree(staged);
        set_message(1, "%s could not be updated: the game may be reading its files. Turn it off, restart the game "
                       "and update it then.%s", e.row.name, NULL);
        return;
    }
    if (plat_fs_rename(staged, dir, 1) != PLAT_FS_OK) {
        if (had) plat_fs_rename(old, dir, 0);
        rm_tree(staged);
        set_message(1, "%s could not be put in the mods folder.%s", e.row.name, NULL);
        return;
    }
    if (had) rm_tree(old);
    if (!strcmp(e.row.kind, "lua") && !in_list("catalog.txt", id)) write_list("catalog.txt", id, 1);
    romfs_mods_reload();   // an updated mod that is on: its new files
    set_message(0, had ? "Updated %s.%s" : "Installed %s. Turn it on below.%s", e.row.name, NULL);
    rt_log("mod library: installed %s %s", id, e.row.version);
}

// Leftovers of an interrupted install or a removal the game held files of.
static int cleanup_entry(const char *name, int kind, uint64_t size, void *ctx) {
    (void)size; (void)ctx;
    if ((!strncmp(name, ".old-", 5) || !strncmp(name, ".install-", 9)) && kind == PLAT_FS_DIR) {
        char path[4200];
        join(path, sizeof path, root(), name);
        rm_tree(path);
    }
    return 1;
}

static void set_enabled_now(const char *id, int on);

static void run_script(const char *script) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", script);
    for (char *step = buf, *next; step && *step; step = next) {
        next = strchr(step, ';');
        if (next) *next++ = 0;
        char verb[16] = "", id[65] = "";
        if (sscanf(step, " %15s %64s", verb, id) != 2 || !valid_id(id)) continue;
        if (!strcmp(verb, "install")) install(id);
        else if (!strcmp(verb, "remove")) remove_job(id);
        else if (!strcmp(verb, "on") || !strcmp(verb, "off")) {
            rescan();
            set_enabled_now(id, verb[1] == 'n');
            set_message(0, "%s %s", verb, id);
        } else continue;
        rescan();
        plat_mutex_lock(&lock);
        rt_log("mod library script: %s %s -> %s", verb, id, view.message);
        plat_mutex_unlock(&lock);
    }
}

static void job_thread(void *arg) {
    Job job = *(Job *)arg;
    free(arg);
    if (job.type == JOB_INDEX) {
        static int cleaned;
        if (!cleaned++ && root()) plat_fs_list(root(), cleanup_entry, NULL);
        normalise();
        rescan();
        load_index();
        static int scripted;
        const char *script = getenv("XCX_MODLIB_SCRIPT");
        if (script && !scripted++) {
            rescan();
            run_script(script);
        }
    } else if (job.type == JOB_INSTALL) {
        install(job.id);
    } else {
        remove_job(job.id);
    }
    rescan();
    plat_mutex_lock(&lock);
    busy = 0;
    view.state = MODLIB_IDLE;
    view.job[0] = 0;
    view.done = view.total = 0;
    publish_locked();
    plat_mutex_unlock(&lock);
}

static void start(int type, const char *id) {
    if (!root()) return;
    plat_mutex_lock(&lock);
    if (busy) { plat_mutex_unlock(&lock); return; }
    Job *job = calloc(1, sizeof *job);
    if (!job) { plat_mutex_unlock(&lock); return; }
    job->type = type;
    snprintf(job->id, sizeof job->id, "%s", id ? id : "");
    busy = 1;
    started = 1;
    view.state = type == JOB_INDEX ? MODLIB_LOADING : MODLIB_WORKING;
    snprintf(view.job, sizeof view.job, "%s", job->id);
    view.done = view.total = 0;
    if (type != JOB_INDEX) view.message[0] = 0;
    publish_locked();
    plat_mutex_unlock(&lock);
    if (!plat_thread_spawn(job_thread, job, "mod library", 0)) {
        free(job);
        plat_mutex_lock(&lock);
        busy = 0;
        view.state = MODLIB_IDLE;
        plat_mutex_unlock(&lock);
    }
}

void modlib_snapshot(ModLibView *v) {
    if (!__atomic_load_n(&started, __ATOMIC_RELAXED)) start(JOB_INDEX, NULL);
    plat_mutex_lock(&lock);
    *v = view;
    plat_mutex_unlock(&lock);
}

void modlib_refresh(void) { start(JOB_INDEX, NULL); }

void modlib_install(const char *id) {
    if (id && valid_id(id)) start(JOB_INSTALL, id);
}

void modlib_remove(const char *id) {
    if (id && valid_id(id)) start(JOB_REMOVE, id);
}

void modlib_set_enabled(const char *id, int on) {
    if (id && valid_id(id) && root()) set_enabled_now(id, on);
}

static void set_enabled_now(const char *id, int on) {
    char kind[8] = "";   // copied: the view may change once the lock is dropped
    plat_mutex_lock(&lock);
    for (int i = 0; i < view.count; i++)
        if (!strcmp(view.rows[i].id, id) && view.rows[i].installed) memcpy(kind, view.rows[i].kind, sizeof kind);
    plat_mutex_unlock(&lock);
    if (!kind[0]) return;
    int ok = !strcmp(kind, "romfs") ? romfs_mods_set(id, on) : write_list("enabled.txt", id, on);
    plat_mutex_lock(&lock);
    for (int i = 0; i < view.count; i++)
        if (!strcmp(view.rows[i].id, id) && ok) view.rows[i].enabled = on;
    publish_locked();
    plat_mutex_unlock(&lock);
}
