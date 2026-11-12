// Loose RomFS mod files in place of sts.ard entries, switchable while the game runs.
//
// Layout, under the mods folder of runtime/mods.c (XCX_MOD_DIR, else "mods" beside xcx.exe):
//   romfs-enabled.txt      one mod id per line, '#' starts a comment; earlier lines win when two mods have a file
//   <id>/romfs/<path>      a file that replaces (or adds) the game file <path>, e.g. <id>/romfs/chr/np/np009001.wimdo
//
// The archive device (sts.arh/sts.ard) keeps a list of paths it declines, which the game then opens as loose
// rom:/<path> files (recomp/romfs_mods.py). Where the archive reads that list for a path, romfs_mod_declines hands it
// a list holding just that path when an enabled mod has it; the game's open of rom:/<path> then reaches fs_host.c,
// which asks romfs_mod_file for the mod's copy.
//
// The list file is read again at most twice a second, and the file map is rebuilt when its bytes change, so a mod
// manager switches mods by rewriting romfs-enabled.txt. A changed mod applies to files the game opens after that;
// what it has already loaded stays until it loads the file again.
#include "romfs_mods.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "platform/platform.h"

#define LIST_CAP 4096          // bytes of romfs-enabled.txt
#define MOD_CAP 32             // enabled mods
#define CHECK_MS 500           // how often the list file is read again

typedef struct { char *key; char *host; } ModFile;   // key: lowercase game path without a leading '/'

static PlatMutex lock = PLAT_MUTEX_INIT;   // guards files / nfiles (shared for lookups)
static ModFile *files;
static size_t nfiles;
static PlatMutex check_lock = PLAT_MUTEX_INIT;
static char root[4096];
static int root_state;                     // 0 not looked up, 1 found, -1 none
static char list_seen[LIST_CAP + 1];
static int64_t list_len_seen = -2;         // -2: never read
static uint64_t next_check_ms;
static int claim_logs;

static int mods_root(void) {
    if (root_state) return root_state > 0;
    root_state = -1;
    const char *configured = getenv("XCX_MOD_DIR");
    if (configured && *configured) {
        if (strlen(configured) >= sizeof root - 64) return 0;
        strcpy(root, configured);
    } else {
        if (!plat_exe_path(root, sizeof root - 64)) return 0;
        char *slash = strrchr(root, '/'), *back = strrchr(root, '\\');
        if (back && (!slash || back > slash)) slash = back;
        if (!slash) return 0;
        strcpy(slash + 1, "mods");
    }
    root_state = 1;
    return 1;
}

typedef struct { ModFile *v; size_t n, cap; char host[4096]; size_t hlen; char key[1024]; size_t klen; } Walk;

static int add_file(Walk *w) {
    for (size_t i = 0; i < w->n; i++)   // an earlier mod's file wins; lists are small enough for a scan at rebuild
        if (!strcmp(w->v[i].key, w->key)) return 1;
    if (w->n == w->cap) {
        size_t cap = w->cap ? w->cap * 2 : 256;
        ModFile *v = realloc(w->v, cap * sizeof *v);
        if (!v) return 0;
        w->v = v;
        w->cap = cap;
    }
    w->v[w->n].key = strdup(w->key);
    w->v[w->n].host = strdup(w->host);
    if (!w->v[w->n].key || !w->v[w->n].host) return 0;
    w->n++;
    return 1;
}

static int walk_entry(const char *name, int kind, uint64_t size, void *ctx) {
    (void)size;
    Walk *w = ctx;
    size_t n = strlen(name), hl = w->hlen, kl = w->klen;
    if (!n || !strcmp(name, ".") || !strcmp(name, "..") || hl + n + 2 >= sizeof w->host ||
        kl + n + 2 >= sizeof w->key)
        return 1;   // skipped; plat_fs_list goes on while the callback returns nonzero
    w->host[hl] = '/';
    memcpy(w->host + hl + 1, name, n + 1);
    if (kl) w->key[kl] = '/';
    char *k = w->key + (kl ? kl + 1 : 0);
    for (size_t i = 0; i <= n; i++) k[i] = (char)tolower((unsigned char)name[i]);
    w->hlen = hl + 1 + n;
    w->klen = (kl ? kl + 1 : 0) + n;
    if (kind == PLAT_FS_DIR) plat_fs_list(w->host, walk_entry, w);
    else if (kind == PLAT_FS_FILE) add_file(w);
    w->hlen = hl;
    w->klen = kl;
    w->host[hl] = 0;
    w->key[kl] = 0;
    return 1;
}

static int by_key(const void *a, const void *b) { return strcmp(((const ModFile *)a)->key, ((const ModFile *)b)->key); }

// The Lua mods' id rule: a-z first, then a-z 0-9 . _ -, at most 64 characters.
static int valid_id(const char *id, size_t n) {
    if (!n || n > 64 || !islower((unsigned char)id[0])) return 0;
    for (size_t i = 0; i < n; i++)
        if (!islower((unsigned char)id[i]) && !isdigit((unsigned char)id[i]) && !strchr("._-", id[i])) return 0;
    return 1;
}

// Rebuilds the file map from the list file's text (NUL-terminated).
static void rebuild(const char *list) {
    Walk w = {0};
    int mods = 0;
    char line[256];
    for (const char *p = list; *p && mods < MOD_CAP;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n && p[n - 1] == '\r') n--;
        if (n && n < sizeof line && p[0] != '#') {
            memcpy(line, p, n);
            line[n] = 0;
            if (valid_id(line, n)) {
                int len = snprintf(w.host, sizeof w.host, "%s/%s/romfs", root, line);
                if (len > 0 && (size_t)len < sizeof w.host && plat_fs_kind(w.host) == PLAT_FS_DIR) {
                    w.hlen = (size_t)len;
                    w.klen = 0;
                    w.key[0] = 0;
                    plat_fs_list(w.host, walk_entry, &w);
                    mods++;
                } else {
                    rt_log("romfs mods: %s has no romfs folder", line);
                }
            } else if (n) {
                rt_log("romfs mods: romfs-enabled.txt has an invalid id");
            }
        }
        if (!e) break;
        p = e + 1;
    }
    if (w.n) qsort(w.v, w.n, sizeof *w.v, by_key);
    plat_mutex_lock(&lock);
    ModFile *old = files;
    size_t nold = nfiles;
    files = w.v;
    nfiles = w.n;
    plat_mutex_unlock(&lock);
    for (size_t i = 0; i < nold; i++) {
        free(old[i].key);
        free(old[i].host);
    }
    free(old);
    rt_log("romfs mods: %d mod%s, %zu file%s", mods, mods == 1 ? "" : "s", w.n, w.n == 1 ? "" : "s");
}

// Reads romfs-enabled.txt again when CHECK_MS passed, and rebuilds when it changed. One thread checks at a time;
// the others go on with the current map.
static void maybe_reload(void) {
    uint64_t now = plat_now_ms();
    if (now < __atomic_load_n(&next_check_ms, __ATOMIC_RELAXED) || !plat_mutex_trylock(&check_lock)) return;
    __atomic_store_n(&next_check_ms, now + CHECK_MS, __ATOMIC_RELAXED);
    if (mods_root()) {
        char path[4200], buf[LIST_CAP + 1];
        snprintf(path, sizeof path, "%s/romfs-enabled.txt", root);
        int64_t len = -1;
        PlatFile *f = plat_file_open(path);
        if (f) {
            len = plat_file_pread(f, buf, LIST_CAP, 0);
            plat_file_close(f);
        }
        if (len < 0) len = -1;
        buf[len > 0 ? len : 0] = 0;
        if (len != list_len_seen || (len > 0 && memcmp(buf, list_seen, (size_t)len))) {
            memcpy(list_seen, buf, (size_t)(len > 0 ? len : 0) + 1);
            list_len_seen = len;
            if (len >= 0 || nfiles) rebuild(buf);
        }
    }
    plat_mutex_unlock(&check_lock);
}

// The host path of the mod file for key (lowercase, no leading '/'), copied to out; 0 when no enabled mod has it.
static int lookup(const char *key, char *out, size_t cap) {
    int found = 0;
    plat_mutex_lock_shared(&lock);
    size_t lo = 0, hi = nfiles;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int d = strcmp(files[mid].key, key);
        if (!d) {
            if (out && strlen(files[mid].host) < cap) strcpy(out, files[mid].host);
            found = 1;
            break;
        }
        if (d < 0) lo = mid + 1;
        else hi = mid;
    }
    plat_mutex_unlock_shared(&lock);
    return found;
}

// key from a game path: drops a "<mount>:" prefix and leading slashes, lowercases.
static int make_key(const char *path, char *key, size_t cap) {
    const char *colon = strchr(path, ':');
    if (colon) path = colon + 1;
    while (*path == '/' || *path == '\\') path++;
    size_t n = strlen(path);
    if (!n || n >= cap) return 0;
    for (size_t i = 0; i <= n; i++) key[i] = path[i] == '\\' ? '/' : (char)tolower((unsigned char)path[i]);
    return 1;
}

// A printable guest C string at p (at least 2 characters), copied to out; 0 when there is none.
static int guest_string(uint64_t p, char *out, size_t cap) {
    if (!p || !plat_mem_readable(p, 1)) return 0;
    size_t n = 0;
    for (; n < cap - 1; n++) {
        if (n && ((p + n) & 0xfff) == 0 && !plat_mem_readable(p + n, 1)) return 0;
        unsigned char ch = *(const unsigned char *)(uintptr_t)(p + n);
        if (!ch) break;
        if (ch < 32 || ch >= 127) return 0;
        out[n] = (char)ch;
    }
    out[n] = 0;
    return n >= 2 && n < cap - 1;
}

uint64_t romfs_mod_declines(uint64_t list, uint64_t path) {
    maybe_reload();
    if (!__atomic_load_n(&nfiles, __ATOMIC_RELAXED)) return list;
    // One entry, then the empty string that ends the list. Thread-local: the archive walks it right away on this thread.
    static __thread char one[1026];
    char raw[1024];
    if (!guest_string(path, raw, sizeof raw)) return list;
    if (!make_key(raw, one, sizeof one - 1) || !lookup(one, NULL, 0)) return list;
    one[strlen(one) + 1] = 0;
    if (__atomic_fetch_add(&claim_logs, 1, __ATOMIC_RELAXED) < 64) rt_log("romfs mods: %s from a mod", one);
    return (uint64_t)(uintptr_t)one;
}

int romfs_mod_file(const char *path, char *out, size_t cap) {
    maybe_reload();
    if (!__atomic_load_n(&nfiles, __ATOMIC_RELAXED) || !path) return 0;
    char key[1024];
    return make_key(path, key, sizeof key) && lookup(key, out, cap);
}

// ---- switching, for the F1 menu (runtime/mod_library.c) ------------------------------------------------------------------------------------------

// The ids of romfs-enabled.txt, in order, into ids (each 65 bytes); the count.
static int read_enabled(char (*ids)[65], int cap) {
    char path[4200];
    snprintf(path, sizeof path, "%s/romfs-enabled.txt", root);
    uint64_t size = 0;
    char *text = (char *)plat_file_read(path, &size);
    int n = 0;
    for (char *p = text; p && *p && n < cap;) {
        char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        while (len && (p[len - 1] == '\r' || p[len - 1] == ' ')) len--;
        if (p[0] != '#' && valid_id(p, len)) {
            int dup = 0;
            for (int i = 0; i < n; i++) dup |= strlen(ids[i]) == len && !memcmp(ids[i], p, len);
            if (!dup) {
                memcpy(ids[n], p, len);
                ids[n++][len] = 0;
            }
        }
        if (!e) break;
        p = e + 1;
    }
    free(text);
    return n;
}

const char *romfs_mods_dir(void) { return mods_root() ? root : NULL; }

void romfs_mods_reload(void) {
    plat_mutex_lock(&check_lock);
    list_len_seen = -2;   // rebuilt from the list at the next check even when its bytes are the same
    plat_mutex_unlock(&check_lock);
    __atomic_store_n(&next_check_ms, 0, __ATOMIC_RELAXED);
}

int romfs_mods_set(const char *id, int on) {
    if (!mods_root() || !id || !valid_id(id, strlen(id))) return 0;
    char ids[MOD_CAP][65];
    int n = read_enabled(ids, MOD_CAP);
    char text[LIST_CAP];
    size_t len = 0;
    if (on) len += (size_t)snprintf(text, sizeof text, "%s\n", id);   // a mod turned on goes first, so its files win
    for (int i = 0; i < n && len + 66 < sizeof text; i++)
        if (strcmp(ids[i], id)) len += (size_t)snprintf(text + len, sizeof text - len, "%s\n", ids[i]);
    plat_file_mkdir_p(root);
    char path[4200];
    snprintf(path, sizeof path, "%s/romfs-enabled.txt", root);
    int ok = plat_file_write_atomic(path, text, len);
    rt_log("romfs mods: %s turned %s from the menu%s", id, on ? "on" : "off", ok ? "" : " (the list could not be written)");
    __atomic_store_n(&next_check_ms, 0, __ATOMIC_RELAXED);   // the next file lookup reads the new list
    return ok;
}
