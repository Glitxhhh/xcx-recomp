// Test-only main linked with candidate objects. Includes the draw implementation to exercise its private view fallback.
#include <assert.h>
#include <sys/mman.h>
#include "../runtime/vk_exec_draw.c"
#include "../runtime/appendage_glyph.h"
#include "../runtime/input_glyphs.h"
#include "layout.h"
#include "nvn_tex.h"
#include "inline_atlas_fixture.h"
static const void *fixture_prefix, *fixture_prefix2, *fixture_hint, *fixture_soul, *fixture_hold;
static int g_pad_snapshot;   // 1: report a connected PlayStation pad with default bindings (pad styles need one)
void __real_input_glyph_snapshot(int *source, int *family, int *layout, InputMap *map);
void __wrap_input_glyph_snapshot(int *source, int *family, int *layout, InputMap *map) {
    __real_input_glyph_snapshot(source, family, layout, map);
    if (g_pad_snapshot) { *source = GLYPH_SOURCE_PAD; *family = GLYPH_PAD_PS; *layout = PAD_LAYOUT_XBOX; }
}
void __real_sha256(const void *, size_t, uint8_t *);
void __wrap_sha256(const void *p, size_t n, uint8_t h[32]) {
    if (p == fixture_prefix2 && n == 128) {   // the stick-direction image's resource object: its guard hash
        static const uint8_t sticks[32] = STICKS_GUARD_SHA256;
        memcpy(h, sticks, 32); return;
    }
    if (p == fixture_hint && n == HINT_BYTES) {   // the mini-map hint image's texture memory: the pinned content hash
        static const uint8_t hint[32] = HINT_NATIVE_SHA256;
        memcpy(h, hint, 32); return;
    }
    if (p == fixture_hold && n == HOLD_BYTES) {   // the held field-action button's texture memory: the pinned content hash
        static const uint8_t hold[32] = HOLD_NATIVE_SHA256;
        memcpy(h, hold, 32); return;
    }
    if (p == fixture_soul && n == SOUL_BYTES) {   // the Soul Challenge button image's texture memory: the pinned content hash
        static const uint8_t soul[32] = SOUL_NATIVE_SHA256;
        memcpy(h, soul, 32); return;
    }
    if ((p != fixture_prefix || n != 128) && n != 84) { __real_sha256(p, n, h); return; }
    const uint8_t expected[32] = {0xed,0x91,0x64,0xc1,0x3b,0xf5,0x5b,0x99,0x9b,0xf7,0x8b,0xe9,0xd5,0x2d,0x3b,0x65,
      0x71,0x70,0x62,0xc6,0x22,0xdb,0xc8,0xb1,0x31,0x5a,0x30,0xe7,0xd9,0x7d,0x32,0xf7};
    const uint8_t record[32] = {0x34,0x62,0x54,0xa9,0x27,0x7c,0x80,0xdf,0x8e,0x54,0x43,0x79,0xed,0x61,0xe8,0x4c,0xee,0x25,0xb0,0x27,0x45,0x3c,0x68,0xfc,0xa8,0x25,0x58,0x3b,0x34,0xb8,0x04,0xd5};
    memcpy(h, n==128 ? expected : record, 32);
}
static void write64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }
static void write16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void check_pixels(const VkTexImage *image, unsigned art) {
    VkCtx *c = vk_ctx(); const size_t bytes = 512 * 192 * 4;
    VkBuffer buffer; VmaAllocation alloc; VmaAllocationInfo info;
    VkBufferCreateInfo bi = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage=VMA_MEMORY_USAGE_AUTO,.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT};
    assert(vmaCreateBuffer(c->vma,&bi,&ai,&buffer,&alloc,&info)==VK_SUCCESS && info.pMappedData);
    VkCommandBuffer cb = vk_upload_begin();
    VkBufferImageCopy region = {.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={512,192,1}};
    vkCmdCopyImageToBuffer(cb,image->image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&region);
    VkMemoryBarrier2 mb = {.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,.srcStageMask=VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT,.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT,.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT};
    VkDependencyInfo di = {.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO,.memoryBarrierCount=1,.pMemoryBarriers=&mb};
    vkCmdPipelineBarrier2(cb,&di); vk_upload_end(); vk_upload_flush(1);
    assert(vmaInvalidateAllocation(c->vma,alloc,0,bytes)==VK_SUCCESS);
    uint32_t *expected = calloc(1,bytes); assert(expected);
    assert(glyph_art_pixels(art, expected));
    assert(!memcmp(expected,info.pMappedData,bytes)); free(expected);
    vmaDestroyBuffer(c->vma,buffer,alloc);
}

// ---- inline atlas fixture: a 368x300 BC7 texture of solid mode-6 blocks, so the expected decode is known exactly.
static void put_bits(uint8_t *b, int *pos, uint32_t v, int n) { for (int i = 0; i < n; i++, (*pos)++) if (v >> i & 1) b[*pos >> 3] |= (uint8_t)(1u << (*pos & 7)); }
static void bc7_solid(uint8_t b[16], unsigned r, unsigned g, unsigned bl, unsigned a) {   // channels are 7-bit endpoint values
    memset(b, 0, 16); int pos = 0;
    put_bits(b, &pos, 1u << 6, 7);                                  // mode 6: six zero bits then a one
    unsigned v[4] = {r, g, bl, a};
    for (int c = 0; c < 4; c++) { put_bits(b, &pos, v[c], 7); put_bits(b, &pos, v[c], 7); }   // both endpoints equal
    put_bits(b, &pos, 0, 2);                                        // p-bits 0: decoded value is the 7-bit value * 2
    // 63 index bits stay 0: every texel takes endpoint 0.
}
static uint32_t block_rgba(int bx, int by) {
    unsigned r = (unsigned)(bx * 7 + 3) % 128, g = (unsigned)(by * 5 + 11) % 128, b = (unsigned)(bx + by * 3 + 1) % 128;
    return (r * 2) | (g * 2) << 8 | (b * 2) << 16 | 254u << 24;
}
static void read_image(const VkTexImage *image, unsigned w, unsigned h, size_t bytes, void *out) {
    VkCtx *c = vk_ctx();
    VkBuffer buffer; VmaAllocation alloc; VmaAllocationInfo info;
    VkBufferCreateInfo bi = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage=VMA_MEMORY_USAGE_AUTO,.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT};
    assert(vmaCreateBuffer(c->vma,&bi,&ai,&buffer,&alloc,&info)==VK_SUCCESS && info.pMappedData);
    VkCommandBuffer cb = vk_upload_begin();
    VkBufferImageCopy region = {.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
    vkCmdCopyImageToBuffer(cb,image->image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&region);
    VkMemoryBarrier2 mb = {.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,.srcStageMask=VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT,.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT,.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT};
    VkDependencyInfo di = {.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO,.memoryBarrierCount=1,.pMemoryBarriers=&mb};
    vkCmdPipelineBarrier2(cb,&di); vk_upload_end(); vk_upload_flush(1);
    assert(vmaInvalidateAllocation(c->vma,alloc,0,bytes)==VK_SUCCESS);
    memcpy(out,info.pMappedData,bytes);
    vmaDestroyBuffer(c->vma,alloc?buffer:buffer,alloc);
}
static uint64_t inline_handle;
static unsigned import_slot = 825;
static void inline_import(Cpu *c) {   // the BindTexture import inside the guest bind helper
    uint64_t lr = c->x[30]; c->x[30] = MOD_MAIN_BASE + 0x95c9cc;
    inline_handle = appendage_glyph_handle(c, 0x10000000u | import_slot);
    c->x[30] = lr;
}
static pool_t *g_pool; static const char *g_settings, *g_stage; static uint8_t *g_resource, *g_resource2;
// width, height, cell width, cell height, columns, records of each layout
static const unsigned test_geom[INLINE_LAYOUTS][6] = {   // (the hint image has its own test, check_hint)
    {INLINE_ATLAS_W, INLINE_ATLAS_H, INLINE_CELL_W, INLINE_CELL_H, INLINE_COLUMNS, INLINE_RECORDS},
    {SHEET_W, SHEET_H, SHEET_CELL, SHEET_CELL, SHEET_COLUMNS, SHEET_RECORDS},
    {STICKS_W, STICKS_H, STICKS_CELL, STICKS_CELL, STICKS_COLUMNS, STICKS_RECORDS},
};

// ---- one inline layout end to end: a synthetic BC7 image of solid blocks, the guest-side arming, the tag, the capture, the
// composite built on the GPU and read back. layout 0: the 368x300 text atlas (slot 825); layout 1: the 512x192 icon_button sheet
// (slot 826), armed through a type-1 pane.
static void check_layout(int layout) {
    const unsigned AW = test_geom[layout][0], AH = test_geom[layout][1], BW = (AW + 3) / 4, BH = (AH + 3) / 4;   // the white-mask image is 150 rows: its last block row is partial
    const unsigned CW = test_geom[layout][2], CH = test_geom[layout][3];
    const unsigned RECORDS = test_geom[layout][5], COLUMNS = test_geom[layout][4];
    const unsigned slot = 825 + layout; import_slot = slot;
    const char *stage = g_stage;
    tex_t atlas = {.d = {.target = NVN_TEXTURE_TARGET_2D, .format = INLINE_NVN_FORMAT, .flags = NVN_TEXTURE_FLAGS_LINEAR,
                         .width = AW, .height = AH, .depth = 1, .levels = 1, .stride = BW * 16}, .pool = g_pool, .serial = 31};
    nvn_tex_layout(&atlas.d, &atlas.L); atlas.mem = calloc(1, atlas.L.size); assert(atlas.mem);
    uint8_t *blocks = malloc((size_t)BW * BH * 16); assert(blocks);
    for (unsigned by = 0; by < BH; by++) for (unsigned bx = 0; bx < BW; bx++) {
        uint32_t col = block_rgba((int)bx, (int)by);
        bc7_solid(blocks + ((size_t)by * BW + bx) * 16, (col & 255) / 2, (col >> 8 & 255) / 2, (col >> 16 & 255) / 2, 127);
        memcpy(atlas.mem + (size_t)by * BW * 16 + (size_t)bx * 16, blocks + ((size_t)by * BW + bx) * 16, 16);
    }
    vk_texture_register(slot, &atlas, NULL);
    // Guest-side fixture. Text atlas: node -> source -> table, texture set -> entry 0 naming the atlas storage. Sheet: a type-1 pane
    // whose resource is the verified icon_button object and whose first texture object names this slot.
    static _Alignas(16) uint8_t X[0x60], T[0x20 + INLINE_RECORDS * INLINE_RECORD_BYTES], tset[0x40], entries[0x78], node[0x400], tbl[8], obj[0x38];
    Cpu b = {0};
    if (!layout) {
        inline_atlas_records(T + 0x20); { uint32_t off = 0x20; memcpy(T + 0x18, &off, 4); }
        write64(X + 0x38, (uintptr_t)T); { uint32_t cnt = INLINE_RECORDS; memcpy(X + 0x48, &cnt, 4); float a = INLINE_SOURCE_SCALE_A, c2 = INLINE_SOURCE_SCALE_B; memcpy(X + 0x4c, &a, 4); memcpy(X + 0x50, &c2, 4); }
        write64(X + 0x40, (uintptr_t)tset); write64(tset + 0x30, (uintptr_t)entries);
        write64(entries, INLINE_ENTRY_Q0); write64(entries + 0x20, (uintptr_t)atlas.mem); write64(node + 0x3b8, (uintptr_t)X);
        b.x[19] = (uintptr_t)node; b.x[1] = (uintptr_t)entries; b.x[30] = MOD_MAIN_BASE + 0xa325fc;
    } else {
        write64(node, MOD_MAIN_BASE + 0x1c94318); node[0x18] = 1; write16(node + 0x34e, (uint16_t)RECORDS); write16(node + 0x34c, layout == 2 ? 0 : SYM_A);
        write64(node + 0xe0, (uintptr_t)(layout == 2 ? g_resource2 : g_resource)); write64(node + 0x330, (uintptr_t)tbl); write64(tbl, (uintptr_t)obj);
        { uint32_t off = (uint32_t)slot - 824; memcpy(obj + 0x34, &off, 4); }   // texture id = the global base (824) + this offset
    }
    #define ARM() do { if (!layout) appendage_glyph_icon_bind(&b, inline_import); else appendage_glyph_bind(&b, (uintptr_t)node, inline_import); } while (0)
    // No gamepad here: the text atlas and the button sheet are exercised with the keyboard style; the stick-direction image has no
    // keyboard symbols, so it uses a (reported) PlayStation pad.
    g_pad_snapshot = layout == 2;
    cfg_set("XCX_GLYPH_STYLE", layout == 2 ? "playstation" : "keyboard"); cfg_set("XCX_GLYPH_APPEARANCE", "standard");
    assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
    inline_handle = 0; ARM();
    const int source_stage = !strcmp(stage, "source");
    const int expected_base = strcmp(stage, "pixels") && strcmp(stage, "image") && strcmp(stage, "upload") && strcmp(stage, "blit") && !source_stage;
    assert((inline_handle & 0xffffffffu) == (0x10000000u | slot));
    assert(appendage_inline_tagged(inline_handle) || source_stage);   // a refused capture hands the guest its own handle back
    const int tagged = appendage_inline_tagged(inline_handle);
    const int expected = tagged && expected_base;
    int16_t arts[GLYPH_SYMBOLS]; int sig_layout = -1;
    memset(arts, -1, sizeof arts);
    if (tagged) { assert(appendage_inline_signature(appendage_inline_id(inline_handle), arts, &sig_layout)); assert(sig_layout == layout); }
    vk_texture_lock();
    const VkTexImage *native = vk_texture_by_id_locked((int)slot, NULL, NULL); assert(native);
    const VkTexImage *picked = vk_appendage_texture_locked(inline_handle, NULL, NULL);
    assert(picked && ((picked == native) == !expected));
    VkExec ex = {0}; GfxShaderRes res = {.type = 2}; VkDescriptorImageInfo out; VkImage image;
    tex_info_full(&ex, &res, inline_handle, 0, 0, &out, &image);
    assert(out.imageView && image == (expected && strcmp(stage, "view") ? picked->image : native->image));
    if (expected) {
        // Expected image: the native decode everywhere, the fitted tiles in the replaced cells only.
        uint32_t *want = malloc((size_t)AW * AH * 4), *got = malloc((size_t)AW * AH * 4); assert(want && got);
        for (unsigned y = 0; y < AH; y++) for (unsigned x = 0; x < AW; x++) want[y * AW + x] = block_rgba((int)x / 4, (int)y / 4);
        unsigned replaced = 0;
        for (unsigned k = 0; k < RECORDS; k++) {
            unsigned sym = layout == 2 ? SYM_SLV + k : k % SYM_PER_SHEET;
            if (arts[sym] < 0 || !inline_symbol_replaceable(sym)) continue;
            const InlineBox *box = layout == 2 ? &inline_stick_boxes[k] : layout == 1 ? &inline_sheet_boxes[k] : &inline_font_boxes[k / SYM_PER_SHEET][sym];
            uint32_t cell[88 * 88];
            const int round_cell = layout == 1 && (sym == SYM_X || sym == SYM_Y || sym == SYM_A || sym == SYM_B);
            assert((round_cell ? glyph_art_cell_round : glyph_art_cell)((unsigned)arts[sym], (int)CW, (int)CH, box->x, box->y, box->w, box->h, cell));
            unsigned cx = (k % COLUMNS) * CW, cy = (k / COLUMNS) * CH;
            for (unsigned y = 0; y < CH; y++) for (unsigned x = 0; x < CW; x++) want[(cy + y) * AW + cx + x] = cell[y * CW + x];
            replaced++;
        }
        assert(replaced >= (layout == 2 ? 4u : 4u) && picked != native);
        read_image(picked, AW, AH, (size_t)AW * AH * 4, got);
        { unsigned bad = 0, x0 = AW, y0 = AH, x1 = 0, y1 = 0;
          for (unsigned i = 0; i < AW * AH; i++) if (want[i] != got[i]) {
              if (!bad++) fprintf(stderr, "layout %d first mismatch %u,%u: want %08x got %08x\n", layout, i % AW, i / AW, want[i], got[i]);
              if (i % AW < x0) x0 = i % AW; if (i % AW > x1) x1 = i % AW; if (i / AW < y0) y0 = i / AW; if (i / AW > y1) y1 = i / AW;
          }
          if (bad) { fprintf(stderr, "layout %d: %u mismatching texels in x %u..%u y %u..%u\n", layout, bad, x0, x1, y0, y1); assert(0); } }
        // The native image still holds exactly its original BC7 blocks: nothing was written to it.
        uint8_t *raw = malloc((size_t)BW * BH * 16); assert(raw);
        read_image(native, AW, AH, (size_t)BW * BH * 16, raw);
        assert(!memcmp(raw, blocks, (size_t)BW * BH * 16));
        free(raw); free(want); free(got);
        // A second resolution returns the same owned image; a new signature gets its own image and leaves this one alone.
        assert(vk_appendage_texture_locked(inline_handle, NULL, NULL) == picked);
        const unsigned first_id = appendage_inline_id(inline_handle);
        if (layout == 2) cfg_set("XCX_GLYPH_STYLE", "xbox"); else cfg_set("XCX_BIND_R", "key:t,pad:r");   // another style or binding, another signature
        assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
        ARM();
        assert(appendage_inline_id(inline_handle) != first_id);
        const VkTexImage *second = vk_appendage_texture_locked(inline_handle, NULL, NULL);
        assert(second && second != picked && second != native);
        uint32_t *cmp = malloc((size_t)AW * AH * 4), *again = malloc((size_t)AW * AH * 4); assert(cmp && again);
        read_image(second, AW, AH, (size_t)AW * AH * 4, cmp);
        read_image(picked, AW, AH, (size_t)AW * AH * 4, again);
        // Different art in the replaced cells. (The stick tiles of the three pad families are the same drawing, so that image can only
        // differ in its signature and its owned image, both asserted above.)
        if (layout != 2) assert(memcmp(cmp, again, (size_t)AW * AH * 4));
        if (layout != 2) for (unsigned y = 0; y < CH; y++) for (unsigned x = 0; x < CW; x++)   // the power cell (never replaced) equals the native decode in both
            assert(cmp[y * AW + CW + x] == block_rgba((int)(CW + x) / 4, (int)y / 4) && again[y * AW + CW + x] == cmp[y * AW + CW + x]);
        free(cmp); free(again);
        if (layout == 2) cfg_set("XCX_GLYPH_STYLE", "playstation"); else cfg_set("XCX_BIND_R", "key:r,pad:r");
        assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
    }
    vk_texture_unlock();
    // Identity: the same handle against a different registration (new serial, or another storage) falls back to native.
    atlas.serial++; vk_texture_register(slot, &atlas, NULL);
    vk_texture_lock();
    assert(vk_appendage_texture_locked(inline_handle, NULL, NULL) == vk_texture_by_id_locked((int)slot, NULL, NULL));
    vk_texture_unlock();
    if (!layout) {
        write64(entries + 0x20, (uintptr_t)atlas.mem ^ 0x1000);   // the entry names storage the slot does not have
        inline_handle = 0; ARM();
        assert(!appendage_inline_tagged(inline_handle));
    }
    #undef ARM
    // atlas.mem stays allocated until the process ends: the renderer keys its texture cache by address, so a freed block that a later
    // layout reuses would be served stale content (the runtime keeps its own owned-image buffers alive for the same reason).
    free(blocks);
}
// The mini-map hint: a plain handle to a 204x176 RGBA8 texture whose memory hashes to the pinned image (the hash is faked for the
// fixture memory only). Two cells are replaced; a texture of the same shape with other content, and the Original style, stay native.
static void check_hint(void) {
    const unsigned slot = 840, other = 841, W = HINT_W, H = HINT_H;
    const char *stage = g_stage;
    const int expected_base = strcmp(stage, "pixels") && strcmp(stage, "image") && strcmp(stage, "upload") && strcmp(stage, "blit") && strcmp(stage, "source");
    tex_t hint = {.d = {.target = NVN_TEXTURE_TARGET_2D, .format = HINT_NVN_FORMAT, .flags = NVN_TEXTURE_FLAGS_LINEAR,
                        .width = W, .height = H, .depth = 1, .levels = 1, .stride = W * 4}, .pool = g_pool, .serial = 51};
    nvn_tex_layout(&hint.d, &hint.L);
    uint32_t *mem = calloc(1, HINT_BYTES); assert(mem && hint.L.size <= HINT_BYTES);
    for (unsigned y = 0; y < H; y++) for (unsigned x = 0; x < W; x++) mem[y * W + x] = block_rgba((int)x / 4, (int)y / 4);
    hint.mem = (uint8_t *)mem; fixture_hint = mem;
    vk_texture_register(slot, &hint, NULL);
    tex_t twin = hint; twin.serial = 52; twin.mem = calloc(1, HINT_BYTES); assert(twin.mem);   // same shape, other content: its hash is the real one
    memcpy(twin.mem, mem, (size_t)W * H * 4);
    vk_texture_register(other, &twin, NULL);
    g_pad_snapshot = 0;
    cfg_set("XCX_GLYPH_STYLE", "original"); cfg_set("XCX_GLYPH_APPEARANCE", "standard");
    assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
    vk_texture_lock();
    const VkTexImage *native = vk_texture_by_id_locked((int)slot, NULL, NULL); assert(native);
    assert(vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL) == native);   // Original: native
    vk_texture_unlock();
    cfg_set("XCX_GLYPH_STYLE", "keyboard");
    assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
    vk_texture_lock();
    const VkTexImage *picked = vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL);
    assert(picked && ((picked == native) == !expected_base));
    if (expected_base) {
        const unsigned sig = appendage_plain_signature(INLINE_LAYOUT_HINT); assert(sig);
        int16_t arts[GLYPH_SYMBOLS]; int layout = -1; assert(appendage_inline_signature(sig, arts, &layout) && layout == INLINE_LAYOUT_HINT);
        uint32_t *want = malloc((size_t)W * H * 4), *got = malloc((size_t)W * H * 4); assert(want && got);
        memcpy(want, mem, (size_t)W * H * 4);
        unsigned replaced = 0;
        for (unsigned k = 0; k < HINT_CELLS; k++) {
            const unsigned sym = k ? SYM_CRS : SYM_R;
            if (arts[sym] < 0) continue;
            uint32_t cell[HINT_CELL_W * HINT_CELL_H]; const unsigned cw = hint_cell_dims[k][0], ch = hint_cell_dims[k][1];
            assert(glyph_art_cell((unsigned)arts[sym], (int)cw, (int)ch, hint_boxes[k].x, hint_boxes[k].y, hint_boxes[k].w, hint_boxes[k].h, cell));
            glyph_art_shadowed(cell, (int)cw, (int)ch);   // the soft shadow around the glyph
            for (unsigned y = 0; y < ch; y++) for (unsigned x = 0; x < cw; x++) want[(hint_origins[k][1] + y) * W + hint_origins[k][0] + x] = cell[y * cw + x];
            replaced++;
        }
        assert(replaced == 2);   // the keyboard style draws the R key and the 1-4 cluster
        read_image(picked, W, H, (size_t)W * H * 4, got);
        { unsigned bad = 0; for (unsigned i = 0; i < W * H; i++) if (want[i] != got[i]) { if (!bad++) fprintf(stderr, "hint first mismatch %u,%u: want %08x got %08x\n", i % W, i / W, want[i], got[i]); }
          if (bad) { fprintf(stderr, "hint: %u mismatching texels\n", bad); assert(0); } }
        read_image(native, W, H, (size_t)W * H * 4, got);   // the native image is untouched
        assert(!memcmp(got, mem, (size_t)W * H * 4));
        assert(vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL) == picked);   // one owned image per signature
        cfg_set("XCX_BIND_R", "key:t,pad:r"); assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
        const VkTexImage *second = vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL);   // another binding, another image
        assert(second && second != picked && second != native);
        cfg_set("XCX_BIND_R", "key:r,pad:r"); assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
        assert(vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL) == picked);
        free(want); free(got);
    }
    {   // A pad style draws the D-pad with its down button lit (not the whole D-pad), and the slot's token follows the signature.
        const uint64_t token_before = vk_texture_slot_token((int)slot);
        g_pad_snapshot = 1; cfg_set("XCX_GLYPH_STYLE", "playstation"); assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
        const VkTexImage *pad = vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL);
        if (expected_base) {
            assert(pad && pad != native);
            int16_t all[GLYPH_SYMBOLS], arts[GLYPH_SYMBOLS]; int layout = -1;
            glyph_current_symbol_set(all);
            assert(appendage_inline_signature(appendage_plain_signature(INLINE_LAYOUT_HINT), arts, &layout) && layout == INLINE_LAYOUT_HINT);
            assert(all[SYM_CRSD] >= 0 && arts[SYM_CRS] == all[SYM_CRSD] && arts[SYM_R] == all[SYM_R] && arts[SYM_CRSD] < 0);
            if (token_before) assert(vk_texture_slot_token((int)slot) != token_before);
        }
        g_pad_snapshot = 0;
    }
    // The same shape with other content is not the hint.
    assert(vk_appendage_texture_locked(0x10000000u | other, NULL, NULL) == vk_texture_by_id_locked((int)other, NULL, NULL));
    vk_texture_unlock();
}
// The Soul Challenge button: a 96x96 RGBA8 texture whose memory hashes to the pinned image (faked for the fixture only). The whole image
// is replaced by the style's B tile as a mask (white disc, the glyph a hole); Original stays native.
static void check_soul(void) {
    const unsigned slot = 842, W = SOUL_W, H = SOUL_H;
    const int expected_base = strcmp(g_stage, "pixels") && strcmp(g_stage, "image") && strcmp(g_stage, "upload") && strcmp(g_stage, "blit") && strcmp(g_stage, "source");
    tex_t soul = {.d = {.target = NVN_TEXTURE_TARGET_2D, .format = HINT_NVN_FORMAT, .flags = NVN_TEXTURE_FLAGS_LINEAR,
                        .width = W, .height = H, .depth = 1, .levels = 1, .stride = W * 4}, .pool = g_pool, .serial = 61};
    nvn_tex_layout(&soul.d, &soul.L);
    uint32_t *mem = calloc(1, SOUL_BYTES); assert(mem && soul.L.size <= SOUL_BYTES);
    for (unsigned y = 0; y < H; y++) for (unsigned x = 0; x < W; x++) mem[y * W + x] = block_rgba((int)x / 4, (int)y / 4);
    soul.mem = (uint8_t *)mem; fixture_soul = mem;
    vk_texture_register(slot, &soul, NULL);
    g_pad_snapshot = 0;
    cfg_set("XCX_GLYPH_STYLE", "original"); cfg_set("XCX_GLYPH_APPEARANCE", "standard");
    assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
    vk_texture_lock();
    const VkTexImage *native = vk_texture_by_id_locked((int)slot, NULL, NULL); assert(native);
    assert(vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL) == native);
    vk_texture_unlock();
    cfg_set("XCX_GLYPH_STYLE", "keyboard"); assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
    vk_texture_lock();
    const VkTexImage *picked = vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL);
    assert(picked && ((picked == native) == !expected_base));
    if (expected_base) {
        int16_t arts[GLYPH_SYMBOLS]; int layout = -1;
        assert(appendage_inline_signature(appendage_plain_signature(INLINE_LAYOUT_SOUL), arts, &layout) && layout == INLINE_LAYOUT_SOUL);
        assert(arts[SYM_B] >= 0);
        uint32_t *want = malloc((size_t)W * H * 4), *got = malloc((size_t)W * H * 4), *cell = malloc((size_t)W * H * 4); assert(want && got && cell);
        assert(glyph_art_cell((unsigned)arts[SYM_B], (int)W, (int)H, soul_box.x, soul_box.y, soul_box.w, soul_box.h, cell));
        glyph_art_to_mask(cell, (int)(W * H));
        memcpy(want, cell, (size_t)W * H * 4);
        read_image(picked, W, H, (size_t)W * H * 4, got);
        { unsigned bad = 0; for (unsigned i = 0; i < W * H; i++) if (want[i] != got[i]) { if (!bad++) fprintf(stderr, "soul first mismatch %u,%u: want %08x got %08x\n", i % W, i / W, want[i], got[i]); }
          if (bad) { fprintf(stderr, "soul: %u mismatching texels\n", bad); assert(0); } }
        read_image(native, W, H, (size_t)W * H * 4, got);
        assert(!memcmp(got, mem, (size_t)W * H * 4));   // the native image is untouched
        free(want); free(got); free(cell);
    }
    vk_texture_unlock();
}
// The held field-action button: a 64x64 BC7 texture whose memory hashes to the pinned image (faked for the fixture only). The whole
// image is replaced by the drawn teal ring and disc with the style's A tile in the middle; Original stays native.
static void check_hold(void) {
    const unsigned slot = 843, W = HOLD_W, H = HOLD_H;
    const int expected_base = strcmp(g_stage, "pixels") && strcmp(g_stage, "image") && strcmp(g_stage, "upload") && strcmp(g_stage, "blit") && strcmp(g_stage, "source");
    tex_t hold = {.d = {.target = NVN_TEXTURE_TARGET_2D, .format = INLINE_NVN_FORMAT, .flags = NVN_TEXTURE_FLAGS_LINEAR,
                        .width = W, .height = H, .depth = 1, .levels = 1, .stride = (W / 4) * 16}, .pool = g_pool, .serial = 71};
    nvn_tex_layout(&hold.d, &hold.L);
    uint8_t *mem = calloc(1, HOLD_BYTES > hold.L.size ? HOLD_BYTES : hold.L.size); assert(mem);
    for (unsigned by = 0; by < H / 4; by++) for (unsigned bx = 0; bx < W / 4; bx++) {
        uint32_t col = block_rgba((int)bx, (int)by);
        bc7_solid(mem + ((size_t)by * (W / 4) + bx) * 16, (col & 255) / 2, (col >> 8 & 255) / 2, (col >> 16 & 255) / 2, 127);
    }
    hold.mem = mem; fixture_hold = mem;
    vk_texture_register(slot, &hold, NULL);
    g_pad_snapshot = 0;
    cfg_set("XCX_GLYPH_STYLE", "original"); cfg_set("XCX_GLYPH_APPEARANCE", "standard");
    assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
    vk_texture_lock();
    const VkTexImage *native = vk_texture_by_id_locked((int)slot, NULL, NULL); assert(native);
    assert(vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL) == native);
    vk_texture_unlock();
    cfg_set("XCX_GLYPH_STYLE", "keyboard"); assert(cfg_save_to(g_settings)); assert(cfg_load(g_settings));
    vk_texture_lock();
    const VkTexImage *picked = vk_appendage_texture_locked(0x10000000u | slot, NULL, NULL);
    assert(picked && ((picked == native) == !expected_base));
    if (expected_base) {
        int16_t arts[GLYPH_SYMBOLS]; int layout = -1;
        assert(appendage_inline_signature(appendage_plain_signature(INLINE_LAYOUT_HOLD), arts, &layout) && layout == INLINE_LAYOUT_HOLD);
        assert(arts[SYM_A] >= 0);
        uint32_t *want = malloc((size_t)W * H * 4), *got = malloc((size_t)W * H * 4); assert(want && got);
        assert(glyph_art_cell((unsigned)arts[SYM_A], (int)W, (int)H, hold_box.x, hold_box.y, hold_box.w, hold_box.h, want));
        glyph_art_hold_button(want, (int)W, (int)H);
        read_image(picked, W, H, (size_t)W * H * 4, got);
        { unsigned bad = 0; for (unsigned i = 0; i < W * H; i++) if (want[i] != got[i]) { if (!bad++) fprintf(stderr, "hold first mismatch %u,%u: want %08x got %08x\n", i % W, i / W, want[i], got[i]); }
          if (bad) { fprintf(stderr, "hold: %u mismatching texels\n", bad); assert(0); } }
        assert((want[0] >> 24) == 0 && (want[32 * W + 9] >> 24) >= 250);   // the corner is clear, the ring is there
        free(want); free(got);
    }
    vk_texture_unlock();
}
int __wrap_main(int argc, char **argv) {
    assert(argc==2); cfg_init();
    const char *settings = getenv("GLYPH_TEST_CONFIG"); assert(settings);
    FILE *file = fopen(settings,"wb"); assert(file);
    fputs("pad_layout = xbox\nbind_R = key:r,pad:r\n",file); fclose(file);
    assert(cfg_load(settings));
    cfg_set("XCX_GLYPH_STYLE","original"); cfg_set("XCX_GLYPH_APPEARANCE","colored"); assert(cfg_save_to(settings));
    assert(cfg_load(settings));
    assert(!strcmp(cfg_value("XCX_GLYPH_STYLE"),"original"));
    assert(!strcmp(cfg_value("XCX_GLYPH_APPEARANCE"),"colored"));
    assert(!strcmp(cfg_value("XCX_PAD_LAYOUT"),"xbox"));
    assert(!strcmp(cfg_value("XCX_BIND_R"),"key:r,pad:r"));
    assert(vk_init());
    uint8_t *main = mmap((void *)MOD_MAIN_BASE,0x2000000,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); assert(main==(void *)MOD_MAIN_BASE);
    uint8_t rect[0x350]={0},resource[0x1000]={0},object[0x38]={0},wrapper[0x40]={0};
    uint64_t table=(uintptr_t)object; uint32_t base=824;
    fixture_prefix=resource;
    uint32_t base_record=0x80,relative=0x100; memcpy(resource+0x2c,&base_record,4); memcpy(resource+0x98,&relative,4);
    write64(main+0x1d64f18,(uintptr_t)&base); write64(rect,MOD_MAIN_BASE+0x1c94318); rect[0x18]=1;
    write64(rect+0xe0,(uintptr_t)resource); write64(rect+0x330,(uintptr_t)&table);
    write16(rect+0x34c,14); write16(rect+0x34e,24); write64(wrapper+0x38,(uintptr_t)rect);
    Cpu cpu={0}; cpu.x[0]=(uintptr_t)wrapper; cpu.x[1]=14; cpu.x[30]=MOD_MAIN_BASE+0x60f690;
    appendage_glyph_attach(&cpu);
    pool_t pool={.flags=NVN_MEMORY_POOL_FLAGS_CPU_CACHED};
    tex_t source={.d={.target=NVN_TEXTURE_TARGET_2D,.format=77,.flags=NVN_TEXTURE_FLAGS_LINEAR,
      .width=512,.height=192,.depth=1,.levels=1},.pool=&pool,.serial=9};
    nvn_tex_layout(&source.d,&source.L); source.mem=calloc(1,source.L.size); assert(source.mem);
    vk_texture_register(824,&source,NULL);
    uint64_t handle=APPENDAGE_GLYPH_TAG|(1ull<<44)|0x10400338;
    assert(vk_appendage_capture_source(handle));
    vk_texture_lock();
    const VkTexImage *original=vk_texture_by_id_locked(824,NULL,NULL);
    assert(original);
    const VkTexImage *chosen=vk_appendage_texture_locked(handle,NULL,NULL);
    int failed=strcmp(argv[1],"normal") && strcmp(argv[1],"view") && strcmp(argv[1],"blit");   // blit only affects the inline atlas
    assert(chosen && ((chosen==original)==failed));
    VkExec ex={0}; GfxShaderRes res={.type=2}; VkDescriptorImageInfo out; VkImage image;
    tex_info_full(&ex,&res,handle,0,0,&out,&image);
    assert(out.imageView && image==(failed || !strcmp(argv[1],"view") ? original->image : chosen->image));
    VkDescriptorImageInfo copied=out;
    appendage_glyph_retire((uintptr_t)rect); memset(rect,0xa5,sizeof rect);
    assert(vk_appendage_texture_locked(handle,NULL,NULL)==chosen); // Copied command survives the node.
    assert(copied.imageView==out.imageView);
    if (!failed) check_pixels(chosen,0);
    if (!strcmp(argv[1],"normal")) for (unsigned art=1;art<glyph_art_count()&&art<GLYPH_ART_MAX;art++) {   // the HUD handle names its art in 7 bits
        uint64_t h=handle | (uint64_t)art<<32;
        const VkTexImage *variant=vk_appendage_texture_locked(h,NULL,NULL);
        assert(variant && variant != original);
        tex_info_full(&ex,&res,h,0,0,&out,&image); assert(out.imageView && image==variant->image);
        check_pixels(variant,art);
    }
    vk_texture_unlock();
    source.serial++; vk_texture_register(824,&source,NULL);
    assert(!vk_appendage_capture_source(handle));
    vk_texture_lock();
    assert(vk_appendage_texture_locked(handle,NULL,NULL)==vk_texture_by_id_locked(824,NULL,NULL));
    vk_texture_unlock();

    // ---- inline text-icon atlas and icon_button sheet: guest arm -> tag -> capture -> composite on the GPU -> readback.
    g_pool = &pool; g_settings = settings; g_stage = argv[1]; g_resource = resource;
    inline_sheet_records(resource + base_record + relative);   // the sheet's verified record table, hashed for real by the runtime
    uint8_t resource2[0x1000] = {0};
    memcpy(resource2 + 0x2c, &base_record, 4); memcpy(resource2 + 0x98, &relative, 4);
    inline_sticks_records(resource2 + base_record + relative);
    fixture_prefix2 = resource2; g_resource2 = resource2;
    check_layout(0);
    check_layout(1);
    check_layout(2);
    check_hint();
    check_soul();
    check_hold();
    g_pad_snapshot = 0;
    int validation = vk_ctx()->validation;
    vk_upload_flush(1); vk_mem_shutdown(); vk_shutdown();
    free(source.mem); munmap(main,0x2000000);
    assert(!vk_validation_errors_total());
    printf("Appendage GPU %s: pixel/readback or original fallback, copied view, retired node, slot reuse, inline text atlas and button sheet composite/native-untouched/fallback, shutdown passed; validation enabled %d, reported errors 0\n",argv[1],validation);
    return 0;
}
