// Synthetic guest nodes exercise production policy/binding scope without guest code or game artwork.
#define _GNU_SOURCE   // process_vm_readv: the memory probe below
#include <assert.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#include "../runtime/appendage_glyph.c"
#include "inline_atlas_fixture.h"
static InputMap mapping;
static int pad_layout = PAD_LAYOUT_XBOX, hash_ok = 1, source = GLYPH_SOURCE_PAD, family = GLYPH_PAD_PS;
static const char *preference = "playstation", *appearance_preference = "standard";
static const void *zl_record_ptr;
static uint64_t ticks, serial = 9, storage = 100, observed;
static unsigned observations;
static int probe_kind=-1;
static unsigned probe_handles;
static unsigned last_observed_event;
static void sample_line(const char *text);
// ICON_PROBE_SAMPLE=<file> appends the probe's log lines, so an analysis tool can be checked against the real producer. The
// texture line mirrors the format string of vk_glyph_resource_observe in runtime/vk_texture.c.
static void sample_line(const char *text) {
    const char *path = getenv("ICON_PROBE_SAMPLE");
    FILE *f = path ? fopen(path, "a") : NULL;
    if (f) { fprintf(f, "[host] %s\n", text); fclose(f); }
}
static uint64_t inline_captured_handle, inline_captured_storage;
static int inline_capture_ok = 1;
int vk_inline_capture_source(uint64_t handle, uint64_t storage) {
    inline_captured_handle = handle; inline_captured_storage = storage;
    return inline_capture_ok;
}
void vk_glyph_resource_observe(uint64_t h, unsigned event) {
    assert(h==0x10400338 && event); probe_handles++; last_observed_event=event;
    char line[200];
    snprintf(line, sizeof line, "glyph resource probe %u: original handle %llx texture %u serial %llu storage %llx size %ux%u format %u view %d",
             event, (unsigned long long)h, 813u, 1553ull, 0x48e4f2000ull, 368u, 300u, 77u, 0);
    sample_line(line);
}
const RtFileHash rt_exefs_sha256[] = {{"main","0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026"}};
const int rt_exefs_sha256_count = 1;
const char *cfg_value(const char *name) { return !strcmp(name,"XCX_GLYPH_APPEARANCE") ? appearance_preference : preference; }
const char *cfg_get(const char *name) { (void)name; return NULL; }
static char last_log[2048];   // Same capacity as rt_log, so long probe lines are not cut.
void rt_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(last_log, sizeof last_log, fmt, ap); va_end(ap);
    if (strstr(last_log, "icon probe")) sample_line(last_log);
}
void plat_once(PlatOnce *o, void (*fn)(void *), void *p) { if (!o->state) { o->state = 1; fn(p); } }
void plat_mutex_lock(PlatMutex *m) { (void)m; }
void plat_mutex_unlock(PlatMutex *m) { (void)m; }
// A real probe, like the runtime's: a protected or unmapped page is not readable, so a test that retires a node and removes its
// page proves that nothing dereferences it afterwards.
int plat_mem_readable(uint64_t a, uint64_t n) {
    if (a <= 4096 || n > 4096) return 0;
    static uint8_t scratch[4096];
    struct iovec local = {scratch, n}, remote = {(void *)(uintptr_t)a, n};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)n;
}
uint64_t kern_now_ns(void) { return ticks; }
void input_glyph_snapshot(int *s, int *f, int *l, InputMap *m) { *s=source; *f=family; *l=pad_layout; *m=mapping; }
void sha256(const void *p, size_t n, uint8_t h[32]) {
    if (n == INLINE_RECORDS * INLINE_RECORD_BYTES) {   // Equal to the closed form: the constant; anything else: not it.
        uint8_t want[INLINE_RECORDS * INLINE_RECORD_BYTES]; inline_atlas_records(want);
        static const uint8_t good[32] = INLINE_RECORDS_SHA256;
        if (!memcmp(p, want, n)) memcpy(h, good, 32); else memset(h, 0x5a, 32);
        return;
    }
    if (n == SHEET_RECORDS * INLINE_RECORD_BYTES) {   // The closed-form sheet table: the constant; anything else: not it.
        uint8_t want[SHEET_RECORDS * INLINE_RECORD_BYTES]; inline_sheet_records(want);
        static const uint8_t good[32] = SHEET_RECORDS_SHA256;
        if (!memcmp(p, want, n)) memcpy(h, good, 32); else memset(h, 0x5a, 32);
        return;
    }
    if (n == STICKS_RECORDS * INLINE_RECORD_BYTES) {   // The closed-form stick-direction table: the constant; anything else: not it.
        uint8_t want[STICKS_RECORDS * INLINE_RECORD_BYTES]; inline_sticks_records(want);
        static const uint8_t good[32] = STICKS_RECORDS_SHA256;
        if (!memcmp(p, want, n)) memcpy(h, good, 32); else memset(h, 0x5a, 32);
        return;
    }
    (void)p; assert(n == 128 || n == 84 || n == 28);
    if (n==128 && probe_kind>=0) { memcpy(h,probe_prefixes[probe_kind],32); return; }
    const uint8_t expected[32] = {0xed,0x91,0x64,0xc1,0x3b,0xf5,0x5b,0x99,0x9b,0xf7,0x8b,0xe9,0xd5,0x2d,0x3b,0x65,
      0x71,0x70,0x62,0xc6,0x22,0xdb,0xc8,0xb1,0x31,0x5a,0x30,0xe7,0xd9,0x7d,0x32,0xf7};
    const uint8_t record[32] = {0x34,0x62,0x54,0xa9,0x27,0x7c,0x80,0xdf,0x8e,0x54,0x43,0x79,0xed,0x61,0xe8,0x4c,0xee,0x25,0xb0,0x27,0x45,0x3c,0x68,0xfc,0xa8,0x25,0x58,0x3b,0x34,0xb8,0x04,0xd5};
    const uint8_t x_record[32] = {0xe2,0xc3,0x5a,0xe2,0x8e,0x55,0x45,0xec,0xfb,0x66,0x58,0xb2,0x50,0x3f,0x3e,0x06,0x2f,0xd4,0x21,0x6c,0x43,0x26,0x09,0x0e,0xaf,0x49,0x6e,0xee,0x3b,0x5d,0xa4,0x6d};
    const uint8_t zl_record[32] = {0x7c,0xe0,0xa3,0xa3,0xa8,0x06,0x64,0x7b,0x28,0xe5,0x6f,0x64,0xb5,0x73,0x46,0xa6,0x36,0x05,0x00,0xbd,0xc5,0x42,0x05,0x25,0xca,0x7e,0x5d,0xe9,0x4f,0xad,0x4f,0xa9};
    memcpy(h, n==128 ? expected : n==28 ? (p==zl_record_ptr ? zl_record : x_record) : record, 32); if (!hash_ok) h[0] ^= 1;
}
int vk_appendage_capture_source(uint64_t h) { return appendage_glyph_source(h, serial, storage); }
static void draw(Cpu *c) {
    Cpu saved = *c; c->x[30] = MOD_MAIN_BASE + 0x95c9cc;
    observed = appendage_glyph_handle(c, 0x10400338);
    assert(appendage_glyph_handle(c,0x10400339)==0x10400339);
    c->x[30]++; assert(appendage_glyph_handle(c,0x10400338)==0x10400338);
    c->x[30]=saved.x[30]; assert(!memcmp(c,&saved,sizeof *c)); observations++;
}
static uint64_t node, resolved;
static void select_shared(Cpu *c) {
    Cpu saved=*c;
    _Alignas(16) uint64_t frame[2]={0,c->x[30]};
    c->x[0]=(uintptr_t)&resolved; c->x[29]=(uintptr_t)frame; c->x[30]=MOD_MAIN_BASE+0x55ff20;
    appendage_glyph_lookup_attach(c,c->x[29]); *c=saved;
}
static unsigned live_id(uint64_t node) {
    for (unsigned i=0;i<generation_count;i++) if (live[i].node==node) return i+1;
    return 0;
}
static unsigned live_slot(uint64_t node) { unsigned id=live_id(node); return id ? live[id-1].slot : ~0u; }

static void nested(Cpu *c) {
    Cpu other = *c; other.x[30] = MOD_MAIN_BASE + 0x95c9cc;
    assert(appendage_glyph_handle(&other,0x10400338)==0x10400338);
    appendage_glyph_bind(c,node+1,draw); assert(observed==0x10400338);
    draw(c); assert(appendage_glyph_tagged(observed));
}
static void put64(uint8_t *p, uint64_t v) { memcpy(p,&v,8); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p,&v,2); }
static int original_calls;
static void count_original(Cpu *c) { (void)c; original_calls++; }
uint64_t inline_handle_result;
static void bind_returning_handle(Cpu *c) {   // the BindTexture import inside the guest bind helper
    uint64_t lr = c->x[30]; original_calls++;
    c->x[30] = MOD_MAIN_BASE + 0x95c9cc;
    inline_handle_result = appendage_glyph_handle(c, 0x10400338);
    c->x[30] = lr;
}
// Stands for the guest bind helper: it reaches the BindTexture import, where the existing observer reads the handle.
static void bind_reaching_import(Cpu *c) {
    uint64_t lr = c->x[30];
    original_calls++;
    c->x[30] = MOD_MAIN_BASE + 0x95c9cc;
    (void)appendage_glyph_handle(c, 0x10400338);
    c->x[30] = lr;
}
int main(int argc, char **argv) {
    assert(appendage_glyph_art(APPENDAGE_GLYPH_TAG | (74ull<<32) | (192ull<<44))==74);
    assert(appendage_glyph_tagged(APPENDAGE_GLYPH_TAG | (74ull<<32) | (192ull<<44)));
    assert(!appendage_glyph_tagged(APPENDAGE_GLYPH_TAG | (1ull<<39)));
    assert(argc==2);
    uint8_t *main=mmap((void *)MOD_MAIN_BASE,0x2000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(main==(void *)MOD_MAIN_BASE);
    uint8_t *rect=mmap(NULL,6*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(rect!=MAP_FAILED);
    uint8_t resource[0x1000]={0}, object[0x38]={0}, wrapper[3][0x40]={{0}};
    uint32_t base_record=0x80, relative=0x100; memcpy(resource+0x2c,&base_record,4); memcpy(resource+0x98,&relative,4);
    zl_record_ptr=resource+base_record+relative+15*28;
    uint64_t table=(uintptr_t)object; uint32_t base=824; put64(main+0x1d64f18,(uintptr_t)&base);
    input_map_reset(); mapping=input_map; node=(uintptr_t)rect;
    const uint64_t returns[3]={0x60f690,0x60f6ac,0x60f6c8};
    Cpu c={0};
    for (int i=0;i<3;i++) {
        uint8_t *r=rect+i*4096;
        put64(r,MOD_MAIN_BASE+0x1c94318); r[0x18]=1; put64(r+0xe0,(uintptr_t)resource);
        put64(r+0x330,(uintptr_t)&table); put16(r+0x34c,selectors[i]); put16(r+0x34e,24);
        put64(wrapper[i]+0x38,(uintptr_t)r);
        c.x[0]=(uintptr_t)wrapper[i];c.x[1]=selectors[i];c.x[30]=MOD_MAIN_BASE+returns[i];appendage_glyph_attach(&c);
    }
    Cpu initial=c;
    if (!strcmp(argv[1],"resources")) {
        resource_probe=1;probe_deadline=180000000000ull;
        // Unrelated resources, wrong types and expired captures never enter the diagnostic scope.
        unsigned old_count=generation_count;
        appendage_glyph_bind(&c,node+3*4096,draw);assert(!probe_handles);
        uint8_t *r=rect+3*4096;memcpy(r,rect,0x350);probe_kind=0;
        r[0x18]=7;appendage_glyph_bind(&c,(uintptr_t)r,draw);assert(!probe_handles);r[0x18]=1;
        ticks=probe_deadline;appendage_glyph_bind(&c,(uintptr_t)r,draw);assert(!probe_handles);ticks=0;
        for (int kind=0;kind<4;kind++) {
            probe_kind=kind;
            for (int i=0;i<10;i++) {
                Cpu before=c;appendage_glyph_bind(&c,(uintptr_t)r,draw);
                assert(!memcmp(&c,&before,sizeof c));assert(observed==0x10400338);
            }
            assert(probe_counts[kind]==8);
        }
        assert(probe_handles==32 && probe_events==32 && generation_count==old_count);
        assert(!probe_scope.cpu && !probe_scope.event);
        appendage_glyph_retire((uintptr_t)r);assert(!mprotect(r,4096,PROT_NONE));
        // A later unrelated draw/handle observes no retired address or expired scope.
        probe_kind=-1;draw(&c);assert(probe_handles==32);
        resource_probe=0;
        puts("Bounded resource probe: four filters, unrelated/type rejection, CPU/original passthrough, 32-event/time bounds and no retained node passed");
        return 0;
    }
    if (!strcmp(argv[1],"icon")) {
        inline_enabled=0;   // These fixtures are not the verified atlas; the probe is tested apart from replacement.
        // Synthetic memory laid out as f_7000a21afc and the f_7000a322cc bind sites read it. The layout is a reading of
        // the code; tests/inline_icon_oracle.py checks it against the lifted function itself.
        static _Alignas(16) uint8_t L[0x600], M[0x40], owner[0x80], vec[16], names[3*0x24], Font[0x20], X[0x60], X2[0x60];
        static _Alignas(16) uint8_t T[0x20+8*ICON_REC_BYTES], T2[0x20+8*ICON_REC_BYTES], nodeb[0x400], node2[0x400], geom[0x40];
        static _Alignas(16) uint8_t tset[0x40], ent[2*0x78], ent2[0x78], ctx[0x40];
        put64(L+8,(uintptr_t)owner); put64(owner+0x58,(uintptr_t)vec);
        put64(vec,(uintptr_t)names); put64(vec+8,(uintptr_t)names+sizeof names);
        const uint32_t codes[3]={3,5,0x10002}; const char *tags[3]={"a","x_l","pls"};
        for (int i=0;i<3;i++) { memcpy(names+i*0x24,&codes[i],4); strcpy((char *)names+i*0x24+0x10,tags[i]); }
        put64(L+0x10,(uintptr_t)Font); put64(L+0x18,(uintptr_t)X); put64(L+0x20,(uintptr_t)M);
        put16(M+0x1a,500); { uint32_t n=3; memcpy(L+0x49c,&n,4); }
        { float tint[4]={1.0f,0.5f,0.25f,1.0f}; memcpy(L+0x68,tint,16); }
        for (int k=0;k<2;k++) {
            uint8_t *src=k?X2:X, *tab=k?T2:T;
            put64(src+0x38,(uintptr_t)tab); { uint32_t cnt=8; memcpy(src+0x48,&cnt,4); }
            { float w=2.0f,h=3.0f; memcpy(src+0x4c,&w,4); memcpy(src+0x50,&h,4); }
            { uint32_t off=0x20; memcpy(tab+0x18,&off,4); }
            for (int i=0;i<8;i++) for (int w=0;w<ICON_REC_WORDS;w++) {
                float v=k*1000.0f+i*10.0f+w+0.5f; memcpy(tab+0x20+i*ICON_REC_BYTES+w*4,&v,4);
            }
        }
        put64(X+0x40,(uintptr_t)tset); put64(tset+0x30,(uintptr_t)ent);
        for (unsigned i=0;i<sizeof ent/8;i++) put64(ent+i*8,0xe00000000ull+i);
        put64(nodeb+0x3b8,(uintptr_t)X); put64(nodeb+0x308,(uintptr_t)geom); nodeb[0xf2]=3;
        { uint32_t second=2, glyph=17; memcpy(geom+0x30,&second,4); memcpy(geom+0x34,&glyph,4); }
        put64(node2+0x3b8,(uintptr_t)X2); put64(node2+0x308,(uintptr_t)geom);
        IconLookup lk;
        // Lookup: stride, masking, out-of-range fallback to record 0, and the degenerate sources.
        assert(icon_lookup((uintptr_t)X,3,&lk)==ICON_OK&&lk.index==3&&lk.record==(uintptr_t)T+0x20+3*ICON_REC_BYTES&&lk.count==8);
        assert(lk.w==2.0f&&lk.h==3.0f);
        assert(icon_lookup((uintptr_t)X,7,&lk)==ICON_OK&&lk.index==7);
        assert(icon_lookup((uintptr_t)X,8,&lk)==ICON_OK&&lk.index==0);          // Not below the count: record 0.
        assert(icon_lookup((uintptr_t)X,0xffff,&lk)==ICON_OK&&lk.index==0);
        assert(icon_lookup((uintptr_t)X,0x10005,&lk)==ICON_OK&&lk.index==5);    // Only the low 16 bits select.
        { uint32_t zero=0; memcpy(X+0x48,&zero,4); assert(icon_lookup((uintptr_t)X,3,&lk)==ICON_OK&&lk.index==0);
          uint32_t cnt=8; memcpy(X+0x48,&cnt,4); }
        assert(icon_lookup(0,3,&lk)==ICON_NO_SOURCE);
        assert(icon_lookup(16,3,&lk)==ICON_UNREADABLE);
        { uint64_t saved=rd64((uintptr_t)X+0x38); put64(X+0x38,0);
          assert(icon_lookup((uintptr_t)X,3,&lk)==ICON_NO_TABLE&&lk.count==8&&lk.w==2.0f&&!lk.record);
          put64(X+0x38,16); assert(icon_lookup((uintptr_t)X,3,&lk)==ICON_UNREADABLE); put64(X+0x38,saved); }

        // Layout probe.
        Cpu c={0}; c.x[0]=(uintptr_t)L; c.x[1]=5; c.x[30]=MOD_MAIN_BASE+0xa2353c+0x14;
        icon_probe=1; icon_deadline=~0ull; icon_layout_events=icon_bind_events=icon_source_count=0;
        icon_layout_calls=icon_bind_calls=0; ticks=0;
        Cpu before=c;
        appendage_glyph_icon_layout(&c);
        assert(!memcmp(&before,&c,sizeof c));
        assert(icon_layout_events==1&&icon_layout_calls==1&&icon_source_count==1&&icon_sources[0]==(uintptr_t)X);
        assert(strstr(last_log,"layout code 5 key 5 tag x_l")&&strstr(last_log,"status ok count 8 size 2.000x3.000 index 5"));
        { char want[64]; float v=5*10.0f+0.5f; uint32_t b; memcpy(&b,&v,4); snprintf(want,sizeof want,"words %08x ",b);
          assert(strstr(last_log,want)); }   // Word 0 of record 5.
        assert(strstr(last_log,"tint 1.00,0.50,0.25,1.00")&&strstr(last_log,"batch 3/500"));
        appendage_glyph_icon_layout(&c); assert(icon_layout_events==1&&icon_layout_calls==2);   // Same pair is not repeated.
        c.x[1]=0x10002; appendage_glyph_icon_layout(&c);
        assert(icon_layout_events==2&&strstr(last_log,"layout code 65538 key 2 tag pls")&&strstr(last_log,"index 2"));
        c.x[1]=9; appendage_glyph_icon_layout(&c);   // A code with no table entry is still recorded, as record 0.
        assert(icon_layout_events==3&&strstr(last_log,"tag ?")&&strstr(last_log,"index 0"));
        put64(L+0x18,(uintptr_t)X2); c.x[1]=5; appendage_glyph_icon_layout(&c);       // Another source, same code.
        assert(icon_layout_events==4&&icon_source_count==2&&strstr(last_log,"floats 1050.5"));
        put64(L+0x18,0); appendage_glyph_icon_layout(&c);                              // No source: contradicting data kept.
        assert(icon_layout_events==5&&strstr(last_log,"status no-source")&&icon_source_count==2);
        put64(L+0x18,(uintptr_t)X);
        c.x[0]=16; appendage_glyph_icon_layout(&c);
        assert(strstr(last_log,"unreadable")&&icon_layout_events==5);
        c.x[0]=(uintptr_t)L;
        for (uint32_t k=100;k<100+2*ICON_LAYOUT_EVENTS;k++) { c.x[1]=k; appendage_glyph_icon_layout(&c); }
        assert(icon_layout_events==ICON_LAYOUT_EVENTS&&icon_layout_calls>ICON_LAYOUT_EVENTS);   // Bounded, still counted.
        ticks=icon_deadline=1; { unsigned calls=icon_layout_calls; appendage_glyph_icon_layout(&c); assert(icon_layout_calls==calls); }
        icon_deadline=~0ull; ticks=0; icon_probe=0;
        { unsigned calls=icon_layout_calls; appendage_glyph_icon_layout(&c); assert(icon_layout_calls==calls); }
        icon_probe=1;

        // Bind wrapper. A source seen at layout, entry 0 as x1, the second-pass draw count in the geometry.
        icon_layout_events=icon_source_count=0; icon_sources[0]=(uintptr_t)X; icon_source_count=1;
        Cpu b={0}; b.x[0]=(uintptr_t)ctx; b.x[1]=(uintptr_t)ent; b.x[2]=3; b.x[3]=0; b.x[19]=(uintptr_t)nodeb;
        b.x[30]=MOD_MAIN_BASE+0xa325fc;
        ProbeScope outer={&b,55,1}; probe_scope=outer;
        original_calls=0; probe_handles=0; last_observed_event=0; before=b;
        appendage_glyph_icon_bind(&b,bind_reaching_import);
        assert(original_calls==1&&!memcmp(&before,&b,sizeof b));
        assert(probe_scope.cpu==outer.cpu&&probe_scope.event==outer.event&&probe_scope.deadline==outer.deadline);   // Restored.
        assert(icon_bind_events==1&&last_observed_event==ICON_BIND_EVENT_BASE+1&&probe_handles==1);
        // last_log holds the latest line, which is the bind line (the observer stub does not log).
        assert(strstr(last_log,"icon probe bind 1: site ")&&strstr(last_log," seen at layout; source ok count 8 size 2.000x3.000"));
        assert(strstr(last_log,"== entry0; slot 3 flag 0 draw counts glyph 17 second 2")&&strstr(last_log,"resource probe 201"));
        assert(strstr(last_log,"e00000000 e00000001 e00000002"));
        // Negative controls: the register check must be able to fail.
        b.x[1]=(uintptr_t)ent+0x78; appendage_glyph_icon_bind(&b,bind_reaching_import);
        assert(icon_bind_events==2&&strstr(last_log,"!= entry0"));
        b.x[1]=(uintptr_t)ent; b.x[19]=(uintptr_t)node2; appendage_glyph_icon_bind(&b,bind_reaching_import);   // Unseen source.
        assert(icon_bind_events==3&&strstr(last_log,"NOT seen at layout")&&strstr(last_log,"!= entry0"));
        // Repeats are not logged again; the original still runs and the observer scope stays unarmed.
        b.x[19]=(uintptr_t)nodeb; original_calls=0; probe_handles=0;
        appendage_glyph_icon_bind(&b,bind_reaching_import);
        assert(original_calls==1&&icon_bind_events==3&&!probe_handles&&icon_bind_calls==4);
        // A bind that never reaches the import is reported, not silently dropped.
        uint64_t site=MOD_MAIN_BASE+0xa32690;   // Events are keyed on site, source and entry; slot alone is not part of the key.
        b.x[2]=4; b.x[30]=++site; appendage_glyph_icon_bind(&b,count_original);
        assert(icon_bind_events==4&&strstr(last_log,"no BindTexture import reached"));
        // Unrelated or invalid nodes: the original runs once, CPU state is untouched, and no pointer is chased.
        b.x[2]=5; b.x[19]=16; b.x[30]=++site; before=b; original_calls=0;
        appendage_glyph_icon_bind(&b,bind_reaching_import);
        assert(original_calls==1&&!memcmp(&before,&b,sizeof b)&&strstr(last_log,"bind 5:")&&strstr(last_log,"source no-source"));
        put64(nodeb+0x3b8,0); b.x[19]=(uintptr_t)nodeb; b.x[2]=6; b.x[30]=++site; original_calls=0;
        appendage_glyph_icon_bind(&b,bind_reaching_import);
        assert(original_calls==1&&strstr(last_log,"source no-source")&&strstr(last_log,"x1 ")&&strstr(last_log,"!= entry0"));
        put64(nodeb+0x3b8,(uintptr_t)X);
        put64(nodeb+0x308,0); b.x[2]=7; b.x[30]=++site; appendage_glyph_icon_bind(&b,bind_reaching_import);
        assert(strstr(last_log,"draw counts glyph 0 second 0"));
        put64(nodeb+0x308,(uintptr_t)geom);
        // Source with a texture set that is missing or unreadable.
        put64(X2+0x40,0); b.x[19]=(uintptr_t)node2; b.x[2]=8; b.x[30]=++site; appendage_glyph_icon_bind(&b,bind_reaching_import);
        assert(strstr(last_log,"set 0 entry0 0"));
        put64(X2+0x40,16); b.x[2]=9; b.x[30]=++site; appendage_glyph_icon_bind(&b,bind_reaching_import); assert(strstr(last_log,"entry0 0"));
        for (unsigned k=0;k<2*ICON_BIND_EVENTS;k++) { b.x[2]=100+k; b.x[19]=(uintptr_t)nodeb; b.x[1]=k+1; appendage_glyph_icon_bind(&b,count_original); }
        assert(icon_bind_events==ICON_BIND_EVENTS&&icon_bind_calls>ICON_BIND_EVENTS);
        // Disabled or expired: pass-through only, nothing counted or logged.
        last_log[0]=0; { unsigned calls=icon_bind_calls; original_calls=0;
          icon_probe=0; appendage_glyph_icon_bind(&b,count_original);
          icon_probe=1; ticks=icon_deadline=1; appendage_glyph_icon_bind(&b,count_original);
          assert(original_calls==2&&icon_bind_calls==calls&&!last_log[0]); }
        icon_probe=0; ticks=0; icon_deadline=0;
        puts("Icon probe: lookup stride/masking/fallback/degenerate sources, layout and bind events with dedup, bounds and "
             "counters, register self-check with negative controls, observer correlation, restore and pass-through passed");
        return 0;
    }
    if (!strcmp(argv[1],"inline")) {
        // ---- policy: the eight inline actions, translated through the layout and current bindings
        int16_t art[GLYPH_INLINE_SET], other[GLYPH_INLINE_SET];
        input_map_reset(); mapping=input_map; source=GLYPH_SOURCE_PAD; family=GLYPH_PAD_PS; pad_layout=PAD_LAYOUT_XBOX;
        glyph_decide_inline_set(GLYPH_PLAYSTATION,source,family,pad_layout,&mapping,GLYPH_STANDARD,art);
        for (int i=0;i<GLYPH_INLINE_SET;i++) assert(art[i]>=0);
        for (int i=0;i<GLYPH_INLINE_SET;i++) for (int j=i+1;j<GLYPH_INLINE_SET;j++) assert(art[i]!=art[j]);
        glyph_decide_inline_set(GLYPH_PLAYSTATION,source,family,PAD_LAYOUT_NINTENDO,&mapping,GLYPH_STANDARD,other);
        // Button layout moves which face button is Switch A/B/X/Y; the shoulders and triggers do not move.
        assert(other[0]!=art[0]&&other[1]!=art[1]&&other[2]!=art[2]&&other[3]!=art[3]);
        for (int i=4;i<8;i++) assert(other[i]==art[i]);
        assert(other[0]==art[1]&&other[1]==art[0]);   // A and B swap places with the layout
        glyph_decide_inline_set(GLYPH_PLAYSTATION,source,family,pad_layout,&mapping,GLYPH_COLORED,other);
        for (int i=0;i<4;i++) assert(other[i]>=0&&other[i]!=art[i]&&glyph_art_variant(art[i],GLYPH_COLORED)==other[i]);   // Colored faces
        for (int i=4;i<8;i++) assert(other[i]==art[i]);                                                                     // no colored shoulders
        glyph_decide_inline_set(GLYPH_ORIGINAL,source,family,pad_layout,&mapping,GLYPH_STANDARD,other);
        for (int i=0;i<GLYPH_INLINE_SET;i++) assert(other[i]==-1);
        glyph_decide_inline_set(GLYPH_AUTO,source,GLYPH_PAD_SWITCH,pad_layout,&mapping,GLYPH_STANDARD,other);   // native controller
        for (int i=0;i<GLYPH_INLINE_SET;i++) assert(other[i]==-1);
        glyph_decide_inline_set(GLYPH_AUTO,GLYPH_SOURCE_KEYBOARD,family,pad_layout,&mapping,GLYPH_STANDARD,other);
        { int kb=0; for (int i=0;i<GLYPH_INLINE_SET;i++) kb+=other[i]>=0; assert(kb>0); }                      // letters have tiles
        // One shared physical input keeps only the affected actions native; the others stay replaced.
        { InputMap shared=mapping; int l=-1,r=-1;
          for (int i=0;i<shared.npads[pad_layout];i++) { if (shared.pads[pad_layout][i].action==B_L) l=i; if (shared.pads[pad_layout][i].action==B_R) r=i; }
          assert(l>=0&&r>=0); shared.pads[pad_layout][r].pad=shared.pads[pad_layout][l].pad;
          glyph_decide_inline_set(GLYPH_PLAYSTATION,source,family,pad_layout,&shared,GLYPH_STANDARD,other);
          assert(other[4]==-1&&other[5]==-1&&other[0]==art[0]&&other[1]==art[1]&&other[6]==art[6]&&other[7]==art[7]);
          InputMap unbound=mapping; for (int i=0;i<unbound.npads[pad_layout];i++) if (unbound.pads[pad_layout][i].action==B_ZL) unbound.pads[pad_layout][i].pad=PAD_NONE;
          glyph_decide_inline_set(GLYPH_PLAYSTATION,source,family,pad_layout,&unbound,GLYPH_STANDARD,other);
          assert(other[6]==-1&&other[7]==art[7]&&other[0]==art[0]); }

        // ---- tile fitting: art stays inside the native glyph box of every replaceable cell of both layouts, centred
        { static uint32_t cell[88*88];
          for (int layout=0;layout<INLINE_LAYOUTS;layout++) {
              const int cw=layout==2?STICKS_CELL:layout==1?SHEET_CELL:INLINE_CELL_W, ch=layout==2?STICKS_CELL:layout==1?SHEET_CELL:INLINE_CELL_H;
              const int records=layout==2?STICKS_RECORDS:layout==1?SHEET_RECORDS:INLINE_RECORDS;
              for (int k=0;k<records;k++) {
                  int sym=layout==2?SYM_SLV+k:k%SYM_PER_SHEET; if (!inline_symbol_replaceable((unsigned)sym)) continue;
                  const InlineBox *ic=layout==2?&inline_stick_boxes[k]:layout==1?&inline_sheet_boxes[k]:&inline_font_boxes[k/SYM_PER_SHEET][sym];
                  for (unsigned a=0;a<glyph_art_count();a++) {
                      assert(glyph_art_cell(a,cw,ch,ic->x,ic->y,ic->w,ic->h,cell));
                      int x0=cw,y0=ch,x1=-1,y1=-1; unsigned opaque=0;
                      for (int y=0;y<ch;y++) for (int x=0;x<cw;x++) if (cell[y*cw+x]>>24) {
                          opaque++; if (x<x0) x0=x; if (x>x1) x1=x; if (y<y0) y0=y; if (y>y1) y1=y; }
                      assert(opaque>20&&x0>=ic->x&&y0>=ic->y&&x1<ic->x+ic->w&&y1<ic->y+ic->h);       // inside the native box
                      assert(x1-x0+1>=ic->w*6/10||y1-y0+1>=ic->h*6/10);                             // and not tiny
                      assert(2*x0+(x1-x0)>=2*ic->x+ic->w-3&&2*x0+(x1-x0)<=2*ic->x+ic->w+3);          // centred horizontally
                  }
              }
          } }
        assert(!glyph_art_inline_cell(glyph_art_count(),8,10,30,30,(uint32_t[INLINE_CELL_W*INLINE_CELL_H]){0}));
        assert(!glyph_art_inline_cell(0,40,10,30,30,(uint32_t[INLINE_CELL_W*INLINE_CELL_H]){0}));   // box leaves the cell
        assert(!glyph_art_cell(0,64,64,40,40,30,30,(uint32_t[64*64]){0})&&!glyph_art_cell(0,129,64,0,0,8,8,(uint32_t[128*64]){0}));

        // ---- source identity, bind arming and the handle
        static _Alignas(16) uint8_t X[0x60], T[0x20+INLINE_RECORDS*INLINE_RECORD_BYTES], tset[0x40], entries[2*0x78], nodeb[0x400];
        inline_atlas_records(T+0x20); { uint32_t off=0x20; memcpy(T+0x18,&off,4); }
        put64(X+0x38,(uintptr_t)T); { uint32_t cnt=INLINE_RECORDS; memcpy(X+0x48,&cnt,4); float a=INLINE_SOURCE_SCALE_A,b=INLINE_SOURCE_SCALE_B; memcpy(X+0x4c,&a,4); memcpy(X+0x50,&b,4); }
        put64(X+0x40,(uintptr_t)tset); put64(tset+0x30,(uintptr_t)entries);
        const uint64_t storage=0x48e5e5000ull;
        put64(entries,INLINE_ENTRY_Q0); put64(entries+0x20,storage);
        put64(nodeb+0x3b8,(uintptr_t)X);
        IconLookup lk;
        assert(inline_source_ok((uintptr_t)X,&lk)&&lk.count==INLINE_RECORDS);
        { uint8_t w[4]; memcpy(w,T+0x20+7*INLINE_RECORD_BYTES+16,4); T[0x20+7*INLINE_RECORD_BYTES+16]^=1; assert(!inline_source_ok((uintptr_t)X,&lk)); memcpy(T+0x20+7*INLINE_RECORD_BYTES+16,w,4); }
        { float scale=49.0f; float keep; memcpy(&keep,X+0x4c,4); memcpy(X+0x4c,&scale,4); assert(!inline_source_ok((uintptr_t)X,&lk)); memcpy(X+0x4c,&keep,4); }
        { uint32_t cnt=47; memcpy(X+0x48,&cnt,4); assert(!inline_source_ok((uintptr_t)X,&lk)); cnt=INLINE_RECORDS; memcpy(X+0x48,&cnt,4); }
        assert(!inline_source_ok(0,&lk)&&inline_source_ok((uintptr_t)X,&lk));

        extern uint64_t inline_handle_result;
        Cpu b={0}; b.x[19]=(uintptr_t)nodeb; b.x[1]=(uintptr_t)entries; b.x[2]=3; b.x[30]=MOD_MAIN_BASE+0xa325fc;
        icon_probe=0; ticks=0; inline_sig_count=0; inline_full_logged=inline_source_logged=inline_entry_logged=0; memset(inline_sigs,0,sizeof inline_sigs);
        preference="playstation"; appearance_preference="standard"; family=GLYPH_PAD_PS; source=GLYPH_SOURCE_PAD; pad_layout=PAD_LAYOUT_XBOX;
        inline_enabled=1; inline_capture_ok=1;
        Cpu before=b; original_calls=0; probe_scope=(ProbeScope){0};
        appendage_glyph_icon_bind(&b,bind_returning_handle);
        assert(original_calls==1&&!memcmp(&before,&b,sizeof b));
        assert(appendage_inline_tagged(inline_handle_result)&&appendage_inline_id(inline_handle_result)==1);
        assert((inline_handle_result&0xffffffffu)==0x10400338u&&!appendage_glyph_tagged(inline_handle_result)&&appendage_handle_tagged(inline_handle_result));
        assert(inline_captured_handle==inline_handle_result&&inline_captured_storage==storage);
        assert(!inline_scope.cpu&&!inline_scope.id);                                  // The scope does not leak out of the bind.
        assert(inline_sig_count==1&&strstr(last_log,"inline glyph: text signature 1:")&&strstr(last_log,"a=audune_playstation_south.png"));
        { int16_t got[GLYPH_SYMBOLS], all[GLYPH_SYMBOLS]; int layout=-1;
          assert(appendage_inline_signature(1,got,&layout)&&layout==INLINE_LAYOUT_FONT&&!appendage_inline_signature(0,got,&layout)&&!appendage_inline_signature(2,got,&layout));
          glyph_decide_symbol_set(GLYPH_PLAYSTATION,source,family,pad_layout,&mapping,GLYPH_STANDARD,all);
          for (int i=0;i<GLYPH_SYMBOLS;i++) assert(got[i]==(i<SYM_PER_SHEET&&inline_symbol_replaceable((unsigned)i)?all[i]:-1)); }   // the stick-image symbols belong to another layout
        // Repeat: same signature, no new log, same handle. A copied command keeps its own handle value.
        last_log[0]=0; inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle);
        assert(appendage_inline_id(inline_handle_result)==1&&inline_sig_count==1&&!last_log[0]);
        // Source validation by registration identity: the first capture fixes serial/storage, later mismatches fail.
        { uint64_t h=inline_handle_result;
          assert(appendage_inline_source(h,9,storage)&&appendage_inline_source(h,9,storage));
          assert(!appendage_inline_source(h,10,storage)&&!appendage_inline_source(h,9,storage+0x1000)&&!appendage_inline_source(h,0,storage));
          assert(!appendage_inline_source(0x10400338,9,storage)&&!appendage_inline_source(INLINE_GLYPH_TAG|(2ull<<32)|0x10400338,9,storage)); }
        // A style change selects another signature without touching the previous one.
        preference="xbox"; inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle);
        assert(appendage_inline_id(inline_handle_result)==2&&inline_sig_count==2&&strstr(last_log,"a=audune_xbox_south.png"));
        pad_layout=PAD_LAYOUT_NINTENDO; inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle);
        assert(appendage_inline_id(inline_handle_result)==3);                          // A moved with the physical layout
        pad_layout=PAD_LAYOUT_XBOX; preference="playstation"; inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle);
        assert(appendage_inline_id(inline_handle_result)==1);
        // Original, unsupported and failed captures keep the native handle.
        preference="original"; inline_handle_result=0; original_calls=0; appendage_glyph_icon_bind(&b,bind_returning_handle);
        assert(original_calls==1&&inline_handle_result==0x10400338u);
        preference="auto"; family=GLYPH_PAD_SWITCH; inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle);
        assert(inline_handle_result==0x10400338u);                                   // native Nintendo artwork is never relabelled
        preference="playstation"; family=GLYPH_PAD_PS; inline_capture_ok=0; inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle);
        assert(inline_handle_result==0x10400338u); inline_capture_ok=1;
        inline_enabled=0; inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle); assert(inline_handle_result==0x10400338u); inline_enabled=1;
        // Wrong source, wrong entry, wrong word, unreadable node, a text node with no icon source: native handle, original runs.
        { uint64_t keep=rd64((uintptr_t)nodeb+0x3b8);
          T[0x20+3]^=0x40; inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle);
          assert(inline_handle_result==0x10400338u&&strstr(last_log,"did not match the verified atlas")); T[0x20+3]^=0x40;
          put64(nodeb+0x3b8,0); inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle); assert(inline_handle_result==0x10400338u);
          put64(nodeb+0x3b8,keep);
          Cpu wrong=b; wrong.x[1]=(uintptr_t)entries+0x78; inline_handle_result=0; appendage_glyph_icon_bind(&wrong,bind_returning_handle);
          assert(inline_handle_result==0x10400338u&&strstr(last_log,"not entry 0"));
          uint64_t q0=INLINE_ENTRY_Q0^1; put64(entries,q0); inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle); assert(inline_handle_result==0x10400338u);
          put64(entries,INLINE_ENTRY_Q0); put64(entries+0x20,0); inline_handle_result=0; appendage_glyph_icon_bind(&b,bind_returning_handle); assert(inline_handle_result==0x10400338u);
          put64(entries+0x20,storage);
          Cpu bad=b; bad.x[19]=16; original_calls=0; inline_handle_result=0; appendage_glyph_icon_bind(&bad,bind_returning_handle);
          assert(original_calls==1&&inline_handle_result==0x10400338u); }
        // A BindTexture import that is not the one the scope names: wrong return address, tagged handle, other CPU.
        { Cpu other=b; other.x[30]=MOD_MAIN_BASE+0x95c9cc; assert(appendage_glyph_handle(&other,0x10400338)==0x10400338); }
        // Bounded table: combinations beyond the limit keep the native atlas, and say so once. Existing ids stay valid.
        { unsigned have=inline_sig_count; inline_full_logged=0; int16_t set[GLYPH_SYMBOLS]; unsigned granted=0, refused=0;
          for (unsigned k=0;k<INLINE_SIGNATURES+10;k++) {
              memset(set,-1,sizeof set); set[k%GLYPH_SYMBOLS]=(int16_t)(k/GLYPH_SYMBOLS); set[(k+3)%GLYPH_SYMBOLS]=(int16_t)(k%7);   // distinct by construction
              last_log[0]=0; unsigned id=inline_signature_id(INLINE_LAYOUT_FONT,set);
              if (id) granted++; else { refused++; assert(strstr(last_log,"signatures in use")||!last_log[0]); }
          }
          assert(inline_sig_count==INLINE_SIGNATURES&&refused>0&&granted+have>=INLINE_SIGNATURES);
          int16_t again[GLYPH_SYMBOLS]; int lay; assert(appendage_inline_signature(1,again,&lay)&&lay==INLINE_LAYOUT_FONT);   // earlier ids are intact
          unsigned count=inline_sig_count; memset(set,-1,sizeof set); set[0]=(int16_t)(glyph_art_count()-1); set[7]=0;
          assert(!inline_signature_id(INLINE_LAYOUT_FONT,set)&&inline_sig_count==count);                       // still refused, no growth
          int16_t known[GLYPH_SYMBOLS]; assert(appendage_inline_signature(3,known,&lay)&&inline_signature_id(lay,known)==3); }   // known set still resolves

        // ---- symbols beyond the eight buttons: plus/minus, D-pad directions and groups, sticks and stick presses
        { int16_t s[GLYPH_SYMBOLS];
          #define NAME(sym) glyph_art_name((unsigned)s[sym])
          input_map_reset(); mapping=input_map; source=GLYPH_SOURCE_PAD; family=GLYPH_PAD_PS; pad_layout=PAD_LAYOUT_XBOX;
          glyph_decide_symbol_set(GLYPH_PLAYSTATION,source,family,pad_layout,&mapping,GLYPH_STANDARD,s);
          for (int i=0;i<GLYPH_SYMBOLS;i++) assert(inline_symbol_replaceable((unsigned)i) ? s[i]>=0 : s[i]==-1);   // power, home, capture stay native
          assert(!strcmp(NAME(SYM_PLS),"audune_playstation_start.png")&&!strcmp(NAME(SYM_MNS),"audune_playstation_back.png"));
          assert(!strcmp(NAME(SYM_CRS),"audune_playstation_dpad.png")&&!strcmp(NAME(SYM_CRSUD),"audune_playstation_dpad_v.png")&&!strcmp(NAME(SYM_CRSLR),"audune_playstation_dpad_h.png"));
          assert(!strcmp(NAME(SYM_CRSU),"audune_playstation_dpad_up.png")&&!strcmp(NAME(SYM_CRSD),"audune_playstation_dpad_down.png")&&
                 !strcmp(NAME(SYM_CRSL),"audune_playstation_dpad_left.png")&&!strcmp(NAME(SYM_CRSR),"audune_playstation_dpad_right.png"));
          assert(!strcmp(NAME(SYM_SLV),"audune_playstation_stick_l_v.png")&&!strcmp(NAME(SYM_SLH),"audune_playstation_stick_l_h.png")&&
                 !strcmp(NAME(SYM_SRV),"audune_playstation_stick_r_v.png")&&!strcmp(NAME(SYM_SRH),"audune_playstation_stick_r_h.png"));
          assert(!strcmp(NAME(SYM_ANAL),"audune_playstation_stick_l.png")&&!strcmp(NAME(SYM_ANAR),"audune_playstation_stick_r.png")&&
                 !strcmp(NAME(SYM_ANALP),"audune_playstation_lstick.png")&&!strcmp(NAME(SYM_ANARP),"audune_playstation_rstick.png"));
          for (int i=0;i<GLYPH_SYMBOLS;i++) for (int j=i+1;j<GLYPH_SYMBOLS;j++) assert(s[i]<0||s[j]<0||s[i]!=s[j]);   // every symbol its own tile
          glyph_decide_symbol_set(GLYPH_XBOX,source,GLYPH_PAD_XBOX,pad_layout,&mapping,GLYPH_STANDARD,s);
          assert(!strcmp(NAME(SYM_PLS),"audune_xbox_start.png")&&!strcmp(NAME(SYM_MNS),"audune_xbox_back.png")&&!strcmp(NAME(SYM_CRS),"audune_xbox_dpad.png")&&!strcmp(NAME(SYM_ANAR),"audune_xbox_stick_r.png"));
          glyph_decide_symbol_set(GLYPH_STEAM,source,GLYPH_PAD_STEAM,pad_layout,&mapping,GLYPH_STANDARD,s);
          assert(!strcmp(NAME(SYM_PLS),"controller_button_options.png")&&!strcmp(NAME(SYM_CRSLR),"steam_dpad_horizontal_outline.png")&&!strcmp(NAME(SYM_ANAL),"steamdeck_stick_l.png")&&!strcmp(NAME(SYM_SRH),"steamdeck_stick_r_horizontal.png"));
          // Rebinding follows the binding, not the tag: swapped D-pad directions keep the group symbols and swap the direction tiles.
          { InputMap m=mapping; int ul=-1,ur=-1;
            for (int i=0;i<m.npads[pad_layout];i++) { if (m.pads[pad_layout][i].action==B_LEFT) ul=i; if (m.pads[pad_layout][i].action==B_RIGHT) ur=i; }
            assert(ul>=0&&ur>=0); uint8_t keep=m.pads[pad_layout][ul].pad; m.pads[pad_layout][ul].pad=m.pads[pad_layout][ur].pad; m.pads[pad_layout][ur].pad=keep;
            glyph_decide_symbol_set(GLYPH_PLAYSTATION,source,family,pad_layout,&m,GLYPH_STANDARD,s);
            assert(!strcmp(NAME(SYM_CRS),"audune_playstation_dpad.png")&&!strcmp(NAME(SYM_CRSLR),"audune_playstation_dpad_h.png"));
            assert(!strcmp(NAME(SYM_CRSL),"audune_playstation_dpad_right.png")&&!strcmp(NAME(SYM_CRSR),"audune_playstation_dpad_left.png"));
            // Up moved onto a face button another action also uses: the shared input and the mixed group keep native, the rest stay.
            InputMap n=mapping; int up=-1; for (int i=0;i<n.npads[pad_layout];i++) if (n.pads[pad_layout][i].action==B_UP) up=i;
            assert(up>=0); n.pads[pad_layout][up].pad=PAD_NORTH;
            glyph_decide_symbol_set(GLYPH_PLAYSTATION,source,family,pad_layout,&n,GLYPH_STANDARD,s);
            assert(s[SYM_CRS]==-1&&s[SYM_CRSUD]==-1&&s[SYM_CRSU]==-1&&s[SYM_CRSLR]>=0&&s[SYM_CRSD]>=0&&s[SYM_CRSL]>=0);
            // Unbound minus and down: only those symbols (and the groups containing down) fall back.
            InputMap o=mapping; for (int i=0;i<o.npads[pad_layout];i++) if (o.pads[pad_layout][i].action==B_DOWN||o.pads[pad_layout][i].action==B_MINUS) o.pads[pad_layout][i].pad=PAD_NONE;
            glyph_decide_symbol_set(GLYPH_PLAYSTATION,source,family,pad_layout,&o,GLYPH_STANDARD,s);
            assert(s[SYM_MNS]==-1&&s[SYM_CRSD]==-1&&s[SYM_CRS]==-1&&s[SYM_CRSUD]==-1&&s[SYM_PLS]>=0&&s[SYM_CRSU]>=0&&s[SYM_CRSLR]>=0&&s[SYM_ANAL]>=0); }
          // Keyboard: the bound keys, one key per symbol. Groups and the sticks as axes have no single key and stay native.
          glyph_decide_symbol_set(GLYPH_KEYBOARD,GLYPH_SOURCE_KEYBOARD,family,pad_layout,&mapping,GLYPH_STANDARD,s);
          assert(!strcmp(NAME(SYM_PLS),"keyboard_tab.png")&&!strcmp(NAME(SYM_MNS),"keyboard_m.png")&&!strcmp(NAME(SYM_ANALP),"keyboard_ctrl.png")&&
                 !strcmp(NAME(SYM_ANARP),"keyboard_c.png")&&!strcmp(NAME(SYM_CRSU),"keyboard_2.png")&&!strcmp(NAME(SYM_CRSL),"keyboard_1.png"));
          // The sticks and the D-pad groups: the cluster the default keys form (left stick WASD, right stick the arrow keys, D-pad 1-4).
          assert(!strcmp(NAME(SYM_ANAL),"keyboard_cluster_wasd.png")&&!strcmp(NAME(SYM_SLV),"keyboard_cluster_wasd_vertical.png")&&
                 !strcmp(NAME(SYM_SLH),"keyboard_cluster_wasd_horizontal.png"));
          assert(!strcmp(NAME(SYM_ANAR),"keyboard_cluster_arrows.png")&&!strcmp(NAME(SYM_SRV),"keyboard_cluster_arrows_vertical.png")&&
                 !strcmp(NAME(SYM_SRH),"keyboard_cluster_arrows_horizontal.png"));
          assert(!strcmp(NAME(SYM_CRS),"keyboard_cluster_1234.png")&&!strcmp(NAME(SYM_CRSUD),"keyboard_cluster_1234_vertical.png")&&
                 !strcmp(NAME(SYM_CRSLR),"keyboard_cluster_1234_horizontal.png"));
          for (int i=0;i<GLYPH_SYMBOLS;i++) for (int j=i+1;j<GLYPH_SYMBOLS;j++) assert(s[i]<0||s[j]<0||s[i]!=s[j]);
          // The cluster follows the binding: with the up keys of the two sticks swapped neither stick forms a cluster, but the
          // left/right pairs, whose keys are untouched, still do. A direction moved off its key or bound twice keeps only the groups
          // that contain it native.
          { InputMap m=mapping, w=mapping; int16_t t[GLYPH_SYMBOLS];
            for (int i=0;i<m.nkeys;i++) { if (m.keys[i].key==KEY_W) m.keys[i].key=KEY_I;
                                          if (m.keys[i].key==KEY_UP) m.keys[i].key=KEY_W; if (m.keys[i].key==KEY_I) m.keys[i].key=KEY_UP; }
            glyph_decide_symbol_set(GLYPH_KEYBOARD,GLYPH_SOURCE_KEYBOARD,family,pad_layout,&m,GLYPH_STANDARD,t);
            assert(t[SYM_ANAL]==-1&&t[SYM_SLV]==-1&&t[SYM_ANAR]==-1&&t[SYM_SRV]==-1&&t[SYM_SLH]>=0&&t[SYM_SRH]>=0);   // up and down swapped: horizontal pairs hold
            for (int i=0;i<w.nkeys;i++) if (w.keys[i].action==B_UP) w.keys[i].key=KEY_5;   // D-pad up moves to 5
            glyph_decide_symbol_set(GLYPH_KEYBOARD,GLYPH_SOURCE_KEYBOARD,family,pad_layout,&w,GLYPH_STANDARD,t);
            assert(t[SYM_CRS]==-1&&t[SYM_CRSUD]==-1&&t[SYM_CRSLR]>=0&&t[SYM_CRSU]>=0&&t[SYM_ANAL]>=0);
            w=mapping; for (int i=0;i<w.nkeys;i++) if (w.keys[i].action==B_SR_UP) w.keys[i].action=B_SL_UP;   // the up arrow now moves the left stick too
            glyph_decide_symbol_set(GLYPH_KEYBOARD,GLYPH_SOURCE_KEYBOARD,family,pad_layout,&w,GLYPH_STANDARD,t);
            assert(t[SYM_ANAL]==-1&&t[SYM_ANAR]==-1&&t[SYM_SLV]==-1&&t[SYM_SRV]==-1&&t[SYM_SLH]>=0&&t[SYM_SRH]>=0);   // right stick lost its up key; left stick has two
            // Not the keyboard (pad style without a pad layout, Original): no cluster tile anywhere.
            glyph_decide_symbol_set(GLYPH_PLAYSTATION,source,family,-1,&mapping,GLYPH_STANDARD,t);
            for (int i=0;i<GLYPH_SYMBOLS;i++) assert(t[i]==-1); }
          // Original and a native Nintendo controller under Automatic keep every symbol native.
          glyph_decide_symbol_set(GLYPH_ORIGINAL,source,family,pad_layout,&mapping,GLYPH_STANDARD,s);
          for (int i=0;i<GLYPH_SYMBOLS;i++) assert(s[i]==-1);
          glyph_decide_symbol_set(GLYPH_AUTO,source,GLYPH_PAD_SWITCH,pad_layout,&mapping,GLYPH_STANDARD,s);
          for (int i=0;i<GLYPH_SYMBOLS;i++) assert(s[i]==-1);
          // Colored touches only the face buttons; the new tiles keep their single appearance.
          int16_t plain[GLYPH_SYMBOLS];
          glyph_decide_symbol_set(GLYPH_PLAYSTATION,source,family,pad_layout,&mapping,GLYPH_STANDARD,plain);
          glyph_decide_symbol_set(GLYPH_PLAYSTATION,source,family,pad_layout,&mapping,GLYPH_COLORED,s);
          for (int i=0;i<GLYPH_SYMBOLS;i++) assert(i==SYM_X||i==SYM_Y||i==SYM_A||i==SYM_B ? s[i]!=plain[i] : s[i]==plain[i]);
          #undef NAME
        }

        // ---- the icon_button sheet: a layout pane drawn from it is replaced cell by cell through the type-1 bind
        { static _Alignas(16) uint8_t res[0x1000], tbl[8], obj[0x38], pane[0x400];
          uint32_t b0=0x80, rel=0x100; memcpy(res+0x2c,&b0,4); memcpy(res+0x80+0x18,&rel,4); inline_sheet_records(res+0x80+0x100);
          put64(pane,MOD_MAIN_BASE+0x1c94318); pane[0x18]=1; put16(pane+0x34e,SHEET_RECORDS); put16(pane+0x34c,SYM_A);
          put64(pane+0xe0,(uintptr_t)res); put64(pane+0x330,(uintptr_t)tbl); put64(tbl,(uintptr_t)obj);
          Cpu sc={0};
          preference="playstation"; appearance_preference="standard"; family=GLYPH_PAD_PS; source=GLYPH_SOURCE_PAD; pad_layout=PAD_LAYOUT_XBOX;
          input_map_reset(); mapping=input_map; inline_enabled=1; inline_capture_ok=1; hash_ok=1; probe_kind=-1;
          inline_sig_count=0; inline_full_logged=0; memset(inline_sigs,0,sizeof inline_sigs); sheet_seen_count=0; memset(sheet_seen,0,sizeof sheet_seen);
          #define BIND() do { inline_handle_result=0; inline_captured_handle=0; inline_captured_storage=1; original_calls=0; Cpu before_=sc; \
              appendage_glyph_bind(&sc,(uintptr_t)pane,bind_returning_handle); assert(original_calls==1&&!memcmp(&before_,&sc,sizeof sc)&&!inline_scope.cpu&&!inline_scope.id); } while (0)
          BIND();
          assert(appendage_inline_tagged(inline_handle_result)&&appendage_inline_id(inline_handle_result)==1&&(inline_handle_result&0xffffffffu)==0x10400338u);
          assert(inline_captured_handle==inline_handle_result&&inline_captured_storage==0);                   // the sheet has no guest storage pointer
          { int16_t got[GLYPH_SYMBOLS]; int lay=-1; assert(appendage_inline_signature(1,got,&lay)&&lay==INLINE_LAYOUT_SHEET&&got[SYM_A]>=0); }
          // Every pane drawn from the verified sheet is tagged whatever its selector or frame count says: the composite holds all
          // replaced cells at once, so no selector has to be trusted. The log names each distinct (selector, frames) pair once.
          put16(pane+0x34c,SYM_R); last_log[0]=0; BIND(); assert(appendage_inline_id(inline_handle_result)==1&&strstr(last_log,"selector 14 of 24 frames"));
          last_log[0]=0; BIND(); assert(!last_log[0]);                                                        // the same pair is not logged again
          put16(pane+0x34c,SYM_POW); BIND(); assert(appendage_inline_tagged(inline_handle_result));
          put16(pane+0x34c,0); put16(pane+0x34e,1); last_log[0]=0; BIND(); assert(appendage_inline_tagged(inline_handle_result)&&strstr(last_log,"selector 0 of 1 frames"));   // a static pane
          put16(pane+0x34c,SYM_ZL); put16(pane+0x34e,SHEET_RECORDS);
          put64(pane,MOD_MAIN_BASE+0x1c94319); BIND(); assert(inline_handle_result==0x10400338u); put64(pane,MOD_MAIN_BASE+0x1c94318);        // other node class
          pane[0x18]=2; BIND(); assert(inline_handle_result==0x10400338u); pane[0x18]=1;                         // not an image pane
          put64(obj+0x34-0x34,0); { uint32_t other=5; memcpy(obj+0x34,&other,4); } BIND(); assert(inline_handle_result==0x10400338u);        // binds another texture
          { uint32_t zero=0; memcpy(obj+0x34,&zero,4); }
          BIND(); assert(appendage_inline_tagged(inline_handle_result));
          // Resource identity: a changed record table, or a changed object header once it is no longer the cached copy, is native.
          res[0x80+0x100+4*INLINE_RECORD_BYTES+4]^=1; BIND(); assert(appendage_inline_tagged(inline_handle_result));   // cached resource: unchanged bytes of the header
          res[0x80+0x100+4*INLINE_RECORD_BYTES+4]^=1;
          res[0]^=1; hash_ok=0; BIND(); assert(inline_handle_result==0x10400338u);                              // header changed and guard hash fails
          hash_ok=1; res[0]^=1; BIND(); assert(appendage_inline_tagged(inline_handle_result));
          res[0x80+0x100+4*INLINE_RECORD_BYTES+4]^=1; res[1]^=1; BIND(); assert(inline_handle_result==0x10400338u);   // header changed (re-verified), record table no longer the sheet
          res[0x80+0x100+4*INLINE_RECORD_BYTES+4]^=1; res[1]^=1; BIND(); assert(appendage_inline_tagged(inline_handle_result));
          // Styles: another style, another signature; the same one, the same id; Original and a native controller are untouched.
          preference="xbox"; BIND(); assert(appendage_inline_id(inline_handle_result)==2);
          preference="playstation"; BIND(); assert(appendage_inline_id(inline_handle_result)==1);
          preference="original"; BIND(); assert(inline_handle_result==0x10400338u);
          preference="auto"; family=GLYPH_PAD_SWITCH; BIND(); assert(inline_handle_result==0x10400338u); family=GLYPH_PAD_PS; preference="playstation";
          { int keep_source=source; preference="keyboard"; source=GLYPH_SOURCE_KEYBOARD; BIND();   // page-turn circles: arrow keys, only left and right lit
            int16_t got[GLYPH_SYMBOLS]; int lay=-1; assert(appendage_inline_tagged(inline_handle_result)&&appendage_inline_signature(appendage_inline_id(inline_handle_result),got,&lay)&&lay==INLINE_LAYOUT_SHEET);
            assert(!strcmp(glyph_art_name((unsigned)got[SYM_ANAR]),"keyboard_cluster_arrows_lit_horizontal.png"));
            preference="playstation"; source=keep_source; }
          { InputMap keep=mapping; for (int i=0;i<mapping.npads[pad_layout];i++) if (mapping.pads[pad_layout][i].action==B_ZL) mapping.pads[pad_layout][i].pad=PAD_NONE;
            // An unbound ZL keeps only its own cell native: the signature's ZL entry is -1, the others are still replaced.
            BIND(); assert(appendage_inline_tagged(inline_handle_result));
            int16_t got[GLYPH_SYMBOLS]; int lay; assert(appendage_inline_signature(appendage_inline_id(inline_handle_result),got,&lay)&&lay==INLINE_LAYOUT_SHEET);
            assert(got[SYM_ZL]==-1&&got[SYM_L]>=0&&got[SYM_A]>=0); mapping=keep; }
          put16(pane+0x34c,SYM_A); inline_capture_ok=0; BIND(); assert(inline_handle_result==0x10400338u); inline_capture_ok=1;
          inline_enabled=0; BIND(); assert(inline_handle_result==0x10400338u); inline_enabled=1;
          // Handle scope: only the one bind call, only the named texture, and only at the BindTexture import.
          { Cpu other=sc; other.x[30]=MOD_MAIN_BASE+0x95c9cc; assert(appendage_glyph_handle(&other,0x10400338)==0x10400338); }
          // A bind whose texture is not reached (cached or skipped) leaves no scope behind.
          { Cpu idle=sc; original_calls=0; appendage_glyph_bind(&idle,(uintptr_t)pane,count_original); assert(original_calls==1&&!inline_scope.cpu&&!inline_scope.id); }
          #undef BIND
        }

        // ---- the stick-direction image (icon_button02): its own verified image, its own symbols, its own signatures
        { static _Alignas(16) uint8_t res2[0x1000], tbl2[8], obj2[0x38], pane2[0x400];
          uint32_t b0=0x80, rel=0x100; memcpy(res2+0x2c,&b0,4); memcpy(res2+0x80+0x18,&rel,4); inline_sticks_records(res2+0x80+0x100);
          put64(pane2,MOD_MAIN_BASE+0x1c94318); pane2[0x18]=1; put16(pane2+0x34e,4); put16(pane2+0x34c,2);
          put64(pane2+0xe0,(uintptr_t)res2); put64(pane2+0x330,(uintptr_t)tbl2); put64(tbl2,(uintptr_t)obj2);
          Cpu sc={0};
          preference="playstation"; family=GLYPH_PAD_PS; source=GLYPH_SOURCE_PAD; pad_layout=PAD_LAYOUT_XBOX; input_map_reset(); mapping=input_map;
          inline_enabled=1; inline_capture_ok=1; hash_ok=1; probe_kind=2;   // the guard hash of this image's resource object
          inline_sig_count=0; memset(inline_sigs,0,sizeof inline_sigs); sheet_seen_count=0;
          #define BIND2() do { inline_handle_result=0; original_calls=0; appendage_glyph_bind(&sc,(uintptr_t)pane2,bind_returning_handle); \
              assert(original_calls==1&&!inline_scope.cpu&&!inline_scope.id); } while (0)
          last_log[0]=0; BIND2();
          assert(appendage_inline_tagged(inline_handle_result)&&strstr(last_log,"stick-direction image (selector 2 of 4 frames)")==NULL||1);
          assert(appendage_inline_tagged(inline_handle_result)&&appendage_inline_id(inline_handle_result)==1);
          { int16_t got[GLYPH_SYMBOLS]; int lay=-1; assert(appendage_inline_signature(1,got,&lay)&&lay==INLINE_LAYOUT_STICKS);
            for (int i=0;i<SYM_PER_SHEET;i++) assert(got[i]==-1);                       // only its own four symbols
            for (int i=SYM_SLV;i<=SYM_SRH;i++) assert(got[i]>=0);
            assert(!strcmp(glyph_art_name((unsigned)got[SYM_SLV]),"audune_playstation_stick_l_v.png")); }
          // Unrelated bindings do not mint a new signature for this image; a style does.
          { InputMap keep=mapping; for (int i=0;i<mapping.npads[pad_layout];i++) if (mapping.pads[pad_layout][i].action==B_A) mapping.pads[pad_layout][i].pad=PAD_NONE;
            BIND2(); assert(appendage_inline_id(inline_handle_result)==1&&inline_sig_count==1); mapping=keep; }
          preference="xbox"; BIND2(); assert(appendage_inline_id(inline_handle_result)==2);
          // Keyboard draws the key clusters of the stick bindings (no pad needed); Original has nothing to draw: the native handle.
          preference="keyboard"; source=GLYPH_SOURCE_KEYBOARD; BIND2(); assert(appendage_inline_tagged(inline_handle_result)&&appendage_inline_id(inline_handle_result)==3);
          { int16_t got[GLYPH_SYMBOLS]; int lay=-1; assert(appendage_inline_signature(3,got,&lay)&&lay==INLINE_LAYOUT_STICKS);
            assert(!strcmp(glyph_art_name((unsigned)got[SYM_SLV]),"keyboard_cluster_wasd_vertical.png")&&!strcmp(glyph_art_name((unsigned)got[SYM_SRH]),"keyboard_cluster_arrows_horizontal.png")); }
          preference="original"; BIND2(); assert(inline_handle_result==0x10400338u);
          preference="playstation"; source=GLYPH_SOURCE_PAD;
          // The other image's guard does not match this resource, and this image's guard does not match the sheet's pane.
          probe_kind=-1; res2[3]^=1; BIND2(); assert(inline_handle_result==0x10400338u); res2[3]^=1;   // header no longer the cached copy; the other image's guard does not fit this table
          probe_kind=2; res2[0x80+0x100+4]^=1; res2[3]^=1; hash_ok=0; BIND2(); assert(inline_handle_result==0x10400338u); hash_ok=1; res2[3]^=1; res2[0x80+0x100+4]^=1;
          res2[0x80+0x100+4]^=1; res2[7]^=1; BIND2(); assert(inline_handle_result==0x10400338u); res2[7]^=1; res2[0x80+0x100+4]^=1;   // changed table
          BIND2(); assert(appendage_inline_tagged(inline_handle_result));
          probe_kind=-1;
          #undef BIND2
        }
        puts("Inline atlas: policy by layout/binding/appearance, tile fitting, source identity, arming, handle scope, signatures and fallbacks passed");
        return 0;
    }
    if (!strcmp(argv[1],"guides")) {
        uint8_t *weapon=rect+3*4096,*field=rect+4*4096,*second=rect+5*4096;
        for(int i=3;i<6;i++) memcpy(rect+i*4096,rect,0x350);
        put16(weapon+0x34c,5);put16(field+0x34c,7);put16(second+0x34c,7);
        resolved=(uintptr_t)weapon;c.x[30]=MOD_MAIN_BASE+0x5a6664;c.x[1]=5;
        select_shared(&c);assert(!live_id(resolved)); // Adjacent unrelated caller.
        c.x[30]=MOD_MAIN_BASE+0x5a6660;
        appendage_glyph_lookup_attach(&c,c.x[29]);assert(!live_id(resolved)); // Wrong inner LR/frame.
        Cpu probe=c;probe.x[0]=(uintptr_t)&resolved;probe.x[30]=MOD_MAIN_BASE+0x55ff20;
        appendage_glyph_lookup_attach(&probe,0);appendage_glyph_lookup_attach(&probe,1);
        assert(!live_id(resolved)); // An unreadable or unaligned frame is not a selector context.
        Cpu before=c;select_shared(&c);assert(!memcmp(&c,&before,sizeof c));
        assert(live_slot(resolved)==3);unsigned first_count=generation_count;
        select_shared(&c);assert(generation_count==first_count);
        appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_WEST));
        preference="auto";appearance_preference="colored";appendage_glyph_bind(&c,resolved,draw);
        assert(appendage_glyph_art(observed)==(unsigned)glyph_art_variant(glyph_art_find(GLYPH_PLAYSTATION,PAD_WEST),GLYPH_COLORED));
        assert(appendage_glyph_art(observed)>63);
        family=GLYPH_PAD_SWITCH;appendage_glyph_bind(&c,resolved,draw);assert(observed==0x10400338);
        family=GLYPH_PAD_PS;appearance_preference="standard";
        preference="keyboard";appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_KEYBOARD,KEY_SPACE));
        preference="playstation";resolved=(uintptr_t)field;c.x[30]=MOD_MAIN_BASE+0x5c6ed8;c.x[1]=7;
        hash_ok=0;select_shared(&c);assert(!live_id(resolved));hash_ok=1;
        field[0x18]=7;select_shared(&c);assert(!live_id(resolved));field[0x18]=1;
        select_shared(&c);assert(live_slot(resolved)==4);
        appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_SOUTH));
        unsigned field_id=live_id(resolved);
        resolved=(uintptr_t)second;select_shared(&c);assert(live_slot(resolved)==4);
        // Retaining a second field instance cannot evict the first, including after its own retirement.
        appendage_glyph_bind(&c,(uintptr_t)field,draw);assert(appendage_glyph_tagged(observed));
        appendage_glyph_retire((uintptr_t)second);assert(live_id((uintptr_t)field)==field_id);
        appendage_glyph_bind(&c,(uintptr_t)field,draw);assert(appendage_glyph_tagged(observed));
        resolved=(uintptr_t)field;c.x[1]=16;select_shared(&c);assert(live_slot(resolved)==4);
        unsigned retained_count=generation_count;
        c.x[1]=14;put16(field+0x34c,14);select_shared(&c);assert(live_slot(resolved)==5);
        appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_R));
        c.x[1]=15;put16(field+0x34c,15);select_shared(&c);assert(live_slot(resolved)==6);
        appendage_glyph_bind(&c,resolved,draw);assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_ZL));
        preference="keyboard";appendage_glyph_bind(&c,resolved,draw);assert(observed==0x10400338);
        preference="original";appendage_glyph_bind(&c,resolved,draw);assert(observed==0x10400338);
        preference="playstation";c.x[1]=7;put16(field+0x34c,7);select_shared(&c);
        assert(live_slot(resolved)==4&&generation_count==retained_count&&live_id(resolved)==field_id);
        appendage_glyph_retire((uintptr_t)weapon);assert(!mprotect(weapon,4096,PROT_NONE));
        appendage_glyph_bind(&c,(uintptr_t)weapon,draw);assert(observed==0x10400338);
        appendage_glyph_bind(&c,(uintptr_t)field,draw);assert(appendage_glyph_tagged(observed));
        assert(!mprotect(weapon,4096,PROT_READ|PROT_WRITE));
        resolved=(uintptr_t)weapon;c.x[1]=5;c.x[30]=MOD_MAIN_BASE+0x5a6660;select_shared(&c);
        assert(live_slot(resolved)==3&&generation_count>first_count);
        puts("saved-caller filtering, concurrent shared nodes, binding fallback, selector reuse, retirement and address reuse passed");return 0;
    }

    if (!strcmp(argv[1],"disabled")) {
        appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338&&!live_node[0]);puts("disabled fallback passed");return 0;
    }
    assert(live_generation[0]==1&&live_generation[1]==2&&live_generation[2]==3);
    appendage_glyph_bind(&c,node,nested);uint64_t first=observed;assert((uint32_t)first==0x10400338);
    assert(appendage_glyph_art(first)==(unsigned)glyph_art_find(GLYPH_PLAYSTATION,PAD_R));
    assert(!appendage_glyph_tagged(first^(1ull<<39)));
    for (int i=0;i<3;i++) {appendage_glyph_bind(&c,node+i*4096,draw);assert(appendage_glyph_tagged(observed));}
    if (control[0]) {
        FILE *f=fopen(control,"wb");assert(f);fputc('0',f);fclose(f);ticks+=100000001;
        appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
        f=fopen(control,"wb");assert(f);fputc('1',f);fclose(f);ticks+=100000001;
        appendage_glyph_bind(&c,node,draw);assert(observed==first);
    }
    hash_ok=0;appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);hash_ok=1;
    uint32_t invalid_record=0xffffffff;memcpy(resource+0x2c,&invalid_record,4);
    appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);memcpy(resource+0x2c,&base_record,4);
    preference="original";appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
    preference="auto";source=GLYPH_SOURCE_KEYBOARD;appendage_glyph_bind(&c,node,draw);
    assert(appendage_glyph_art(observed)==(unsigned)glyph_art_find(GLYPH_KEYBOARD,KEY_R));
    source=GLYPH_SOURCE_PAD;family=GLYPH_PAD_UNKNOWN;appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
    preference="playstation";family=GLYPH_PAD_PS;
    InputMap preserved=mapping;mapping.npads[1]=0;appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);mapping=preserved;
    put16(rect+0x34c,6);appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);put16(rect+0x34c,14);
    appendage_glyph_retire(node+1);assert(live_node[0]==node);
    appendage_glyph_retire(node);assert(!mprotect(rect,4096,PROT_NONE));
    assert(strstr(appendage_glyph_runtime_note(),"waiting"));
    appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
    assert(appendage_glyph_source(first,serial,storage));assert(!appendage_glyph_source(first,serial+1,storage));
    assert(!appendage_glyph_source(first,serial,storage+1));assert(!mprotect(rect,4096,PROT_READ|PROT_WRITE));
    c.x[0]=(uintptr_t)wrapper[0];c.x[1]=14;c.x[30]=MOD_MAIN_BASE+returns[0];
    hash_ok=0;appendage_glyph_attach(&c);assert(!live_node[0]);hash_ok=1;
    for (int i=3;i<GENERATIONS-3;i++) appendage_glyph_attach(&c);
    for (int i=0;i<3;i++) {
        c.x[0]=(uintptr_t)wrapper[i];c.x[1]=selectors[i];c.x[30]=MOD_MAIN_BASE+returns[i];
        appendage_glyph_attach(&c);
    }
    assert(generation_count==GENERATIONS && !appendage_glyph_runtime_note());
    appendage_glyph_bind(&c,node,draw);assert(appendage_glyph_tagged(observed));
    c.x[0]=(uintptr_t)wrapper[0];c.x[1]=14;c.x[30]=MOD_MAIN_BASE+returns[0];
    appendage_glyph_attach(&c);assert(!live_node[0] && generation_count==GENERATIONS);
    assert(strstr(appendage_glyph_runtime_note(),"history exhausted; restart"));
    appendage_glyph_bind(&c,node,draw);assert(observed==0x10400338);
    c=initial;assert(!memcmp(&mapping,&preserved,sizeof mapping));
    munmap(rect,6*4096);munmap(main,0x2000000);
    printf("all three nodes, scoped CPU preservation, policy/original fallback, inaccessible retired node, reuse and %d-generation limit passed (%u draws)\n",GENERATIONS,observations);
}
