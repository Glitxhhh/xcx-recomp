// XCX_VOICE_LANG (issue #153): the voice language, Japanese or English, independent of the text language.
//
// How the game picks its voices (2.0.0): the game's options keep the choice as bit 1 of an option word (set:
// Japanese), at +0xa9f84 in the object whose pointer lives at 0x7001dc1408 (0x7000349ce0 returns its address). The
// options' first value, at boot and again on starting a New Game (0x70003473f0), takes the bit from the text language
// (0x7000004850: Japanese for Japanese or Chinese text, English otherwise) and hands it at once to the sound manager's
// setter (0x70000252c0), which switches Wwise to "jp" or "en" and loads sound/<lang>.pck. Applying the options later
// (0x70000defac, 0x70000df014, as on loading a save) hands the setter the bit again. The lip sync follows the sound
// manager's language (0x7000025338, read by 0x7000303a78 to open ev/lip/sev/jp/... or ev/lip/sev/us/...).
//
// The patch sets voicejp and voiceen (recomp/patches-upd.txt) make all three places use the setting: the first value
// is taken as if the text were Japanese or American English, and both apply functions pass the chosen language
// whatever the bit holds. They are switched at start-up (voice_lang_init, before the game's first options exist) and
// when the setting changes. Without them only the bit was written, at every present: a New Game played its opening and
// first scenes in the voices of the text language (#58), as the game sets the language there before the host can write
// the bit and does not apply its options again until a save is loaded. The bit is still written, so the game's options
// and the save agree with the setting; the write is an atomic or/and on one bit, so it never loses the game's other
// option bits. A change while playing takes effect when the game next applies its options (loading a save): calling the
// setter from the presenting thread hung the game in testing, right after the voice package loaded. Unset
// ("automatic"), both sets are off, nothing is written and the game keeps its own choice. Other builds than 2.0.0 lack
// these addresses and sets: the setting does nothing.
#include <stdint.h>
#include <string.h>

#include "cfg.h"
#include "host.h"

extern const char rt_game_name[];

#define OPTIONS 0x7001dc1408ull   // the pointer to the object holding the game's options
#define OPTION_WORD 0xa9f84       // its option word
#define VOICE_JP 0x2u             // the option word's bit for Japanese voices
#define SOUND 0x7001df6100ull     // the pointer to the sound manager (0x70000252c0 reads it)
#define SOUND_LANG 0x1060         // its voice language: 1 Japanese, 2 English

// 1 Japanese, 2 English, 0 automatic (the game's choice).
static int wanted(void) {
    const char *v = cfg_value("XCX_VOICE_LANG");
    return !strcmp(v, "ja") ? 1 : !strcmp(v, "en") ? 2 : 0;
}

// The language the sound manager plays now, logged when it changes: 1 Japanese, 2 English, 0 none yet.
static void log_playing(void) {
    static uint32_t last;
    uint64_t sound = __atomic_load_n((volatile uint64_t *)(uintptr_t)SOUND, __ATOMIC_ACQUIRE);
    if (!sound) return;
    uint32_t now = __atomic_load_n((uint32_t *)(uintptr_t)(sound + SOUND_LANG), __ATOMIC_RELAXED);
    if (now == last) return;
    last = now;
    rt_log("voice: the game plays %s voices", now == 1 ? "jp" : now == 2 ? "en" : "no");
}

static int ok = -1, want, set_jp = -1, set_en = -1;
static unsigned seen = ~0u;

// The sets of the setting; reads the setting again when it changed. 1 when this is game 2.0.0.
static int follow_setting(void) {
    if (ok < 0) {
        ok = !strcmp(rt_game_name, "2.0.0");
        set_jp = rt_patch_set("voicejp");
        set_en = rt_patch_set("voiceen");
    }
    if (cfg_version() == seen) return ok;
    seen = cfg_version();
    want = wanted();
    if (want && !ok) rt_log("voice: XCX_VOICE_LANG needs game 2.0.0; ignored");
    if (!ok) return 0;
    rt_patch_switch(set_jp, want == 1);
    rt_patch_switch(set_en, want == 2);
    if (want && (set_jp < 0 || set_en < 0))
        rt_log("voice: this build has no voice patch sets; the setting applies only when the game next applies its "
               "options");
    return 1;
}

void voice_lang_init(void) { follow_setting(); }

void voice_lang_tick(void) {
    static int logged;
    static unsigned logged_for = ~0u;
    if (!follow_setting()) return;
    log_playing();
    if (logged_for != seen) logged_for = seen, logged = 0;
    if (!want) return;
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
