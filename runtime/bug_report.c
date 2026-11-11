// The in-game bug report (bug_report.h): collects the files a report needs into one zip (PKWARE APPNOTE 4.3.7 and
// 4.3.12; entries deflated by mod_zip.c mod_deflate, or stored when that does not make them smaller). The zip is built
// in memory and written with plat_file_write_atomic, so a failed run leaves no half-written file.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "bug_report.h"
#include "host.h"
#include "mod_zip.h"
#include "platform/platform.h"
#include "romfs_mods.h"

extern const char rt_game_name[];

enum { ZIP_MAX_ENTRIES = 4096 };
#define ZIP_FILE_LIMIT ((uint64_t)512 << 20)    // bytes of one file the zip takes; a larger one is left out
#define ZIP_TOTAL_LIMIT ((uint64_t)1 << 31)     // the zip stays below 2 GiB: no Zip64

typedef struct {
    uint8_t *buf; size_t n, cap;
    uint8_t *central; size_t cn, ccap;
    int count, failed;
    uint16_t time, date;   // MS-DOS local time of every entry: when the report was made
    char skipped[1024];    // the files left out, for the log
} Zip;

static int grow(uint8_t **p, size_t *cap, size_t need) {
    if (need <= *cap) return 1;
    size_t c = *cap ? *cap : 1 << 20;
    while (c < need) c *= 2;
    uint8_t *q = realloc(*p, c);
    if (!q) return 0;
    *p = q;
    *cap = c;
    return 1;
}

static void le16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, v & 0xffff), le16(p + 2, v >> 16); }

static void note_skip(Zip *z, const char *name, const char *why) {
    size_t at = strlen(z->skipped);
    snprintf(z->skipped + at, sizeof z->skipped - at, "%s%s (%s)", at ? ", " : "", name, why);
}

// One entry; name uses '/' and is UTF-8 (general purpose flag bit 11).
static void zip_add_raw(Zip *z, const char *name, const uint8_t *data, size_t n) {
    if (z->failed) return;
    size_t nlen = strlen(name);
    if (z->count >= ZIP_MAX_ENTRIES || n > ZIP_FILE_LIMIT || z->n + n + nlen + 64 > ZIP_TOTAL_LIMIT) {
        note_skip(z, name, "too large");
        return;
    }
    uint32_t crc = mod_crc32(data, n);
    size_t at = z->n, hdr = 30 + nlen;
    if (!grow(&z->buf, &z->cap, at + hdr + n + 64) || !grow(&z->central, &z->ccap, z->cn + 46 + nlen)) {
        z->failed = 1;
        return;
    }
    size_t csize = n ? mod_deflate(data, n, z->buf + at + hdr, n - 1) : 0;   // 0: not smaller, stored
    unsigned method = csize ? 8 : 0;
    if (!csize) {
        if (n) memcpy(z->buf + at + hdr, data, n);
        csize = n;
    }
    uint8_t *l = z->buf + at;
    le32(l, 0x04034b50), le16(l + 4, 20), le16(l + 6, 0x0800), le16(l + 8, method), le16(l + 10, z->time);
    le16(l + 12, z->date), le32(l + 14, crc), le32(l + 18, (uint32_t)csize), le32(l + 22, (uint32_t)n);
    le16(l + 26, (unsigned)nlen), le16(l + 28, 0);
    memcpy(l + 30, name, nlen);
    z->n = at + hdr + csize;
    uint8_t *c = z->central + z->cn;
    le32(c, 0x02014b50), le16(c + 4, 20), le16(c + 6, 20), le16(c + 8, 0x0800), le16(c + 10, method);
    le16(c + 12, z->time), le16(c + 14, z->date), le32(c + 16, crc), le32(c + 20, (uint32_t)csize);
    le32(c + 24, (uint32_t)n), le16(c + 28, (unsigned)nlen), le16(c + 30, 0), le16(c + 32, 0), le16(c + 34, 0);
    le16(c + 36, 0), le32(c + 38, 0), le32(c + 42, (uint32_t)at);
    memcpy(c + 46, name, nlen);
    z->cn += 46 + nlen;
    z->count++;
}

// ---- masking: the copies of the text files in the zip (logs, crash reports, system-info.txt, xcx.ini, the mod lists)
// carry no personal names. The user's home folder becomes <home>, the user name <user>, the computer name <computer>;
// the files on the disk stay as they are. Matches ignore case (Windows paths do) and treat / and \ alike; a name
// matches only as a whole word, so "<user>" does not cut a longer word that holds the name, and the home folder only
// up to a separator or the end of a word ("C:\Users\jo" leaves "C:\Users\joan" to the name rule).
typedef struct { char text[PLAT_PATH_MAX]; size_t n; const char *with; int name; } Mask;   // name: a whole word
static Mask g_masks[6];
static int g_nmasks;

static void mask_add(const char *text, const char *with, int name) {
    size_t n = strlen(text);
    while (n && (text[n - 1] == '/' || text[n - 1] == '\\')) n--;
    if (n < 2 || n >= sizeof g_masks[0].text || g_nmasks >= (int)(sizeof g_masks / sizeof *g_masks)) return;
    for (int i = 0; i < g_nmasks; i++)
        if (g_masks[i].n == n && !strncasecmp(g_masks[i].text, text, n)) return;
    Mask *m = &g_masks[g_nmasks++];
    memcpy(m->text, text, n);
    m->text[n] = 0;
    m->n = n;
    m->with = with;
    m->name = name;
}

// Longest first, so the home folder goes before the user name inside it.
static int by_length(const void *a, const void *b) {
    size_t x = ((const Mask *)a)->n, y = ((const Mask *)b)->n;
    return x < y ? 1 : x > y ? -1 : 0;
}

static void masks_init(void) {
    char v[PLAT_PATH_MAX];
    g_nmasks = 0;
#ifdef _WIN32
    if (plat_getenv("USERPROFILE", v, sizeof v)) mask_add(v, "<home>", 0);
    if (plat_getenv("USERNAME", v, sizeof v)) mask_add(v, "<user>", 1);
    if (plat_getenv("COMPUTERNAME", v, sizeof v)) mask_add(v, "<computer>", 1);
#else
    if (plat_getenv("HOME", v, sizeof v)) mask_add(v, "<home>", 0);
    if (plat_getenv("USER", v, sizeof v)) mask_add(v, "<user>", 1);
    if (plat_getenv("LOGNAME", v, sizeof v)) mask_add(v, "<user>", 1);
    if (!gethostname(v, sizeof v)) v[sizeof v - 1] = 0, mask_add(v, "<computer>", 1);
    else if (plat_getenv("HOSTNAME", v, sizeof v)) mask_add(v, "<computer>", 1);
#endif
    qsort(g_masks, (size_t)g_nmasks, sizeof *g_masks, by_length);
}

static int word_char(uint8_t c) { return isalnum(c) || c == '_' || c >= 0x80; }

static int same(uint8_t a, uint8_t b) {
    if ((a == '/' || a == '\\') && (b == '/' || b == '\\')) return 1;
    return tolower(a) == tolower(b);
}

// The masked copy of src (malloc'd, its length in *out_n); NULL when out of memory.
static uint8_t *mask_text(const uint8_t *src, size_t n, size_t *out_n) {
    size_t cap = n + n / 8 + 64, at = 0;
    uint8_t *out = malloc(cap);
    if (!out) return NULL;
    for (size_t i = 0; i < n;) {
        const Mask *hit = NULL;
        for (int k = 0; k < g_nmasks && !hit; k++) {
            const Mask *m = &g_masks[k];
            if (i + m->n > n || !same(src[i], (uint8_t)m->text[0])) continue;
            size_t j = 1;
            while (j < m->n && same(src[i + j], (uint8_t)m->text[j])) j++;
            if (j < m->n) continue;
            if (i + m->n < n && word_char(src[i + m->n])) continue;     // the match ends a word
            if (m->name && i && word_char(src[i - 1])) continue;   // a name also starts one
            hit = m;
        }
        size_t add = hit ? strlen(hit->with) : 1;
        if (at + add > cap) {
            size_t c = cap * 2 + add;
            uint8_t *q = realloc(out, c);
            if (!q) { free(out); return NULL; }
            out = q, cap = c;
        }
        if (hit) {
            memcpy(out + at, hit->with, add);
            i += hit->n;
        } else {
            out[at] = src[i++];
        }
        at += add;
    }
    *out_n = at;
    return out;
}

// A text entry: masked first.
static void zip_add(Zip *z, const char *name, const uint8_t *data, size_t n) {
    size_t mn = 0;
    uint8_t *m = n ? mask_text(data, n, &mn) : NULL;
    if (n && !m) {
        z->failed = 1;
        return;
    }
    zip_add_raw(z, name, m, mn);
    free(m);
}

// A file from the disk; a missing one is no entry. text: masked (zip_add), else copied as it is (the save).
static void zip_file_as(Zip *z, const char *name, const char *path, int text) {
    if (!path || !plat_file_exists(path)) return;
    int64_t size = plat_file_size(path);
    if (size < 0 || (uint64_t)size > ZIP_FILE_LIMIT) {
        note_skip(z, name, size < 0 ? "unreadable" : "too large");
        return;
    }
    uint64_t n = 0;
    uint8_t *data = size ? plat_file_read(path, &n) : NULL;
    if (size && !data) {
        note_skip(z, name, "unreadable");
        return;
    }
    if (text) zip_add(z, name, data, (size_t)n);
    else zip_add_raw(z, name, data, (size_t)n);
    free(data);
}

static void zip_file(Zip *z, const char *name, const char *path) { zip_file_as(z, name, path, 1); }

static int zip_finish(Zip *z) {
    if (z->failed || !grow(&z->buf, &z->cap, z->n + z->cn + 22)) return 0;
    size_t cd = z->n;
    memcpy(z->buf + z->n, z->central, z->cn);
    z->n += z->cn;
    uint8_t *e = z->buf + z->n;
    le32(e, 0x06054b50), le16(e + 4, 0), le16(e + 6, 0), le16(e + 8, (unsigned)z->count);
    le16(e + 10, (unsigned)z->count), le32(e + 12, (uint32_t)z->cn), le32(e + 16, (uint32_t)cd), le16(e + 20, 0);
    z->n += 22;
    return 1;
}

// ---- what goes in

// The directory part of path into out (no trailing separator); 0 when path has none.
static int dir_of(const char *path, char *out, size_t cap) {
    if (snprintf(out, cap, "%s", path) >= (int)cap) return 0;
    char *s = plat_last_sep(out);
    if (!s) return 0;
    *s = 0;
    return 1;
}

static const char *base_of(const char *path) {
    const char *s = plat_last_sep(path);
    return s ? s + 1 : path;
}

void bug_report_version_line(char *out, size_t cap) {
    char game[16];
    if (*rt_game_name) snprintf(game, sizeof game, "%s", rt_game_name);
    else snprintf(game, sizeof game, "%d.%d.%d", rt_game_version / 100, rt_game_version / 10 % 10, rt_game_version % 10);
    snprintf(out, cap, "XCX Recomp %s, game %s, runtime built %s %s", rt_port_version, game, __DATE__, __TIME__);
}

typedef struct { Zip *z; const char *dir, *prefix; int depth; } Walk;

// The save tree, file by file; links are not followed.
static int walk_entry(const char *name, int kind, uint64_t size, void *ctx) {
    (void)size;
    Walk *w = ctx;
    char path[PLAT_PATH_MAX * 2], entry[PLAT_PATH_MAX * 2];
    if (snprintf(path, sizeof path, "%s/%s", w->dir, name) >= (int)sizeof path ||
        snprintf(entry, sizeof entry, "%s/%s", w->prefix, name) >= (int)sizeof entry)
        return 1;
    if (kind == PLAT_FS_FILE) {
        zip_file_as(w->z, entry, path, 0);
    } else if (kind == PLAT_FS_DIR && w->depth < 8) {
        Walk sub = {w->z, path, entry, w->depth + 1};
        plat_fs_list(path, walk_entry, &sub);
    }
    return !w->z->failed;
}

typedef struct { Zip *z; const char *dir; char *list; size_t n, cap; } ModWalk;

static int mod_entry(const char *name, int kind, uint64_t size, void *ctx) {
    ModWalk *m = ctx;
    size_t len = strlen(name);
    if (m->n + len + 64 < m->cap)
        m->n += (size_t)snprintf(m->list + m->n, m->cap - m->n, "%s%s\n", name,
                                 kind == PLAT_FS_DIR ? "/" : kind == PLAT_FS_LINK ? " (link)" : "");
    (void)size;
    if (kind == PLAT_FS_FILE && len > 4 && !strcmp(name + len - 4, ".txt")) {   // romfs-enabled.txt, enabled.txt, ...
        char path[PLAT_PATH_MAX * 2], entry[PLAT_PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", m->dir, name);
        snprintf(entry, sizeof entry, "mods/%s", name);
        zip_file(m->z, entry, path);
    }
    return 1;
}

const char *bug_report_write(int with_save, const char *sysinfo, char *out, size_t cap) {
    const char *why = NULL;
    char exe_dir[PLAT_PATH_MAX], path[PLAT_PATH_MAX * 2], dir[PLAT_PATH_MAX * 2], line[256];
    if (cap) out[0] = 0;
    if (!plat_exe_path(exe_dir, sizeof exe_dir) || !plat_last_sep(exe_dir)) {
        rt_log("bug report: the folder of xcx.exe is unknown");
        return "the folder of xcx.exe is unknown";
    }
    *plat_last_sep(exe_dir) = 0;
    struct tm t;
    plat_local_time(&t);
    masks_init();
    Zip z = {.time = (uint16_t)(t.tm_hour << 11 | t.tm_min << 5 | t.tm_sec / 2),
             .date = (uint16_t)((t.tm_year - 80) << 9 | (t.tm_mon + 1) << 5 | t.tm_mday)};

    // the logs: this run's and the previous run's (log_open in main.c keeps xcx.1.log .. xcx.4.log beside xcx.log),
    // else the file stderr goes to (a run started by a script)
    const char *log = rt_log_path();
    if (log) {
        zip_file(&z, "logs/xcx.log", log);
        if (dir_of(log, dir, sizeof dir)) {
            snprintf(path, sizeof path, "%s/xcx.1.log", dir);
            zip_file(&z, "logs/xcx.1.log", path);
        }
    } else if (plat_stderr_path(path, sizeof path)) {
        snprintf(dir, sizeof dir, "logs/%s", base_of(path));
        zip_file(&z, dir, path);
    }
    for (int i = 0; i < RT_CRASH_KEEP; i++)   // crash.txt, the newest, and the older ones
        if (rt_crash_report_nth(i, path, sizeof path)) {
            char entry[32];
            if (i) snprintf(entry, sizeof entry, "crash.%d.txt", i);
            else snprintf(entry, sizeof entry, "crash.txt");
            zip_file(&z, entry, path);
        }
    snprintf(path, sizeof path, "%s/xcx.ini", exe_dir);
    zip_file(&z, "xcx.ini", path);

    // system info: the version line first, then the paths a support question asks about
    {
        size_t sn = sysinfo ? strlen(sysinfo) : 0, tn = sn + 4096;
        char *text = malloc(tn);
        if (text) {
            bug_report_version_line(line, sizeof line);
            int k = snprintf(text, tn, "%s\nReport made %04d-%02d-%02d %02d:%02d:%02d local time, save %s\n"
                             "Game folder: %s\nProfile folder: %s\n\n%s", line, t.tm_year + 1900, t.tm_mon + 1,
                             t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, with_save ? "included" : "not included",
                             exe_dir, rt_config()->profile ? rt_config()->profile : "?", sysinfo ? sysinfo : "");
            zip_add(&z, "system-info.txt", (const uint8_t *)text, k > 0 ? strlen(text) : 0);
            free(text);
        }
    }

    // the mods folder: its list files and what it holds
    const char *mods = romfs_mods_dir();
    if (mods && plat_dir_exists(mods)) {
        static char list[32768];
        ModWalk m = {&z, mods, list, 0, sizeof list};
        list[0] = 0;
        plat_fs_list(mods, mod_entry, &m);
        zip_add(&z, "mods/folders.txt", (const uint8_t *)list, m.n);
    }

    // the save, only when the player asked: the committed save (fs_host.c), not the staging and backup folders
    if (with_save && rt_config()->profile) {
        snprintf(dir, sizeof dir, "%s/save", rt_config()->profile);
        if (plat_dir_exists(dir)) {
            Walk w = {&z, dir, "save", 0};
            plat_fs_list(dir, walk_entry, &w);
        }
    }

    if (!zip_finish(&z)) {
        why = "out of memory";
    } else {
        snprintf(dir, sizeof dir, "%s/bug-reports", exe_dir);
        plat_file_mkdir_p(dir);
        snprintf(path, sizeof path, "%s/xcx-bug-report-%04d%02d%02d-%02d%02d%02d.zip", dir, t.tm_year + 1900,
                 t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        if (strchr(exe_dir, '\\'))   // one separator in the path the menu shows
            for (char *c = path; *c; c++)
                if (*c == '/') *c = '\\';
        if (!plat_file_write_atomic(path, z.buf, z.n)) why = "the zip could not be written";
    }
    if (why) {
        rt_log("bug report: %s", why);
    } else {
        if (cap) snprintf(out, cap, "%s", path);
        rt_log("bug report: %s, %d files, %.1f MiB%s%s%s", path, z.count, z.n / 1048576.0,
               with_save ? ", save included" : "", z.skipped[0] ? "; left out: " : "", z.skipped);
    }
    free(z.buf);
    free(z.central);
    return why;
}
