// Cheat files in the Atmosphere text format (docs/modding.md): parsing and the interpreter. Nothing here touches the
// game: memory, the held buttons and the log come through CheatEnv, so --probe-cheats runs without game files.
#pragma once
#include <stddef.h>
#include <stdint.h>

// The memory regions of the format: main's image, the heap, the alias region, the ASLR region, and 4, no base.
enum { CHEAT_REGIONS = 5 };

typedef struct {
    uint64_t base[CHEAT_REGIONS];
    uint64_t keys;                                                    // held buttons, nn::hid bits
    int (*read)(void *ctx, uint64_t addr, void *out, int n);          // 0: the address cannot be read
    int (*write)(void *ctx, uint64_t addr, const void *in, int n);    // 0: the address cannot be written
    void (*log)(void *ctx, int id, uint64_t value);                   // an FFF opcode's value changed
    void *ctx;
} CheatEnv;

typedef enum {
    CHEAT_OK,
    CHEAT_MALFORMED,    // a word is not an opcode this format has, or the code ends inside one
    CHEAT_WRITES_CODE,  // writes to main's code or read-only data, which the lifted C does not read back
    // Set by cheats.c from the setup's build (tools/cheats.py); the interpreter runs none of these.
    CHEAT_PATCHED,      // built into the game as a patch set, which cheats.c switches
    CHEAT_NOT_BUILT,    // changes code, but the game was built without it (added or edited after the last setup)
    CHEAT_UNSUPPORTED,  // changes code in a way the setup could not build in
    CHEAT_HIDDEN,       // nothing to switch: a code cave other cheats take their code from, or a restore of the original
} CheatState;

typedef struct {
    char *name;
    uint32_t *words;
    uint32_t nwords;
    uint64_t *latch;    // per word: an FFF opcode's last value, a C4 opcode's "already ran"
    int master;         // written in {braces}: always runs, ahead of the others
    int enabled;
    CheatState state;
    uint64_t refused;   // writes the running code aimed at memory that cannot be written
    int refusal_logged;
} Cheat;

typedef struct {
    Cheat *cheats;
    int count;
    uint64_t statics[128];   // the static registers, which survive from one run to the next
} CheatFile;

// Reads a whole file. Sections without code (credits) are dropped. A section with a word that is not eight hex digits,
// or an opcode the format does not have, stays in the list as CHEAT_MALFORMED and never runs. 0 when memory runs out.
int cheat_parse(const char *text, size_t n, CheatFile *out);
void cheat_free(CheatFile *f);
// Marks the sections that store to a fixed address below code_end in main as CHEAT_WRITES_CODE.
void cheat_scan_code_writes(CheatFile *f, uint64_t code_end);
// One pass: the master sections, then the enabled ones, sharing registers as one program. The registers start at 0.
void cheat_run(CheatFile *f, const CheatEnv *env);
