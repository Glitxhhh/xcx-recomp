// --probe-cheats: the cheat format and interpreter (cheat_vm.c) against a buffer that stands in for the game's memory.
// Needs no game files.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cheat_vm.h"
#include "host.h"

enum { MEM = 0x1000, HEAP = 0x800, CODE_END = 0x100 };
static uint8_t mem[MEM];
static int fails, logged;
static uint64_t log_value;
#define CHECK(c, what) do { if (!(c)) { fails++; fprintf(stderr, "FAIL cheats: %s\n", what); } } while (0)

static int fake_read(void *ctx, uint64_t a, void *out, int n) {
    (void)ctx;
    if (a + (uint64_t)n > MEM) return 0;
    memcpy(out, mem + a, (size_t)n);
    return 1;
}

static int fake_write(void *ctx, uint64_t a, const void *in, int n) {
    (void)ctx;
    if (a + (uint64_t)n > MEM) return 0;
    memcpy(mem + a, in, (size_t)n);
    return 1;
}

static void fake_log(void *ctx, int id, uint64_t v) {
    (void)ctx, (void)id;
    logged++;
    log_value = v;
}

static uint32_t u32_at(uint64_t a) {
    uint32_t v;
    memcpy(&v, mem + a, 4);
    return v;
}

static void put32(uint64_t a, uint32_t v) { memcpy(mem + a, &v, 4); }

static int parse(const char *text, CheatFile *f) { return cheat_parse(text, strlen(text), f); }

// Parses text, turns every section on and runs passes passes with these keys.
static void run(const char *text, uint64_t keys, int passes) {
    CheatFile f;
    CHECK(cheat_parse(text, strlen(text), &f), "parse");
    for (int i = 0; i < f.count; i++) f.cheats[i].enabled = 1;
    CheatEnv env = {.base = {0, HEAP, 0, 0, 0}, .keys = keys, .read = fake_read, .write = fake_write, .log = fake_log};
    for (int i = 0; i < passes; i++) cheat_run(&f, &env);
    cheat_free(&f);
}

int probe_cheats(void) {
    // the text format: credits are dropped, a bad word or opcode keeps the section out of the runs
    CheatFile f;
    static const char file[] = "\xEF\xBB\xBF" "a comment\r\n[One]\r\n04000000 00000200 0000002A\r\n\r\n"
                               "{Master}\n04000000 00000204 00000001\n[Bad word]\nxyz\n[Bad op]\nB0000000\n"
                               "[Cut short]\n04000000 00000200\n[Code]\n04000000 00000010 D503201F\n[Made by someone]\n";
    CHECK(parse(file, &f) && f.count == 6, "the sections with code");
    cheat_scan_code_writes(&f, CODE_END);
    CHECK(!strcmp(f.cheats[0].name, "One") && f.cheats[0].state == CHEAT_OK, "a plain section");
    CHECK(f.cheats[1].master && f.cheats[1].state == CHEAT_OK, "a master section");
    CHECK(f.cheats[2].state == CHEAT_MALFORMED, "a word that is not hex");
    CHECK(f.cheats[3].state == CHEAT_MALFORMED && f.cheats[4].state == CHEAT_MALFORMED &&
              f.cheats[5].state == CHEAT_WRITES_CODE, "reserved opcode, cut short, code");
    cheat_free(&f);

    // a store through a register that was loaded has no fixed address, here or after a master section loaded it
    CHECK(parse("[Loaded]\n40010000 00000000 00100000\n04010000 00000000 00000001\n"
                "[Unused register]\n04010000 00000000 00000001\n", &f) && f.count == 2, "parse");
    cheat_scan_code_writes(&f, CODE_END);
    CHECK(f.cheats[0].state == CHEAT_OK && f.cheats[1].state == CHEAT_WRITES_CODE, "a register decides the address");
    cheat_free(&f);
    CHECK(parse("{M}\n40010000 00000000 00100000\n[C]\n04010000 00000000 00000001\n", &f) && f.count == 2, "parse");
    cheat_scan_code_writes(&f, CODE_END);
    CHECK(f.cheats[1].state == CHEAT_OK, "a register a master section loaded");
    cheat_free(&f);

    // only enabled sections run, the master always
    CHECK(parse("{M}\n04000000 00000300 00000001\n[C]\n04000000 00000304 00000002\n", &f), "parse");
    CheatEnv env = {.base = {0, HEAP, 0, 0, 0}, .read = fake_read, .write = fake_write, .log = fake_log};
    cheat_run(&f, &env);
    CHECK(u32_at(0x300) == 1 && u32_at(0x304) == 0, "the master runs, a cheat that is off does not");
    cheat_free(&f);

    // 0: store, the widths and a region; a 64-bit value is two words, high first
    run("[x]\n01000000 00000010 000000AB\n02000000 00000012 0000BEEF\n01100000 00000020 00000077\n", 0, 1);
    CHECK(mem[0x10] == 0xAB && mem[0x12] == 0xEF && mem[0x13] == 0xBE && mem[HEAP + 0x20] == 0x77, "store widths");
    run("[x]\n08000000 00000030 11223344 55667788\n", 0, 1);
    CHECK(u32_at(0x30) == 0x55667788 && u32_at(0x34) == 0x11223344, "store 64 bits");

    // the same shape as the 60 fps mod's cheat: two blocks on one value
    put32(0x40, 3);
    run("[x]\n14040000 00000040 00000003\n04000000 00000044 00000001\n20000000\n"
        "14020000 00000040 00000004\n04000000 00000044 00000002\n20000000\n", 0, 1);
    CHECK(u32_at(0x44) == 1, "<= takes the first block, >= skips the second");

    // 1: Else, and a nested block inside a skipped one
    put32(0x48, 9);
    run("[x]\n14050000 00000048 00000001\n14050000 00000048 00000009\n04000000 0000004C 00000001\n20000000\n"
        "21000000\n04000000 00000050 00000002\n20000000\n", 0, 1);
    CHECK(u32_at(0x4C) == 0 && u32_at(0x50) == 2, "a false test runs Else, past the nested block");
    run("[x]\n14050000 00000048 00000009\n04000000 00000054 00000003\n21000000\n04000000 00000058 00000004\n20000000\n",
        0, 1);
    CHECK(u32_at(0x54) == 3 && u32_at(0x58) == 0, "a true test skips Else");

    // 3, 4, 6: a loop that walks a register through memory
    run("[x]\n40010000 00000000 00000060\n30020000 00000003\n64011000 00000000 00000005\n31020000\n", 0, 1);
    CHECK(u32_at(0x60) == 5 && u32_at(0x64) == 5 && u32_at(0x68) == 5 && u32_at(0x6C) == 0, "loop and increment");

    // 5: a pointer chain, [0x70] -> 0x80, [0x80 + 8] -> 100; 9: that + 0x23; A: stored at 0xF0
    put32(0x70, 0x80);
    put32(0x88, 100);
    run("[x]\n54000000 00000070\n54001000 00000008\n94010100 00000023\n"
        "40020000 00000000 000000F0\nA4120000\n", 0, 1);
    CHECK(u32_at(0xF0) == 135, "pointer chain, add, store through a register");
    run("[x]\n40000000 00000000 00000090\n40010000 00000000 00000005\n94221100 00000003\nA4200000\n", 0, 1);
    CHECK(u32_at(0x90) == 15, "multiply, store through a register");

    // 9: a float add
    run("[x]\n40000000 00000000 3FC00000\n94A10100 40000000\n40020000 00000000 000000A0\nA4120000\n", 0, 1);
    CHECK(u32_at(0xA0) == 0x40600000, "1.5 + 2.0 = 3.5");

    // 8, C4: a held button, and once per press
    run("[x]\n80000041\n04000000 000000B0 00000001\n20000000\n", 0x40, 1);
    CHECK(u32_at(0xB0) == 0, "a mask of two buttons needs both");
    run("[x]\n80000041\n04000000 000000B0 00000001\n20000000\n", 0x41, 1);
    CHECK(u32_at(0xB0) == 1, "both held");
    run("[x]\nC4000000 00000000 00000001\n40000000 00000000 000000B8\n54010000 000000B8\n"
        "94011100 00000001\nA4100000\n20000000\n", 1, 3);
    CHECK(u32_at(0xB8) == 1, "C4 once: three passes held, one run");
    put32(0xBC, 0);
    run("[x]\nC4100000 00000000 00000001\n40000000 00000000 000000BC\n54010000 000000BC\n"
        "94011100 00000001\nA4100000\n20000000\n", 1, 3);
    CHECK(u32_at(0xBC) == 3, "C4 while held: three runs");

    // C0: a register against memory, a static value and another register. The compared value is in r3: opcode 0 always
    // adds its register R, here r0, to the address.
    put32(0xC0, 7);
    run("[x]\n40030000 00000000 00000007\nC0453000 000000C0\n04000000 000000C4 00000001\n20000000\n"
        "C0453400 00000007\n04000000 000000C8 00000001\n20000000\n"
        "40010000 00000000 00000008\nC0433510\n04000000 000000CC 00000001\n20000000\n", 0, 1);
    CHECK(u32_at(0xC4) == 1 && u32_at(0xC8) == 1 && u32_at(0xCC) == 1, "register conditions");

    // C1, C2, C3: saved registers, and a static register that outlives the pass
    run("[x]\n40000000 00000000 00000011\nC1000010\nC1000030\nC1000000\n"
        "40020000 00000000 000000D0\nA4020000\n", 0, 1);
    CHECK(u32_at(0xD0) == 0x11, "save a register, clear it, restore it");
    run("[x]\n40000000 00000000 00000022\nC2100001\nC2300001\nC2000001\n"
        "40020000 00000000 000000D8\nA4020000\n", 0, 1);
    CHECK(u32_at(0xD8) == 0x22, "the same by mask");
    CHECK(parse("[x]\nC3000000\n40010000 00000000 000000D4\nA4010000\n", &f), "parse");
    f.cheats[0].enabled = 1;
    f.statics[0] = 0x55;
    cheat_run(&f, &env);
    CHECK(u32_at(0xD4) == 0x55, "a static register reads back");
    cheat_free(&f);

    // FFF logs a value once per change; a write outside memory counts as refused and stops nothing
    put32(0xE0, 0x1234);
    logged = 0;
    run("[x]\nFFF40000 000000E0\n", 0, 3);
    CHECK(logged == 1 && log_value == 0x1234, "log on change");
    CHECK(parse("[x]\n04000000 00100000 00000001\n04000000 000000E4 00000002\n", &f), "parse");
    f.cheats[0].enabled = 1;
    cheat_run(&f, &env);
    CHECK(f.cheats[0].refused == 1 && u32_at(0xE4) == 2, "a refused write");
    cheat_free(&f);

    // a loop that never ends gives up
    run("[x]\n30000000 00000000\n31000000\n", 0, 1);

    fprintf(stderr, "cheats: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
