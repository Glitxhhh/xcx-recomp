// Synthetic inline-icon source used by the unit and GPU tests: the 48-record table is the closed form recorded in
// runtime/inline_glyph.h, so no game data is involved. The Python test checks its SHA-256 against INLINE_RECORDS_SHA256.
#pragma once
#include <stdint.h>
#include <string.h>
#include "../runtime/inline_glyph.h"

static void inline_atlas_records(uint8_t *out) {   // INLINE_RECORDS * INLINE_RECORD_BYTES
    for (int i = 0; i < INLINE_RECORDS; i++) {
        int col = i % INLINE_COLUMNS, row = i / INLINE_COLUMNS;
        float w[6] = {1.0f, 1.0f, (float)(45.0 / 368.0), (float)(149.0 / 900.0),
                      (float)((col * 46 + 0.5) / 368.0), (float)((row * 50 + 0.5) / 300.0)};
        uint16_t sz[2] = {46, 50};
        memcpy(out + i * INLINE_RECORD_BYTES, w, sizeof w);
        memcpy(out + i * INLINE_RECORD_BYTES + sizeof w, sz, sizeof sz);
    }
}

// The icon_button sheet's 24 records, closed form: 8 columns x 3 rows of 64x64 cells in a 512x192 image.
static void inline_sheet_records(uint8_t *out) {   // SHEET_RECORDS * INLINE_RECORD_BYTES
    for (int i = 0; i < SHEET_RECORDS; i++) {
        int col = i % SHEET_COLUMNS, row = i / SHEET_COLUMNS;
        float w[6] = {1.0f, 1.0f, (float)(63.0 / 512.0), (float)(63.0 / 192.0),
                      (float)((col * 64 + 0.5) / 512.0), (float)((row * 64 + 0.5) / 192.0)};
        uint16_t sz[2] = {64, 64};
        memcpy(out + i * INLINE_RECORD_BYTES, w, sizeof w);
        memcpy(out + i * INLINE_RECORD_BYTES + sizeof w, sz, sizeof sz);
    }
}

// The stick-direction image's 4 records, closed form: 4 cells of 88x88 in a 352x88 image.
static void inline_sticks_records(uint8_t *out) {   // STICKS_RECORDS * INLINE_RECORD_BYTES
    for (int i = 0; i < STICKS_RECORDS; i++) {
        float w[6] = {1.0f, 1.0f, (float)(87.75 / 352.0), (float)(87.0 / 88.0), (float)((i * 88 + 0.5) / 352.0), (float)(0.5 / 88.0)};
        uint16_t sz[2] = {88, 88};
        memcpy(out + i * INLINE_RECORD_BYTES, w, sizeof w);
        memcpy(out + i * INLINE_RECORD_BYTES + sizeof w, sz, sizeof sz);
    }
}
