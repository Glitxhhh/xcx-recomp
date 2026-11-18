// Recomp Achievements: unlocks reported to RecompLauncher, which shows the toast, keeps them and decides what counts
// as Ranked. Our own client of the channel its spec describes (github.com/elripalda/RecompAchievements, docs/SPEC.md,
// version 1; its license lets any game use the format and channel); none of its library's code is here.
//
// RecompLauncher starts the game with RL_SESSION naming a local channel (a named pipe on Windows, a Unix socket
// elsewhere; platform.h plat_ipc_*). Without it this file does nothing. Both sides send one JSON object per line.
//
// The list is a "native" list: the game's own achievements, passed on as the game awards them. native_list, right
// after hello, names them with the game's own titles and descriptions (read from the player's copy as it runs);
// native <id> is an award, with earlier when the loaded save already had it; each award not earlier also goes out as
// the event ach_<id>, for an events list. The launcher unlocks an achievement once. A thread looks at the game's own
// data twice a second; nothing runs on the game's threads. What it reads, all from the game data object whose
// pointer is at main + GAME_DATA (runtime/cheats.c, the port's Max Funds, reads the same object):
//   achievements ACH_IDS records ACH_STRIDE apart from +ACH_RECORDS, one per row of the game's own list (below)
//   characters   CHARACTERS slots CHARACTER_STRIDE apart from the object, the level a u16 at +LEVEL in each
//   story        u16 at +STORY, the scenario number (below)
// The levels and the scenario number only tell a save load from play.
//
// The game's achievements are the rows of its table SCL_AchiveList (844 rows: IDs 1..843, then ACHIVE_END, an empty
// row), and <id> is the row's ID. Each row names a statistic and a count; the game awards the achievement when that
// statistic is counted up to the count and keeps the result in a 16-byte record per ID: main+0x4fa03c, which the
// achievements menu asks, reads the record at [main+0x4e4054's block] + 0x17AB0 + id * 16, that block being the game
// data object + 0x79A78, so the record is at +ACH_RECORDS + id * 16. Its first word holds flags: ACH_EARNED (bit 1) is
// set when the achievement is awarded, bit 2 once the menu has shown it, bit 0 with bit 1; the third word is the time
// it was awarded (seconds since 1970). The flags are saved with the rest of the object.
//
// The scenario number is what the game's scripts read with getScenario (its native at main+0x3ed320 asks the flag
// store at main+0xc96d0 for 16-bit value 1). The flag store lives inside the game data object at +0x72E78 and keeps its
// 16-bit values from +0x5800 in it, so value 1 is at +STORY. It counts chapter * 1000 + step: the prologue runs 1..8,
// chapter 1 starts at 1001, chapter 12 at 12001, and the Definitive Edition's chapter 13 runs 13002..15026, 15027 being
// the end. The value only grows in play, by one step or from one chapter's last step to the next one's first.
//
// A load must not send anything as awarded now: the object holds defaults for the first moments after boot, then the
// save the game reads at start, and loading another save rewrites it in place (the pointer stays). A load is when the
// object first appears or changes to another one, when LOAD_LEVELS or more characters' levels change in one look, when
// any level or the scenario goes down, when the scenario skips a chapter, when an earned achievement is no longer
// earned, or when more than LOAD_ACH achievements are earned in one look. An achievement arms only once it was seen not
// earned with the values settled SETTLE_MS since the last load, and is sent when it is then earned: what a save already
// has is sent once as earlier instead (as the spec's memory rules wait for a false frame). A load none of these catch
// (another save at nearly the same story point and levels, with at most LOAD_ACH more achievements) is not seen; its
// extra achievements are then sent as awarded now.
//
// Also sent: hello on connect; save_loaded at each load after the object first appears (the save read at start is one);
// casual "cheats" while a cheat is switched on, so its unlocks are kept as Casual. From the launcher, peek (its dev
// tools' memory viewer) is answered from guest memory; list and quiet need nothing from a native list.
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
#define CHARACTERS 23
#define CHARACTER_STRIDE 0x7F8u
#define LEVEL 0x172u
#define STORY 0x7867Au          // the scenario number: the flag store (+0x72E78) 16-bit value 1 (+0x5800 + 2)
#define STORY_MAX 15027         // the last scenario number; one read above it means the offsets do not fit this build
#define LEVEL_MAX 99            // a level read above it is not a level: the offsets do not fit this build
#define LOAD_LEVELS 3           // characters whose level changes in one look: a save loaded, not play
#define ACH_RECORDS 0x91528u    // the game's achievement records: block +0x79A78, records +0x17AB0 in it
#define ACH_STRIDE 16u
#define ACH_IDS 843             // SCL_AchiveList's achievements, IDs 1..843; record 0 is unused
#define ACH_EARNED 2u           // record flags: awarded
#define ACH_FLAGS 7u            // every flag the game sets; other bits mean the offsets do not fit this build
#define LOAD_ACH 8              // achievements earned in one look: more is a save loaded, not play
#define SETTLE_MS 10000

// The game's achievements, by ID: earned in the last look, seen not earned since the last load (settled), sent.
static uint8_t ach_earned[ACH_IDS + 1], ach_armed[ACH_IDS + 1], ach_fired[ACH_IDS + 1];

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

// The game awarded achievement id: a native unlock, and the same as the event ach_<id> for an events list. earlier:
// the save had it when it loaded (no event then: an events list must not unlock from a loaded save).
static void send_achievement(int id, int earlier) {
    char line[160];
    snprintf(line, sizeof line, "{\"t\":\"native\",\"id\":%d,\"frame\":%" PRIu64 "%s}", id, frame,
             earlier ? ",\"earlier\":true" : "");
    send_line(line);
    if (earlier) return;
    snprintf(line, sizeof line, "{\"t\":\"event\",\"name\":\"ach_%d\",\"frame\":%" PRIu64 "}", id, frame);
    send_line(line);
    rt_log("achievements: game achievement %d", id);
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

// ---- The game's own achievement list, for native_list: its tables as the game loaded them.
//
// Both are BDAT version 4 tables the game keeps on its heap (the alias region; their address changes from run to run,
// and no pointer from the modules' data leads to them): SCL_AchiveList, one row per achievement, and the text table
// MNU_Achievement_ms. A table starts "BDAT" 0x04, then u32s: columns, rows, first row ID, one unused, then the offsets
// from the table's start of the column definitions (3 bytes each: type u8, name u16), the hashes, the rows, the row
// length, the strings and their length. A string column holds an offset into the strings, which end with a 0 byte.
// The list is found by its shape, ACH_LIST_COLS columns, ACH_IDS + 1 rows (ACHIVE_END last) of ACH_LIST_ROW bytes; the
// text table by its name (bdat_named). Of the list's columns, ACH_COL_TITLE and
// ACH_COL_DESC hold row IDs in the text table, ACH_COL_DISP is 1 for the achievements the game's menu shows.
#define ACH_LIST_COLS 14
#define ACH_LIST_ROW 26
#define ACH_COL_TITLE 1
#define ACH_COL_DESC 2
#define ACH_COL_DISP 7
#define ACH_TEXT "MNU_Achievement_ms"
#define LIST_RETRY_MS 20000   // the tables are looked for again this often until both are found
#define TEXT_MAX 512          // bytes of one title or description

typedef struct {
    uint64_t at;
    uint32_t cols, rows, base, coldef, rows_off, row_len, str_off, str_len;
} Bdat;

static int bdat_at(uint64_t at, Bdat *b) {
    char magic[5];
    uint32_t f[10];
    if (!plat_mem_read(at, magic, sizeof magic) || memcmp(magic, "BDAT\x04", 5) || !plat_mem_read(at + 8, f, sizeof f))
        return 0;
    *b = (Bdat){at, f[0], f[1], f[2], f[4], f[6], f[7], f[8], f[9]};
    return b->cols && b->cols < 300 && b->rows && b->row_len && b->str_len &&
           b->rows_off + (uint64_t)b->rows * b->row_len <= b->str_off;
}

// Column col's offset in a row and its size in bytes; 0 for a type this reader does not know.
static int bdat_col(const Bdat *b, int col, uint32_t *off, int *size) {
    static const uint8_t sizes[] = {0, 1, 2, 4, 1, 2, 4, 4, 4, 4, 1, 4, 1, 2};   // by type: u8 u16 u32 .. str float ..
    uint32_t o = 0;
    for (int c = 0; c <= col && c < (int)b->cols; c++) {
        uint8_t type = 0;
        if (!plat_mem_read(b->at + b->coldef + 3u * c, &type, 1) || type >= sizeof sizes || !sizes[type]) return 0;
        if (c == col) return *off = o, *size = sizes[type], o + sizes[type] <= b->row_len;
        o += sizes[type];
    }
    return 0;
}

static uint32_t bdat_value(const Bdat *b, uint32_t row, uint32_t off, int size) {
    uint32_t v = 0;
    if (row < b->rows) plat_mem_read(b->at + b->rows_off + (uint64_t)row * b->row_len + off, &v, (size_t)size);
    return size == 1 ? v & 0xff : size == 2 ? v & 0xffff : v;
}

// The string at offset v of the strings, cut at TEXT_MAX - 1 bytes.
static void bdat_string(const Bdat *b, uint32_t v, char out[TEXT_MAX]) {
    out[0] = 0;
    if (v >= b->str_len) return;
    uint32_t n = b->str_len - v < TEXT_MAX - 1 ? b->str_len - v : TEXT_MAX - 1;
    if (!plat_mem_read(b->at + b->str_off + v, out, n)) n = 0;
    out[n] = 0;
}

// The table's name, in the names block the loaded table keeps between its header (0x30 bytes) and its column
// definitions: the rows' names, then the table's, then its columns', each ending with a 0 byte. Only the block's last
// bytes are read: the block holds every row's name.
static int printable(char c) { return c >= 0x20 && c < 0x7f; }
static int bdat_named(const Bdat *b, const char *name) {
    char names[1024];
    size_t n = strlen(name), len = b->coldef > 0x30u ? b->coldef - 0x30u : 0;
    if (len > sizeof names) len = sizeof names;
    if (!len || !plat_mem_read(b->at + b->coldef - len, names, len)) return 0;
    for (size_t i = 0; i + n <= len; i++)
        if ((i == 0 || !printable(names[i - 1])) && (i + n == len || !printable(names[i + n])) &&
            !memcmp(names + i, name, n))
            return 1;
    return 0;
}

// One pass over the committed pages of the alias region (the game's heap), 1 MiB at a time with an overlap for a
// header across a seam.
static int find_tables(Bdat *list, Bdat *text) {
    static uint8_t buf[(1u << 20) + 16];
    const RtGuestLayout *l = mem_layout();
    int have = 0;   // bit 0 list, bit 1 text
    uint64_t base = l->alias.base, limit = l->alias.base + l->alias.size;
    for (uint64_t p = base, end; p < limit && have != 3; p = end) {
        int state = plat_mem_state(p, limit, &end);
        if (end <= p) break;
        if (state != PLAT_PROT_READ && state != PLAT_PROT_RW) continue;
        for (uint64_t q = p; q < end && have != 3; q += 1u << 20) {
            size_t n = end - q < sizeof buf ? (size_t)(end - q) : sizeof buf;
            if (!plat_mem_read(q, buf, n)) continue;
            for (size_t i = 0; i + 5 <= n && i < (1u << 20); i++) {
                if (buf[i] != 'B' || memcmp(buf + i, "BDAT\x04", 5)) continue;
                Bdat b;
                if (!bdat_at(q + i, &b)) continue;
                if (!(have & 1) && b.cols == ACH_LIST_COLS && b.rows == ACH_IDS + 1 && b.row_len == ACH_LIST_ROW)
                    *list = b, have |= 1;
                else if (!(have & 2) && bdat_named(&b, ACH_TEXT)) *text = b, have |= 2;
            }
        }
    }
    if (trace && have != 3)
        rt_log("achievements: game tables in %#llx+%#llx: list %s, text %s", (unsigned long long)base,
               (unsigned long long)l->alias.size, have & 1 ? "found" : "not found", have & 2 ? "found" : "not found");
    return have == 3;
}

// JSON string body: UTF-8 passes through, quotes, backslashes and control characters are escaped.
static size_t json_append(char *out, size_t at, size_t cap, const char *s) {
    for (; *s && at + 8 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') out[at++] = '\\', out[at++] = (char)c;
        else if (c < 0x20) at += (size_t)snprintf(out + at, cap - at, "\\u%04x", c);
        else out[at++] = (char)c;
    }
    out[at] = 0;
    return at;
}

// native_list: every achievement the game's menu shows, with the game's own title and description (from the player's
// copy of the game, read while it runs). No gamerscore: the game has no points; the launcher's list sets them.
static int send_native_list(void) {
    Bdat list, text;
    if (!find_tables(&list, &text)) return 0;
    uint32_t t_off, d_off, s_off, disp_off, text_off;
    int t_size, d_size, s_size, disp_size, text_size = 0, text_col = -1;
    if (!bdat_col(&list, ACH_COL_TITLE, &t_off, &t_size) || !bdat_col(&list, ACH_COL_DESC, &d_off, &d_size) ||
        !bdat_col(&list, ACH_COL_DISP, &disp_off, &disp_size) || !bdat_col(&list, 0, &s_off, &s_size)) {
        rt_log("achievements: the game's achievement list does not have the expected columns; no native_list");
        return -1;
    }
    for (int c = 0; c < (int)text.cols && text_col < 0; c++) {   // the text column: the first string column
        uint8_t type = 0;
        if (plat_mem_read(text.at + text.coldef + 3u * c, &type, 1) && type == 7) text_col = c;
    }
    if (text_col < 0 || !bdat_col(&text, text_col, &text_off, &text_size)) {
        rt_log("achievements: " ACH_TEXT " has no string column; no native_list");
        return -1;
    }
    size_t cap = 1u << 20, at = 0;
    char *out = malloc(cap), title[TEXT_MAX], desc[TEXT_MAX];
    if (!out) return -1;
    at += (size_t)snprintf(out, cap, "{\"t\":\"native_list\",\"achievements\":[");
    int count = 0;
    for (uint32_t row = 0; row < ACH_IDS && at + 3 * TEXT_MAX < cap; row++) {
        if (bdat_value(&list, row, disp_off, disp_size) != 1) continue;
        uint32_t t = bdat_value(&list, row, t_off, t_size) - text.base;
        uint32_t d = bdat_value(&list, row, d_off, d_size) - text.base;
        bdat_string(&text, bdat_value(&text, t, text_off, text_size), title);
        bdat_string(&text, bdat_value(&text, d, text_off, text_size), desc);
        at += (size_t)snprintf(out + at, cap - at, "%s{\"id\":%u,\"title\":\"", count ? "," : "", list.base + row);
        at = json_append(out, at, cap, title);
        at += (size_t)snprintf(out + at, cap - at, "\",\"description\":\"");
        at = json_append(out, at, cap, desc);
        at += (size_t)snprintf(out + at, cap - at, "\"}");
        count++;
    }
    at += (size_t)snprintf(out + at, cap - at, "]}\n");
    int ok = channel && plat_ipc_write(channel, out, at);
    free(out);
    rt_log("achievements: native_list, %d achievements (%zu bytes)", count, at);
    return ok ? 1 : -1;
}

static uint16_t last_levels[CHARACTERS], last_story;

// One look at the game: fills ach_earned, and sets *loaded when the achievements, the levels or the scenario changed
// as only a load changes them; 0 when the data does not fit.
static int look(uint64_t data, int *loaded) {
    uint16_t story = 0;
    if (!plat_mem_read(data + STORY, &story, sizeof story) || story > STORY_MAX) return 0;
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
    // Play moves the scenario one step on, or from a chapter's last step to the next chapter's first: anything else
    // (back, or past a whole chapter) is another save.
    int story_jump = story < last_story || story / 1000 > last_story / 1000 + 1;
    // The game's achievements: the flags word of each record. Play only ever adds a few at once.
    static uint32_t records[(ACH_IDS + 1) * ACH_STRIDE / 4];
    if (!plat_mem_read(data + ACH_RECORDS, records, sizeof records)) return 0;
    int lost = 0, gained = 0, earned_count = 0;
    for (int id = 1; id <= ACH_IDS; id++) {
        uint32_t flags = records[id * ACH_STRIDE / 4];
        if (flags & ~ACH_FLAGS) return 0;
        uint8_t earned = (flags & ACH_EARNED) != 0;
        lost |= ach_earned[id] && !earned;
        gained += earned && !ach_earned[id];
        earned_count += earned;
        ach_earned[id] = earned;
    }
    *loaded = changed >= LOAD_LEVELS || down || story_jump || lost || gained > LOAD_ACH;
    memcpy(last_levels, now, sizeof now);
    last_story = story;
    static int64_t last_top = -1, last_traced_story = -1, last_earned = -1;
    if (trace && (top != last_top || story != last_traced_story || earned_count != last_earned)) {
        rt_log("achievements: top level %d, scenario %u, game achievements earned %d", top, story, earned_count);
        last_top = top, last_traced_story = story, last_earned = earned_count;
    }
    return 1;
}

static void achievements_thread(void *arg) {
    (void)arg;
    uint64_t main_base = ldr_module(0)->base, last_data = 0, next_connect = 0, settled_from = 0;
    int casual_sent = 0, bad_logged = 0, loads = 0, earlier_due = 0, list_sent = 0;
    uint64_t next_list = 0;
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
                memset(ach_fired, 0, sizeof ach_fired);
                earlier_due = 1;
                list_sent = 0, next_list = 0;
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
        if (!list_sent && now >= next_list) {   // once the game has its tables loaded; -1: they do not fit, stop
            next_list = now + LIST_RETRY_MS;
            list_sent = send_native_list();
        }
        int loaded = 0;
        if (!look(data, &loaded)) {
            if (!bad_logged) rt_log("achievements: the game data does not fit this build's offsets; no events");
            bad_logged = 1;
            continue;
        }
        if (data != last_data || loaded) {   // a save loaded: every achievement waits to be seen unearned, settled
            last_data = data;
            settled_from = now + SETTLE_MS;
            memset(ach_armed, 0, sizeof ach_armed);
            earlier_due = 1;
            if (loads++) {   // the first is the boot's defaults, or the save read at start
                char line[96];
                snprintf(line, sizeof line, "{\"t\":\"save_loaded\",\"frame\":%" PRIu64 "}", frame);
                send_line(line);
            }
            if (trace) rt_log("achievements: load %d", loads);
            continue;
        }
        int settled = now >= settled_from, earlier = 0;
        if (settled && earlier_due) {   // what the save had, once its values settled: each sent once a session
            earlier_due = 0;
            for (int id = 1; id <= ACH_IDS; id++)
                if (ach_earned[id] && !ach_fired[id]) ach_fired[id] = 1, send_achievement(id, 1), earlier++;
            if (trace && earlier) rt_log("achievements: %d game achievements the save already had", earlier);
        }
        for (int id = 1; id <= ACH_IDS; id++) {
            if (!ach_earned[id]) ach_armed[id] |= settled;
            else if (ach_armed[id] && !ach_fired[id]) {
                ach_fired[id] = 1;
                send_achievement(id, 0);
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
