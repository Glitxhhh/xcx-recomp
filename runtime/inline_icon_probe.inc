// Read-only lookup of the icon record that the ML:icon layout method reads. Included by appendage_glyph.c and by
// tests/inline_icon_oracle.c, which both define rd16/rd32/rd64/rdf and plat_mem_readable first.
//
// The target is f_7000a21afc, layouter vtable slot +0x80 (address point 0x1c93f08): x0 is the layouter, x1 the entry code
// from the ML:icon tag table. The layouter's icon source `src` is read at layouter+0x18:
//   src+0x38  table pointer; zero selects a fixed full-texture fallback
//   src+0x48  record count; src+0x4c / src+0x50 two floats scaling the record
//   table+0x18 byte offset of the 28-byte records from the table start
//   record index = (code & 0xffff), or 0 when that is not below the count
// tests/inline_icon_oracle.py runs the lifted function on synthetic memory and checks that exactly the words found here
// influence its output, so this layout is not only a reading of the code.
enum { ICON_SRC_BYTES = 0x58, ICON_REC_BYTES = 0x1c, ICON_REC_WORDS = ICON_REC_BYTES / 4 };
enum { ICON_OK, ICON_NO_SOURCE, ICON_NO_TABLE, ICON_UNREADABLE };
typedef struct { int status; uint32_t count, index; float w, h; uint64_t table, record; } IconLookup;

static const char *icon_status_name(int status) {
    return status == ICON_OK ? "ok" : status == ICON_NO_SOURCE ? "no-source" :
           status == ICON_NO_TABLE ? "no-table(fallback uv)" : "unreadable";
}

static int icon_lookup(uint64_t src, uint32_t code, IconLookup *out) {
    memset(out, 0, sizeof *out);
    if (!src) return out->status = ICON_NO_SOURCE;
    if (!plat_mem_readable(src, ICON_SRC_BYTES)) return out->status = ICON_UNREADABLE;
    out->table = rd64(src + 0x38);
    out->count = rd32(src + 0x48);
    out->w = rdf(src + 0x4c);
    out->h = rdf(src + 0x50);
    if (!out->table) return out->status = ICON_NO_TABLE;
    if (!plat_mem_readable(out->table, 0x1c)) return out->status = ICON_UNREADABLE;
    uint32_t key = code & 0xffff;
    out->index = out->count > key ? key : 0;
    out->record = out->table + rd32(out->table + 0x18) + (uint64_t)out->index * ICON_REC_BYTES;
    if (!plat_mem_readable(out->record, ICON_REC_BYTES)) return out->status = ICON_UNREADABLE;
    return out->status = ICON_OK;
}
