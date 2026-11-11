// The Atmosphere cheat format, written from its public description (Atmosphere docs/features/cheats.md); the code is
// our own. A cheat is a list of 32-bit words. The first nibble of the first word picks the opcode; 0xC, 0xF extend it.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheat_vm.h"

#define NREGS 16
#define RUN_STEPS (1u << 22)   // a pass stops here: a loop whose count wrapped must not hold the cheat thread

static uint32_t nib(uint32_t w, int i) { return w >> (28 - i * 4) & 0xF; }   // i-th nibble from the left
static int width_ok(uint32_t t) { return t == 1 || t == 2 || t == 4 || t == 8; }
static uint64_t width_mask(uint32_t t) { return t >= 8 ? ~0ull : (1ull << (t * 8)) - 1; }
static uint64_t rel40(uint32_t w0, uint32_t w1) { return (uint64_t)(w0 & 0xFF) << 32 | w1; }   // AA AAAAAAAA
static uint64_t rel36(uint32_t w0, uint32_t w1) { return (uint64_t)(w0 & 0xF) << 32 | w1; }    // a aaaaaaaa

// The value words of an opcode: two for width 8, else one.
static uint64_t value_at(const uint32_t *w, uint32_t t) { return t == 8 ? (uint64_t)w[0] << 32 | w[1] : w[0]; }

// The opcode at words[ip]: its length in words, 0 when it is not valid or does not fit.
static uint32_t op_len(const uint32_t *words, uint32_t ip, uint32_t n) {
    uint32_t w = words[ip], t = nib(w, 1), len = 0;
    switch (nib(w, 0)) {
    case 0x0:
        if (width_ok(t) && nib(w, 2) < CHEAT_REGIONS) len = t == 8 ? 4 : 3;
        break;
    case 0x1:
        if (width_ok(t) && nib(w, 2) < CHEAT_REGIONS && nib(w, 3) >= 1 && nib(w, 3) <= 6 && nib(w, 4) <= 1)
            len = t == 8 ? 4 : 3;
        break;
    case 0x2:
        if (nib(w, 1) <= 1) len = 1;
        break;
    case 0x3:
        if (nib(w, 1) == 0) len = 2;
        else if (nib(w, 1) == 1) len = 1;
        break;
    case 0x4: len = 3; break;
    case 0x5:
        if (width_ok(t) && nib(w, 2) < CHEAT_REGIONS && nib(w, 4) <= 3) len = 2;
        break;
    case 0x6:
        if (width_ok(t)) len = 3;
        break;
    case 0x7:
        if (width_ok(t) && nib(w, 4) <= 4) len = 2;
        break;
    case 0x8: len = 1; break;
    case 0x9: {
        uint32_t op = nib(w, 2), fp = op >= 10 && op <= 13;
        if (width_ok(t) && op <= 13 && nib(w, 5) <= 1 && (!fp || t >= 4))
            len = nib(w, 5) ? (t == 8 ? 3 : 2) : 1;
        break;
    }
    case 0xA: {
        uint32_t o = nib(w, 5);
        if (!width_ok(t) || o > 5 || (o >= 3 && nib(w, 6) >= CHEAT_REGIONS)) break;
        len = o == 2 || o >= 4 ? 2 : 1;
        break;
    }
    case 0xC:
        switch (nib(w, 1)) {
        case 0: {
            uint32_t tt = nib(w, 2), c = nib(w, 3), x = nib(w, 5);
            if (!width_ok(tt) || c < 1 || c > 6 || x > 5) break;
            if (x <= 1 && nib(w, 6) >= CHEAT_REGIONS) break;
            len = x == 0 || x == 2 ? 2 : x == 4 ? (tt == 8 ? 3 : 2) : 1;
            break;
        }
        case 1:
            if (nib(w, 6) <= 3) len = 1;
            break;
        case 2:
            if (nib(w, 2) <= 3) len = 1;
            break;
        case 3: len = 1; break;
        case 4:
            if (nib(w, 2) <= 1) len = 3;
            break;
        }
        break;
    case 0xF:
        if (nib(w, 1) == 0xF) {
            uint32_t k = nib(w, 2), tt = nib(w, 3), x = nib(w, 5);
            if (k == 0 || k == 1) len = 1;
            else if (k == 0xF && width_ok(tt) && x <= 4 && (x > 1 || nib(w, 6) < CHEAT_REGIONS))
                len = x == 0 || x == 2 ? 2 : 1;
        }
        break;
    }
    return len && ip + len <= n ? len : 0;
}

static int program_valid(const Cheat *c) {
    for (uint32_t ip = 0, len; ip < c->nwords; ip += len)
        if (!(len = op_len(c->words, ip, c->nwords))) return 0;
    return 1;
}

// ---- text
static int add_section(CheatFile *f, const char *name, size_t len, int master) {
    Cheat *grown = realloc(f->cheats, (size_t)(f->count + 1) * sizeof *grown);
    if (!grown) return 0;
    f->cheats = grown;
    Cheat *c = &f->cheats[f->count];
    memset(c, 0, sizeof *c);
    c->name = malloc(len + 1);
    if (!c->name) return 0;
    memcpy(c->name, name, len);
    c->name[len] = 0;
    c->master = master;
    f->count++;
    return 1;
}

static int add_word(Cheat *c, uint32_t w) {
    uint32_t *grown = realloc(c->words, (c->nwords + 1) * sizeof *grown);
    if (!grown) return 0;
    c->words = grown;
    c->words[c->nwords++] = w;
    return 1;
}

static void drop_section(Cheat *c) {
    free(c->name);
    free(c->words);
    free(c->latch);
}

int cheat_parse(const char *text, size_t n, CheatFile *out) {
    memset(out, 0, sizeof *out);
    if (n >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) text += 3, n -= 3;
    int ok = 1;
    for (size_t i = 0; i < n && ok;) {
        size_t end = i;
        while (end < n && text[end] != '\n') end++;
        size_t a = i, b = end;
        i = end + 1;
        while (a < b && isspace((unsigned char)text[a])) a++;
        while (b > a && isspace((unsigned char)text[b - 1])) b--;
        if (a == b) continue;
        if (text[a] == '[' || text[a] == '{') {
            char close = text[a] == '[' ? ']' : '}';
            if (text[b - 1] == close) {
                size_t x = a + 1, y = b - 1;
                while (x < y && isspace((unsigned char)text[x])) x++;
                while (y > x && isspace((unsigned char)text[y - 1])) y--;
                ok = add_section(out, text + x, y - x, close == '}');
            } else if (out->count) {
                out->cheats[out->count - 1].state = CHEAT_MALFORMED;
            }
            continue;
        }
        if (!out->count) continue;   // text ahead of the first section is a comment
        Cheat *c = &out->cheats[out->count - 1];
        for (size_t p = a; p < b && ok;) {
            size_t q = p;
            while (q < b && !isspace((unsigned char)text[q])) q++;
            uint32_t w = 0;
            int hex = q - p == 8;
            for (size_t k = p; k < q && hex; k++) {
                hex = isxdigit((unsigned char)text[k]) != 0;
                w = w << 4 | (uint32_t)(isdigit((unsigned char)text[k]) ? text[k] - '0'
                                                                        : (tolower((unsigned char)text[k]) - 'a' + 10));
            }
            if (hex) ok = add_word(c, w);
            else c->state = CHEAT_MALFORMED;
            p = q;
            while (p < b && isspace((unsigned char)text[p])) p++;
        }
    }
    int kept = 0;
    for (int k = 0; k < out->count; k++) {
        Cheat *c = &out->cheats[k];
        if (!c->nwords && c->state == CHEAT_OK) {   // credits, or a heading with nothing under it
            drop_section(c);
            continue;
        }
        if (ok && c->nwords) {
            c->latch = calloc(c->nwords, sizeof *c->latch);
            ok = c->latch != NULL;
        }
        if (c->state == CHEAT_OK && !program_valid(c)) c->state = CHEAT_MALFORMED;
        out->cheats[kept++] = *c;
    }
    out->count = kept;
    if (!ok) cheat_free(out);
    return ok;
}

void cheat_free(CheatFile *f) {
    for (int k = 0; k < f->count; k++) drop_section(&f->cheats[k]);
    free(f->cheats);
    memset(f, 0, sizeof *f);
}

// The registers the opcode at w can change, as a mask.
static uint32_t regs_changed(uint32_t w) {
    switch (nib(w, 0)) {
    case 0x3: case 0x4: case 0x5: case 0x6: case 0x7: case 0x9: case 0xA: return 1u << nib(w, 3);
    case 0xC: return nib(w, 1) >= 1 && nib(w, 1) <= 3 ? 0xFFFF : 0;   // save and restore: any of them
    default: return 0;
    }
}

// Opcode 0 adds a register to its address. A register nothing has changed is 0, so the address is the one written in
// the code; one that was loaded gives an address known only when it runs, which the write check of the host answers.
// The master sections run first, so what they change counts for every section.
void cheat_scan_code_writes(CheatFile *f, uint64_t code_end) {
    uint32_t master_regs = 0;
    for (int k = 0; k < f->count; k++) {
        const Cheat *c = &f->cheats[k];
        if (!c->master || c->state != CHEAT_OK) continue;
        for (uint32_t ip = 0, len; ip < c->nwords; ip += len) {
            len = op_len(c->words, ip, c->nwords);
            master_regs |= regs_changed(c->words[ip]);
        }
    }
    for (int k = 0; k < f->count; k++) {
        Cheat *c = &f->cheats[k];
        if (c->state != CHEAT_OK) continue;
        uint32_t changed = master_regs;
        for (uint32_t ip = 0, len; ip < c->nwords; ip += len) {
            len = op_len(c->words, ip, c->nwords);
            uint32_t w = c->words[ip];
            uint64_t a = UINT64_MAX;
            if (nib(w, 0) == 0x0 && nib(w, 2) == 0 && !(changed >> nib(w, 3) & 1)) a = rel40(w, c->words[ip + 1]);
            else if (nib(w, 0) == 0xA && nib(w, 5) == 4 && nib(w, 6) == 0) a = rel36(w, c->words[ip + 1]);
            if (a < code_end) c->state = CHEAT_WRITES_CODE;
            changed |= regs_changed(w);
        }
    }
}

// ---- the interpreter
typedef struct {
    CheatFile *file;
    const CheatEnv *env;
    uint64_t regs[NREGS], saved[NREGS];
    uint32_t loop_top[NREGS];
    uint32_t steps;
} Vm;

static uint64_t load(const Vm *vm, uint64_t addr, uint32_t t) {
    uint64_t v = 0;
    vm->env->read(vm->env->ctx, addr, &v, (int)t);
    return v;
}

static void store(const Vm *vm, Cheat *c, uint64_t addr, uint64_t v, uint32_t t) {
    if (!vm->env->write(vm->env->ctx, addr, &v, (int)t)) c->refused++;
}

static int compare(uint64_t a, uint64_t b, uint32_t cond) {
    switch (cond) {
    case 1: return a > b;
    case 2: return a >= b;
    case 3: return a < b;
    case 4: return a <= b;
    case 5: return a == b;
    default: return a != b;
    }
}

static int begins_block(uint32_t w) {
    uint32_t op = nib(w, 0);
    return op == 0x1 || op == 0x8 || (op == 0xC && (nib(w, 1) == 0 || nib(w, 1) == 4));
}

// The word after the End that closes the block ip stands in, or after its Else when stop_at_else.
static uint32_t skip_block(const Cheat *c, uint32_t ip, int stop_at_else) {
    int depth = 0;
    for (uint32_t len; ip < c->nwords; ip += len) {
        len = op_len(c->words, ip, c->nwords);
        uint32_t w = c->words[ip];
        if (begins_block(w)) depth++;
        else if (nib(w, 0) == 0x2) {
            if (nib(w, 1) == 0 && depth-- == 0) return ip + len;
            if (nib(w, 1) == 1 && depth == 0 && stop_at_else) return ip + len;
        }
    }
    return ip;
}

static uint64_t arith(uint32_t op, uint32_t t, uint64_t a, uint64_t b) {
    uint64_t r = 0;
    switch (op) {
    case 0: r = a + b; break;
    case 1: r = a - b; break;
    case 2: r = a * b; break;
    case 3: r = b < 64 ? a << b : 0; break;
    case 4: r = b < 64 ? a >> b : 0; break;
    case 5: r = a & b; break;
    case 6: r = a | b; break;
    case 7: r = ~a; break;
    case 8: r = a ^ b; break;
    case 9: r = a; break;
    default:
        if (t == 4) {
            float x, y, z;
            uint32_t ua = (uint32_t)a, ub = (uint32_t)b, ur;
            memcpy(&x, &ua, 4);
            memcpy(&y, &ub, 4);
            z = op == 10 ? x + y : op == 11 ? x - y : op == 12 ? x * y : x / y;
            memcpy(&ur, &z, 4);
            r = ur;
        } else {
            double x, y, z;
            memcpy(&x, &a, 8);
            memcpy(&y, &b, 8);
            z = op == 10 ? x + y : op == 11 ? x - y : op == 12 ? x * y : x / y;
            memcpy(&r, &z, 8);
        }
    }
    return r & width_mask(t);
}

static uint64_t store_addr(const Vm *vm, uint32_t w, const uint32_t *w1) {
    uint64_t base = vm->regs[nib(w, 3)];
    uint32_t x = nib(w, 6);
    switch (nib(w, 5)) {
    case 1: return base + vm->regs[x];
    case 2: return base + rel36(w, *w1);
    case 3: return vm->env->base[x] + base;
    case 4: return vm->env->base[x] + rel36(w, *w1);
    case 5: return vm->env->base[x] + base + rel36(w, *w1);
    default: return base;
    }
}

// The address of a C0 or FFF operand of form x (0..3): nibble a and b below name the region or registers.
static uint64_t operand_addr(const Vm *vm, uint32_t x, uint32_t a, uint32_t b, uint32_t w, const uint32_t *w1) {
    switch (x) {
    case 0: return vm->env->base[a] + rel36(w, *w1);
    case 1: return vm->env->base[a] + vm->regs[b];
    case 2: return vm->regs[a] + rel36(w, *w1);
    default: return vm->regs[a] + vm->regs[b];
    }
}

static void run_one(Vm *vm, Cheat *c) {
    uint64_t *r = vm->regs;
    const uint64_t *base = vm->env->base;
    for (uint32_t ip = 0; ip < c->nwords;) {
        if (++vm->steps > RUN_STEPS) return;
        uint32_t len = op_len(c->words, ip, c->nwords), w = c->words[ip], t = nib(w, 1), next = ip + len;
        const uint32_t *arg = c->words + ip + 1;
        int keep = 1;   // a conditional block's test
        switch (nib(w, 0)) {
        case 0x0:
            store(vm, c, base[nib(w, 2)] + r[nib(w, 3)] + rel40(w, arg[0]), value_at(arg + 1, t), t);
            break;
        case 0x1: {
            uint64_t a = base[nib(w, 2)] + (nib(w, 4) ? r[nib(w, 5)] : 0) + rel40(w, arg[0]);
            keep = compare(load(vm, a, t), value_at(arg + 1, t) & width_mask(t), nib(w, 3));
            break;
        }
        case 0x2:
            if (nib(w, 1) == 1) next = skip_block(c, next, 0);   // Else, reached by a block that ran
            break;
        case 0x3:
            if (t == 0) {
                r[nib(w, 3)] = arg[0];
                vm->loop_top[nib(w, 3)] = next;
            } else if (--r[nib(w, 3)]) {
                next = vm->loop_top[nib(w, 3)];
            }
            break;
        case 0x4: r[nib(w, 3)] = value_at(arg, 8); break;
        case 0x5: {
            uint32_t reg = nib(w, 3), form = nib(w, 4), s = nib(w, 5), m = nib(w, 2);
            uint64_t a = rel40(w, arg[0]);
            a += form == 0 ? base[m] : form == 1 ? r[reg] : form == 2 ? r[s] : base[m] + r[s];
            r[reg] = load(vm, a, t);
            break;
        }
        case 0x6: {
            uint32_t reg = nib(w, 3);
            store(vm, c, r[reg] + (nib(w, 5) ? r[nib(w, 6)] : 0), value_at(arg, 8), t);
            if (nib(w, 4)) r[reg] += t;
            break;
        }
        case 0x7: r[nib(w, 3)] = arith(nib(w, 4), t, r[nib(w, 3)], arg[0]); break;
        case 0x8: {
            uint64_t mask = w & 0x0FFFFFFF;
            keep = (vm->env->keys & mask) == mask;
            break;
        }
        case 0x9: {
            uint64_t right = nib(w, 5) ? value_at(arg, t) : r[nib(w, 6)];
            r[nib(w, 3)] = arith(nib(w, 2), t, r[nib(w, 4)], right);
            break;
        }
        case 0xA:
            store(vm, c, store_addr(vm, w, arg), r[nib(w, 2)], t);
            if (nib(w, 4)) r[nib(w, 3)] += t;
            break;
        case 0xC:
            switch (t) {
            case 0: {
                uint32_t tt = nib(w, 2), x = nib(w, 5);
                uint64_t right = x == 4   ? value_at(arg, tt)
                               : x == 5   ? r[nib(w, 6)]
                                          : load(vm, operand_addr(vm, x, nib(w, 6), nib(w, 7), w, arg), tt);
                keep = compare(r[nib(w, 4)] & width_mask(tt), right & width_mask(tt), nib(w, 3));
                break;
            }
            case 1: {
                uint32_t d = nib(w, 3), s = nib(w, 5);
                switch (nib(w, 6)) {
                case 0: r[d] = vm->saved[s]; break;
                case 1: vm->saved[d] = r[s]; break;
                case 2: vm->saved[d] = 0; break;
                default: r[d] = 0; break;
                }
                break;
            }
            case 2:
                for (int i = 0; i < NREGS; i++) {
                    if (!(w >> i & 1)) continue;
                    switch (nib(w, 2)) {
                    case 0: r[i] = vm->saved[i]; break;
                    case 1: vm->saved[i] = r[i]; break;
                    case 2: vm->saved[i] = 0; break;
                    default: r[i] = 0; break;
                    }
                }
                break;
            case 3: {
                uint32_t i = w >> 4 & 0xFF, reg = w & 0xF;
                if (i < 0x80) r[reg] = vm->file->statics[i];
                else vm->file->statics[i - 0x80] = r[reg];
                break;
            }
            case 4: {
                uint64_t mask = value_at(arg, 8);
                int held = (vm->env->keys & mask) == mask;
                keep = held;
                if (!nib(w, 2)) {   // once per press
                    keep = held && !c->latch[ip];
                    c->latch[ip] = (uint64_t)held;
                }
                break;
            }
            }
            break;
        case 0xF:
            if (nib(w, 2) == 0xF) {
                uint32_t tt = nib(w, 3), x = nib(w, 5);
                uint64_t v = x == 4 ? r[nib(w, 6)] & width_mask(tt)
                                    : load(vm, operand_addr(vm, x, nib(w, 6), nib(w, 7), w, arg), tt);
                if (v != c->latch[ip] && vm->env->log) vm->env->log(vm->env->ctx, (int)nib(w, 4), v);
                c->latch[ip] = v;
            }
            break;   // FF0 and FF1 pause the game for the next opcodes; ours writes while the game runs
        }
        ip = keep ? next : skip_block(c, next, 1);
    }
}

void cheat_run(CheatFile *f, const CheatEnv *env) {
    Vm vm = {.file = f, .env = env};
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < f->count; k++) {
            Cheat *c = &f->cheats[k];
            if (c->state != CHEAT_OK || (pass == 0 ? !c->master : c->master || !c->enabled)) continue;
            run_one(&vm, c);
        }
}
