// Experimental, build-specific HUD glyph styles. No input or guest-state writes.
#pragma once
#include "cpu.h"
#include "input_glyphs.h"

// Bits 32..38 select immutable artwork; bits 39..43 remain reserved.
#define APPENDAGE_GLYPH_TAG 0xa700000000000000ull
#define APPENDAGE_GLYPH_TAG_MASK 0xff000f8000000000ull
static inline int appendage_glyph_tagged(uint64_t h) {
    return (h & APPENDAGE_GLYPH_TAG_MASK) == APPENDAGE_GLYPH_TAG;
}
static inline unsigned appendage_glyph_art(uint64_t h) { return (unsigned)(h >> 32 & 127); }
// Inline text-icon atlas: a second tag family. Bits 32..43 name a replacement signature (a set of art choices); bits 44..55
// stay zero. The low 32 bits, texture id and sampler id, are untouched like the HUD tag's.
#define INLINE_GLYPH_TAG 0xa600000000000000ull
#define INLINE_GLYPH_TAG_MASK 0xfffff00000000000ull
static inline int appendage_inline_tagged(uint64_t h) { return (h & INLINE_GLYPH_TAG_MASK) == INLINE_GLYPH_TAG; }
static inline unsigned appendage_inline_id(uint64_t h) { return (unsigned)(h >> 32 & 0xfff); }
static inline int appendage_handle_tagged(uint64_t h) { return appendage_glyph_tagged(h) || appendage_inline_tagged(h); }
void appendage_glyph_attach(const Cpu *c);
// Exact saved callers in the verified shared-selector frame identify supported guides.
void appendage_glyph_lookup_attach(const Cpu *c, uint64_t frame);
void appendage_glyph_retire(uint64_t node);
void appendage_glyph_bind(Cpu *c, uint64_t node, GuestFn original);
uint64_t appendage_glyph_handle(const Cpu *c, uint64_t original);
// Called with the texture lock held; copies serial/storage identity, never a guest pointer.
int appendage_glyph_source(uint64_t h, uint64_t serial, uint64_t storage);
int vk_appendage_capture_source(uint64_t handle);
// Inline atlas and icon_button sheet: the art choice of a signature (an art id per symbol, -1 keeps the native cell) and the
// layout it applies to (INLINE_LAYOUT_*), and its source identity. Both copy scalars under the module lock; the texture lock may
// be held.
enum { INLINE_LAYOUT_FONT, INLINE_LAYOUT_SHEET, INLINE_LAYOUT_STICKS, INLINE_LAYOUT_HINT, INLINE_LAYOUT_SOUL, INLINE_LAYOUT_HOLD, INLINE_LAYOUTS };
// The signature (1-based) the current style gives a single baked image recognized by content (INLINE_LAYOUT_HINT: the mini-map hint;
// INLINE_LAYOUT_SOUL: the Soul Challenge button; INLINE_LAYOUT_HOLD: the field-action button while it is held), or 0 when it stays native (Original, nothing replaced).
unsigned appendage_plain_signature(int layout);
int appendage_inline_signature(unsigned id, int16_t art[GLYPH_SYMBOLS], int *layout);
int appendage_inline_source(uint64_t h, uint64_t serial, uint64_t storage);
// storage 0 (the icon_button sheet has no guest storage pointer of its own) takes the registered texture's own address.
int vk_inline_capture_source(uint64_t handle, uint64_t storage);
// Read-only, opt-in diagnostic at the verified original BindTexture import.
void vk_glyph_resource_observe(uint64_t handle, unsigned event);
// Opt-in (XCX_GLYPH_ICON_PROBE=1), read-only. Entry of f_7000a21afc, the ML:icon layout method (layouter vtable slot +0x80).
void appendage_glyph_icon_layout(const Cpu *c);
// Opt-in diagnostic around the two f_7000a322cc binds of the icon source (node+0x3b8); always calls original unchanged.
void appendage_glyph_icon_bind(Cpu *c, GuestFn original);
int appendage_glyph_failure(const char *stage);

void appendage_glyph_note_fallback(void);
const char *appendage_glyph_runtime_note(void);
