#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_glyphs.h"
#include "cfg.h"
#include "host.h"
#include "appendage_glyph.h"

static int magnitude(int32_t value) { return value == INT32_MIN ? INT32_MAX : value < 0 ? -value : value; }
void glyph_intent_sample(GlyphIntent *s, const GlyphIntentSample *p) {
    int changed = p->device != s->device;
    if (changed && s->source == GLYPH_SOURCE_PAD) s->source = GLYPH_SOURCE_NONE;
    int baseline = !p->active || !s->active;
    if (!baseline) {
        int keyboard = 0, controller = 0;
        for (int i = 1; i < KEY_COUNT; i++) keyboard |= p->keys[i] && !s->keys[i];
        if (!changed && p->device) controller = (p->buttons & ~s->buttons) != 0;
        for (int i = 0; i < 6; i++) {
            int value = i < 4 ? magnitude(p->axes[i]) : p->axes[i];
            unsigned bit = 1u << i;
            if (i < 4 && ((p->axes[i] > 20000 && s->previous_axes[i] < -20000) ||
                          (p->axes[i] < -20000 && s->previous_axes[i] > 20000))) {
                s->axes &= ~bit; s->axis_since[i] = p->now;
            }
            if (value < 10000) { s->axes &= ~bit; s->axis_since[i] = 0; }
            else if (value > 20000 && !(s->axes & bit) && !changed && p->device) {
                if (!s->axis_since[i]) s->axis_since[i] = p->now;
                if (p->now - s->axis_since[i] >= 40000000) { s->axes |= bit; controller = 1; }
            } else if (value <= 20000) s->axis_since[i] = 0;
        }
        if (p->captured && s->captured && p->now >= s->pointer_ready) {
            uint64_t distance = (uint64_t)magnitude(p->mouse_x) + (uint64_t)magnitude(p->mouse_y);
            if (p->now - s->pointer_since > 150000000) {
                s->pointer_since = p->now; s->pointer_distance = s->pointer_samples = 0;
            }
            // A single warp/spike or incidental motion cannot change the source.
            if (distance && distance < 512) { s->pointer_distance += (unsigned)distance; s->pointer_samples++; }
            if (s->pointer_distance >= 32 && s->pointer_samples >= 2 && p->now - s->pointer_since >= 35000000) {
                keyboard = 1; s->pointer_distance = s->pointer_samples = 0; s->pointer_since = p->now;
            }
        }
        if (controller) s->source = GLYPH_SOURCE_PAD;
        if (keyboard) s->source = GLYPH_SOURCE_KEYBOARD; // A fresh key/click wins simultaneous sampling ties.
    }
    if (baseline || changed) {
        s->axes = 0;
        memset(s->axis_since, 0, sizeof s->axis_since);
        for (int i = 0; i < 6; i++) if ((i < 4 ? magnitude(p->axes[i]) : p->axes[i]) >= 10000) s->axes |= 1u << i;
    }
    if (baseline || p->captured != s->captured) {
        s->pointer_ready = p->now + 250000000;
        s->pointer_since = p->now; s->pointer_distance = s->pointer_samples = 0;
    }
    memcpy(s->previous_axes, p->axes, sizeof s->previous_axes);
    memcpy(s->keys, p->keys, sizeof s->keys);
    s->buttons = p->buttons; s->device = p->device; s->active = p->active; s->captured = p->captured;
}
static const char *const names[] = {"Original", "Automatic", "PlayStation", "Xbox", "Steam Controller", "Keyboard / Mouse"};
static const char *const values[] = {"original", "auto", "playstation", "xbox", "steam", "keyboard"};
int glyph_style_parse(const char *value) {
    for (int i = 0; value && i < GLYPH_STYLE_COUNT; i++) if (!strcmp(value, values[i])) return i;
    return GLYPH_ORIGINAL;
}
// The Button Icon Type and Button Appearance settings, read again only when the settings change (cfg_version): the icon
// checks ask on every image pane the game binds, hundreds of times a frame, and cfg_value starts with a getenv.
static unsigned style_version = ~0u, appearance_version = ~0u;
static int style_cached, appearance_cached;
int glyph_style_setting(void) {
    unsigned v = cfg_version();
    if (__atomic_load_n(&style_version, __ATOMIC_ACQUIRE) != v) {
        __atomic_store_n(&style_cached, glyph_style_parse(cfg_value("XCX_GLYPH_STYLE")), __ATOMIC_RELAXED);
        __atomic_store_n(&style_version, v, __ATOMIC_RELEASE);
    }
    return __atomic_load_n(&style_cached, __ATOMIC_RELAXED);
}
int glyph_appearance_setting(void) {
    unsigned v = cfg_version();
    if (__atomic_load_n(&appearance_version, __ATOMIC_ACQUIRE) != v) {
        __atomic_store_n(&appearance_cached, glyph_appearance_parse(cfg_value("XCX_GLYPH_APPEARANCE")), __ATOMIC_RELAXED);
        __atomic_store_n(&appearance_version, v, __ATOMIC_RELEASE);
    }
    return __atomic_load_n(&appearance_cached, __ATOMIC_RELAXED);
}
const char *glyph_style_name(int style) { return style >= 0 && style < GLYPH_STYLE_COUNT ? names[style] : names[0]; }
static int pad_style(int style) { return style == GLYPH_PLAYSTATION || style == GLYPH_XBOX || style == GLYPH_STEAM; }
// The style a preference resolves to for the current source and controller family (Automatic follows the last-used device).
static int effective_style(int preference, int source, int family, const char **reason) {
    int style = preference;
    if (style < 0 || style >= GLYPH_STYLE_COUNT) style = GLYPH_ORIGINAL;
    if (style != GLYPH_AUTO) return style;
    if (source == GLYPH_SOURCE_KEYBOARD) return GLYPH_KEYBOARD;
    if (source == GLYPH_SOURCE_PAD) {
        const int styles[] = {GLYPH_ORIGINAL,GLYPH_ORIGINAL,GLYPH_PLAYSTATION,GLYPH_XBOX,GLYPH_STEAM};
        if (family == GLYPH_PAD_UNKNOWN) *reason = "Original virtual labels: controller family is unknown";
        return family >= 0 && family < 5 ? styles[family] : GLYPH_ORIGINAL;
    }
    *reason = "Original virtual labels: waiting for intentional input";
    return GLYPH_ORIGINAL;
}
static GlyphDecision decide(int preference, int source, int family, int layout, const InputMap *map, const int *actions, int count) {
    GlyphDecision d = {.style = preference, .art = {-1,-1,-1}, .reason = NULL};
    d.style = effective_style(preference, source, family, &d.reason);
    // Original, including Automatic on Nintendo controllers, retains native virtual artwork.
    if (d.style == GLYPH_ORIGINAL) return d;
    if (d.style != GLYPH_KEYBOARD && (layout < 0 || layout >= PAD_LAYOUT_COUNT)) {
        d.reason = "Original virtual labels: no exposed controller bindings"; goto fallback;
    }
    for (int n = 0; n < count; n++) {
        uint8_t inputs[INPUT_BINDING_MAX];
        int count = d.style == GLYPH_KEYBOARD ? input_map_keys_of(map, actions[n], inputs, sizeof inputs) :
                    input_map_pads_of(map, actions[n], layout, inputs, sizeof inputs);
        if (count != 1) { d.reason = "Original virtual labels: a guide action is unbound or has multiple bindings"; goto fallback; }
        if (d.style == GLYPH_KEYBOARD && inputs[0] == KEY_NONE) { d.reason = "Original virtual labels: unsupported keyboard label"; goto fallback; }
        if (d.style == GLYPH_KEYBOARD) {
            for (int i = 0; i < map->nkeys; i++) if (map->keys[i].key == inputs[0] && map->keys[i].action != actions[n]) {
                d.reason = "Original virtual labels: a key is shared by different actions"; goto fallback;
            }
        } else {
            for (int i = 0; i < map->npads[layout]; i++) if (map->pads[layout][i].pad == inputs[0] && map->pads[layout][i].action != actions[n]) {
                d.reason = "Original virtual labels: a controller input is shared by different actions"; goto fallback;
            }
        }
        d.art[n] = glyph_art_find(d.style, inputs[0]);
        if (d.art[n] < 0) { d.reason = "Original virtual labels: this binding has no verified glyph"; goto fallback; }
    }
    return d;
fallback:
    d.style = GLYPH_ORIGINAL;
    for (int i = 0; i < 3; i++) d.art[i] = -1;
    return d;
}
GlyphDecision glyph_decide(int preference, int source, int family, int layout, const InputMap *map) {
    static const int actions[3] = {B_R,B_Y,B_A};
    return decide(preference,source,family,layout,map,actions,3);
}
GlyphDecision glyph_decide_button(int preference, int source, int family, int layout, const InputMap *map, int action) {
    // Only the verified shared-widget actions are supported by this adapter.
    if (action != B_X && action != B_A && action != B_R && action != B_ZL) return (GlyphDecision){.style=GLYPH_ORIGINAL,.art={-1,-1,-1},.reason="Original virtual label: unsupported guide action"};
    return decide(preference,source,family,layout,map,&action,1);
}
// The symbols that stand for one virtual action each. Power, home and capture have no host input and stay native; the
// compound D-pad symbols and the two sticks are decided separately below.
static const struct { int symbol, action; } symbol_actions[] = {
    {SYM_MNS, B_MINUS}, {SYM_PLS, B_PLUS}, {SYM_X, B_X}, {SYM_Y, B_Y}, {SYM_A, B_A}, {SYM_B, B_B},
    {SYM_ANALP, B_STICK_L}, {SYM_ANARP, B_STICK_R}, {SYM_L, B_L}, {SYM_R, B_R}, {SYM_ZL, B_ZL}, {SYM_ZR, B_ZR},
    {SYM_CRSU, B_UP}, {SYM_CRSD, B_DOWN}, {SYM_CRSL, B_LEFT}, {SYM_CRSR, B_RIGHT},
};
// A group of D-pad actions drawn as one symbol: the pad inputs of the actions must be exactly the expected D-pad inputs (in any
// assignment), each bound once and shared with no other action. Rebound or mixed directions keep the native symbol.
static int dpad_group_art(int style, int layout, const InputMap *map, const int *actions, int n, const uint8_t *expected, int art_input) {
    uint8_t seen[4] = {0};
    for (int i = 0; i < n; i++) {
        uint8_t inputs[INPUT_BINDING_MAX];
        if (input_map_pads_of(map, actions[i], layout, inputs, sizeof inputs) != 1) return -1;
        for (int j = 0; j < map->npads[layout]; j++)
            if (map->pads[layout][j].pad == inputs[0] && map->pads[layout][j].action != actions[i]) return -1;
        int hit = -1;
        for (int j = 0; j < n; j++) if (inputs[0] == expected[j]) hit = j;
        if (hit < 0 || seen[hit]) return -1;
        seen[hit] = 1;
    }
    return glyph_art_find(style, art_input);
}
// Keyboard: the directions of a stick or of the D-pad (actions in the order left, up, right, down) each bound to exactly one key
// that no other action uses. When the keys of the directions a shape stands for (0 all four, 1 up and down, 2 left and right) are
// those of a known cluster, in the cluster's own positions, the cluster's tile for that shape is used; anything else stays native.
static int key_group_art(const InputMap *map, const int *actions, int shape) {
    static const uint8_t clusters[3][4] = {{KEY_LEFT, KEY_UP, KEY_RIGHT, KEY_DOWN}, {KEY_A, KEY_W, KEY_D, KEY_S}, {KEY_1, KEY_2, KEY_3, KEY_4}};
    static const uint8_t wanted[3][4] = {{1, 1, 1, 1}, {0, 1, 0, 1}, {1, 0, 1, 0}};   // the directions each shape stands for
    uint8_t bound[4] = {0};
    for (int i = 0; i < 4; i++) {
        if (!wanted[shape][i]) continue;
        uint8_t keys[INPUT_BINDING_MAX];
        if (input_map_keys_of(map, actions[i], keys, sizeof keys) != 1 || keys[0] == KEY_NONE) return -1;
        for (int j = 0; j < map->nkeys; j++) if (map->keys[j].key == keys[0] && map->keys[j].action != actions[i]) return -1;
        bound[i] = keys[0];
    }
    for (int c = 0; c < 3; c++) {
        int same = 1;
        for (int i = 0; i < 4; i++) same &= !wanted[shape][i] || bound[i] == clusters[c][i];
        if (same) return glyph_art_find(GLYPH_KEYBOARD, GLYPH_IN_KB_BASE + c * 3 + shape);
    }
    return -1;
}
void glyph_decide_symbol_set(int preference, int source, int family, int layout, const InputMap *map, int appearance, int16_t art[GLYPH_SYMBOLS]) {
    for (int i = 0; i < GLYPH_SYMBOLS; i++) art[i] = -1;
    const char *reason = NULL;
    int style = effective_style(preference, source, family, &reason);
    if (style == GLYPH_ORIGINAL) return;
    for (unsigned i = 0; i < sizeof symbol_actions / sizeof *symbol_actions; i++) {
        GlyphDecision d = decide(preference, source, family, layout, map, &symbol_actions[i].action, 1);
        art[symbol_actions[i].symbol] = d.style != GLYPH_ORIGINAL && d.art[0] >= 0 ? (int16_t)glyph_art_variant(d.art[0], appearance) : -1;
    }
    if (style == GLYPH_KEYBOARD) {   // sticks and D-pad groups: the keys they are bound to, when those form a known cluster
        static const int sl[4] = {B_SL_LEFT, B_SL_UP, B_SL_RIGHT, B_SL_DOWN}, sr[4] = {B_SR_LEFT, B_SR_UP, B_SR_RIGHT, B_SR_DOWN},
                         dp[4] = {B_LEFT, B_UP, B_RIGHT, B_DOWN};
        art[SYM_ANAL] = (int16_t)key_group_art(map, sl, 0);  art[SYM_SLV] = (int16_t)key_group_art(map, sl, 1);
        art[SYM_SLH] = (int16_t)key_group_art(map, sl, 2);
        art[SYM_ANAR] = (int16_t)key_group_art(map, sr, 0);  art[SYM_SRV] = (int16_t)key_group_art(map, sr, 1);
        art[SYM_SRH] = (int16_t)key_group_art(map, sr, 2);
        art[SYM_CRS] = (int16_t)key_group_art(map, dp, 0);   art[SYM_CRSUD] = (int16_t)key_group_art(map, dp, 1);
        art[SYM_CRSLR] = (int16_t)key_group_art(map, dp, 2);
        return;
    }
    if (!pad_style(style) || layout < 0 || layout >= PAD_LAYOUT_COUNT) return;   // compound D-pad symbols and sticks: pad styles only
    static const int all4[4] = {B_LEFT, B_UP, B_RIGHT, B_DOWN}, ud[2] = {B_UP, B_DOWN}, lr[2] = {B_LEFT, B_RIGHT};
    static const uint8_t all4_in[4] = {PAD_DPAD_LEFT, PAD_DPAD_UP, PAD_DPAD_RIGHT, PAD_DPAD_DOWN}, ud_in[2] = {PAD_DPAD_UP, PAD_DPAD_DOWN},
                         lr_in[2] = {PAD_DPAD_LEFT, PAD_DPAD_RIGHT};
    art[SYM_CRS] = (int16_t)dpad_group_art(style, layout, map, all4, 4, all4_in, GLYPH_IN_DPAD);
    art[SYM_CRSUD] = (int16_t)dpad_group_art(style, layout, map, ud, 2, ud_in, GLYPH_IN_DPAD_V);
    art[SYM_CRSLR] = (int16_t)dpad_group_art(style, layout, map, lr, 2, lr_in, GLYPH_IN_DPAD_H);
    // The sticks as axes (movement and camera) are not rebindable on a pad: a fixed tile for the family.
    art[SYM_ANAL] = (int16_t)glyph_art_find(style, GLYPH_IN_STICK_L);
    art[SYM_ANAR] = (int16_t)glyph_art_find(style, GLYPH_IN_STICK_R);
    art[SYM_SLV] = (int16_t)glyph_art_find(style, GLYPH_IN_STICK_L_V);
    art[SYM_SLH] = (int16_t)glyph_art_find(style, GLYPH_IN_STICK_L_H);
    art[SYM_SRV] = (int16_t)glyph_art_find(style, GLYPH_IN_STICK_R_V);
    art[SYM_SRH] = (int16_t)glyph_art_find(style, GLYPH_IN_STICK_R_H);
}
void glyph_current_symbol_set(int16_t art[GLYPH_SYMBOLS]) {
    int source, family, layout; InputMap map;
    input_glyph_snapshot(&source, &family, &layout, &map);
    glyph_decide_symbol_set(glyph_style_setting(), source, family, layout, &map,
                            glyph_appearance_setting(), art);
}
// The eight ordinary buttons only, in the order A, B, X, Y, L, R, ZL, ZR (the first inline-atlas pass).
static const int inline_symbols[GLYPH_INLINE_SET] = {SYM_A, SYM_B, SYM_X, SYM_Y, SYM_L, SYM_R, SYM_ZL, SYM_ZR};
void glyph_decide_inline_set(int preference, int source, int family, int layout, const InputMap *map, int appearance, int16_t art[GLYPH_INLINE_SET]) {
    int16_t all[GLYPH_SYMBOLS];
    glyph_decide_symbol_set(preference, source, family, layout, map, appearance, all);
    for (int i = 0; i < GLYPH_INLINE_SET; i++) art[i] = all[inline_symbols[i]];
}
void glyph_current_inline_set(int16_t art[GLYPH_INLINE_SET]) {
    int16_t all[GLYPH_SYMBOLS];
    glyph_current_symbol_set(all);
    for (int i = 0; i < GLYPH_INLINE_SET; i++) art[i] = all[inline_symbols[i]];
}
int glyph_build_supported(void) {
    for (int i = 0; i < rt_exefs_sha256_count; i++)
        if (!strcmp(rt_exefs_sha256[i].file,"main"))
            return !strcmp(rt_exefs_sha256[i].sha256,"0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026");
    return 0;
}
int glyph_appearance_parse(const char *value) { return value && !strcmp(value,"colored") ? GLYPH_COLORED : GLYPH_STANDARD; }
static GlyphDecision appearance(GlyphDecision d) {
    int variant = glyph_appearance_setting();
    for (int i=0;i<3;i++) if (d.art[i]>=0) d.art[i]=glyph_art_variant(d.art[i],variant);
    return d;
}
GlyphDecision glyph_current_decision(void) {
    int source, family, layout; InputMap map;
    input_glyph_snapshot(&source,&family,&layout,&map);
    return appearance(glyph_decide(glyph_style_setting(),source,family,layout,&map));
}
GlyphDecision glyph_current_button(int action) {
    int source,family,layout; InputMap map;
    input_glyph_snapshot(&source,&family,&layout,&map);
    return appearance(glyph_decide_button(glyph_style_setting(),source,family,layout,&map,action));
}
const char *glyph_status(void) {
    static _Thread_local char status[1280];
    int source,family,layout; InputMap map;
    input_glyph_snapshot(&source,&family,&layout,&map);
    int preference = glyph_style_setting();
    GlyphDecision d = glyph_decide(preference,source,family,layout,&map);
    GlyphDecision weapon = glyph_decide_button(preference,source,family,layout,&map,B_X);
    GlyphDecision target = glyph_decide_button(preference,source,family,layout,&map,B_R);
    GlyphDecision board = glyph_decide_button(preference,source,family,layout,&map,B_ZL);
    GlyphDecision field = glyph_decide_button(preference,source,family,layout,&map,B_A);
    const char *note = preference != GLYPH_ORIGINAL ? appendage_glyph_runtime_note() : NULL;
    if (note) snprintf(status,sizeof status,"Verified HUD guides — %s",note);
    else if (d.reason || weapon.reason || field.reason || target.reason || board.reason)
        snprintf(status,sizeof status,"Appendage: %s; weapon: %s; field A: %s; Target: %s; Board: %s",
            d.reason ? d.reason : glyph_style_name(d.style),
            weapon.reason ? weapon.reason : glyph_style_name(weapon.style),
            field.reason ? field.reason : glyph_style_name(field.style),
            target.reason ? target.reason : glyph_style_name(target.style),
            board.reason ? board.reason : glyph_style_name(board.style));
    else snprintf(status,sizeof status,"Verified HUD guides — %s",glyph_style_name(d.style));
    return status;
}
