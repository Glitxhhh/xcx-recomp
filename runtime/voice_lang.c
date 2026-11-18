// XCX_VOICE_LANG (issue #153): the voice language, Japanese or English, independent of the text language.
//
// How the game picks its voices (2.0.0): the game's options keep the choice as bit 1 of an option word (set:
// Japanese), at +0xa9f84 in the object whose pointer lives at 0x7001dc1408 (0x7000349ce0 returns its address). The
// option word starts from the system language: Japanese for Japanese or Chinese text, English otherwise
// (0x70003473f0). When the game applies its options (0x70000defac) it hands the bit to the sound manager's setter
// (0x70000252c0), which switches Wwise to "jp" or "en" and loads sound/<lang>.pck; the lip sync follows the same value
// (0x7000303a78 opens ev/lip/sev/jp/... or ev/lip/sev/us/...).
//
// So at every present the setting puts that bit to the chosen language, and the game's own code does the switch on
// its own thread the next time it applies its options, as at start-up and on loading a save. Calling the setter from
// the presenting thread instead hung the game in testing, right after the voice package loaded. The write is an
// atomic or/and on one bit, so it never loses the game's other option bits. Unset ("automatic"), nothing is written
// and the game keeps its own choice. Other builds than 2.0.0 lack these addresses: the setting does nothing.
#include <stdint.h>
#include <string.h>

#include "cfg.h"
#include "host.h"

extern const char rt_game_name[];

#define OPTIONS 0x7001dc1408ull   // the pointer to the object holding the game's options
#define OPTION_WORD 0xa9f84       // its option word
#define VOICE_JP 0x2u             // the option word's bit for Japanese voices

// 1 Japanese, 2 English, 0 automatic (the game's choice).
static int wanted(void) {
    const char *v = cfg_value("XCX_VOICE_LANG");
    return !strcmp(v, "ja") ? 1 : !strcmp(v, "en") ? 2 : 0;
}

void voice_lang_tick(void) {
    static int checked, ok, logged;
    static unsigned seen = ~0u;
    static int want;
    if (cfg_version() != seen) {
        seen = cfg_version();
        want = wanted();
        logged = 0;
    }
    if (!want) return;
    if (!checked) {
        checked = 1;
        ok = !strcmp(rt_game_name, "2.0.0");
        if (!ok) rt_log("voice: XCX_VOICE_LANG needs game 2.0.0; ignored");
    }
    if (!ok) return;
    uint64_t opt = __atomic_load_n((volatile uint64_t *)(uintptr_t)OPTIONS, __ATOMIC_ACQUIRE);
    if (!opt) return;
    uint32_t *word = (uint32_t *)(uintptr_t)(opt + OPTION_WORD);
    uint32_t now = __atomic_load_n(word, __ATOMIC_RELAXED);
    if (want == 1 && !(now & VOICE_JP)) __atomic_fetch_or(word, VOICE_JP, __ATOMIC_RELAXED);
    else if (want == 2 && (now & VOICE_JP)) __atomic_fetch_and(word, ~VOICE_JP, __ATOMIC_RELAXED);
    else return;
    if (!logged) {
        logged = 1;
        rt_log("voice: option set to %s voices", want == 1 ? "jp" : "en");
    }
}
