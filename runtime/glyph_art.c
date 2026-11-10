// Fixed licensed tiles, copied only into five verified HUD button atlas regions.
#include <string.h>
#include <zstd.h>
#include "input_glyphs.h"
#include "glyph_art.inc"
unsigned glyph_art_count(void) { return sizeof glyph_arts / sizeof *glyph_arts; }
int glyph_art_find(int style, int physical) {
    for (unsigned i = 0; i < glyph_art_count(); i++)
        if (glyph_arts[i].style == style && glyph_arts[i].input == physical && glyph_arts[i].appearance == GLYPH_STANDARD) return (int)i;
    return -1;
}
int glyph_art_variant(int art, int appearance) {
    if (art < 0 || (unsigned)art >= glyph_art_count()) return -1;
    if (appearance == GLYPH_COLORED) for (unsigned i=0;i<glyph_art_count();i++)
        if (glyph_arts[i].style == glyph_arts[art].style && glyph_arts[i].input == glyph_arts[art].input &&
            glyph_arts[i].appearance == GLYPH_COLORED) return (int)i;
    return art;
}
const char *glyph_art_name(unsigned art) { return art < glyph_art_count() ? glyph_arts[art].name : "original"; }
int glyph_art_pixels(unsigned art, uint32_t *pixels) {
    if (art >= glyph_art_count()) return 0;
    uint32_t tile[64*64];
    const typeof(glyph_arts[0]) *a = &glyph_arts[art];
    size_t n = ZSTD_decompress(tile,sizeof tile,glyph_art_bytes+a->offset,a->length);
    if (ZSTD_isError(n) || n != sizeof tile) return 0;
    memset(pixels,0,512*192*4);
    // Verified half-texel origins: X=(320.5,.5), Y=(384.5,.5), A=(448.5,.5), R=(384.5,64.5), ZL=(448.5,64.5).
    for (unsigned row = 0; row < 64; row++) {
        memcpy(pixels+row*512+320,tile+row*64,256); // Shared weapon-change X.
        memcpy(pixels+row*512+384,tile+row*64,256);
        memcpy(pixels+row*512+448,tile+row*64,256);
        memcpy(pixels+(row+64)*512+384,tile+row*64,256);
        memcpy(pixels+(row+64)*512+448,tile+row*64,256); // Shared Board ZL.
    }
    return 1;
}

void glyph_art_to_mask(uint32_t *cell, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t p = cell[i], a = p >> 24;
        uint32_t luma = ((p & 255) * 299 + (p >> 8 & 255) * 587 + (p >> 16 & 255) * 114) / 1000;
        uint32_t v = luma <= 40 ? 0 : luma >= 170 ? 255 : (luma - 40) * 255 / 130;
        cell[i] = a ? (0x00ffffffu | (a * v / 255) << 24) : 0;
    }
}
// The soft dark shadow the mini-map hint picture draws around its drawings: texels of RGB 14,15,23 whose alpha falls off with the distance
// to the drawing (measured on the native image: 115 next to it, 82 at 3 texels, 61 at 5, 47 at 7, 38 at 9, 31 at 11, 24 at 13, 18 at 17, 13
// at 21, and about 13 beyond). A cell holding a replacement glyph gets the same shadow around the glyph and the glyph is composited over it, so no edge of a
// plain cleared (or flat-filled) rectangle shows against the native shadow next to it.
void glyph_art_shadowed(uint32_t *cell, int cw, int ch) {
    static const float knots[][2] = {{0, 128}, {1, 115}, {3, 82}, {5, 61}, {7, 47}, {9, 38}, {11, 31}, {13, 24}, {15, 20}, {17, 18}, {19, 16}, {21, 13}, {40, 11}};
    enum { N = sizeof knots / sizeof knots[0] };
    for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) {
        const uint32_t p = cell[y * cw + x];
        float best = 1e9f;
        for (int j = 0; j < ch && best > 0.5f; j++) for (int i = 0; i < cw; i++)
            if ((cell[j * cw + i] >> 24) >= 128) { const float d = (float)((i - x) * (i - x) + (j - y) * (j - y)); if (d < best) best = d; }
        float dist = 99.0f;   // square root by Newton steps: this file is also built into tests that do not link libm
        if (best < 1e8f) { dist = best + 1.0f; for (int k = 0; k < 24; k++) dist = 0.5f * (dist + best / dist); }
        float sa = 0;
        for (int k = 1; k < N; k++) if (dist <= knots[k][0]) {
            const float t = (dist - knots[k - 1][0]) / (knots[k][0] - knots[k - 1][0]);
            sa = (knots[k - 1][1] + t * (knots[k][1] - knots[k - 1][1])) / 255.0f; break;
        }
        const float a = (float)(p >> 24) / 255.0f, oa = a + sa * (1.0f - a);
        if (oa <= 0) { cell[y * cw + x] = 0; continue; }
        static const float shade[3] = {14, 15, 23};
        uint32_t out = 0;
        for (int c = 0; c < 3; c++) {
            const float v = ((float)(p >> (8 * c) & 255) * a + shade[c] * sa * (1.0f - a)) / oa;
            out |= (uint32_t)(v + 0.5f > 255.0f ? 255.0f : v + 0.5f) << (8 * c);
        }
        cell[y * cw + x] = out | (uint32_t)(oa * 255.0f + 0.5f) << 24;
    }
}

// The field-action button while it is held: a teal ring (RGBA 0,166,166,254) around a dark teal disc (0,88,96,207) centered in the cell, with
// a soft shadow below (measured on the native image: disc radius 24, ring about 3.5 texels wide). The fitted glyph is composited over it.
void glyph_art_hold_button(uint32_t *cell, int cw, int ch) {
    static const float ring[4] = {0, 166, 166, 254}, disc[4] = {0, 88, 96, 207};
    const float cx = (float)cw * 0.5f, cy = (float)ch * 0.5f;
    for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) {
        float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, r2 = dx * dx + dy * dy, r = 0;
        if (r2 > 0) { r = r2; for (int k = 0; k < 20; k++) r = 0.5f * (r + r2 / r); }   // sqrt by Newton steps (no libm in every test build)
        float sdy = dy - 2.0f, sr2 = dx * dx + sdy * sdy, sr = 0;
        if (sr2 > 0) { sr = sr2; for (int k = 0; k < 20; k++) sr = 0.5f * (sr + sr2 / sr); }
        float col[3] = {0, 0, 0}, a = 0;
        const float shadow = sr < 24.0f ? 0.0f : sr > 29.0f ? 0.0f : (29.0f - sr) / 5.0f * 70.0f / 255.0f;
        if (r <= 24.5f) {   // the ring with a soft outer edge, then the disc
            const float edge = r > 23.5f ? 24.5f - r : 1.0f, inner = r < 20.5f ? 0.0f : r < 21.5f ? r - 20.5f : 1.0f;
            const float ra = ring[3] / 255.0f * edge * inner, da = disc[3] / 255.0f * (1.0f - inner) * (r <= 21.0f ? 1.0f : 0.0f);
            a = ra + da;
            if (a > 0) for (int c = 0; c < 3; c++) col[c] = (ring[c] * ra + disc[c] * da) / a;
        }
        const float sa = shadow * (1.0f - a), oa = a + sa;
        float out[3] = {0, 0, 0};
        if (oa > 0) for (int c = 0; c < 3; c++) out[c] = (col[c] * a) / oa;
        // the glyph over the result (straight alpha)
        const uint32_t p = cell[y * cw + x];
        const float ga = (float)(p >> 24) / 255.0f, fa = ga + oa * (1.0f - ga);
        uint32_t px = 0;
        if (fa > 0) {
            for (int c = 0; c < 3; c++) {
                const float v = ((float)(p >> (8 * c) & 255) * ga + out[c] * oa * (1.0f - ga)) / fa;
                px |= (uint32_t)(v + 0.5f > 255.0f ? 255.0f : v + 0.5f) << (8 * c);
            }
            px |= (uint32_t)(fa * 255.0f + 0.5f) << 24;
        }
        cell[y * cw + x] = px;
    }
}

// Fits one licensed tile into the native glyph box (x, y, w, h) of a cw x ch icon cell (46x50 in the inline atlas, 64x64 in the
// icon_button sheet): the tile is cropped to its
// visible area, scaled to fit the box with its aspect kept, centred in it and area-resampled in premultiplied alpha so
// edges stay clean. Everything outside the fitted art is transparent. cell is the whole cw x ch RGBA8 cell.
int glyph_art_inline_cell(unsigned art, int x, int y, int w, int h, uint32_t *cell) { return glyph_art_cell(art, 46, 50, x, y, w, h, cell); }
int glyph_art_cell(unsigned art, int CW, int CH, int x, int y, int w, int h, uint32_t *cell) {
    enum { T = 64 };
    if (art >= glyph_art_count() || CW <= 0 || CH <= 0 || CW > 128 || CH > 128 || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > CW || y + h > CH) return 0;
    uint32_t tile[T * T];
    const typeof(glyph_arts[0]) *a = &glyph_arts[art];
    size_t n = ZSTD_decompress(tile, sizeof tile, glyph_art_bytes + a->offset, a->length);
    if (ZSTD_isError(n) || n != sizeof tile) return 0;
    int x0 = T, y0 = T, x1 = -1, y1 = -1;
    for (int j = 0; j < T; j++) for (int i = 0; i < T; i++)
        if ((tile[j * T + i] >> 24) > 8) { if (i < x0) x0 = i; if (i > x1) x1 = i; if (j < y0) y0 = j; if (j > y1) y1 = j; }
    if (x1 < x0 || y1 < y0) return 0;
    int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    float scale = (float)w / (float)bw < (float)h / (float)bh ? (float)w / (float)bw : (float)h / (float)bh;
    int tw = (int)(bw * scale + 0.5f), th = (int)(bh * scale + 0.5f);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    if (tw > w) tw = w;
    if (th > h) th = h;
    int ox = x + (w - tw) / 2, oy = y + (h - th) / 2;
    memset(cell, 0, CW * CH * sizeof *cell);
    for (int j = 0; j < th; j++) for (int i = 0; i < tw; i++) {
        // Source rectangle of this destination texel, in tile pixels.
        float sx0 = x0 + (float)i * bw / tw, sx1 = x0 + (float)(i + 1) * bw / tw;
        float sy0 = y0 + (float)j * bh / th, sy1 = y0 + (float)(j + 1) * bh / th;
        float r = 0, g = 0, b = 0, al = 0, wsum = 0;
        for (int sy = (int)sy0; sy < (int)(sy1 + 0.999f) && sy < T; sy++) for (int sx = (int)sx0; sx < (int)(sx1 + 0.999f) && sx < T; sx++) {
            float cx = (sx + 1 < sx1 ? sx + 1 : sx1) - (sx > sx0 ? sx : sx0);
            float cy = (sy + 1 < sy1 ? sy + 1 : sy1) - (sy > sy0 ? sy : sy0);
            if (cx <= 0 || cy <= 0) continue;
            uint32_t p = tile[sy * T + sx];
            float pa = (float)(p >> 24) / 255.0f, wgt = cx * cy;
            r += (float)(p & 255) * pa * wgt; g += (float)(p >> 8 & 255) * pa * wgt; b += (float)(p >> 16 & 255) * pa * wgt;
            al += pa * wgt; wsum += wgt;
        }
        if (wsum <= 0 || al <= 0) continue;
        float out_a = al / wsum;
        uint32_t R = (uint32_t)(r / al + 0.5f), G = (uint32_t)(g / al + 0.5f), B = (uint32_t)(b / al + 0.5f), A = (uint32_t)(out_a * 255.0f + 0.5f);
        cell[(oy + j) * CW + ox + i] = (R > 255 ? 255 : R) | (G > 255 ? 255 : G) << 8 | (B > 255 ? 255 : B) << 16 | (A > 255 ? 255 : A) << 24;
    }
    return 1;
}

// A round button's cell (a face button of the icon_button sheet): the native glyph is a disc, so its box is the disc's bounding square. A
// tile with corners (a square keyboard key, the Space bar) fitted into that box pokes its corners out of the circle. This fits the tile as
// glyph_art_cell does, measures how far its farthest opaque texel is from the box center, and when the tile is not round (that distance
// is more than 1.12 times the radius of a disc with the tile's area) and the distance exceeds 0.9 of the circle's radius, fits it again into
// a box shrunk about the same center so the farthest texel lands there. Round tiles (Xbox, PlayStation, Steam faces) are fitted as before.
int glyph_art_cell_round(unsigned art, int CW, int CH, int x, int y, int w, int h, uint32_t *cell) {
    if (!glyph_art_cell(art, CW, CH, x, y, w, h, cell)) return 0;
    const float cx = (float)x + (float)w * 0.5f, cy = (float)y + (float)h * 0.5f, circle = (float)(w < h ? w : h) * 0.5f;
    float rmax2 = 0, area = 0;
    for (int j = 0; j < CH; j++) for (int i = 0; i < CW; i++) {
        if ((cell[j * CW + i] >> 24) <= 128) continue;
        const float dx = (float)i + 0.5f - cx, dy = (float)j + 0.5f - cy, r2 = dx * dx + dy * dy;
        area += 1.0f;
        if (r2 > rmax2) rmax2 = r2;
    }
    const float target = circle * 0.9f;
    if (area <= 0 || rmax2 <= 1.12f * 1.12f * (area / 3.14159265f) || rmax2 <= target * target) return 1;
    float rmax = rmax2;   // square root by Newton steps (no libm in every test build)
    for (int k = 0; k < 24; k++) rmax = 0.5f * (rmax + rmax2 / rmax);
    const float f = target / rmax;
    int nw = (int)((float)w * f + 0.5f), nh = (int)((float)h * f + 0.5f);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    const int nx = (int)(cx - (float)nw * 0.5f + 0.5f), ny = (int)(cy - (float)nh * 0.5f + 0.5f);
    return glyph_art_cell(art, CW, CH, nx < 0 ? 0 : nx, ny < 0 ? 0 : ny, nw, nh, cell);
}
