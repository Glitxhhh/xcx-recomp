// Display policy for the verified HUD guides. Never changes input mapping or emitted inputs.
#pragma once
#include <stdint.h>
#include "input_map.h"
enum { GLYPH_ORIGINAL, GLYPH_AUTO, GLYPH_PLAYSTATION, GLYPH_XBOX, GLYPH_STEAM, GLYPH_KEYBOARD, GLYPH_STYLE_COUNT };
enum { GLYPH_STANDARD, GLYPH_COLORED };
enum { GLYPH_SOURCE_NONE, GLYPH_SOURCE_KEYBOARD, GLYPH_SOURCE_PAD };
enum { GLYPH_PAD_UNKNOWN, GLYPH_PAD_SWITCH, GLYPH_PAD_PS, GLYPH_PAD_XBOX, GLYPH_PAD_STEAM };
// The HUD guide handle names its art in 7 bits, so the HUD route only draws art ids below GLYPH_ART_MAX (the tiles it can use come
// first in the pack); the inline and layout routes address any tile (an int16_t id per symbol).
enum { GLYPH_ART_MAX = 128 };
typedef struct {
    uint8_t keys[KEY_COUNT];
    uint32_t buttons;
    int32_t axes[6];
    uint64_t device, now;
    int active, captured;
    int32_t mouse_x, mouse_y;
} GlyphIntentSample;
typedef struct {
    uint8_t keys[KEY_COUNT];
    uint32_t buttons;
    uint64_t device, axis_since[6], pointer_since, pointer_ready;
    int32_t previous_axes[6];
    unsigned axes, pointer_distance, pointer_samples;
    int active, captured, source;
} GlyphIntent;
void glyph_intent_sample(GlyphIntent *state, const GlyphIntentSample *sample);
int glyph_style_parse(const char *value);
const char *glyph_style_name(int style);
int glyph_style_setting(void);        // XCX_GLYPH_STYLE parsed, cached until the settings change
int glyph_appearance_setting(void);   // XCX_GLYPH_APPEARANCE parsed, the same way
typedef struct { int style, art[3]; const char *reason; } GlyphDecision;
GlyphDecision glyph_decide(int preference, int source, int family, int layout, const InputMap *map);
// Atomically copies the intent, exposed controller family, layout and current bindings under input.c's lock.
void input_glyph_snapshot(int *source, int *family, int *layout, InputMap *map);
int glyph_build_supported(void);
GlyphDecision glyph_current_decision(void);
GlyphDecision glyph_current_button(int action);
GlyphDecision glyph_decide_button(int preference, int source, int family, int layout, const InputMap *map, int action);
const char *glyph_status(void); // Thread-local copied status for the experimental Controls row.
int glyph_art_find(int style, int physical);
int glyph_art_variant(int art, int appearance);
int glyph_appearance_parse(const char *value);
const char *glyph_art_name(unsigned art);
unsigned glyph_art_count(void);
int glyph_art_pixels(unsigned art, uint32_t *pixels); // 512x192 owned destination, all five verified UV tiles.
// Inline text icons: the virtual buttons A, B, X, Y, L, R, ZL, ZR in that order. Each decision is made on its own (so one
// ambiguous or unbound action keeps only itself native) from the same snapshot; art[i] is an art id or -1 for Original.
// Unlike the guide actions above, these use every verified physical tile (face buttons, L/R, ZL/ZR) for the current layout.
enum { GLYPH_INLINE_SET = 8 };
// Prompt symbols. The inline font atlas (24 standard + 24 large records) and the icon_button sheet (24 cells) draw the same
// symbols in the same order, the msbp `icon` tag order starting at `crs`; a record or cell index modulo 24 is the symbol.
// Each symbol is decided on its own from one snapshot; art is an art id or -1 for the native artwork.
enum { SYM_CRS, SYM_POW, SYM_HOM, SYM_MNS, SYM_PLS, SYM_X, SYM_Y, SYM_A, SYM_B, SYM_ANALP, SYM_ANARP, SYM_ANAL, SYM_ANAR,
       SYM_L, SYM_R, SYM_ZL, SYM_ZR, SYM_CRSU, SYM_CRSD, SYM_CRSL, SYM_CRSR, SYM_CRSUD, SYM_CRSLR, SYM_CAP,
       SYM_PER_SHEET,                                   // 24: the symbols of the text atlas and the icon_button sheet
       SYM_SLV = SYM_PER_SHEET, SYM_SLH, SYM_SRV, SYM_SRH,   // the stick-direction images (icon_button02): no text tag, own sheet
       GLYPH_SYMBOLS };
// Art "inputs" with no single physical input: the whole D-pad, its vertical/horizontal pairs and the two sticks as axes.
enum { GLYPH_IN_DPAD = 0x100, GLYPH_IN_DPAD_V, GLYPH_IN_DPAD_H, GLYPH_IN_STICK_L, GLYPH_IN_STICK_R,
       GLYPH_IN_STICK_L_V, GLYPH_IN_STICK_L_H, GLYPH_IN_STICK_R_V, GLYPH_IN_STICK_R_H };
// Keyboard clusters: a stick or the D-pad bound to the keys of one cluster (arrow keys, WASD, 1-4), drawn as the four keys or as
// the vertical or horizontal pair. Three shapes per cluster, in this order: GLYPH_IN_KB_BASE + cluster * 3 + shape.
enum { GLYPH_IN_KB_BASE = 0x110, GLYPH_IN_KB_ARROWS = GLYPH_IN_KB_BASE, GLYPH_IN_KB_ARROWS_V, GLYPH_IN_KB_ARROWS_H,
       GLYPH_IN_KB_WASD, GLYPH_IN_KB_WASD_V, GLYPH_IN_KB_WASD_H, GLYPH_IN_KB_NUM, GLYPH_IN_KB_NUM_V, GLYPH_IN_KB_NUM_H,
       GLYPH_IN_KB_ARROWS_LIT_H /* the arrow cluster with only left and right lit: drawn for the page-turn circles, never chosen by a binding */ };
void glyph_decide_symbol_set(int preference, int source, int family, int layout, const InputMap *map, int appearance, int16_t art[GLYPH_SYMBOLS]);
void glyph_current_symbol_set(int16_t art[GLYPH_SYMBOLS]);
void glyph_decide_inline_set(int preference, int source, int family, int layout, const InputMap *map, int appearance, int16_t art[GLYPH_INLINE_SET]);
void glyph_current_inline_set(int16_t art[GLYPH_INLINE_SET]);
// Fits a tile into the native glyph box (x, y, w, h) of a cw x ch cell; see glyph_art.c. cell is cw*ch RGBA8.
int glyph_art_cell(unsigned art, int cw, int ch, int x, int y, int w, int h, uint32_t *cell);
// Like glyph_art_cell for the box of a round button's disc: a tile with corners (a keyboard key) is fitted so its corners stay inside the circle.
int glyph_art_cell_round(unsigned art, int cw, int ch, int x, int y, int w, int h, uint32_t *cell);
// A fitted tile as a mask cell (icon_button03): white texels whose alpha is the tile's alpha times its brightness, so the dark parts of
// the glyph are cut out like the native white masks; the game tints what is left.
void glyph_art_to_mask(uint32_t *cell, int count);
// Gives a cw x ch cell holding a fitted glyph the soft dark shadow the mini-map hint picture draws around its drawings, in place.
void glyph_art_shadowed(uint32_t *cell, int cw, int ch);
// Composites a fitted 64x64 glyph over a drawn teal ring and dark teal disc (the field-action button while it is held), in place.
void glyph_art_hold_button(uint32_t *cell, int cw, int ch);
// The 46x50 inline-atlas cell.
int glyph_art_inline_cell(unsigned art, int x, int y, int w, int h, uint32_t *cell);
