// Oracle for runtime/inline_icon_probe.inc: runs the lifted guest functions f_7000a204cc (layouter initialiser) and
// f_7000a21afc (ML:icon layout method) on synthetic memory and perturbs one input word at a time. The words whose change
// alters what the guest function writes must be exactly the ones the probe's lookup points at. The functions come from
// generated code, which tests/inline_icon_oracle.py extracts at test time; nothing derived from the game is stored here.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "cpu.h"
#include "simd.h"

volatile int g_fnprof;
void fnprof_hit(uint64_t a, uint64_t lr, uint64_t x0) { (void)a; (void)lr; (void)x0; }
void rt_dispatch(Cpu *c) { (void)c; fprintf(stderr, "unexpected dispatch\n"); abort(); }
void rt_udf(Cpu *c, uint64_t pc) { (void)c; (void)pc; fprintf(stderr, "unexpected udf\n"); abort(); }
static unsigned hook_calls;
static uint64_t hook_x0, hook_x1;
void appendage_glyph_icon_layout(const Cpu *c) { hook_calls++; hook_x0 = c->x[0]; hook_x1 = c->x[1]; }

#include "oracle_fn.inc"   // f_7000a204cc and f_7000a21afc, as the generator emits them

static uint64_t rd64(uint64_t p) { uint64_t v; memcpy(&v, (void *)(uintptr_t)p, 8); return v; }
static uint32_t rd32(uint64_t p) { uint32_t v; memcpy(&v, (void *)(uintptr_t)p, 4); return v; }
static uint16_t rd16(uint64_t p) { uint16_t v; memcpy(&v, (void *)(uintptr_t)p, 2); return v; }
static float rdf(uint64_t p) { float v; memcpy(&v, (void *)(uintptr_t)p, 4); return v; }
static int plat_mem_readable(uint64_t a, uint64_t n) { (void)a; (void)n; return 1; }
#include "inline_icon_probe.inc"

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)

enum { COUNT = 8, RECORDS_MAX = 0x40, SNAP = 0x600 + 0x80 + 0x100 + 0x100 };
static _Alignas(16) uint8_t Db[0x40], Lb[0x600], Mb[0x100], Fb[0x100], Ob[0x100], Bb[0x100], Ab[0x2100], Rb[0x80], Xb[0x100], Tb[0x200];
static unsigned table_offset = 0x20, mode;
static int use_tint, limit_count = -1;
static float tint[4];
static _Alignas(16) uint8_t stack[0x1000];   // The guest function spills floats below sp.

// The initialiser loads the vtable base from a GOT slot at a fixed guest address. The value is the relocated pointer that
// verify_sites.py reads from the pinned image (GOT 0x1d65da0 -> 0x1c93ef8); the initialiser must store it plus 0x10.
enum { GOT_PAGE_ADDR = 0x7001d65000ll, GOT_SLOT = 0xda0 };
static const uint64_t vtable_base = 0x7000000000ull + 0x1c93ef8;

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static void put64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }
static float *word(unsigned rec, unsigned w) { return (float *)(Tb + table_offset + rec * ICON_REC_BYTES + w * 4); }

static void reset_world(void) {
    memset(Fb, 0, sizeof Fb); memset(Mb, 0, sizeof Mb); memset(Tb, 0, sizeof Tb); memset(Xb, 0, sizeof Xb);
    putf(Fb + 0x78, 0.5f); putf(Fb + 0x80, 0.25f); putf(Fb + 0x88, 0.125f); putf(Fb + 0x8c, 0.125f);
    putf(Fb + 0x90, 0.25f); putf(Fb + 0x94, 0.25f);
    // Font descriptor at font+0x60: atlas size floats, cell size and column count that the layout method also reads.
    memset(Db, 0, sizeof Db); putf(Db, 256.0f); putf(Db + 4, 256.0f); put32(Db + 8, 16); put32(Db + 0xc, 16); put32(Db + 0x10, 8);
    put64(Fb + 0x60, (uintptr_t)Db);
    for (int i = 0; i < 5; i++) putf(Mb + i * 4, 1.0f);
    { uint16_t h = 20, lim = 500; memcpy(Mb + 0x18, &h, 2); memcpy(Mb + 0x1a, &lim, 2); }
    put64(Xb + 0x38, (uintptr_t)Tb); put32(Xb + 0x48, COUNT); putf(Xb + 0x4c, 0.5f); putf(Xb + 0x50, 0.75f);
    put32(Tb + 0x18, table_offset);
    for (unsigned r = 0; r < RECORDS_MAX / 8 && r < COUNT; r++)
        for (unsigned w = 0; w < ICON_REC_WORDS; w++) *word(r, w) = 0.01f * (r * ICON_REC_WORDS + w + 1);
}

// One call of the guest layout method on a freshly initialised layouter. Returns the snapshot; *emitted reports whether it
// wrote a glyph run at all.
static void run_once(uint32_t code, uint8_t snap[SNAP], int *emitted) {
    memset(Lb, 0, sizeof Lb); memset(Rb, 0, sizeof Rb); memset(Bb, 0, sizeof Bb); memset(Ab, 0, sizeof Ab);
    Cpu c; memset(&c, 0, sizeof c); c.sp = (uintptr_t)stack + 0x800;
    c.x[0] = (uintptr_t)Lb; c.x[1] = (uintptr_t)Fb; c.x[2] = (uintptr_t)Ob; c.x[3] = (uintptr_t)Mb; c.x[4] = (uintptr_t)Xb;
    c.x[5] = mode;
    f_7000a204cc(&c);
    CHECK(rd64((uintptr_t)Lb) == vtable_base + 0x10);   // Address point 0x1c93f08, stored by the lifted initialiser.
    put64(Lb + 0x60, (uintptr_t)Rb); put64(Lb + 0x4b0, (uintptr_t)Bb); put64(Bb + 0x58, (uintptr_t)Ab);
    if (use_tint) for (int i = 0; i < 4; i++) putf(Lb + 0x68 + i * 4, tint[i]);
    if (limit_count >= 0) put32(Lb + 0x49c, (uint32_t)limit_count);
    Cpu d; memset(&d, 0, sizeof d); d.sp = (uintptr_t)stack + 0x800;
    d.x[0] = (uintptr_t)Lb; d.x[1] = code;
    hook_calls = 0;
    f_7000a21afc(&d);
    CHECK(hook_calls == 1 && hook_x0 == (uintptr_t)Lb && hook_x1 == code);   // The entry hook sees the layouter and the code.
    memcpy(snap, Lb, 0x600); memcpy(snap + 0x600, Rb, 0x80); memcpy(snap + 0x680, Bb, 0x100); memcpy(snap + 0x780, Ab, 0x100);
    *emitted = 0;
    for (int i = 0; i < 0x28; i++) *emitted |= Rb[i] != 0;
}

// Words (record * 8 + word index) of the table whose perturbation changes the guest function's output.
static unsigned table_influence(uint32_t code, unsigned hits[], unsigned max, int *emitted) {
    static uint8_t base[SNAP], other[SNAP];
    run_once(code, base, emitted);
    unsigned n = 0;
    for (unsigned r = 0; r < COUNT; r++)
        for (unsigned w = 0; w < ICON_REC_WORDS; w++) {
            float saved = *word(r, w);
            *word(r, w) = saved + 0.371f;
            int e;
            run_once(code, other, &e);
            *word(r, w) = saved;
            if (memcmp(base, other, SNAP) && n < max) hits[n++] = r * 8 + w;
        }
    return n;
}

static unsigned x_influence(unsigned offset) {
    static uint8_t base[SNAP], other[SNAP];
    int e;
    run_once(3, base, &e);
    float saved = rdf((uintptr_t)Xb + offset);
    putf(Xb + offset, saved + 0.371f);
    run_once(3, other, &e);
    putf(Xb + offset, saved);
    return memcmp(base, other, SNAP) != 0;
}

int main(void) {
    // The page only holds the one synthetic GOT slot. MAP_FIXED because the address may sit in a reserved gap.
    uint8_t *got = mmap((void *)GOT_PAGE_ADDR, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    CHECK(got == (void *)GOT_PAGE_ADDR);
    put64(got + GOT_SLOT, vtable_base);
    const uint32_t codes[] = {0, 3, 7, 8, 0x12, 0x105, 0x10005, 0xffff};   // 0x105 separates a 16-bit from an 8-bit key.
    unsigned union_words = 0;
    unsigned total = 0;
    for (unsigned off = 0x20; off <= 0x30; off += 0x10) {
        for (mode = 0; mode < 3; mode++) {
            table_offset = off;
            reset_world();
            for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++) {
                IconLookup lk;
                CHECK(icon_lookup((uintptr_t)Xb, codes[i], &lk) == ICON_OK);
                unsigned hits[64], n; int emitted;
                n = table_influence(codes[i], hits, 64, &emitted);
                CHECK(emitted);   // Otherwise the perturbation would prove nothing.
                CHECK(n >= 4);
                unsigned sel = hits[0] / 8;
                for (unsigned k = 0; k < n; k++) {
                    CHECK(hits[k] / 8 == sel);                       // One record only: the one selected.
                    CHECK(hits[k] % 8 < 6);                           // Words 0..5; word 6 is never read here.
                    union_words |= 1u << (hits[k] % 8);
                }
                CHECK(sel == lk.index);                               // The probe points at the same record index...
                CHECK(lk.record == (uintptr_t)Tb + off + sel * ICON_REC_BYTES);   // ...and the same address.
                total++;
            }
            // A table offset word other than the default must move the records: the probe followed it above.
        }
    }
    CHECK(union_words == 0x3f);   // Every one of words 0..5 influenced the output for some code.

    table_offset = 0x20; mode = 0; reset_world();
    // Fallback: no table pointer. No table word may matter, and the probe reports it.
    put64(Xb + 0x38, 0);
    { unsigned hits[64]; int emitted; unsigned n = table_influence(3, hits, 64, &emitted); CHECK(emitted && n == 0); }
    { IconLookup lk; CHECK(icon_lookup((uintptr_t)Xb, 3, &lk) == ICON_NO_TABLE); }
    // The two size floats scale the record in both paths.
    CHECK(x_influence(0x4c) && x_influence(0x50));
    reset_world();
    CHECK(x_influence(0x4c) && x_influence(0x50));
    // Count semantics: a code equal to the count falls back to record 0; a larger count makes it a real index.
    { unsigned hits[64]; int emitted; put32(Xb + 0x48, 4);
      unsigned n = table_influence(6, hits, 64, &emitted); CHECK(n >= 4 && hits[0] / 8 == 0);
      n = table_influence(3, hits, 64, &emitted); CHECK(n >= 4 && hits[0] / 8 == 3);
      IconLookup lk; CHECK(icon_lookup((uintptr_t)Xb, 6, &lk) == ICON_OK && lk.index == 0);
      put32(Xb + 0x48, COUNT); }
    // Tint: the run's colour bytes come from layouter+0x68.. (floats times 255), which the probe logs.
    { static uint8_t s[SNAP]; int e; use_tint = 1; tint[0] = 0.25f; tint[1] = 0.5f; tint[2] = 0.75f; tint[3] = 1.0f;
      run_once(3, s, &e); CHECK(e);
      printf("tint 0.25,0.5,0.75,1.0 -> run bytes %u %u %u %u\n", Rb[0x20], Rb[0x21], Rb[0x22], Rb[0x23]);
      CHECK(Rb[0x20] == 63 && Rb[0x21] == 127 && Rb[0x22] == 191 && Rb[0x23] == 255);
      use_tint = 0; }
    // Batch limit: the method returns without a run when layouter+0x49c has reached the metrics limit at +0x1a.
    { static uint8_t s[SNAP]; int e; limit_count = 499; run_once(3, s, &e); CHECK(e);
      limit_count = 500; run_once(3, s, &e); CHECK(!e); limit_count = -1; }
    // No source at all: the method does nothing.
    { static uint8_t s[SNAP]; int e; put64(Lb, 0); uint64_t saved = rd64((uintptr_t)Xb + 0); (void)saved;
      memset(Lb, 0, sizeof Lb); memset(Rb, 0, sizeof Rb);
      Cpu c; memset(&c, 0, sizeof c); c.x[0] = (uintptr_t)Lb; c.x[1] = (uintptr_t)Fb; c.x[3] = (uintptr_t)Mb; c.x[4] = 0; c.sp = (uintptr_t)stack + 0x800;
      f_7000a204cc(&c); put64(Lb + 0x60, (uintptr_t)Rb);
      Cpu d; memset(&d, 0, sizeof d); d.sp = (uintptr_t)stack + 0x800; d.x[0] = (uintptr_t)Lb; d.x[1] = 3; f_7000a21afc(&d);
      e = 0; for (int i = 0; i < 0x28; i++) e |= Rb[i] != 0; CHECK(!e); (void)s;
      IconLookup lk; CHECK(icon_lookup(0, 3, &lk) == ICON_NO_SOURCE); }
    printf("oracle: %u code/offset/mode combinations; table words 0..5 of the selected record only; fallback, count, tint, "
           "batch limit and no-source behaviour match the probe lookup\n", total);
    return 0;
}
