// Verified opt-in HUD guides only. Generation records outlive copied command handles.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appendage_glyph.h"
#include "cfg.h"
#include "host.h"
#include "input.h"
#include "input_map.h"
#include "input_glyphs.h"
#include "layout.h"
#include "platform/platform.h"
#include "sha256.h"
#include "inline_glyph.h"

enum { GENERATIONS = 192, APPENDAGE_SLOTS = 3, NODE_SLOTS = 7 };
static const unsigned selectors[NODE_SLOTS] = {14,6,7,5,7,14,15};
typedef struct { uint32_t tid; uint64_t serial, storage; } GlyphGeneration;
static GlyphGeneration generations[GENERATIONS];
typedef struct { uint64_t node; unsigned slot; } LiveGlyph;
static LiveGlyph live[GENERATIONS];
static uint64_t live_node[APPENDAGE_SLOTS];
static unsigned generation_count, live_generation[APPENDAGE_SLOTS];
static PlatMutex glyph_lock = PLAT_MUTEX_INIT;
static PlatOnce once = PLAT_ONCE_INIT;
static int enabled;
static char control[1024], failure[24];
static uint64_t next_control;
static int control_on = 1;
static unsigned reported_generation[NODE_SLOTS], reported_count[NODE_SLOTS];
static int reported_selection[NODE_SLOTS];
static uint8_t first_binding[GENERATIONS];
static int fallback_observed, history_exhausted;
static unsigned rejected_guides;
typedef struct { const Cpu *cpu; unsigned generation, art; uint32_t tid; } BindScope;
static _Thread_local BindScope scope;
// Opt-in research only: observe these resources during the existing type-1 bind, without attaching nodes.
static const uint8_t probe_prefixes[4][32] = {
    {0x7b,0x2a,0xf2,0xb3,0x88,0xb5,0xe4,0xaa,0xc0,0xdc,0xaf,0x1a,0xd3,0x4b,0xcf,0xef,0xaa,0x21,0x7c,0x1a,0x70,0x59,0x7d,0x7c,0xd3,0x70,0x9b,0x29,0x35,0x8b,0xb1,0x35},
    {0x7a,0x8f,0xf9,0x24,0xca,0xd7,0x7d,0x57,0x75,0xce,0x28,0xe3,0x93,0x90,0xa2,0x1c,0xcf,0x99,0x93,0xb9,0x55,0xff,0x45,0x10,0xc5,0xdd,0x0e,0x8e,0x59,0x7d,0xa6,0xd6},
    {0xbb,0xe2,0xf0,0xce,0x2c,0x5a,0xda,0x43,0x5b,0x8b,0xc8,0x4c,0x1e,0xbe,0xdb,0xeb,0xa1,0x3d,0x54,0xc0,0x5f,0xbb,0xa5,0x39,0xff,0xf7,0x2c,0x12,0xe4,0xe8,0x7d,0x38},
    {0xcd,0x32,0x01,0x49,0x62,0x2f,0xbd,0xd1,0x60,0x89,0xee,0x10,0x88,0xc7,0x83,0x13,0xbb,0x78,0x5a,0xc5,0xd3,0xd7,0xdd,0x55,0x6f,0xcd,0xf9,0x72,0xaf,0xd6,0x53,0x4e}
};
static const char *const probe_names[4] = {"font_icon", "02110_judgering_btn", "icon_button02", "icon_button03"};
static int resource_probe;
static uint64_t probe_deadline;
static unsigned probe_counts[4], probe_events;
typedef struct { const Cpu *cpu; unsigned event; uint64_t deadline; } ProbeScope;
static _Thread_local ProbeScope probe_scope;
// Opt-in research (XCX_GLYPH_ICON_PROBE=1), read-only: the ML:icon layout method f_7000a21afc and the draw's two bind sites
// for the icon source (0xa325f8, 0xa32688). Rendering and guest state are unchanged. Addresses logged here are numbers for
// correlating one event with another; none is dereferenced after the call that produced it, except the live node/source
// the draw itself is using during the bind.
typedef struct { uint64_t source; uint32_t code; } IconSeen;
typedef struct { uint64_t site, source, entry; } IconBindSeen;
enum { ICON_LAYOUT_EVENTS = 64, ICON_BIND_EVENTS = 32, ICON_SOURCES = 16, ICON_BIND_EVENT_BASE = 200 };
static int icon_probe;
static uint64_t icon_deadline;
static unsigned icon_layout_events, icon_bind_events, icon_source_count;
static unsigned icon_layout_calls, icon_bind_calls;   // every call, also those past the event caps
static IconSeen icon_seen[ICON_LAYOUT_EVENTS];
static IconBindSeen icon_bind_seen[ICON_BIND_EVENTS];
static uint64_t icon_sources[ICON_SOURCES];

// Inline text-icon atlas replacement. A signature is the art chosen for each virtual button (A, B, X, Y, L, R, ZL, ZR; -1 keeps
// that button's native cells). Signatures are never recycled: copied draw commands carry the handle's signature id until
// they execute. The table is bounded; once full, new combinations keep the native atlas.
typedef struct { int layout; int16_t art[GLYPH_SYMBOLS]; uint64_t serial, storage; } InlineSignature;
typedef struct { const Cpu *cpu; unsigned id; uint64_t storage; uint32_t tid; } InlineScope;   // tid: sheet only, the texture id the bind must name
static InlineSignature inline_sigs[INLINE_SIGNATURES];
static unsigned inline_sig_count, inline_full_logged, inline_source_logged, inline_entry_logged;
static int inline_enabled = 1;
static _Thread_local InlineScope inline_scope;

static uint64_t rd64(uint64_t p) { uint64_t v; memcpy(&v, (void *)(uintptr_t)p, 8); return v; }
static uint32_t rd32(uint64_t p) { uint32_t v; memcpy(&v, (void *)(uintptr_t)p, 4); return v; }
static uint16_t rd16(uint64_t p) { uint16_t v; memcpy(&v, (void *)(uintptr_t)p, 2); return v; }
static float rdf(uint64_t p) { float v; memcpy(&v, (void *)(uintptr_t)p, 4); return v; }
#include "inline_icon_probe.inc"
// The inspected resource header bounds the record table before the 0x1000 texture region.
static int records_match(uint64_t resource, unsigned extra) {
    if (!resource || !plat_mem_readable(resource, 0x1000)) return 0;
    uint32_t base = rd32(resource + 0x2c);
    if (base > 0x1000 - 0x1c) return 0;
    uint32_t relative = rd32(resource + base + 0x18);
    if (relative > 0x1000 - base) return 0;
    base += relative;
    if (base > 0x1000 - 24 * 28) return 0;
    uint8_t records[3*28], hash[32];
    for (int i = 0; i < 3; i++) memcpy(records+i*28, (void *)(uintptr_t)(resource+base+selectors[i]*28),28);
    static const uint8_t expected[32] = {0x34,0x62,0x54,0xa9,0x27,0x7c,0x80,0xdf,0x8e,0x54,0x43,0x79,0xed,0x61,0xe8,0x4c,0xee,0x25,0xb0,0x27,0x45,0x3c,0x68,0xfc,0xa8,0x25,0x58,0x3b,0x34,0xb8,0x04,0xd5};
    sha256(records,sizeof records,hash);
    if (memcmp(hash,expected,sizeof hash)) return 0;
    if (extra) {
        static const uint8_t x_record[32] = {0xe2,0xc3,0x5a,0xe2,0x8e,0x55,0x45,0xec,0xfb,0x66,0x58,0xb2,0x50,0x3f,0x3e,0x06,0x2f,0xd4,0x21,0x6c,0x43,0x26,0x09,0x0e,0xaf,0x49,0x6e,0xee,0x3b,0x5d,0xa4,0x6d};
        static const uint8_t zl_record[32] = {0x7c,0xe0,0xa3,0xa3,0xa8,0x06,0x64,0x7b,0x28,0xe5,0x6f,0x64,0xb5,0x73,0x46,0xa6,0x36,0x05,0x00,0xbd,0xc5,0x42,0x05,0x25,0xca,0x7e,0x5d,0xe9,0x4f,0xad,0x4f,0xa9};
        sha256((void *)(uintptr_t)(resource+base+extra*28),28,hash);
        if (memcmp(hash,extra==5 ? x_record : zl_record,sizeof hash)) return 0;
    }
    return 1;
}
static void init(void *unused) {
    (void)unused;
    const char *v = getenv("XCX_APPENDAGE_GLYPH");
    enabled = !v || strcmp(v, "0");
    v = getenv("XCX_APPENDAGE_GLYPH_CONTROL");
    if (v && strlen(v) < sizeof control) strcpy(control, v);
    v = getenv("XCX_APPENDAGE_GLYPH_FAIL");
    if (v && strlen(v) < sizeof failure) strcpy(failure, v);
    v = getenv("XCX_GLYPH_RESOURCE_PROBE");
    resource_probe = enabled && v && !strcmp(v,"1");
    if (resource_probe) {
        probe_deadline = kern_now_ns() + 180000000000ull;
        rt_log("glyph resource probe: CAPTURE READY; existing type-1 route only, four resource prefixes, 8 samples each, 180 seconds; unchanged rendering");
    }
    v = getenv("XCX_GLYPH_ICON_PROBE");
    icon_probe = enabled && v && !strcmp(v,"1");
    if (icon_probe) {
        // Generous because the clock starts at boot, long before a menu page can be opened; the event caps bound the log.
        icon_deadline = kern_now_ns() + 480000000000ull;
        rt_log("glyph icon probe: CAPTURE READY; ML:icon layout f_7000a21afc (64 code/source pairs) and icon-source binds "
               "0xa325f8/0xa32688 (32 site/source/entry triples), 480 seconds; unchanged rendering");
    }
    v = getenv("XCX_INLINE_GLYPH");
    inline_enabled = enabled && (!v || strcmp(v, "0"));
    if (enabled) rt_log("appendage glyph: experimental Appendage styles; original rendering is the fallback");
}

static ProbeScope probe_bind(const Cpu *c, uint64_t node) {
    ProbeScope result = {0};
    if (!resource_probe || kern_now_ns() >= probe_deadline) return result;
    plat_mutex_lock(&glyph_lock);
    int full=probe_events==32;
    plat_mutex_unlock(&glyph_lock);
    if (full || !plat_mem_readable(node,0x350) ||
        rd64(node)!=MOD_MAIN_BASE+0x1c94318 || *(uint8_t *)(uintptr_t)(node+0x18)!=1) return result;
    uint64_t resource=rd64(node+0xe0);
    if (!plat_mem_readable(resource,128)) return result;
    uint8_t hash[32]; sha256((void *)(uintptr_t)resource,128,hash);
    for (unsigned i=0;i<4;i++) if (!memcmp(hash,probe_prefixes[i],32)) {
        plat_mutex_lock(&glyph_lock);
        if (probe_counts[i]<8 && kern_now_ns()<probe_deadline) {
            probe_counts[i]++; result=(ProbeScope){c,++probe_events,probe_deadline};
            // Addresses correlate this call only; no pointer is retained after the original bind returns.
            rt_log("glyph resource probe %u: resource %s prefix128 node %llx type 1 selector %u count %u texture-object %llx",
                   result.event,probe_names[i],(unsigned long long)node,rd16(node+0x34c),rd16(node+0x34e),
                   (unsigned long long)c->x[1]);
        }
        plat_mutex_unlock(&glyph_lock);
        break;
    }
    return result;
}
// The ML:icon branch of the tag dispatcher (f_7000a23150) matches the tag value against 0x24-byte entries
// {u32 code, ..., char name[0x14] at +0x10} in the vector at layouter->+8->+0x58 and passes the matching code on.
static void icon_tag_name(uint64_t layouter, uint32_t code, char name[0x15]) {
    strcpy(name, "?");
    uint64_t owner = rd64(layouter + 8);
    if (!owner || !plat_mem_readable(owner, 0x60)) return;
    uint64_t vector = rd64(owner + 0x58);
    if (!vector || !plat_mem_readable(vector, 16)) return;
    uint64_t begin = rd64(vector), end = rd64(vector + 8);
    if (end < begin || (end - begin) / 0x24 > 1024 || !plat_mem_readable(begin, end - begin)) return;
    for (uint64_t e = begin; e + 0x24 <= end; e += 0x24) {
        if (rd32(e) != code) continue;
        memcpy(name, (void *)(uintptr_t)(e + 0x10), 0x14);
        name[0x14] = 0;
        for (char *q = name; *q; q++) if (*q < 0x20 || *q > 0x7e) *q = '.';
        return;
    }
}

// Logs at 1, 10, 100, ... so the totals stay visible after the event caps are reached.
static void icon_count_note(const char *what, unsigned n) {
    unsigned p = 1;
    while (p < n && p <= 100000000u) p *= 10;
    if (p == n) rt_log("glyph icon probe: %u %s calls so far", n, what);
}

void appendage_glyph_icon_layout(const Cpu *c) {
    plat_once(&once, init, NULL);
    if (!icon_probe || kern_now_ns() >= icon_deadline) return;
    uint64_t layouter = c->x[0], caller = c->x[30];
    uint32_t code = (uint32_t)c->x[1];
    plat_mutex_lock(&glyph_lock);
    unsigned calls = ++icon_layout_calls;
    plat_mutex_unlock(&glyph_lock);
    icon_count_note("layout", calls);
    if (!plat_mem_readable(layouter, 0x4f8)) {
        rt_log("glyph icon probe: layout call %u code %u: layouter %llx unreadable", calls, code, (unsigned long long)layouter);
        return;
    }
    uint64_t source = rd64(layouter + 0x18), metrics = rd64(layouter + 0x20), font = rd64(layouter + 0x10);
    IconLookup lk;
    icon_lookup(source, code, &lk);
    uint32_t words[ICON_REC_WORDS] = {0};
    if (lk.status == ICON_OK) memcpy(words, (void *)(uintptr_t)lk.record, sizeof words);
    float f[ICON_REC_WORDS];
    memcpy(f, words, sizeof f);
    unsigned limit = metrics && plat_mem_readable(metrics, 0x20) ? rd16(metrics + 0x1a) : 0;
    plat_mutex_lock(&glyph_lock);
    int seen = icon_layout_events >= ICON_LAYOUT_EVENTS;
    for (unsigned i = 0; i < icon_layout_events && !seen; i++) seen = icon_seen[i].source == source && icon_seen[i].code == code;
    int known = 0;
    for (unsigned i = 0; i < icon_source_count; i++) known |= icon_sources[i] == source;
    if (!known && source && icon_source_count < ICON_SOURCES) icon_sources[icon_source_count++] = source;
    unsigned event = 0;
    if (!seen) { icon_seen[icon_layout_events] = (IconSeen){source, code}; event = ++icon_layout_events; }
    plat_mutex_unlock(&glyph_lock);
    if (!event) return;
    char name[0x15];
    icon_tag_name(layouter, code, name);
    rt_log("glyph icon probe %u: layout code %u key %u tag %s source %llx status %s count %u size %.3fx%.3f index %u "
           "table %llx record %llx words %08x %08x %08x %08x %08x %08x %08x floats %g %g %g %g %g %g "
           "tint %.2f,%.2f,%.2f,%.2f batch %u/%u font %llx metrics %llx caller %llx",
           event, code, code & 0xffff, name, (unsigned long long)source, icon_status_name(lk.status), lk.count, lk.w, lk.h,
           lk.index, (unsigned long long)lk.table, (unsigned long long)lk.record,
           words[0], words[1], words[2], words[3], words[4], words[5], words[6], f[0], f[1], f[2], f[3], f[4], f[5],
           rdf(layouter + 0x68), rdf(layouter + 0x6c), rdf(layouter + 0x70), rdf(layouter + 0x74),
           rd32(layouter + 0x49c), limit, (unsigned long long)font, (unsigned long long)metrics, (unsigned long long)caller);
}

// Source identity: 48 records of 28 bytes whose SHA-256 is INLINE_RECORDS_SHA256, with the scale floats of the pinned
// build. The hash covers the UV table only; it identifies the atlas layout without any pixel data.
static int inline_source_ok(uint64_t src, IconLookup *lk) {
    icon_lookup(src, 0, lk);
    if (lk->status != ICON_OK || lk->count != INLINE_RECORDS || lk->w != INLINE_SOURCE_SCALE_A || lk->h != INLINE_SOURCE_SCALE_B ||
        !plat_mem_readable(lk->record, INLINE_RECORDS * INLINE_RECORD_BYTES)) return 0;
    static const uint8_t expected[32] = INLINE_RECORDS_SHA256;
    uint8_t hash[32];
    sha256((void *)(uintptr_t)lk->record, INLINE_RECORDS * INLINE_RECORD_BYTES, hash);
    return !memcmp(hash, expected, sizeof hash);
}

static const char *const symbol_names[GLYPH_SYMBOLS] = {"crs","pow","hom","mns","pls","x","y","a","b","analp","anarp","anal","anar",
                                                         "l","r","zl","zr","crsu","crsd","crsl","crsr","crsud","crslr","cap","stick-l-v","stick-l-h","stick-r-v","stick-r-h"};
static unsigned inline_signature_id(int layout, const int16_t art[GLYPH_SYMBOLS]) {
    unsigned id = 0, created = 0;
    plat_mutex_lock(&glyph_lock);
    for (unsigned i = 0; i < inline_sig_count && !id; i++)
        if (inline_sigs[i].layout == layout && !memcmp(inline_sigs[i].art, art, sizeof inline_sigs[i].art)) id = i + 1;
    if (!id && inline_sig_count < INLINE_SIGNATURES) {
        inline_sigs[inline_sig_count].layout = layout;
        memcpy(inline_sigs[inline_sig_count].art, art, sizeof inline_sigs[0].art);
        id = ++inline_sig_count; created = 1;
    }
    int full = !id && !inline_full_logged++;
    plat_mutex_unlock(&glyph_lock);
    if (created) {
        char list[GLYPH_SYMBOLS * 40 + 1] = "";
        for (int i = 0; i < GLYPH_SYMBOLS; i++) {
            if (art[i] < 0) continue;
            size_t n = strlen(list);
            snprintf(list + n, sizeof list - n, "%s=%s ", symbol_names[i], glyph_art_name((unsigned)art[i]));
        }
        rt_log("inline glyph: %s signature %u: %s", layout == INLINE_LAYOUT_SHEET ? "sheet" : layout == INLINE_LAYOUT_STICKS ? "stick" :
               layout == INLINE_LAYOUT_HINT ? "mini-map hint" : layout == INLINE_LAYOUT_SOUL ? "soul challenge button" : layout == INLINE_LAYOUT_HOLD ? "field action hold button" : "text", id, list);
    }
    if (full) rt_log("inline glyph: %u signatures in use; further style or binding combinations keep the native icons", INLINE_SIGNATURES);
    return id;
}
// The replaceable symbols the current style would draw for one layout, as a set (symbols of other layouts are masked out so an
// unrelated binding change does not mint a new signature); returns whether any symbol of this layout is replaced.
static int layout_symbols(int layout, int16_t art[GLYPH_SYMBOLS]) {
    glyph_current_symbol_set(art);
    int any = 0;
    for (int i = 0; i < GLYPH_SYMBOLS; i++) {
        int mine = layout == INLINE_LAYOUT_STICKS ? i >= SYM_PER_SHEET : layout == INLINE_LAYOUT_HINT ? i == SYM_R || i == SYM_CRS || i == SYM_CRSD : layout == INLINE_LAYOUT_SOUL ? i == SYM_B : layout == INLINE_LAYOUT_HOLD ? i == SYM_A : i < SYM_PER_SHEET;
        if (!mine || !inline_symbol_replaceable((unsigned)i)) art[i] = -1;
        any |= art[i] >= 0;
    }
    // The mini-map hint draws the D-pad with its down button lit: a pad style shows its lit-down D-pad tile in the D-pad cell; the
    // keyboard keeps the 1-4 cluster (the hint does not mean the down key).
    if (layout == INLINE_LAYOUT_HINT && art[SYM_CRSD] >= 0) {
        const char *name = glyph_art_name((unsigned)art[SYM_CRSD]);
        if (strncmp(name, "keyboard_", 9) && strncmp(name, "mouse_", 6)) art[SYM_CRS] = art[SYM_CRSD];
    }
    if (layout == INLINE_LAYOUT_HINT) { art[SYM_CRSD] = -1; any = art[SYM_R] >= 0 || art[SYM_CRS] >= 0; }
    // The page-turn circles (left and right chevrons around a right-stick cell) draw the arrow keys with only left and right lit.
    if (layout == INLINE_LAYOUT_SHEET && art[SYM_ANAR] >= 0 && art[SYM_ANAR] == glyph_art_find(GLYPH_KEYBOARD, GLYPH_IN_KB_ARROWS)) {
        int lit = glyph_art_find(GLYPH_KEYBOARD, GLYPH_IN_KB_ARROWS_LIT_H);
        if (lit >= 0) art[SYM_ANAR] = (int16_t)lit;
    }
    return any;
}
// At the icon-source binds (the same two sites as the probe, whose register meanings are in the notes there): when the
// current style replaces at least one symbol and the node's source and entry 0 are the verified inline atlas, the next
// BindTexture import inside this call may name a replacement. Nothing is written to the guest or the native atlas.
static void inline_arm(Cpu *c) {
    int16_t art[GLYPH_SYMBOLS];
    if (!layout_symbols(INLINE_LAYOUT_FONT, art)) return;
    uint64_t node = c->x[19], entry = c->x[1];
    if (!plat_mem_readable(node, 0x3c0)) return;
    uint64_t src = rd64(node + 0x3b8);
    IconLookup lk;
    if (!src || !inline_source_ok(src, &lk)) {
        if (src && !inline_source_logged++) rt_log("inline glyph: an icon source did not match the verified atlas; native icons kept");
        return;
    }
    uint64_t set = rd64(src + 0x40), first = set && plat_mem_readable(set, 0x38) ? rd64(set + 0x30) : 0;
    if (!first || entry != first || !plat_mem_readable(entry, 0x78) || rd64(entry) != INLINE_ENTRY_Q0 || !rd64(entry + 0x20)) {
        if (!inline_entry_logged++) rt_log("inline glyph: the bound texture entry is not entry 0 of the verified source; native icons kept");
        return;
    }
    unsigned id = inline_signature_id(INLINE_LAYOUT_FONT, art);
    if (id) inline_scope = (InlineScope){c, id, rd64(entry + 0x20), 0};
}
// The icon_button sheet and the stick-direction image: every image pane drawn from them goes through the type-1 bind (the loop at
// 0xa275c0 binds table[frame] once per layer). A pane is one of them when its resource object matches that image's guard hash and
// its record table matches the closed form's SHA-256. A pane shows one cell of them (its selector, +0x34c, is the record index).
// A resource is verified once per distinct address and content, then trusted only while its first 128 bytes stay equal.
typedef struct { int layout; unsigned records; uint8_t guard[32], table[32]; uint64_t checked; uint8_t copy[128]; } SheetKind;
enum { SHEET_KINDS = 2 };
static SheetKind sheet_kinds[SHEET_KINDS] = {
    {INLINE_LAYOUT_SHEET, SHEET_RECORDS, {0xed,0x91,0x64,0xc1,0x3b,0xf5,0x5b,0x99,0x9b,0xf7,0x8b,0xe9,0xd5,0x2d,0x3b,0x65,
        0x71,0x70,0x62,0xc6,0x22,0xdb,0xc8,0xb1,0x31,0x5a,0x30,0xe7,0xd9,0x7d,0x32,0xf7}, SHEET_RECORDS_SHA256},
    {INLINE_LAYOUT_STICKS, STICKS_RECORDS, STICKS_GUARD_SHA256, STICKS_RECORDS_SHA256},
};
static SheetKind *sheet_resource(uint64_t resource) {
    if (!resource || !plat_mem_readable(resource, 0x1000)) return NULL;
    for (unsigned k = 0; k < SHEET_KINDS; k++)
        if (sheet_kinds[k].checked == resource && !memcmp(sheet_kinds[k].copy, (void *)(uintptr_t)resource, sizeof sheet_kinds[k].copy)) return &sheet_kinds[k];
    uint8_t hash[32];
    sha256((void *)(uintptr_t)resource, 128, hash);
    for (unsigned k = 0; k < SHEET_KINDS; k++) {
        SheetKind *kind = &sheet_kinds[k];
        if (memcmp(hash, kind->guard, 32)) continue;
        uint32_t base = rd32(resource + 0x2c);
        if (base > 0x1000 - 0x1c) return NULL;
        uint32_t relative = rd32(resource + base + 0x18);
        if (relative > 0x1000 - base) return NULL;
        base += relative;
        if (base > 0x1000 - kind->records * INLINE_RECORD_BYTES) return NULL;
        sha256((void *)(uintptr_t)(resource + base), kind->records * INLINE_RECORD_BYTES, hash);
        if (memcmp(hash, kind->table, 32)) return NULL;
        memcpy(kind->copy, (void *)(uintptr_t)resource, sizeof kind->copy);
        kind->checked = resource;
        return kind;
    }
    return NULL;
}
// Evidence only: the distinct (selector, frame count) pairs of panes seen drawing the sheet, so a log shows which panes were reached.
enum { SHEET_SEEN = 48 };
static uint64_t sheet_seen[SHEET_SEEN]; static unsigned sheet_seen_count;
static void sheet_arm(Cpu *c, uint64_t node) {
    // Hundreds of image panes bind per frame: with Original selected (the default) nothing below runs.
    if (!inline_enabled || glyph_style_parse(cfg_value("XCX_GLYPH_STYLE")) == GLYPH_ORIGINAL) return;
    if (!plat_mem_readable(node, 0x350) || rd64(node) != MOD_MAIN_BASE + 0x1c94318 || *(uint8_t *)(uintptr_t)(node + 0x18) != 1) return;
    const SheetKind *kind = sheet_resource(rd64(node + 0xe0));
    if (!kind) return;
    // Every pane drawn from a verified image may use the composite: it holds all replaced cells at once and the native pixels
    // everywhere else, so which cell this pane shows (selector +0x34c) need not be known to be right.
    int16_t art[GLYPH_SYMBOLS];
    if (!layout_symbols(kind->layout, art)) return;
    uint64_t table = rd64(node + 0x330);
    if (!table || !plat_mem_readable(table, 8)) return;
    uint64_t object = rd64(table);
    if (!object || !plat_mem_readable(object, 0x38)) return;
    uint32_t tid = rd32(rd64(MOD_MAIN_BASE + 0x1d64f18)) + rd32(object + 0x34);
    unsigned id = inline_signature_id(kind->layout, art);
    if (!id) return;
    uint64_t pair = (uint64_t)kind->layout << 40 | (uint64_t)rd16(node + 0x34c) << 20 | rd16(node + 0x34e);
    plat_mutex_lock(&glyph_lock);
    unsigned seen = 0;
    for (unsigned i = 0; i < sheet_seen_count; i++) seen |= sheet_seen[i] == pair;
    int first = !seen && sheet_seen_count < SHEET_SEEN;
    if (first) sheet_seen[sheet_seen_count++] = pair;
    plat_mutex_unlock(&glyph_lock);
    if (first) rt_log("inline glyph: a layout pane draws the %s (selector %u of %u frames); using the signature's composite",
                      kind->layout == INLINE_LAYOUT_STICKS ? "stick-direction image" : "button sheet",
                      rd16(node + 0x34c), rd16(node + 0x34e));
    inline_scope = (InlineScope){c, id, 0, tid};
}
// The two f_7000a322cc binds of the icon source (node+0x3b8). Argument meanings, checked against the pinned main by
// verify_sites.py: x0 render context, x1 entry 0 of the source's texture set (*(*(source+0x40)+0x30), 0x78-byte entries),
// x2 slot byte node+0xf2, x3 zero; x19 still holds the text node. Comparing x1 with an entry recomputed from c->x[19]
// checks that last assumption at run time: it can only match when c->x[19] really is the node the draw uses.
void appendage_glyph_icon_bind(Cpu *c, GuestFn original) {
    ProbeScope saved = probe_scope;
    InlineScope saved_inline = inline_scope;
    probe_scope = (ProbeScope){0};
    inline_scope = (InlineScope){0};
    plat_once(&once, init, NULL);
    unsigned event = 0;
    if (icon_probe && kern_now_ns() < icon_deadline) {
        uint64_t node = c->x[19], site = c->x[30], entry = c->x[1];
        plat_mutex_lock(&glyph_lock);
        unsigned calls = ++icon_bind_calls;
        plat_mutex_unlock(&glyph_lock);
        icon_count_note("bind", calls);
        uint64_t source = 0, geom = 0, set = 0, first = 0;
        if (plat_mem_readable(node, 0x3c0)) { source = rd64(node + 0x3b8); geom = rd64(node + 0x308); }
        IconLookup lk;
        icon_lookup(source, 0, &lk);   // header fields only: count, size, table pointer
        if (source && lk.status != ICON_UNREADABLE) {
            set = rd64(source + 0x40);
            if (set && plat_mem_readable(set, 0x38)) first = rd64(set + 0x30);
        }
        unsigned draw1 = 0, draw2 = 0;
        if (geom && plat_mem_readable(geom, 0x38)) { draw1 = rd32(geom + 0x34); draw2 = rd32(geom + 0x30); }
        plat_mutex_lock(&glyph_lock);
        int seen = icon_bind_events >= ICON_BIND_EVENTS;
        for (unsigned i = 0; i < icon_bind_events && !seen; i++)
            seen = icon_bind_seen[i].site == site && icon_bind_seen[i].source == source && icon_bind_seen[i].entry == entry;
        int at_layout = 0;
        for (unsigned i = 0; i < icon_source_count; i++) at_layout |= source && icon_sources[i] == source;
        if (!seen) { icon_bind_seen[icon_bind_events] = (IconBindSeen){site, source, entry}; event = ++icon_bind_events; }
        plat_mutex_unlock(&glyph_lock);
        if (event) {
            uint64_t q[15] = {0};
            if (entry && plat_mem_readable(entry, sizeof q)) memcpy(q, (void *)(uintptr_t)entry, sizeof q);
            probe_scope = (ProbeScope){c, ICON_BIND_EVENT_BASE + event, icon_deadline};
            rt_log("glyph icon probe bind %u: site %llx node %llx source %llx %s at layout; source %s count %u size %.3fx%.3f "
                   "set %llx entry0 %llx x1 %llx %s entry0; slot %u flag %u draw counts glyph %u second %u; "
                   "entry %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx; texture follows as resource probe %u",
                   event, (unsigned long long)site, (unsigned long long)node, (unsigned long long)source,
                   at_layout ? "seen" : "NOT seen", icon_status_name(lk.status), lk.count, lk.w, lk.h,
                   (unsigned long long)set, (unsigned long long)first, (unsigned long long)entry,
                   first && entry == first ? "==" : "!=", (unsigned)c->x[2] & 0xff, (unsigned)c->x[3], draw1, draw2,
                   (unsigned long long)q[0], (unsigned long long)q[1], (unsigned long long)q[2], (unsigned long long)q[3],
                   (unsigned long long)q[4], (unsigned long long)q[5], (unsigned long long)q[6], (unsigned long long)q[7],
                   (unsigned long long)q[8], (unsigned long long)q[9], (unsigned long long)q[10], (unsigned long long)q[11],
                   (unsigned long long)q[12], (unsigned long long)q[13], (unsigned long long)q[14], ICON_BIND_EVENT_BASE + event);
        }
    }
    if (inline_enabled) inline_arm(c);
    original(c);
    // The existing handle observer clears the scope when the original BindTexture import runs; a bind that never
    // reaches it (cached or skipped) is reported instead of silently missing its texture line.
    if (event && probe_scope.cpu == c && probe_scope.event)
        rt_log("glyph icon probe bind %u: no BindTexture import reached during this bind (texture not observed)", event);
    probe_scope = saved;
    inline_scope = saved_inline;
}

int appendage_glyph_failure(const char *stage) {
    plat_once(&once, init, NULL);
    return enabled && !strcmp(failure, stage);
}

void appendage_glyph_retire(uint64_t node) {
    plat_once(&once, init, NULL);
    if (!enabled) return;
    plat_mutex_lock(&glyph_lock);
    for (unsigned i=0;i<generation_count;i++) if (live[i].node==node) live[i].node=0;
    for (int i=0;i<APPENDAGE_SLOTS;i++) if (node==live_node[i]) live_node[i]=0, live_generation[i]=0;
    plat_mutex_unlock(&glyph_lock);
}

static int attach_node(int slot, uint64_t node) {
    plat_once(&once, init, NULL);
    if (!enabled) return 0;
    if (!node || !plat_mem_readable(node, 0x350) || rd64(node) != MOD_MAIN_BASE + 0x1c94318 ||
        *(uint8_t *)(uintptr_t)(node + 0x18) != 1 || rd16(node + 0x34e) != 24) return 0;
    uint64_t resource = rd64(node + 0xe0), table = rd64(node + 0x330);
    if (!resource || !table || !plat_mem_readable(resource, 128) || !plat_mem_readable(table, 8)) return 0;
    uint8_t hash[32];
    static const uint8_t expected[32] = {0xed,0x91,0x64,0xc1,0x3b,0xf5,0x5b,0x99,0x9b,0xf7,0x8b,0xe9,0xd5,0x2d,0x3b,0x65,
        0x71,0x70,0x62,0xc6,0x22,0xdb,0xc8,0xb1,0x31,0x5a,0x30,0xe7,0xd9,0x7d,0x32,0xf7};
    sha256((void *)(uintptr_t)resource, 128, hash);
    if (memcmp(hash, expected, 32) || !records_match(resource,slot==3 ? 5 : slot==6 ? 15 : 0)) return 0;
    uint64_t object = rd64(table);
    if (!object || !plat_mem_readable(object, 0x38)) return 0;
    uint32_t tid = rd32(rd64(MOD_MAIN_BASE + 0x1d64f18)) + rd32(object + 0x34);
    plat_mutex_lock(&glyph_lock);
    // Every live shared-widget instance remains eligible. Generations contain only source identity;
    // a retained field node may change A/R/ZL without changing an already copied command's artwork.
    for (unsigned i=0;i<generation_count;i++) if (live[i].node==node) {
        if (slot>=APPENDAGE_SLOTS && live[i].slot>=APPENDAGE_SLOTS && generations[i].tid==tid) {
            live[i].slot=slot; plat_mutex_unlock(&glyph_lock); return 1;
        }
        live[i].node=0;
    }
    if (slot<APPENDAGE_SLOTS) live_node[slot]=0, live_generation[slot]=0;
    int attached=0;
    if (generation_count<GENERATIONS) {
        unsigned index=generation_count++;
        generations[index]=(GlyphGeneration){.tid=tid}; live[index]=(LiveGlyph){node,(unsigned)slot};
        if (slot<APPENDAGE_SLOTS) live_node[slot]=node, live_generation[slot]=index+1;
        rt_log("HUD glyph: slot %d selector %u generation %u attached, source texture %u",slot,selectors[slot],index+1,tid);
        attached=1;
    } else history_exhausted=1;
    plat_mutex_unlock(&glyph_lock);
    return attached;
}

void appendage_glyph_attach(const Cpu *c) {
    plat_once(&once, init, NULL);
    if (!enabled) return;
    int slot = c->x[30] == MOD_MAIN_BASE + 0x60f690 ? 0 : c->x[30] == MOD_MAIN_BASE + 0x60f6ac ? 1 :
               c->x[30] == MOD_MAIN_BASE + 0x60f6c8 ? 2 : -1;
    if (slot < 0 || c->x[1] != selectors[slot]) return;
    if (!slot) {
        plat_mutex_lock(&glyph_lock);
        for (unsigned i=0;i<generation_count;i++) if (live[i].slot<APPENDAGE_SLOTS) live[i].node=0;
        memset(live_node,0,sizeof live_node);
        memset(live_generation,0,sizeof *live_generation*APPENDAGE_SLOTS);
        plat_mutex_unlock(&glyph_lock);
    }
    if (!plat_mem_readable(c->x[0],0x40)) return;
    uint64_t node = rd64(c->x[0]+0x38);
    appendage_glyph_retire(node);
    attach_node(slot,node);
}

void appendage_glyph_lookup_attach(const Cpu *c, uint64_t frame) {
    if (c->x[30]!=MOD_MAIN_BASE+0x55ff20 || (frame&15) || !plat_mem_readable(frame,16)) return;
    // 55fec0 saves FP/LR at SP and 55fecc sets FP=SP; the tail-call from 5da974 preserves the parent LR.
    uint64_t caller=rd64(frame+8); int slot=-1;
    if (caller==MOD_MAIN_BASE+0x5a6660 && c->x[1]==5) slot=3;
    if (caller==MOD_MAIN_BASE+0x5c6ed8) slot=c->x[1]==7 ? 4 : c->x[1]==14 ? 5 : c->x[1]==15 ? 6 : -1;
    if (slot<0 || !plat_mem_readable(c->x[0],8)) return;
    // a1cfc0 constructs this temporary wrapper with the resolved rectangle at offset zero.
    if (!attach_node(slot,rd64(c->x[0]))) {
        plat_mutex_lock(&glyph_lock); unsigned bit=1u<<slot;
        if (!(rejected_guides&bit)) {
            rejected_guides|=bit;
            rt_log("HUD glyph: shared guide slot %d retained original; node/type/resource guard or history limit",slot);
        }
        plat_mutex_unlock(&glyph_lock);
    }
}

void appendage_glyph_bind(Cpu *c, uint64_t node, GuestFn original) {
    BindScope saved = scope;
    ProbeScope saved_probe = probe_scope;
    InlineScope saved_inline = inline_scope;
    scope = (BindScope){0};
    inline_scope = (InlineScope){0};
    plat_once(&once, init, NULL);
    probe_scope = probe_bind(c,node);
    unsigned selected = 0; int slot = -1;
    if (enabled) {
        plat_mutex_lock(&glyph_lock);
        for (unsigned i=0;i<generation_count;i++) if (live[i].node==node) { selected=i+1; slot=(int)live[i].slot; }
        if (slot < APPENDAGE_SLOTS && (!live_generation[0] || !live_generation[1] || !live_generation[2])) selected = 0;
        plat_mutex_unlock(&glyph_lock);
    }
    GlyphDecision decision = {.style = GLYPH_ORIGINAL};
    if (selected) decision = slot<APPENDAGE_SLOTS ? glyph_current_decision() : glyph_current_button(slot==3 ? B_X : slot==4 ? B_A : slot==5 ? B_R : B_ZL);
    int art = selected ? decision.art[slot<APPENDAGE_SLOTS ? slot : 0] : -1;
    if (selected && decision.style != GLYPH_ORIGINAL && art >= 0) {
        plat_mutex_lock(&glyph_lock);
        if (control[0] && kern_now_ns() >= next_control) {
            next_control = kern_now_ns() + 100000000;
            FILE *f = fopen(control, "rb");
            control_on = f && fgetc(f) == '1';
            if (f) fclose(f);
        }
        // These fields are read only during the original node's live draw call.
        if (control_on && live[selected-1].node == node && live[selected-1].slot == (unsigned)slot &&
            rd64(node) == MOD_MAIN_BASE + 0x1c94318 && rd16(node + 0x34c) == selectors[slot] && records_match(rd64(node + 0xe0),slot==3 ? 5 : slot==6 ? 15 : 0))
            scope = (BindScope){c, selected, (unsigned)art, generations[selected - 1].tid};
        plat_mutex_unlock(&glyph_lock);
    }
    if (selected) {
        plat_mutex_lock(&glyph_lock);
        int on = scope.generation ? (int)scope.art + 1 : 0;
        if (!first_binding[selected-1] || (reported_count[slot] < 8 && (reported_generation[slot] != selected || reported_selection[slot] != on))) {
            first_binding[selected-1]=1;
            reported_generation[slot] = selected;
            reported_selection[slot] = on;
            reported_count[slot]++;
            unsigned active=live[selected-1].node==node ? rd16(node+0x34c) : 0xffff;
            rt_log("HUD glyph: slot %d selector %u active %u generation %u requests %s binding", slot, selectors[slot], active, selected, on ? glyph_art_name(scope.art) : "original");
        }
        plat_mutex_unlock(&glyph_lock);
    }
    // A pane drawn from the icon_button sheet that no verified HUD guide owns: its selected cell may be replaced by signature.
    if (!selected && enabled) sheet_arm(c, node);
    original(c);
    scope = saved;
    probe_scope = saved_probe;
    inline_scope = saved_inline;
}

uint64_t appendage_glyph_handle(const Cpu *c, uint64_t original) {
    if (probe_scope.cpu==c && probe_scope.event && c->x[30]==MOD_MAIN_BASE+0x95c9cc) {
        unsigned event=probe_scope.event; probe_scope.event=0;
        if (kern_now_ns()<probe_scope.deadline) vk_glyph_resource_observe(original,event);
    }
    if (inline_scope.cpu == c && inline_scope.id && c->x[30] == MOD_MAIN_BASE + 0x95c9cc && !(original >> 32) &&
        (!inline_scope.tid || (original & 0xfffff) == inline_scope.tid)) {
        uint64_t tagged = INLINE_GLYPH_TAG | (uint64_t)inline_scope.id << 32 | original;
        inline_scope.id = 0;   // one handle per bind: later imports in this call keep their own
        return vk_inline_capture_source(tagged, inline_scope.storage) ? tagged : original;
    }
    if (scope.cpu != c || !scope.generation || c->x[30] != MOD_MAIN_BASE + 0x95c9cc ||
        original >> 32 || (original & 0xfffff) != scope.tid || scope.art >= GLYPH_ART_MAX) return original;   // the handle names its art in 7 bits
    uint64_t tagged = APPENDAGE_GLYPH_TAG | (uint64_t)scope.generation << 44 | (uint64_t)scope.art << 32 | original;
    return vk_appendage_capture_source(tagged) ? tagged : original;
}

int appendage_glyph_source(uint64_t h, uint64_t serial, uint64_t storage) {
    if (!appendage_glyph_tagged(h) || appendage_glyph_art(h) >= glyph_art_count()) return 0;
    unsigned g = (unsigned)(h >> 44 & 0xfff);
    int valid = 0;
    plat_mutex_lock(&glyph_lock);
    if (g && g <= generation_count) {
        GlyphGeneration *s = &generations[g - 1];
        if ((h & 0xfffff) == s->tid && serial && storage) {
            if (!s->serial) s->serial = serial, s->storage = storage;
            valid = s->serial == serial && s->storage == storage;
        }
    }
    plat_mutex_unlock(&glyph_lock);
    return valid;
}

// The mini-map hint and the Soul Challenge button are single images the texture layer recognizes by their content; only the style
// decides what they show.
unsigned appendage_plain_signature(int layout) {
    plat_once(&once, init, NULL);
    if (!inline_enabled || glyph_style_parse(cfg_value("XCX_GLYPH_STYLE")) == GLYPH_ORIGINAL) return 0;
    int16_t art[GLYPH_SYMBOLS];
    if (!layout_symbols(layout, art)) return 0;
    return inline_signature_id(layout, art);
}

int appendage_inline_signature(unsigned id, int16_t art[GLYPH_SYMBOLS], int *layout) {
    int ok = 0;
    plat_mutex_lock(&glyph_lock);
    if (id && id <= inline_sig_count) { memcpy(art, inline_sigs[id - 1].art, sizeof inline_sigs[0].art); *layout = inline_sigs[id - 1].layout; ok = 1; }
    plat_mutex_unlock(&glyph_lock);
    return ok;
}
int appendage_inline_source(uint64_t h, uint64_t serial, uint64_t storage) {
    if (!appendage_inline_tagged(h)) return 0;
    unsigned id = appendage_inline_id(h);
    int valid = 0;
    plat_mutex_lock(&glyph_lock);
    if (id && id <= inline_sig_count && serial && storage) {
        InlineSignature *g = &inline_sigs[id - 1];
        if (!g->serial) g->serial = serial, g->storage = storage;
        valid = g->serial == serial && g->storage == storage;
    }
    plat_mutex_unlock(&glyph_lock);
    return valid;
}

void appendage_glyph_note_fallback(void) {
    plat_mutex_lock(&glyph_lock); fallback_observed = 1; plat_mutex_unlock(&glyph_lock);
}
const char *appendage_glyph_runtime_note(void) {
    plat_mutex_lock(&glyph_lock);
    int complete=live_generation[0] && live_generation[1] && live_generation[2];
    for (unsigned i=0;i<generation_count;i++) if (live[i].node && live[i].slot>=APPENDAGE_SLOTS) complete=1;
    const char *note = !enabled ? "Original virtual labels: experimental rendering disabled" :
        history_exhausted ? "Original virtual labels: glyph history exhausted; restart game to restore rebuilt guides" :
        !complete ? "Original virtual labels: waiting for verified HUD nodes" :
        fallback_observed ? "Rendering fallback was observed; original virtual labels are retained where needed" : NULL;
    plat_mutex_unlock(&glyph_lock);
    return note;
}
