// Adapted from MacNativeGames games/totk/runtime/services.c and loader.c @ b75be8c (MIT, Copyright (c) 2026
// MacNativeGames contributors), and LAS Recomp runtime/main.c and platform/windows.c @ da35b83 (MIT, Copyright (c)
// 2026 Peiikai).
// Guest address space and the memory SVCs: heap, module data permissions, stack moves,
// QueryMemory, physical memory in the alias region, GetInfo memory ids, and the kernel-region allocator.
//
// Every region is a plain reservation; pages are committed and decommitted at 4 KiB granularity. Decommitted pages
// come back zeroed, so no stale-page bookkeeping is needed. The stack and heap block and the alias region are
// reserved write-watched (platform.h plat_mem_reserve): mem_written_pages reports the pages the CPU wrote, which
// the Vulkan back end uploads again.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "layout.h"
#include "platform/platform.h"

#define PAGE 0x1000ull
#define GiB (1ull << 30)
#define LOW_SEARCH_FROM 0x100000000ull
#define LOW_SIZE (6 * GiB)
#define STACK_OFF 0x10000000ull
#define STACK_SIZE 0x10000000ull
#define HEAP_OFF (STACK_OFF + STACK_SIZE + 0x10000000ull)
#define HEAP_CAP (4 * GiB)
#define ALIAS_SIZE (64 * GiB)
#define ALIAS_ALIGN (4 * GiB)
#define ALIAS_PAGES (ALIAS_SIZE / PAGE)
#define DEFAULT_POOL 0xCD500000ull       // retail application pool
#define SYSTEM_RESOURCE_BUDGET (32ull << 20)
#define MAX_STACK_MAPS 4096
#define REEXEC_LIMIT 20

// Horizon memory states, permissions, attributes and results used here.
enum { ST_FREE = 0, ST_CODE = 3, ST_CODEDATA = 4, ST_NORMAL = 5, ST_STACK = 0xB, ST_THREADLOCAL = 0xC,
       ST_INACCESSIBLE = 0x10 };
enum { PERM_NONE = 0, PERM_R = 1, PERM_RW = 3, PERM_RX = 5 };
enum { ATTR_PERMISSION_LOCKED = 16 };
#define R_OK 0u
#define R_INVALID_ADDRESS 0xcc01u   // misaligned address
#define R_INVALID_SIZE 0xca01u
#define R_INVALID_NEW_PERM 0xd801u
#define R_INVALID_STATE 0xd401u     // InvalidCurrentMemory
#define R_INVALID_REGION 0xdc01u
#define R_LIMIT_REACHED 0x10801u

static RtGuestLayout L;
static uint64_t mods_lo, mods_hi;   // fixed module window
static PlatMutex lock = PLAT_MUTEX_INIT;
static uint64_t heap_size, alias_bytes;
static uint8_t *alias_pages;        // 1 per mapped 4 KiB page of the alias region
typedef struct { uint64_t dst, src, size; } StackMap;
static StackMap stack_maps[MAX_STACK_MAPS];
static unsigned nstack_maps;
static uint8_t *module_perm[MOD_COUNT];   // per data page of each module: perm | ATTR_PERMISSION_LOCKED

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

static int reserve_at(uint64_t at, uint64_t size, int watch) { return plat_mem_reserve(at, size, watch); }

// ---- frozen regions
// A 2 MiB region of the watched reservations that nothing wrote for a while can be made read-only (mem_freeze), so the
// back end's write-watch queries skip it: no page of a frozen region changes before a thaw. The first write faults and
// thaw_handler makes the region writable again before the write retries. A kernel write into guest memory (a file
// read) fails on a read-only page instead of faulting, so file reads thaw their buffer first (mem_thaw), and so does
// every commit, decommit and protection change here. A region stays thawed for FRZ_HOLD_MS after a mem_thaw: a file
// read still in flight writes its buffer later. frz_lock is a leaf lock: nothing writes guest memory under it.
#define FRZ_SHIFT 21
#define FRZ_SIZE (1ull << FRZ_SHIFT)
#define FRZ_HOLD_MS 5000u
static volatile uint8_t frz[RT_ADDRESS_LIMIT >> FRZ_SHIFT];
static uint32_t thaw_ms[RT_ADDRESS_LIMIT >> FRZ_SHIFT];   // frz_lock: GetTickCount of the last thaw request, 0 never
static volatile int32_t nfrz;
static PlatMutex frz_lock = PLAT_MUTEX_INIT;
static volatile int64_t frz_count[4];   // freezes; thaws by a write fault, by mem_thaw, by a mapping change

static int watched(uint64_t addr, uint64_t size) {
    const RtGuestLayout *g = &L;
    return (addr >= g->aslr.base && size <= LOW_SIZE && addr - g->aslr.base <= LOW_SIZE - size) ||
           (addr >= g->alias.base && size <= ALIAS_SIZE && addr - g->alias.base <= ALIAS_SIZE - size);
}

// frz_lock held exclusively. why: 1 a write fault, 2 mem_thaw (holds the region thawed), 3 a mapping change here.
static void thaw_locked(uint64_t r, int why) {
    if (why == 2) thaw_ms[r] = (uint32_t)plat_now_ms() | 1;
    if (!frz[r]) return;
    // The read-only runs only: freeze_locked left reserved holes (heap pages given back) as they were. Every committed
    // page of a frozen region was read-write at the freeze, and mapping changes thaw before they run, so these are all
    // ours.
    uint64_t a = r << FRZ_SHIFT, end = a + FRZ_SIZE, b;
    for (; a < end; a = b)
        if (plat_mem_state(a, end, &b) == PLAT_PROT_READ && !plat_mem_protect(a, b - a, PLAT_PROT_RW))
            rt_fatal("memory: thawing %#llx+%#llx failed", (unsigned long long)a, (unsigned long long)(b - a));
    __atomic_store_n(&frz[r], 0, __ATOMIC_RELEASE);
    plat_dec32(&nfrz);
    plat_inc64(&frz_count[why]);
}

// ---- copy-on-write blocks
// With the replay on its own thread (XCX_REPLAY_ASYNC) the VRAM mirror sync of a submission runs up to a frame after
// it, while the game already rewrites vertex and storage data for the next one. At each submit the back end makes the
// blocks of the pools it mirrors read-only (mem_cow_protect, vk_memory.c vk_mem_cow_submit); the first write into one
// faults, cow_fault_locked saves the block as it was (valid for every submission from the protect to the fault) when
// the replay has not recorded those yet, makes it writable and lets the write retry. mem_cow_read gives the replay the
// bytes as of its submission: the saved copy, or live memory where nothing wrote since. All under frz_lock (a leaf
// lock: the replay's copies under it read guest memory, nothing writes it). A protected block lies in one region and
// never in a frozen one: freeze_locked refuses regions with read-only pages, and mem_cow_protect skips frozen regions.
#define COW_CHUNK_SHIFT 28
#define COW_IDLE 30                 // submissions without a fault before a region's blocks are made writable again
#define COW_HASH 4096
static int cow_shift;               // block size log2; 0: off
static uint32_t *cow_tab[RT_ADDRESS_LIMIT >> COW_CHUNK_SHIFT];   // per block: the submission it was protected at, 0 not
static uint32_t cow_sub, cow_done;  // the newest submission; the newest the replay finished recording
static uint16_t cow_reg_n[RT_ADDRESS_LIMIT >> FRZ_SHIFT];        // protected blocks per region
static uint32_t cow_reg_last[RT_ADDRESS_LIMIT >> FRZ_SHIFT];     // cow_sub at its last protect or fault
static uint32_t *cow_regs;          // regions with protected blocks (possibly some with none left)
static int ncow_regs, ccow_regs;
typedef struct { uint64_t blk, t; uint32_t p, q; int next; } CowSave;   // valid for submissions p..q, taken at t ns
static CowSave *cow_sv;
static uint8_t *cow_arena;
static int cow_free = -1, cow_hash[COW_HASH], *cow_live, ncow_live;
static uint64_t cow_st[8];          // see mem_cow_stats
static uint64_t cow_t[6];           // protect pass: lock wait, plat_mem_state, plat_mem_protect ns; protect calls;
                                    // state calls; the longest pass ns
static uint64_t cow_nprot;          // protected blocks now
static uint64_t cow_arena_n;        // the arena's size in bytes
static uint64_t cow_held;           // blocks left writable because their region is inside its thaw hold

void mem_cow_init(int blk_shift, uint64_t arena_bytes) {
    if (blk_shift < 12) blk_shift = 12;
    if (blk_shift > FRZ_SHIFT) blk_shift = FRZ_SHIFT;
    int n = (int)(arena_bytes >> blk_shift);
    if (n <= 0) return;
    cow_arena = plat_mem_alloc((uint64_t)n << blk_shift);
    cow_sv = calloc((size_t)n, sizeof *cow_sv);
    cow_live = calloc((size_t)n, sizeof *cow_live);
    if (!cow_arena || !cow_sv || !cow_live) rt_fatal("memory: out of memory for copy-on-write blocks");
    for (int i = 0; i < COW_HASH; i++) cow_hash[i] = -1;
    for (int i = n - 1; i >= 0; i--) cow_sv[i].next = cow_free, cow_free = i;
    cow_shift = blk_shift;
    cow_arena_n = (uint64_t)n << blk_shift;
}

int mem_cow_on(void) { return cow_shift != 0; }
uint64_t mem_cow_arena_bytes(void) { return cow_arena_n; }
int mem_cow_blk_shift(void) { return cow_shift; }

static uint32_t *cow_slot(uint64_t a, int create) {
    uint64_t ci = a >> COW_CHUNK_SHIFT;
    if (ci >= sizeof cow_tab / sizeof *cow_tab) return NULL;
    if (!cow_tab[ci]) {
        if (!create) return NULL;
        cow_tab[ci] = calloc(1ull << (COW_CHUNK_SHIFT - cow_shift), sizeof(uint32_t));
        if (!cow_tab[ci]) rt_fatal("memory: out of memory for copy-on-write blocks");
    }
    return &cow_tab[ci][(a >> cow_shift) & ((1ull << (COW_CHUNK_SHIFT - cow_shift)) - 1)];
}

static int cow_hidx(uint64_t blk) { return (int)((blk >> cow_shift) * 0x9E3779B1u >> 20) & (COW_HASH - 1); }

// frz_lock: the block (protected since submission p) is about to change. Saves it when a submission from p on is not
// recorded yet.
static void cow_save_locked(uint64_t blk, uint32_t p) {
    if (cow_sub == cow_done) return;   // the replay recorded every submission: nobody needs the old bytes
    if (cow_free < 0) { cow_st[3]++; return; }   // arena full: read live, late, as without the snapshot
    int i = cow_free;
    cow_free = cow_sv[i].next;
    memcpy(cow_arena + ((uint64_t)i << cow_shift), (const void *)(uintptr_t)blk, 1ull << cow_shift);
    int h = cow_hidx(blk);
    cow_sv[i] = (CowSave){blk, plat_now_ns(), p > cow_done ? p : cow_done + 1, cow_sub, cow_hash[h]};
    cow_hash[h] = i;
    cow_live[ncow_live++] = i;
    cow_st[2]++;
}

// frz_lock: makes the protected block at blk writable again, saving it first.
static void cow_unprotect_locked(uint64_t blk, uint32_t *s) {
    cow_save_locked(blk, *s);
    if (!plat_mem_protect(blk, 1ull << cow_shift, PLAT_PROT_RW))
        rt_fatal("memory: unprotecting %#llx failed", (unsigned long long)blk);
    *s = 0;
    cow_reg_n[blk >> FRZ_SHIFT]--;
    cow_nprot--;
}

// frz_lock: 1 when the write at a hit a protected block (now writable), 0 not one of ours.
static int cow_fault_locked(uint64_t a) {
    if (!cow_shift) return 0;
    uint32_t *s = cow_slot(a, 0);
    if (!s || !*s) return 0;
    uint64_t t0 = plat_now_ns();
    cow_unprotect_locked(a & ~((1ull << cow_shift) - 1), s);
    cow_reg_last[a >> FRZ_SHIFT] = cow_sub;
    cow_st[1]++;
    cow_st[7] += plat_now_ns() - t0;
    return 1;
}

// frz_lock: every protected block over [a, a+n) writable again (a mapping change, a file read, a pool's end).
static void cow_release_range_locked(uint64_t a, uint64_t n) {
    if (!cow_shift || !cow_nprot || !n) return;
    uint64_t bsz = 1ull << cow_shift, end = a + n > a ? a + n : UINT64_MAX;
    if (end > RT_ADDRESS_LIMIT) end = RT_ADDRESS_LIMIT;
    for (uint64_t b = a & ~(bsz - 1); b < end;) {
        if (!cow_reg_n[b >> FRZ_SHIFT]) { b = ((b >> FRZ_SHIFT) + 1) << FRZ_SHIFT; continue; }
        uint32_t *s = cow_slot(b, 0);
        if (s && *s) cow_unprotect_locked(b, s), cow_st[4]++;
        b += bsz;
    }
}

static uint64_t cow_pass_t0;

uint32_t mem_cow_begin(void) {
    uint64_t t0 = plat_now_ns();
    plat_mutex_lock(&frz_lock);
    cow_pass_t0 = plat_now_ns();
    cow_t[0] += cow_pass_t0 - t0;
    cow_sub = cow_sub + 1 ? cow_sub + 1 : 1;
    return cow_sub;
}

// Per block, what plat_mem_state said: 0 not asked since the last mapping change, 1 committed read-write, 2 not (a
// hole, another protection). Asking on every submit cost 5.8 ms a frame in a busy town (protected blocks split
// the regions into many runs), so each block is asked once; commit, decommit and protect here reset the answer.
static uint8_t *cow_kt[RT_ADDRESS_LIMIT >> COW_CHUNK_SHIFT];

static uint8_t *cow_kind(uint64_t a) {
    uint64_t ci = a >> COW_CHUNK_SHIFT;
    if (!cow_kt[ci]) {
        cow_kt[ci] = calloc(1ull << (COW_CHUNK_SHIFT - cow_shift), 1);
        if (!cow_kt[ci]) rt_fatal("memory: out of memory for copy-on-write blocks");
    }
    return &cow_kt[ci][(a >> cow_shift) & ((1ull << (COW_CHUNK_SHIFT - cow_shift)) - 1)];
}

static void cow_kind_reset(uint64_t a, uint64_t n) {   // frz_lock: a mapping change over [a, a+n)
    if (!cow_shift || !n) return;
    uint64_t bsz = 1ull << cow_shift, end = a + n > a && a + n < RT_ADDRESS_LIMIT ? a + n : RT_ADDRESS_LIMIT;
    for (uint64_t b = a & ~(bsz - 1); b < end;) {
        uint64_t ci = b >> COW_CHUNK_SHIFT, ce = (ci + 1) << COW_CHUNK_SHIFT, e = end < ce ? end : ce;
        if (cow_kt[ci]) {
            uint64_t k0 = (b >> cow_shift) & ((1ull << (COW_CHUNK_SHIFT - cow_shift)) - 1);
            uint64_t k1 = ((e - 1) >> cow_shift) & ((1ull << (COW_CHUNK_SHIFT - cow_shift)) - 1);
            memset(cow_kt[ci] + k0, 0, k1 - k0 + 1);
        }
        b = ce;
    }
}

// The block at b can be protected: committed read-write, not protected, not in a frozen region. One query answers
// every block of the run it reports: VirtualQuery on write-watched memory walks the whole run of pages that share its
// attributes, often hundreds of MiB, so asking per block cost seconds while loading (mapping changes reset the cache).
static int cow_can(uint64_t b) {
    uint64_t r = b >> FRZ_SHIFT;
    if (*cow_slot(b, 1) || frz[r]) return 0;
    // A region inside its thaw hold (mem_thaw, FRZ_HOLD_MS) may be the target of a file read in progress
    // (fs_host.c host_read_at, romfs.c read_at): the kernel's write into a read-only page fails with ERROR_NOACCESS
    // instead of faulting into thaw_fault, so the block is left writable, as mem_freeze leaves the region.
    if (thaw_ms[r] && (int32_t)((uint32_t)plat_now_ms() - thaw_ms[r]) < (int32_t)FRZ_HOLD_MS) {
        cow_held++;
        return 0;
    }
    uint8_t *k = cow_kind(b);
    if (!*k) {
        uint64_t bsz = 1ull << cow_shift, lim = ((b >> COW_CHUNK_SHIFT) + 1) << COW_CHUNK_SHIFT, end, t0 = plat_now_ns();
        int st = plat_mem_state(b, lim, &end);
        cow_t[1] += plat_now_ns() - t0, cow_t[4]++;
        if (end <= b) end = b + 1;
        if (st == PLAT_PROT_RW) {   // whole blocks of the run: 1; a block the run ends inside: not
            uint64_t y = b;
            for (; y + bsz <= end; y += bsz) *cow_kind(y) = 1;
            if (y < end) *cow_kind(y) = 2;
        } else {
            for (uint64_t y = b; y < end; y += bsz) *cow_kind(y) = 2;
        }
    }
    return *k == 1;
}

void mem_cow_protect(uint64_t lo, uint64_t hi) {
    if (!cow_shift || hi > RT_ADDRESS_LIMIT) return;
    uint64_t bsz = 1ull << cow_shift, b = (lo + bsz - 1) & ~(bsz - 1), lim = hi & ~(bsz - 1);
    if (b >= lim || !watched(b, lim - b)) return;
    // Runs of blocks that can be protected, one call per run; a run stays in a region.
    while (b < lim) {
        if (!cow_can(b)) { b += bsz; continue; }
        uint64_t c = b + bsz;
        while (c < lim && (c & (FRZ_SIZE - 1)) && cow_can(c)) c += bsz;
        uint64_t t1 = plat_now_ns();
        int ok = plat_mem_protect(b, c - b, PLAT_PROT_READ);
        cow_t[2] += plat_now_ns() - t1, cow_t[3]++;
        if (!ok) {   // the mapping changed under a stale answer: ask again next time
            for (uint64_t y = b; y < c; y += bsz) *cow_kind(y) = 0;
            b = c;
            continue;
        }
        uint64_t r = b >> FRZ_SHIFT;
        if (!cow_reg_n[r]) {
            if (ncow_regs == ccow_regs) {
                ccow_regs = ccow_regs ? ccow_regs * 2 : 256;
                cow_regs = realloc(cow_regs, (size_t)ccow_regs * sizeof *cow_regs);
                if (!cow_regs) rt_fatal("memory: out of memory for copy-on-write blocks");
            }
            int dup = 0;
            for (int i = 0; i < ncow_regs && !dup; i++) dup = cow_regs[i] == r;
            if (!dup) cow_regs[ncow_regs++] = (uint32_t)r;
        }
        for (uint64_t y = b; y < c; y += bsz) *cow_slot(y, 1) = cow_sub, cow_reg_n[r]++, cow_nprot++, cow_st[0]++;
        cow_reg_last[r] = cow_sub;
        b = c;
    }
}

void mem_cow_end(void) {
    for (int i = 0; i < ncow_regs;) {
        uint32_t r = cow_regs[i];
        if (cow_reg_n[r] && cow_sub - cow_reg_last[r] >= COW_IDLE)
            cow_release_range_locked((uint64_t)r << FRZ_SHIFT, FRZ_SIZE);   // lets it freeze
        if (!cow_reg_n[r]) cow_regs[i] = cow_regs[--ncow_regs];
        else i++;
    }
    uint64_t d = plat_now_ns() - cow_pass_t0;
    if (d > cow_t[5]) cow_t[5] = d;
    plat_mutex_unlock(&frz_lock);
}

void mem_cow_done(uint32_t s) {
    if (!cow_shift) return;
    plat_mutex_lock(&frz_lock);
    cow_done = s;
    for (int k = 0; k < ncow_live;) {
        int i = cow_live[k];
        if (cow_sv[i].q - s - 1 < 0x80000000u) { k++; continue; }   // q > s: still needed
        int *pp = &cow_hash[cow_hidx(cow_sv[i].blk)];
        while (*pp != i) pp = &cow_sv[*pp].next;
        *pp = cow_sv[i].next;
        cow_sv[i].next = cow_free, cow_free = i;
        cow_live[k] = cow_live[--ncow_live];
    }
    plat_mutex_unlock(&frz_lock);
}

int mem_cow_read(uint64_t a, uint64_t n, uint32_t s, void *dst, uint64_t *when) {
    uint64_t blk = a & ~((1ull << cow_shift) - 1);
    int ret = 0;
    const void *src = (const void *)(uintptr_t)a;
    plat_mutex_lock(&frz_lock);
    *when = plat_now_ns();
    for (int i = cow_hash[cow_hidx(blk)]; i >= 0; i = cow_sv[i].next)
        if (cow_sv[i].blk == blk && s - cow_sv[i].p < 0x80000000u && cow_sv[i].q - s < 0x80000000u) {
            src = cow_arena + ((uint64_t)i << cow_shift) + (a - blk), ret = 2, *when = cow_sv[i].t;
            break;
        }
    if (!ret) {
        uint32_t *p = cow_slot(blk, 0);
        if (p && *p && s - *p < 0x80000000u) ret = 1;
    }
    memcpy(dst, src, n);
    plat_mutex_unlock(&frz_lock);
    return ret;
}

uint64_t mem_cow_oldest(void) {
    plat_mutex_lock(&frz_lock);
    uint64_t t = plat_now_ns();
    for (int k = 0; k < ncow_live; k++)
        if (cow_sv[cow_live[k]].t < t) t = cow_sv[cow_live[k]].t;
    plat_mutex_unlock(&frz_lock);
    return t;
}

void mem_cow_stats(uint64_t out[15]) {
    plat_mutex_lock(&frz_lock);
    for (int i = 0; i < 8; i++) out[i] = cow_st[i], cow_st[i] = 0;
    for (int i = 0; i < 6; i++) out[8 + i] = cow_t[i], cow_t[i] = 0;
    out[5] = cow_nprot, out[6] = (uint64_t)ncow_live;
    out[14] = cow_held, cow_held = 0;
    plat_mutex_unlock(&frz_lock);
}

// frz_lock held exclusively.
static void thaw_range_locked(uint64_t a, uint64_t n, int why) {
    if (!n || a >= RT_ADDRESS_LIMIT) return;
    cow_release_range_locked(a, n);
    if (why == 3) cow_kind_reset(a, n);
    uint64_t r0 = a >> FRZ_SHIFT, r1 = (a + n - 1 < RT_ADDRESS_LIMIT ? a + n - 1 : RT_ADDRESS_LIMIT - 1) >> FRZ_SHIFT;
    for (uint64_t r = r0; r <= r1; r++) thaw_locked(r, why);
}

void mem_thaw(uint64_t a, uint64_t n) {
    plat_mutex_lock(&frz_lock);   // also when nothing is frozen: the hold keeps a read's buffer writable
    thaw_range_locked(a, n, 2);
    plat_mutex_unlock(&frz_lock);
}

// frz_lock held exclusively. Makes the committed pages of region r read-only when every one of them is read-write;
// reserved holes (heap pages given back) stay as they are, and thaw_locked finds the read-only runs again.
static int freeze_locked(uint64_t r) {
    uint64_t a = r << FRZ_SHIFT, end = a + FRZ_SIZE, run[16][2];
    int n = 0;
    for (uint64_t x = a, y; x < end; x = y) {
        int st = plat_mem_state(x, end, &y);
        if (st >= PLAT_PROT_NONE) {
            if (st != PLAT_PROT_RW || n == 16) return 0;
            run[n][0] = x, run[n][1] = y, n++;
        } else if (st != PLAT_MEM_RESERVED) return 0;
    }
    for (int i = 0; i < n; i++)
        if (!plat_mem_protect(run[i][0], run[i][1] - run[i][0], PLAT_PROT_READ)) {
            while (i--) plat_mem_protect(run[i][0], run[i][1] - run[i][0], PLAT_PROT_RW);
            return 0;
        }
    return n > 0;
}

int mem_freeze(uint64_t a) {
    if ((a & (FRZ_SIZE - 1)) || !watched(a, FRZ_SIZE)) return 0;
    uint64_t r = a >> FRZ_SHIFT;
    int ok = 0;
    plat_mutex_lock(&frz_lock);
    if (frz[r]) ok = 1;
    else if (thaw_ms[r] && (int32_t)((uint32_t)plat_now_ms() - thaw_ms[r]) < (int32_t)FRZ_HOLD_MS) ok = 0;   // |1 may be ahead
    else if (freeze_locked(r)) {
        __atomic_store_n(&frz[r], 1, __ATOMIC_RELEASE);
        plat_inc32(&nfrz);
        plat_inc64(&frz_count[0]);
        ok = 1;
    }
    plat_mutex_unlock(&frz_lock);
    return ok;
}

int mem_frozen(uint64_t a) { return a < RT_ADDRESS_LIMIT && __atomic_load_n(&frz[a >> FRZ_SHIFT], __ATOMIC_ACQUIRE); }

void mem_freeze_stats(uint64_t *frozen, uint64_t *freezes, uint64_t *fault_thaws, uint64_t *other_thaws) {
    *frozen = (uint64_t)__atomic_load_n(&nfrz, __ATOMIC_RELAXED);
    *freezes = (uint64_t)plat_xchg64(&frz_count[0], 0);
    *fault_thaws = (uint64_t)plat_xchg64(&frz_count[1], 0);
    *other_thaws = (uint64_t)plat_xchg64(&frz_count[2], 0) + (uint64_t)plat_xchg64(&frz_count[3], 0);
}

// The first write into a frozen region thaws it and retries. A fault in a region another thread thawed meanwhile
// retries too; any other fault goes on to the crash handler.
// The platform's write-fault hook (plat_mem_fault_hook): 1 retries the write.
static int thaw_fault(uint64_t a) {
    if (a >= RT_ADDRESS_LIMIT || !watched(a, 1)) return 0;
    plat_mutex_lock(&frz_lock);
    if (cow_fault_locked(a)) {
        plat_mutex_unlock(&frz_lock);
        return 1;
    }
    int was = frz[a >> FRZ_SHIFT];
    thaw_locked(a >> FRZ_SHIFT, 1);
    plat_mutex_unlock(&frz_lock);
    if (was) return 1;
    uint64_t end;
    return plat_mem_state(a & ~(PAGE - 1), (a & ~(PAGE - 1)) + PAGE, &end) == PLAT_PROT_RW;
}

// Mapping changes thaw the regions they touch and run under frz_lock, so no freeze sees them half done.
static void commit(uint64_t a, uint64_t n, const char *what) {
    plat_mutex_lock(&frz_lock);
    thaw_range_locked(a, n, 3);
    int ok = !n || plat_mem_commit(a, n);
    plat_mutex_unlock(&frz_lock);
    if (!ok) rt_fatal("memory: commit %s %#llx+%#llx failed", what, (unsigned long long)a, (unsigned long long)n);
}

static void decommit(uint64_t a, uint64_t n, const char *what) {
    plat_mutex_lock(&frz_lock);
    thaw_range_locked(a, n, 3);
    int ok = !n || plat_mem_decommit(a, n);
    plat_mutex_unlock(&frz_lock);
    if (!ok) rt_fatal("memory: decommit %s %#llx+%#llx failed", what, (unsigned long long)a, (unsigned long long)n);
}

static void protect(uint64_t a, uint64_t n, int prot, const char *what) {
    plat_mutex_lock(&frz_lock);
    thaw_range_locked(a, n, 3);
    int ok = !n || plat_mem_protect(a, n, prot);
    plat_mutex_unlock(&frz_lock);
    if (!ok) rt_fatal("memory: protect %s %#llx+%#llx failed", what, (unsigned long long)a, (unsigned long long)n);
}

// The process is re-run when a region is taken: Windows places DLLs, heaps and thread stacks at random addresses,
// and a fresh process gets a fresh layout.
static void reexec_or_fail(const char *what) {
    const char *e = cfg_get("XCX_REEXEC");
    int n = e ? atoi(e) : 0;
    if (n >= REEXEC_LIMIT) rt_fatal("memory: %s is not free after %d attempts", what, n);
    char buf[16];
    snprintf(buf, sizeof buf, "%d", n + 1);
    plat_setenv("XCX_REEXEC", buf);
    plat_reexec();
}

void mem_init(void) {
    if (L.pool) return;
    // Fixed window: modules, kernel region, host-function addresses.
    mods_lo = MOD_MAIN_BASE;
    mods_hi = align_up(rt_layout[rt_layout_count - 1].base + rt_layout[rt_layout_count - 1].size, 0x10000);
    if (mods_hi > RT_KERNEL_BASE) rt_fatal("memory: modules end at %#llx, inside the kernel region",
                                           (unsigned long long)mods_hi);
    if (!reserve_at(mods_lo, mods_hi - mods_lo, 0)) reexec_or_fail("the module window");
    if (!reserve_at(RT_KERNEL_BASE, RT_KERNEL_SIZE, 0)) reexec_or_fail("the kernel region");
    if (!reserve_at(RT_HOSTFN_BASE, RT_HOSTFN_SIZE, 0)) reexec_or_fail("the host-function window");

    // Stack and heap block, then the alias region above it, both searched below the module window.
    uint64_t low = 0, alias = 0;
    for (uint64_t at = LOW_SEARCH_FROM; at + LOW_SIZE <= mods_lo && !low; at += GiB)
        if (reserve_at(at, LOW_SIZE, 1)) low = at;
    if (!low) reexec_or_fail("a 6 GiB stack and heap block below 2^39");
    for (uint64_t at = align_up(low + LOW_SIZE, ALIAS_ALIGN); at + ALIAS_SIZE <= mods_lo && !alias; at += ALIAS_ALIGN)
        if (reserve_at(at, ALIAS_SIZE, 1)) alias = at;
    if (!alias) reexec_or_fail("a 64 GiB alias region below 2^39");

    plat_mem_fault_hook(thaw_fault);   // before the crash handler
    alias_pages = calloc(ALIAS_PAGES, 1);
    if (!alias_pages) rt_fatal("memory: cannot allocate the alias page map");
    // xcx.ini is read here, once the address space is claimed and before the first setting: the pool size below
    // depends on XCX_APP_POOL and on the resolution settings (resolution.c decides them once, on this first call).
    cfg_init();
    const char *pool = cfg_get("XCX_APP_POOL");
    L = (RtGuestLayout){
        .aslr = {low, RT_ADDRESS_LIMIT - low},
        .stack = {low + STACK_OFF, STACK_SIZE},
        .heap = {low + HEAP_OFF, HEAP_CAP},
        .alias = {alias, ALIAS_SIZE},
        .kernel = {RT_KERNEL_BASE, RT_KERNEL_SIZE},
        .hostfn = {RT_HOSTFN_BASE, RT_HOSTFN_SIZE},
        .pool = pool ? strtoull(pool, NULL, 0) : DEFAULT_POOL + res_heap_extra_bytes(),   // resolution.c
    };
    if (!L.pool) rt_fatal("memory: XCX_APP_POOL is 0");
}

const RtGuestLayout *mem_layout(void) { return &L; }

// A range inside one watched reservation (stack and heap block, or alias region) only.
static uint64_t written_pages(uint64_t addr, uint64_t size, uint64_t *pages, uint64_t max, int reset) {
    if (!max || !size) return 0;
    if (!watched(addr, size)) return UINT64_MAX;
    return plat_mem_written(addr, size, pages, max, reset);
}

uint64_t mem_written_pages(uint64_t addr, uint64_t size, uint64_t *pages, uint64_t max) {
    return written_pages(addr, size, pages, max, 1);
}

uint64_t mem_written_peek(uint64_t addr, uint64_t size, uint64_t *pages, uint64_t max) {
    return written_pages(addr, size, pages, max, 0);
}

static uint64_t image_bytes(void) {
    uint64_t n = 0;
    for (int i = 0; i < ldr_count(); i++) n += ldr_module(i)->size;
    return align_up(n, PAGE);
}

static int overlaps(uint64_t a, uint64_t n, uint64_t b, uint64_t m) { return a < b + m && b < a + n; }
static int inside(uint64_t a, uint64_t n, RtRange r) { return a >= r.base && n <= r.size && a - r.base <= r.size - n; }

// ---- 0x01 SetHeapSize
uint32_t mem_set_heap_size(uint64_t size, uint64_t *addr) {
    plat_mutex_lock(&lock);
    if (size > HEAP_CAP || size % 0x200000 || size + image_bytes() > L.pool) {
        plat_mutex_unlock(&lock);
        return R_INVALID_STATE;
    }
    if (size > heap_size) commit(L.heap.base + heap_size, size - heap_size, "heap");
    if (size < heap_size) decommit(L.heap.base + size, heap_size - size, "heap");
    heap_size = size;
    plat_mutex_unlock(&lock);
    if (addr) *addr = L.heap.base;
    return R_OK;
}

// ---- module data permissions (0x02, 0x03) and module pages for QueryMemory
static int module_index(uint64_t a) {
    for (int i = 0; i < ldr_count(); i++) {
        const RtLoaded *m = ldr_module(i);
        if (a - m->base < m->size) return i;
    }
    return -1;
}

static uint8_t *perm_table(int i) {   // under lock
    if (!module_perm[i]) {
        const RtLoaded *m = ldr_module(i);
        size_t n = (m->size - m->data_off) / PAGE;
        module_perm[i] = malloc(n);
        if (!module_perm[i]) rt_fatal("memory: module permission table");
        memset(module_perm[i], PERM_RW, n);
    }
    return module_perm[i];
}

static unsigned module_page_perm(int i, uint64_t off) {   // under lock
    const RtLoaded *m = ldr_module(i);
    if (off < m->data_off) return off < align_up(m->text_end, PAGE) ? PERM_RX : PERM_R;
    return module_perm[i] ? module_perm[i][(off - m->data_off) / PAGE] : PERM_RW;
}

// The module whose data segment holds [a, a+n), or -1.
static int module_data(uint64_t a, uint64_t n) {
    int i = module_index(a);
    if (i < 0) return -1;
    const RtLoaded *m = ldr_module(i);
    uint64_t off = a - m->base;
    return off >= m->data_off && n <= m->size - off ? i : -1;
}

uint32_t mem_set_permission(uint64_t a, uint64_t n, uint32_t perm) {
    if (a & (PAGE - 1)) return R_INVALID_ADDRESS;
    if (!n || (n & (PAGE - 1))) return R_INVALID_SIZE;
    if (perm != PERM_NONE && perm != PERM_R && perm != PERM_RW) return R_INVALID_NEW_PERM;
    int i = module_data(a, n);
    if (i < 0) return R_INVALID_STATE;
    const RtLoaded *m = ldr_module(i);
    plat_mutex_lock(&lock);
    uint8_t *t = perm_table(i) + (a - m->base - m->data_off) / PAGE;
    for (uint64_t p = 0; p < n / PAGE; p++)
        if (t[p] & ATTR_PERMISSION_LOCKED) {
            plat_mutex_unlock(&lock);
            return R_INVALID_STATE;
        }
    memset(t, (int)perm, n / PAGE);
    protect(a, n, perm == PERM_RW ? PLAT_PROT_RW : perm == PERM_R ? PLAT_PROT_READ : PLAT_PROT_NONE, "module data");
    plat_mutex_unlock(&lock);
    rt_log("%s data permission +%#llx size %#llx perm %u", m->name, (unsigned long long)(a - m->base),
           (unsigned long long)n, perm);
    return R_OK;
}

// The only module attribute the SDK sets is the irreversible RELRO lock.
uint32_t mem_set_attribute(uint64_t a, uint64_t n, uint32_t mask, uint32_t at) {
    if (a & (PAGE - 1)) return R_INVALID_ADDRESS;
    if (!n || (n & (PAGE - 1))) return R_INVALID_SIZE;
    if (mask != ATTR_PERMISSION_LOCKED || at != ATTR_PERMISSION_LOCKED) return R_INVALID_NEW_PERM;
    int i = module_data(a, n);
    if (i < 0) return R_INVALID_STATE;
    const RtLoaded *m = ldr_module(i);
    plat_mutex_lock(&lock);
    uint8_t *t = perm_table(i) + (a - m->base - m->data_off) / PAGE;
    for (uint64_t p = 0; p < n / PAGE; p++) t[p] |= ATTR_PERMISSION_LOCKED;
    plat_mutex_unlock(&lock);
    return R_OK;
}

// ---- 0x04/0x05 MapMemory, UnmapMemory: thread stacks move from heap or alias into the stack region by copy; the
// source is inaccessible while moved and gets the contents back on unmap.
static int alias_all_mapped(uint64_t a, uint64_t n) {   // under lock
    uint64_t first = (a - L.alias.base) / PAGE;
    for (uint64_t i = 0; i < n / PAGE; i++)
        if (!alias_pages[first + i]) return 0;
    return 1;
}

static uint32_t stack_move(int unmap, uint64_t dst, uint64_t src, uint64_t n) {
    if (!n || ((dst | src | n) & (PAGE - 1))) return R_INVALID_ADDRESS;
    if (!inside(dst, n, L.stack)) return R_INVALID_REGION;
    int in_heap = inside(src, n, (RtRange){L.heap.base, heap_size});
    int in_alias = inside(src, n, L.alias);
    // XCX gives some threads stacks in main's .bss; Horizon lets module data be aliased like heap memory.
    int in_module = module_data(src, n) >= 0;
    if (!in_heap && !in_alias && !in_module) return R_INVALID_REGION;
    plat_mutex_lock(&lock);
    uint32_t r = R_OK;
    if (in_alias && !alias_all_mapped(src, n)) {
        r = R_INVALID_REGION;
        goto out;
    }
    unsigned found = nstack_maps;
    for (unsigned i = 0; i < nstack_maps; i++)
        if (stack_maps[i].dst == dst && stack_maps[i].src == src && stack_maps[i].size == n) found = i;
    if (unmap) {
        if (found == nstack_maps) {
            r = R_INVALID_STATE;
            goto out;
        }
        protect(src, n, PLAT_PROT_RW, "moved stack source");
        memcpy((void *)(uintptr_t)src, (const void *)(uintptr_t)dst, n);
        decommit(dst, n, "moved stack");
        stack_maps[found] = stack_maps[--nstack_maps];
    } else {
        if (nstack_maps == MAX_STACK_MAPS) {
            r = R_INVALID_STATE;
            goto out;
        }
        for (unsigned i = 0; i < nstack_maps; i++)
            if (overlaps(dst, n, stack_maps[i].dst, stack_maps[i].size) ||
                overlaps(src, n, stack_maps[i].src, stack_maps[i].size)) {
                r = R_INVALID_REGION;
                goto out;
            }
        commit(dst, n, "moved stack");
        memcpy((void *)(uintptr_t)dst, (const void *)(uintptr_t)src, n);
        protect(src, n, PLAT_PROT_NONE, "moved stack source");
        stack_maps[nstack_maps++] = (StackMap){dst, src, n};
    }
out:
    plat_mutex_unlock(&lock);
    return r;
}

uint32_t mem_map(uint64_t dst, uint64_t src, uint64_t n) { return stack_move(0, dst, src, n); }
uint32_t mem_unmap(uint64_t dst, uint64_t src, uint64_t n) { return stack_move(1, dst, src, n); }

// ---- 0x2C/0x2D MapPhysicalMemory, UnmapPhysicalMemory. Horizon maps only the still-unmapped pages of the range and
// leaves mapped pages and their contents alone; XCX's heaps grow by re-mapping ranges they partly own.
static void alias_runs(uint64_t first, uint64_t count, uint8_t want, int do_commit) {   // under lock
    uint64_t i = 0;
    while (i < count) {
        if (alias_pages[first + i] != want) { i++; continue; }
        uint64_t j = i;
        while (j < count && alias_pages[first + j] == want) j++;
        uint64_t a = L.alias.base + (first + i) * PAGE, n = (j - i) * PAGE;
        if (do_commit) commit(a, n, "physical memory");
        else decommit(a, n, "physical memory");
        memset(alias_pages + first + i, !want, j - i);
        i = j;
    }
}

static uint32_t alias_map(int unmap, uint64_t a, uint64_t n) {
    if (!n || ((a | n) & (PAGE - 1))) return R_INVALID_ADDRESS;
    if (!inside(a, n, L.alias)) return R_INVALID_REGION;
    uint64_t first = (a - L.alias.base) / PAGE, count = n / PAGE;
    plat_mutex_lock(&lock);
    uint32_t r = R_OK;
    if (unmap) {
        for (unsigned i = 0; i < nstack_maps; i++)
            if (overlaps(a, n, stack_maps[i].src, stack_maps[i].size)) {
                r = R_INVALID_REGION;
                goto out;
            }
        uint64_t released = 0;
        for (uint64_t i = 0; i < count; i++) released += alias_pages[first + i];
        alias_runs(first, count, 1, 0);
        alias_bytes -= released * PAGE;
    } else {
        uint64_t fresh = 0;
        for (uint64_t i = 0; i < count; i++) fresh += !alias_pages[first + i];
        if (fresh * PAGE + alias_bytes + heap_size + image_bytes() > L.pool) {
            r = R_LIMIT_REACHED;   // the SDK maps LimitReached to its recoverable out-of-memory result
            goto out;
        }
        alias_runs(first, count, 0, 1);
        alias_bytes += fresh * PAGE;
    }
out:
    plat_mutex_unlock(&lock);
    if (r) rt_log("%sMapPhysicalMemory %#llx+%#llx failed %#x (alias %#llx used, heap %#llx)", unmap ? "Un" : "",
                  (unsigned long long)a, (unsigned long long)n, r, (unsigned long long)alias_bytes,
                  (unsigned long long)heap_size);
    return r;
}

uint32_t mem_map_physical(uint64_t a, uint64_t n) { return alias_map(0, a, n); }
uint32_t mem_unmap_physical(uint64_t a, uint64_t n) { return alias_map(1, a, n); }

// ---- 0x06 QueryMemory
// The region around a that no known region covers: everything else below 2^39 belongs to the host and is reported
// as taken (Code, RX), so the SDK never picks it.
static void taken_gap(uint64_t a, uint64_t *base, uint64_t *size) {
    uint64_t lo = 0, hi = RT_ADDRESS_LIMIT;
    RtRange known[3 + MOD_COUNT + 2];
    int n = 0;
    known[n++] = (RtRange){L.aslr.base, LOW_SIZE};
    known[n++] = L.alias;
    known[n++] = L.kernel;
    known[n++] = L.hostfn;
    for (int i = 0; i < ldr_count(); i++) known[n++] = (RtRange){ldr_module(i)->base, ldr_module(i)->size};
    for (int i = 0; i < n; i++) {
        uint64_t e = known[i].base + known[i].size;
        if (e <= a && e > lo) lo = e;
        if (known[i].base > a && known[i].base < hi) hi = known[i].base;
    }
    *base = lo;
    *size = hi - lo;
}

static uint64_t run_end(const uint8_t *p, uint64_t i, uint64_t n, uint8_t v) {
    while (i < n && (i & 7)) { if (p[i] != v) return i; i++; }
    uint64_t w = v ? 0x0101010101010101ull : 0;
    while (i + 8 <= n && *(const uint64_t *)(p + i) == w) i += 8;
    while (i < n && p[i] == v) i++;
    return i;
}

static uint64_t run_start(const uint8_t *p, uint64_t i, uint8_t v) {   // first index of the run that ends at i
    while (i > 0 && (i & 7)) { if (p[i - 1] != v) return i; i--; }
    uint64_t w = v ? 0x0101010101010101ull : 0;
    while (i >= 8 && *(const uint64_t *)(p + i - 8) == w) i -= 8;
    while (i > 0 && p[i - 1] == v) i--;
    return i;
}

uint32_t mem_query(uint64_t a, RtMemoryInfo *out) {
    RtMemoryInfo r = {.base = a & ~(PAGE - 1), .size = PAGE};
    uint64_t low = L.aslr.base;
    plat_mutex_lock_shared(&lock);
    int mi;
    if (a >= RT_ADDRESS_LIMIT) {
        r.base = RT_ADDRESS_LIMIT;
        r.size = 0 - RT_ADDRESS_LIMIT;
        r.state = ST_INACCESSIBLE;
    } else if (a < low) {
        r.base = 0;
        r.size = low;
        r.state = ST_FREE;
    } else if (a < low + LOW_SIZE) {
        uint64_t heap = L.heap.base;
        if (a >= heap && a < heap + heap_size) {
            r = (RtMemoryInfo){.base = heap, .size = heap_size, .state = ST_NORMAL, .perm = PERM_RW};
        } else if (a < heap) {
            r = (RtMemoryInfo){.base = low, .size = heap - low, .state = ST_FREE};
        } else {
            r = (RtMemoryInfo){.base = heap + heap_size, .size = low + LOW_SIZE - heap - heap_size, .state = ST_FREE};
        }
    } else if (a - L.alias.base < L.alias.size) {
        uint64_t p = (a - L.alias.base) / PAGE;
        uint8_t v = alias_pages[p];
        uint64_t s = run_start(alias_pages, p, v), e = run_end(alias_pages, p, ALIAS_PAGES, v);
        r = (RtMemoryInfo){.base = L.alias.base + s * PAGE, .size = (e - s) * PAGE, .state = v ? ST_NORMAL : ST_FREE,
                           .perm = v ? PERM_RW : PERM_NONE};
    } else if ((mi = module_index(a)) >= 0) {
        const RtLoaded *m = ldr_module(mi);
        uint64_t off = (a - m->base) & ~(PAGE - 1), lo = off, hi = off + PAGE;
        unsigned perm = module_page_perm(mi, off);
        int data = off >= m->data_off;
        uint64_t limit = data ? m->data_off : 0, end = data ? m->size : m->data_off;
        while (lo > limit && module_page_perm(mi, lo - PAGE) == perm) lo -= PAGE;
        while (hi < end && module_page_perm(mi, hi) == perm) hi += PAGE;
        r = (RtMemoryInfo){.base = m->base + lo, .size = hi - lo, .state = data ? ST_CODEDATA : ST_CODE,
                           .perm = perm & 7, .attr = perm & ATTR_PERMISSION_LOCKED};
    } else if (a - L.kernel.base < L.kernel.size) {
        r = (RtMemoryInfo){.base = L.kernel.base, .size = L.kernel.size, .state = ST_THREADLOCAL, .perm = PERM_RW};
    } else if (a - L.hostfn.base < L.hostfn.size) {
        r = (RtMemoryInfo){.base = L.hostfn.base, .size = L.hostfn.size, .state = ST_INACCESSIBLE};
    } else {
        taken_gap(a, &r.base, &r.size);
        r.state = ST_CODE;
        r.perm = PERM_RX;
    }
    // Moved stacks: the destination is Stack, the source Normal without access; free and normal regions around them
    // are clipped (MNG services.c).
    for (unsigned i = 0; i < nstack_maps; i++) {
        const StackMap *m = &stack_maps[i];
        uint64_t end = m->dst + m->size, send = m->src + m->size;
        if (a >= m->dst && a < end) {
            r = (RtMemoryInfo){.base = m->dst, .size = m->size, .state = ST_STACK, .perm = PERM_RW};
            break;
        }
        if (a >= m->src && a < send) {
            r = (RtMemoryInfo){.base = m->src, .size = m->size, .state = ST_NORMAL, .perm = PERM_NONE, .attr = 4};
            break;
        }
        uint64_t lo = r.base, hi = r.base + r.size;
        uint64_t b = r.state == ST_FREE ? m->dst : r.state == ST_NORMAL ? m->src : 0;
        uint64_t e = b + m->size;
        if (!b) continue;
        if (e <= a && e > lo) lo = e;
        if (b > a && b < hi) hi = b;
        r.base = lo;
        r.size = hi - lo;
    }
    plat_mutex_unlock_shared(&lock);
    if (out) *out = r;
    return R_OK;
}

// ---- GetInfo memory ids
int mem_info(uint32_t id, uint64_t *out) {
    uint64_t v;
    switch (id) {
    case 2: v = L.alias.base; break;
    case 3: v = L.alias.size; break;
    case 4: v = L.heap.base; break;
    case 5: v = L.heap.size; break;
    case 6: case 21: v = L.pool; break;
    case 7: case 22:
        plat_mutex_lock_shared(&lock);
        v = image_bytes() + heap_size + alias_bytes;
        plat_mutex_unlock_shared(&lock);
        break;
    case 12: v = L.aslr.base; break;
    case 13: v = L.aslr.size; break;
    case 14: v = L.stack.base; break;
    case 15: v = L.stack.size; break;
    case 16: v = SYSTEM_RESOURCE_BUDGET; break;
    case 17: v = sizeof stack_maps + ALIAS_PAGES; break;   // system resource in use: our bookkeeping
    case 28: v = 0; break;   // AliasRegionExtraSize: no kernel-owned extension
    default: return 0;
    }
    *out = v;
    return 1;
}

// ---- kernel region: power-of-two blocks, a free list per size, bump allocation with lazy commit.
#define KCLASSES 29          // 64 B .. 256 MiB
#define KMIN_SHIFT 6
#define KCOMMIT 0x10000ull
typedef struct KFree { struct KFree *next; } KFree;
static KFree *kfree_list[KCLASSES];
static uint64_t kbump, kcommitted;
static PlatMutex klock = PLAT_MUTEX_INIT;

static int kclass(uint64_t size) {
    int s = KMIN_SHIFT;
    while ((1ull << s) < size) s++;
    return s - KMIN_SHIFT;
}

void *mem_kernel_alloc(uint64_t size, uint64_t align) {
    if (!L.pool) rt_fatal("mem_kernel_alloc before mem_init");
    if (align > KCOMMIT || (align & (align - 1))) rt_fatal("mem_kernel_alloc: alignment %#llx", (unsigned long long)align);
    uint64_t want = size > align ? size : align;
    int k = kclass(want ? want : 1);
    if (k >= KCLASSES) rt_fatal("mem_kernel_alloc: %#llx bytes", (unsigned long long)size);
    uint64_t bytes = 1ull << (k + KMIN_SHIFT), al = bytes < KCOMMIT ? bytes : KCOMMIT;
    plat_mutex_lock(&klock);
    KFree *f = kfree_list[k];
    void *p;
    if (f) {
        kfree_list[k] = f->next;
        p = f;
    } else {
        uint64_t a = align_up(kbump ? kbump : RT_KERNEL_BASE, al);
        if (a + bytes > RT_KERNEL_BASE + RT_KERNEL_SIZE) rt_fatal("kernel region exhausted (%#llx bytes)",
                                                                  (unsigned long long)bytes);
        uint64_t need = align_up(a + bytes, KCOMMIT);
        if (!kcommitted) kcommitted = RT_KERNEL_BASE;
        if (need > kcommitted) {
            commit(kcommitted, need - kcommitted, "kernel region");
            kcommitted = need;
        }
        kbump = a + bytes;
        p = (void *)(uintptr_t)a;
    }
    plat_mutex_unlock(&klock);
    memset(p, 0, size ? size : 1);
    return p;
}

void mem_kernel_free(void *p, uint64_t size) {
    if (!p) return;
    uint64_t a = (uintptr_t)p;
    if (a - RT_KERNEL_BASE >= RT_KERNEL_SIZE) rt_fatal("mem_kernel_free(%p): not in the kernel region", p);
    int k = kclass(size ? size : 1);
    // A block allocated with a larger alignment sits in a larger class; freeing it into the smaller class only
    // wastes the rest.
    plat_mutex_lock(&klock);
    KFree *f = p;
    f->next = kfree_list[k];
    kfree_list[k] = f;
    plat_mutex_unlock(&klock);
}
