#include "mods.h"
// xcx.exe entry: reserves the guest address space, parses the command line and runs one mode.
//
//   xcx.exe [--exefs DIR] [--romfs DIR] [--profile DIR] <mode>
//     --selftest        layout, dispatch table and host-function registry; needs no game files
//     --probe-loader    load and relocate the modules, check them against the measured 1.2.1 values
//     --probe-kernel, --probe-ipc, --probe-services, --probe-fs    host probes
//     --probe-gfx-abi   NVN object layouts against nvn.h's reserved sizes; needs no game files
//     --probe-nvn, --probe-nvn-tex, --probe-window, --probe-shader, --probe-vkmem, --probe-vkexec    graphics probes
//     --probe-astc      the compute ASTC decoder against astc_decode_ldr; needs no game files
//     --probe-movie     host VP9 movie::Decoder on one RomFS movie; skips without game data
//     --probe-mask      the bug report's masking (pii_mask.c) of the file XCX_PROBE_MASK_IN to stdout; the comma-
//                       separated XCX_PROBE_MASK_NAMES are masked as user names too; needs no game files
//     --boot            run the game to the first NVN call
//     --seconds S       run the game (implies --boot's start-up) until the world clock (game time since the loaded
//                       save's first frame) passed S seconds, then exit 0 at that present
//     --shot PNG        with --seconds: save that frame; env XCX_GPU=null|vulkan selects the back end
//     (no mode)         run the game
//
// Also here: the runtime services the generated code calls besides rt_dispatch and rt_svc (rt_brk, rt_udf,
// rt_ticks, the function profiler hook), logging, SHA-256, and the crash reporter (a vectored exception handler that
// names the lifted function, the guest registers and the guest frame-pointer chain).
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
#include "sha256.h"
#include "bug_report.h"
#include "pii_mask.h"

#define MAX_MODULES 16

// ---- logging, tracing, hashing
unsigned rt_trace;

// Log file. A game run whose stderr is not a file already (started from Explorer, a shortcut or a console) also
// writes its log to logs\xcx.log next to xcx.exe, so a player has one to attach to a report; run_game.py and
// `2> file` keep their own file. Each start moves the earlier logs to xcx.1.log .. xcx.4.log. A file stops at 64 MiB
// (XCX_TRACE can write that much). Lines from before log_open wait in a static buffer: mem_init runs first so that
// nothing else lands in the guest address space, and an earlier open would put the CRT's heap there.
#define LOG_KEEP 5
#define LOG_CAP (64ull << 20)
enum { LOG_EARLY, LOG_FILE, LOG_OFF };
static int g_log_state;
static PlatAppend *g_log_file;
static char g_log_path[PLAT_PATH_MAX * 3];
static char g_log_early[32768];
static size_t g_log_early_n;
static uint64_t g_log_bytes;
static char g_crash_path[PLAT_PATH_MAX * 3 + 16];   // rt_crash_report_path; set by log_open

// The last 64 KiB written to the log, for the crash report's "last log lines": a ring every write copies into. Lines
// of threads that write at once may interleave in it; the report is for reading, so that is fine.
#define LOG_TAIL (64u << 10)
static char g_log_tail[LOG_TAIL];
static uint64_t g_log_tail_at;

static void tail_put(const char *p, size_t n) {
    if (n > LOG_TAIL) p += n - LOG_TAIL, n = LOG_TAIL;
    uint64_t at = __atomic_fetch_add(&g_log_tail_at, n, __ATOMIC_RELAXED);
    size_t o = (size_t)(at % LOG_TAIL), first = n < LOG_TAIL - o ? n : LOG_TAIL - o;
    memcpy(g_log_tail + o, p, first);
    memcpy(g_log_tail, p + first, n - first);
}

void rt_log_raw(const char *p, size_t n) {
    fwrite(p, 1, n, stderr);
    tail_put(p, n);
    int state = __atomic_load_n(&g_log_state, __ATOMIC_ACQUIRE);
    if (state == LOG_EARLY) {
        size_t at = __atomic_fetch_add(&g_log_early_n, n, __ATOMIC_RELAXED);
        if (at + n <= sizeof g_log_early) memcpy(g_log_early + at, p, n);
    } else if (state == LOG_FILE) {
        uint64_t at = __atomic_fetch_add(&g_log_bytes, n, __ATOMIC_RELAXED);
        if (at + n <= LOG_CAP) {
            plat_append_write(g_log_file, p, n);
        } else if (at <= LOG_CAP) {   // the first write past the cap says so, once
            static const char cut[] = "[host] log: the file reached 64 MiB, the rest of this run goes to stderr only\n";
            plat_append_write(g_log_file, cut, sizeof cut - 1);
        }
    }
}

void rt_vlogf(const char *fmt, va_list ap) {
    char buf[2048];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n > 0) rt_log_raw(buf, n < (int)sizeof buf ? (size_t)n : sizeof buf - 1);
}

void rt_logf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    rt_vlogf(fmt, ap);
    va_end(ap);
}

void rt_log(const char *fmt, ...) {
    char buf[2048];
    int n = snprintf(buf, sizeof buf, "[host] ");
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(buf + n, sizeof buf - n - 1, fmt, ap);
    va_end(ap);
    n = k < 0 ? n : n + k < (int)sizeof buf - 1 ? n + k : (int)sizeof buf - 2;
    buf[n++] = '\n';
    rt_log_raw(buf, n);
}

void rt_fast_cores(void) {
    static int on = -1, logged;
    if (on < 0) on = cfg_bool("XCX_FAST_CORES");
    uint32_t n = on ? plat_thread_prefer_fast_cores() : 0;
    if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED))
        rt_log(!on ? "cpu: fast cores off (fast_cores = 0)"
               : n ? "cpu: hybrid CPU: the game's threads prefer the %u logical processors of its fastest cores"
                   : "cpu: one kind of core: every thread may run on any of them", n);
}

const char *rt_log_path(void) {
    return __atomic_load_n(&g_log_state, __ATOMIC_ACQUIRE) == LOG_FILE ? g_log_path : NULL;
}

const char *rt_crash_report_path(void) { return g_crash_path[0] ? g_crash_path : NULL; }

int rt_crash_report_nth(int i, char *out, size_t cap) {
    if (!g_crash_path[0] || snprintf(out, cap, "%s", g_crash_path) >= (int)cap) return 0;
    if (!i) return 1;
    size_t n = strlen(out);   // ".../crash.txt" -> ".../crash.<i>.txt"
    if (n < 4) return 0;
    out[n - 4] = 0;
    return snprintf(out + n - 4, cap - (n - 4), ".%d.txt", i) < (int)(cap - (n - 4));
}

// Opens the log file for a game run (game = 1) and writes the buffered lines to it; anything else drops them. Runs on
// the main thread before the boot starts other threads.
static void log_open(int game) {
    char dir[PLAT_PATH_MAX * 3], from[PLAT_PATH_MAX * 3 + 16], to[PLAT_PATH_MAX * 3 + 16];
    char *cut = NULL;
    if (game && !plat_stderr_path(dir, sizeof dir) && plat_exe_path(dir, sizeof dir)) {
        cut = plat_last_sep(dir);
        char *f = strrchr(dir, '/');
        if (f > cut) cut = f;
    }
    if (cut && (size_t)(cut - dir) + sizeof "/logs" <= sizeof dir) {
        strcpy(cut, "/logs");
        plat_file_mkdir_p(dir);
        for (int i = LOG_KEEP - 1; i > 0; i--) {   // xcx.3.log -> xcx.4.log, ..., xcx.log -> xcx.1.log
            if (i > 1) snprintf(from, sizeof from, "%s/xcx.%d.log", dir, i - 1);
            else snprintf(from, sizeof from, "%s/xcx.log", dir);
            snprintf(to, sizeof to, "%s/xcx.%d.log", dir, i);
            if (plat_file_exists(from)) plat_file_replace(from, to);
        }
        snprintf(g_log_path, sizeof g_log_path, "%s/xcx.log", dir);
        g_log_file = plat_append_open(g_log_path);
        snprintf(g_crash_path, sizeof g_crash_path, "%s/crash.txt", dir);
    } else if (plat_stderr_path(dir, sizeof dir) && (cut = plat_last_sep(dir))) {   // run_game.py, `2> file`
        *cut = 0;
        snprintf(g_crash_path, sizeof g_crash_path, "%s/crash.txt", dir);
    }
    if (!g_log_file) {
        __atomic_store_n(&g_log_state, LOG_OFF, __ATOMIC_RELEASE);
        return;
    }
    size_t n = __atomic_load_n(&g_log_early_n, __ATOMIC_RELAXED);
    if (n > sizeof g_log_early) n = sizeof g_log_early;
    plat_append_write(g_log_file, g_log_early, n);
    g_log_bytes = n;
    __atomic_store_n(&g_log_state, LOG_FILE, __ATOMIC_RELEASE);
    rt_log("log: %s", g_log_path);
}

static void parse_trace(void) {
    const char *e = cfg_get("XCX_TRACE");   // no default: nothing is traced when unset
    if (!e) return;
    static const struct { const char *name; unsigned bit; } names[] = {
        {"svc", RT_TRACE_SVC}, {"host", RT_TRACE_HOSTCALL}, {"ipc", RT_TRACE_IPC}, {"fs", RT_TRACE_FS},
        {"gfx", RT_TRACE_GFX}, {"perf", RT_TRACE_PERF}, {"sched", RT_TRACE_SCHED}, {"audio", RT_TRACE_AUDIO},
        {"present", RT_TRACE_PRESENT}, {"all", ~0u}};
    for (const char *p = e; *p;) {
        size_t n = strcspn(p, ",");
        int known = 0;
        for (unsigned i = 0; i < sizeof names / sizeof *names; i++)
            if (strlen(names[i].name) == n && !strncmp(p, names[i].name, n)) rt_trace |= names[i].bit, known = 1;
        if (!known && n) rt_log("XCX_TRACE: unknown item '%.*s' (svc, host, ipc, fs, gfx, perf, sched, audio, present, all)", (int)n, p);
        p += n + (p[n] == ',');
    }
}

void rt_sha256(const void *p, size_t n, uint8_t out[32]) { sha256(p, n, out); }

// ---- configuration
static RtConfig cfg;
const RtConfig *rt_config(void) { return &cfg; }

// ---- runtime services the generated code calls (cpu.h)
volatile int g_fnprof = 0;
void fnprof_hit(uint64_t addr, uint64_t lr, uint64_t x0) { (void)addr, (void)lr, (void)x0; }

void rt_brk(Cpu *c, uint32_t imm, uint64_t pc) {
    char at[128], lr[128];
    rt_fatal("brk #%#x at %s (lr %s)", imm, ldr_describe(pc, at, sizeof at), ldr_describe(c->x[30], lr, sizeof lr));
}

void rt_udf(Cpu *c, uint64_t pc) {
    char at[128], lr[128];
    rt_fatal("undefined instruction at %s (lr %s)", ldr_describe(pc, at, sizeof at),
             ldr_describe(c->x[30], lr, sizeof lr));
}

// CNTPCT_EL0: the Switch counter runs at 19.2 MHz. The scale is set before main, so no thread reads it half written.
static PlatScale tick_scale;

__attribute__((constructor)) static void tick_scale_init(void) { tick_scale = plat_scale(19200000u, plat_counter_hz()); }

uint64_t rt_ticks(void) { return plat_scaled(tick_scale, plat_counter()); }

// ---- crash reporter
typedef struct { uintptr_t host; uint64_t guest; } FnRev;
static FnRev *fn_rev;
static uint64_t fn_rev_n;

static int by_host(const void *a, const void *b) {
    uintptr_t x = ((const FnRev *)a)->host, y = ((const FnRev *)b)->host;
    return x < y ? -1 : x > y;
}

// The lifted function that contains a host code address, by the nearest function start below it.
static uint64_t lifted_function(uintptr_t rip) {
    if (!fn_rev) {
        static RtModule mods[MAX_MODULES];
        int n = rt_linked_modules(mods, MAX_MODULES);
        uint64_t total = 0;
        for (int i = 0; i < n; i++) total += mods[i].count;
        fn_rev = malloc((total ? total : 1) * sizeof *fn_rev);
        if (!fn_rev) return 0;
        for (int i = 0; i < n; i++)
            for (uint64_t k = 0; k < mods[i].count; k++)
                fn_rev[fn_rev_n++] = (FnRev){(uintptr_t)mods[i].fns[k], mods[i].addrs[k]};
        qsort(fn_rev, fn_rev_n, sizeof *fn_rev, by_host);
    }
    // The lifted code is one range with the host code after it; an address above the last function start is taken for
    // host code, which costs only the last function's body.
    if (!fn_rev_n || rip > fn_rev[fn_rev_n - 1].host) return 0;
    uint64_t lo = 0, hi = fn_rev_n;
    while (lo < hi) {
        uint64_t mid = (lo + hi) / 2;
        if (fn_rev[mid].host <= rip) lo = mid + 1;
        else hi = mid;
    }
    return lo ? fn_rev[lo - 1].guest : 0;
}

static int readable(uint64_t a, uint64_t n) { return plat_mem_readable(a, n); }

#ifdef _WIN32
// XCX_WATCH=<hex guest address>: a debugging write watchpoint. A thread makes the page read-only once it is committed;
// the first writes to it are logged with the lifted function and guest call chain, the page is made writable for the
// writer, and the thread arms it again 50 ms later (16 hits at most).
static volatile uint64_t watch_page;
static volatile LONG watch_hits, watch_armed;

static DWORD WINAPI watch_thread(void *arg) {
    (void)arg;
    while (watch_hits < 16) {
        if (!watch_armed) {
            MEMORY_BASIC_INFORMATION mbi;
            DWORD old;
            if (VirtualQuery((void *)(uintptr_t)watch_page, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
                VirtualProtect((void *)(uintptr_t)watch_page, 4096, PAGE_READONLY, &old))
                watch_armed = 1;
        }
        Sleep(watch_armed ? 1 : 50);
    }
    return 0;
}

static int watch_hit(EXCEPTION_POINTERS *ep) {
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    if (!watch_page || r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 ||
        r->ExceptionInformation[0] != 1 || (r->ExceptionInformation[1] & ~4095ull) != watch_page)
        return 0;
    DWORD old;
    VirtualProtect((void *)(uintptr_t)watch_page, 4096, PAGE_READWRITE, &old);
    char d[160];
    uint64_t f = lifted_function((uintptr_t)ep->ContextRecord->Rip);
    rt_logf("watch: write %#llx from host xcx.exe rip %p, lifted f_%llx = %s\n",
            (unsigned long long)r->ExceptionInformation[1], (void *)ep->ContextRecord->Rip, (unsigned long long)f,
            f ? ldr_describe(f, d, sizeof d) : "?");
    void *bt[24];
    USHORT nbt = RtlCaptureStackBackTrace(0, 24, bt, NULL);
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL);
    for (USHORT i = 0; i < nbt; i++)
        if ((uintptr_t)bt[i] - exe < (256u << 20)) rt_logf("  host xcx.exe+%#llx\n", (unsigned long long)((uintptr_t)bt[i] - exe));
    Cpu *c = kern_cpu();
    if (c) {
        rt_logf("  lr %s\n", ldr_describe(c->x[30], d, sizeof d));
        uint64_t fp = c->x[29];
        for (int n = 0; n < 12 && fp && !(fp & 7) && readable(fp, 16); n++) {
            uint64_t next = LD64(fp), lr = LD64(fp + 8);
            rt_logf("  guest frame %2d: %s\n", n, ldr_describe(lr, d, sizeof d));
            if (next <= fp || next - fp > (64u << 20)) break;
            fp = next;
        }
    }
    fflush(stderr);
    InterlockedIncrement(&watch_hits);
    Sleep(0);
    watch_armed = 0;
    return 1;
}

// Windows diagnostics: the XCX_WATCH watchpoint ahead of the crash hook (platform/win32.c), which would take its writes
// for crashes; behind it, a log line naming the module of a fault outside our code that nothing resolved, as an
// unhandled one ends the process with no other trace.
static LONG CALLBACK watch_handler(EXCEPTION_POINTERS *ep) {
    return watch_hit(ep) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK diag_handler(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_IN_PAGE_ERROR)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t rip = (uintptr_t)ep->ContextRecord->Rip;
    HMODULE self = GetModuleHandleW(NULL);
    MODULEINFO mi;
    if (!GetModuleInformation(GetCurrentProcess(), self, &mi, sizeof mi) || rip - (uintptr_t)mi.lpBaseOfDll >= mi.SizeOfImage) {
        // Not our code: leave it to the handler that expects it, but name the module, as an unhandled one ends the
        // process with no other trace. First-chance exceptions a DLL handles itself land here too, so only the first
        // few are logged.
        static volatile LONG foreign;
        if (InterlockedIncrement(&foreign) <= 20) {
            HMODULE m = NULL;
            wchar_t name[MAX_PATH] = L"?";
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCWSTR)rip, &m))
                GetModuleFileNameW(m, name, MAX_PATH);
            const wchar_t *base = wcsrchr(name, L'\\');
            Cpu *gc = kern_cpu();
            char d[160];
            rt_logf("exception %#lx in %ls+%#llx (thread %lu%s%s)%s%#llx\n", code, base ? base + 1 : name,
                    (unsigned long long)(rip - (uintptr_t)m), GetCurrentThreadId(), gc ? ", guest lr " : "",
                    gc ? ldr_describe(gc->x[30], d, sizeof d) : "",
                    code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2 ? ", address " : "",
                    code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2
                        ? (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1] : 0ull);
            fflush(stderr);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// The crash report: each line goes to the log (stderr and logs\xcx.log) and to crash.txt (rt_crash_report_path),
// which a player can attach on its own; the bug report zip of the menu carries it too.
static PlatAppend *g_crash_file;
static uint64_t g_crash_tail;   // the log tail's length when the crash began: the lines before the report

static void crash_out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void crash_out(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof buf) n = sizeof buf - 1;
    rt_log_raw(buf, (size_t)n);
    if (g_crash_file) plat_append_write(g_crash_file, buf, (size_t)n);
}

static void crash_file_only(const char *p, size_t n) {
    if (g_crash_file && n) plat_append_write(g_crash_file, p, n);
}

// Opens crash.txt and writes its head: what it is, the version, the time and the log it belongs to.
static void crash_begin(void) {
    g_crash_tail = __atomic_load_n(&g_log_tail_at, __ATOMIC_RELAXED);
    const char *path = rt_crash_report_path();
    if (!path) return;
    char dir[sizeof g_crash_path];
    snprintf(dir, sizeof dir, "%s", path);
    char *cut = plat_last_sep(dir);
    if (cut) *cut = 0, plat_file_mkdir_p(dir);
    char from[sizeof g_crash_path + 16], to[sizeof g_crash_path + 16];
    for (int i = RT_CRASH_KEEP - 1; i > 0; i--)   // crash.6.txt -> crash.7.txt, ..., crash.txt -> crash.1.txt
        if (rt_crash_report_nth(i - 1, from, sizeof from) && rt_crash_report_nth(i, to, sizeof to) &&
            plat_file_exists(from))
            plat_file_replace(from, to);
    g_crash_file = plat_append_open(path);
    if (!g_crash_file) return;
    char line[512];
    struct tm t;
    plat_local_time(&t);
    bug_report_version_line(line, sizeof line);
    int n = snprintf(line + strlen(line), sizeof line - strlen(line),
                     "\nXCX Recomp crash report, %04d-%02d-%02d %02d:%02d:%02d local time\nLog: %s\n\n",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                     rt_log_path() ? rt_log_path() : "stderr");
    (void)n;
    crash_file_only(line, strlen(line));
}

// The log's last lines before the report into crash.txt (the log has them already), then a line in the log that tells
// the player where the report is. Ends the process.
#define CRASH_TAIL_LINES 80
_Noreturn static void crash_end(int code) {
    uint64_t total = g_crash_tail, have = total < LOG_TAIL ? total : LOG_TAIL;
    static char tail[LOG_TAIL];
    for (uint64_t i = 0; i < have; i++) tail[i] = g_log_tail[(total - have + i) % LOG_TAIL];
    size_t start = 0;
    if (total > LOG_TAIL)   // the oldest line is cut: begin at the next one
        while (start < have && tail[start++] != '\n') {}
    size_t at = (size_t)have;
    for (int lines = 0; at > start && lines <= CRASH_TAIL_LINES; at--)
        if (tail[at - 1] == '\n' && ++lines > CRASH_TAIL_LINES) break;
    static const char head[] = "\nThe last lines of the log before the crash:\n";
    crash_file_only(head, sizeof head - 1);
    crash_file_only(tail + at, (size_t)have - at);
    if (g_crash_file)
        rt_logf("[host] crash: the report is in %s. Attach it to a bug report, or after a restart use F1 > Advanced > "
                "Make a bug report, which packs it with the logs.\n", rt_crash_report_path());
    fflush(stderr);
    plat_exit(code);
}

// The guest side of the crashed thread: the registers, the host function a guest call entered, and the guest call
// chain by the frame-pointer chain (x29).
static void crash_guest(void) {
    char d[160];
    Cpu *c = kern_cpu();
    if (!c) {
        crash_out("  not on a guest thread\n");
        return;
    }
    crash_out("guest registers:\n");
    for (int i = 0; i < 31; i++)
        crash_out("  x%-2d %016llx%s", i, (unsigned long long)c->x[i], i % 4 == 3 ? "\n" : "");
    const char *hf = hostfn_name(c->pc);   // a crash inside a host function: c->pc is its synthetic address
    crash_out("  sp  %016llx\n  pc  %016llx%s%s  lr %s\n", (unsigned long long)c->sp, (unsigned long long)c->pc,
              hf ? " host function " : "", hf ? hf : "", ldr_describe(c->x[30], d, sizeof d));
    crash_out("guest call chain:\n");
    uint64_t fp = c->x[29];
    for (int n = 0; n < 32 && fp && !(fp & 7) && readable(fp, 16); n++) {
        uint64_t next = LD64(fp), lr = LD64(fp + 8);
        crash_out("  guest frame %2d: %s\n", n, ldr_describe(lr, d, sizeof d));
        if (next <= fp || next - fp > (64u << 20)) break;
        fp = next;
    }
}

// One host return address: its module and offset, and for this executable the lifted function it is in.
static void crash_host_frame(int i, uintptr_t pc, const char *module, uintptr_t offset) {
    char d[160];
    if (module) {
        crash_out("  host frame %2d: %s+%#llx\n", i, *module ? module : "outside every module", (unsigned long long)offset);
        return;
    }
    uint64_t g = lifted_function(pc);
    if (g) crash_out("  host frame %2d: xcx+%#llx in lifted function f_%llx = %s\n", i, (unsigned long long)offset,
                     (unsigned long long)g, ldr_describe(g, d, sizeof d));
    else crash_out("  host frame %2d: xcx+%#llx\n", i, (unsigned long long)offset);
}

static volatile int32_t g_crash_busy;

static void crash_handler(const PlatCrash *x) {
    if (plat_xchg32(&g_crash_busy, 1)) plat_exit(71);
    char d[160];
    uintptr_t rip = x->rip;
    crash_begin();
    crash_out("FATAL: %s (%#x) at host %p (", x->what, x->code, (void *)rip);
    if (x->module && !*x->module) crash_out("outside every module)");
    else crash_out("%s+%#llx)", x->module ? x->module : "xcx", (unsigned long long)x->offset);
    if (x->access >= 0)
        crash_out(", %s address %#llx", x->access == 1 ? "writing" : x->access == 8 ? "executing" : "reading",
                  (unsigned long long)x->addr);
    crash_out("\n");
    if (x->cxx_type)
        crash_out("  C++ type %s%s%s\n", x->cxx_type, x->cxx_what ? ", what(): " : "", x->cxx_what ? x->cxx_what : "");
    uint64_t f = x->module ? 0 : lifted_function(rip);
    if (f) crash_out("  in lifted function f_%llx = %s\n", (unsigned long long)f, ldr_describe(f, d, sizeof d));
    crash_guest();
    if (x->nframes) crash_out("host stack:\n");
    for (int i = 0; i < x->nframes; i++) crash_host_frame(i, x->frames[i], x->frame_module[i], x->frame_offset[i]);
    if (x->frames_left_out) crash_out("  (%d frames in other modules left out)\n", x->frames_left_out);
    crash_end(71);
}

_Noreturn void rt_crash_fatal(const char *msg) {
    if (plat_xchg32(&g_crash_busy, 1)) plat_exit(70);
    crash_begin();
    crash_out("FATAL: %s\n", msg);
    crash_guest();
    uintptr_t frames[32];
    int n = plat_backtrace(frames, 32, 2);   // past rt_crash_fatal and its caller, rt_fatal
    uintptr_t exe = plat_exe_base();
    if (n) crash_out("host stack:\n");
    for (int i = 0; i < n; i++) {
        char name[128];
        uintptr_t base = 0;
        if (plat_module_of(frames[i], name, sizeof name, &base))
            crash_host_frame(i, frames[i], base == exe ? NULL : name, frames[i] - base);
        else
            crash_host_frame(i, frames[i], "", frames[i]);
    }
    crash_end(70);
}

// ---- self-test: layout, dispatch table, host-function registry, without game files
static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); putchar('\n'); } } while (0)

static double now_s(void) { return (double)plat_now_ns() / 1e9; }

static volatile int host_probe_hits;
static void host_probe(Cpu *c) { host_probe_hits++; c->x[0] = 42; }

static void check_cpu(void) {
    __builtin_cpu_init();
    int ok = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma") && __builtin_cpu_supports("bmi2");
    printf("cpu: x86-64-v3 (AVX2, FMA, BMI2) %s\n", ok ? "present" : "MISSING");
    CHECK(ok, "the lifted code is built for x86-64-v3");
}

// Layout: ascending, no overlap, below 2^39, and inside the window mem_init reserved.
static void check_layout(void) {
    const RtLayoutEntry *lay = rt_layout;
    int n = rt_layout_count;
    printf("layout: %d modules\n", n);
    for (int i = 0; i < n; i++) {
        printf("  %-8s base %#llx  size %#10llx  end %#llx\n", lay[i].name, (unsigned long long)lay[i].base,
               (unsigned long long)lay[i].size, (unsigned long long)(lay[i].base + lay[i].size));
        CHECK(lay[i].size > 0, "%s has size 0", lay[i].name);
        CHECK(lay[i].base % 0x10000 == 0, "%s base is not 64 KiB aligned", lay[i].name);
        CHECK(lay[i].base + lay[i].size <= RT_KERNEL_BASE, "%s reaches into the kernel region", lay[i].name);
        if (i) {
            CHECK(lay[i].base > lay[i - 1].base, "%s base is not above %s", lay[i].name, lay[i - 1].name);
            CHECK(lay[i - 1].base + lay[i - 1].size <= lay[i].base, "%s overlaps %s", lay[i - 1].name, lay[i].name);
        }
        uint64_t end;
        int ours = plat_mem_state(lay[i].base, lay[i].base + 1, &end) == PLAT_MEM_RESERVED;
        CHECK(ours, "%s is not inside the reserved module window", lay[i].name);
    }
    const RtGuestLayout *g = mem_layout();
    printf("guest memory: aslr %#llx+%#llx stack %#llx heap %#llx alias %#llx+%#llx kernel %#llx host functions %#llx\n",
           (unsigned long long)g->aslr.base, (unsigned long long)g->aslr.size, (unsigned long long)g->stack.base,
           (unsigned long long)g->heap.base, (unsigned long long)g->alias.base, (unsigned long long)g->alias.size,
           (unsigned long long)g->kernel.base, (unsigned long long)g->hostfn.base);
    CHECK(g->alias.base + g->alias.size <= lay[0].base && g->heap.base + g->heap.size <= g->alias.base,
          "guest regions overlap");
}

// Every function start dispatches to its own function. The word after a start (unless that is a start too), a
// misaligned address and the word below the module dispatch to nothing.
static void check_modules(const RtModule *mods, int n) {
    uint64_t total = 0;
    for (int i = 0; i < n; i++) {
        const RtModule *m = &mods[i];
        const RtLayoutEntry *l = NULL;
        for (int k = 0; k < rt_layout_count; k++)
            if (!strcmp(rt_layout[k].name, m->name)) l = &rt_layout[k];
        CHECK(l && l->base == m->base && l->size == m->size, "%s: linked base/size differ from the layout", m->name);

        uint64_t bad = 0, bad_neg = 0;
        for (uint64_t k = 0; k < m->count; k++) {
            uint64_t a = m->addrs[k];
            if (rt_dispatch_lookup(a) != m->fns[k] && bad++ < 5)
                printf("  FAIL: %s_fn_addrs[%llu] %#llx does not dispatch to its function\n", m->name,
                       (unsigned long long)k, (unsigned long long)a);
            int next_is_start = k + 1 < m->count && m->addrs[k + 1] == a + 4;
            if ((!next_is_start && rt_dispatch_lookup(a + 4)) || rt_dispatch_lookup(a + 2)) bad_neg++;
        }
        if (rt_dispatch_lookup(m->base - 4)) bad_neg++;
        failures += bad > 0;
        CHECK(!bad_neg, "%s: %llu addresses that are not function starts dispatch to a function", m->name,
              (unsigned long long)bad_neg);
        uint64_t lo = m->count ? m->addrs[0] : 0, hi = m->count ? m->addrs[m->count - 1] : 0;
        printf("  %-8s %8llu functions, first %#llx, last %#llx: %s\n", m->name, (unsigned long long)m->count,
               (unsigned long long)lo, (unsigned long long)hi, bad || bad_neg ? "FAIL" : "ok");
        total += m->count;
    }
    printf("  total    %8llu functions\n", (unsigned long long)total);
}

static void bench(const RtModule *mods, int n) {
    uint64_t total = 0, sink = 0;
    double t0 = now_s();
    for (int i = 0; i < n; i++) {
        for (uint64_t k = 0; k < mods[i].count; k++) sink += (uintptr_t)rt_dispatch_lookup(mods[i].addrs[k]);
        total += mods[i].count;
    }
    double dt = now_s() - t0;
    __asm__ volatile("" ::"r"(sink));
    if (total) printf("lookup: %.1f ns per function start (address order)\n", dt * 1e9 / (double)total);
}

// Memory SVCs without modules: heap, physical memory in the alias region, a stack move, QueryMemory, GetInfo.
static void check_memory(void) {
    const RtGuestLayout *g = mem_layout();
    RtMemoryInfo q;
    uint64_t heap = 0, v = 0;
    int before = failures;
    CHECK(mem_set_heap_size(0x400000, &heap) == 0 && heap == g->heap.base, "SetHeapSize 4 MiB");
    memset((void *)(uintptr_t)heap, 0xAB, 0x400000);
    mem_query(heap + 0x1000, &q);
    CHECK(q.base == heap && q.size == 0x400000 && q.state == 5 && q.perm == 3, "QueryMemory on the heap");
    CHECK(mem_set_heap_size(0x300000, &heap) != 0, "SetHeapSize accepts a size that is not a 2 MiB multiple");

    uint64_t a = g->alias.base + 0x100000;
    CHECK(mem_map_physical(a, 0x10000) == 0, "MapPhysicalMemory");
    ST64(a + 0x8000, 0x1234);
    CHECK(mem_map_physical(a - 0x4000, 0x20000) == 0 && LD64(a + 0x8000) == 0x1234,
          "MapPhysicalMemory over a mapped range lost its contents");
    mem_query(a, &q);
    CHECK(q.base == a - 0x4000 && q.size == 0x20000 && q.state == 5, "QueryMemory on mapped alias pages");
    mem_info(7, &v);
    CHECK(v == 0x400000 + 0x20000, "GetInfo 7 (used memory) %#llx", (unsigned long long)v);

    uint64_t stack = g->stack.base + 0x100000;
    ST64(a + 8, 0x99);
    CHECK(mem_map(stack, a, 0x4000) == 0 && LD64(stack + 8) == 0x99, "MapMemory alias -> stack did not copy");
    ST64(stack + 0x10, 0x5678);
    mem_query(stack, &q);
    CHECK(q.base == stack && q.size == 0x4000 && q.state == 0xB, "QueryMemory on a moved stack");
    mem_query(a, &q);
    CHECK(q.base == a && q.state == 5 && q.perm == 0, "QueryMemory on a moved stack's source");
    CHECK(mem_unmap_physical(a, 0x4000) != 0, "UnmapPhysicalMemory under a moved stack");
    CHECK(mem_unmap(stack, a, 0x4000) == 0 && LD64(a + 0x10) == 0x5678, "UnmapMemory did not copy the stack back");

    CHECK(mem_unmap_physical(a - 0x4000, 0x20000) == 0, "UnmapPhysicalMemory");
    mem_query(a, &q);
    CHECK(q.state == 0 && q.base == g->alias.base && q.size == g->alias.size, "QueryMemory on the free alias region");
    CHECK(mem_map_physical(a, 0x1000) == 0 && LD64(a + 0x10) == 0 && mem_unmap_physical(a, 0x1000) == 0,
          "remapped physical memory is not zeroed");
    CHECK(mem_map_physical(a, 0x100000000ull) == 0x10801, "MapPhysicalMemory beyond the pool is not LimitReached");
    mem_query(RT_HOSTFN_BASE + 0x40, &q);
    CHECK(q.state == 0x10 && q.base == RT_HOSTFN_BASE, "QueryMemory on the host-function window");
    mem_query(g->alias.base + g->alias.size + 0x1000, &q);
    CHECK(q.state == 3 && q.base == g->alias.base + g->alias.size, "QueryMemory on host memory");
    CHECK(mem_set_heap_size(0, &heap) == 0, "SetHeapSize 0");
    printf("memory: heap, physical memory, stack move, QueryMemory, GetInfo: %s\n", failures > before ? "FAIL" : "ok");
}

static int selftest(void) {
    static RtModule mods[MAX_MODULES];
    int n = rt_linked_modules(mods, MAX_MODULES);

    check_cpu();
    check_layout();
    uint64_t probe = hostfn_register("<selftest probe>", host_probe);

    double t0 = now_s();
    hostfn_seal();
    double dt = now_s() - t0;
    RtDispatchStats s = rt_dispatch_stats();
    printf("dispatch: %d modules linked, table covers %#llx-%#llx: %llu slots, %.1f MiB reserved, built in %.0f ms\n",
           n, (unsigned long long)s.lo, (unsigned long long)s.hi, (unsigned long long)s.slots,
           (double)s.table_bytes / (1 << 20), dt * 1e3);
    check_modules(mods, n);
    CHECK(s.functions > 0, "no functions linked");
    Cpu *c = plat_aligned_alloc(sizeof(Cpu), 64);
    memset(c, 0, sizeof *c);
    uint64_t r = rt_call(c, probe, 1, (uint64_t)7);
    CHECK(rt_dispatch_lookup(probe) && r == 42 && host_probe_hits == 1 && c->x[30] == 0,
          "a registered host function does not dispatch through rt_call");
    CHECK(!rt_dispatch_lookup(probe + 16), "an unregistered host-function address dispatches");
    CHECK(rt_dispatch_lookup(RT_RET_HOST), "the rt_call return sentinel is not registered");
    plat_aligned_free(c);
    bench(mods, n);

    void *k1 = mem_kernel_alloc(0x200, 16), *k2 = mem_kernel_alloc(0x2000, 0x1000);
    CHECK((uintptr_t)k1 - RT_KERNEL_BASE < RT_KERNEL_SIZE && !((uintptr_t)k2 & 0xFFF) && k1 != k2,
          "kernel-region allocation");
    mem_kernel_free(k1, 0x200);
    CHECK(mem_kernel_alloc(0x200, 16) == k1, "a freed kernel-region block is not reused");
    check_memory();

    float used, total, proc;
    plat_mem_info(&used, &total, &proc);
    if (proc >= 0) printf("process: working set %.0f MiB\n", proc);
    printf("selftest: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}

static int probe_mask(void) {
    char in[PLAT_PATH_MAX], names[1024];
    if (!plat_getenv("XCX_PROBE_MASK_IN", in, sizeof in)) {
        fprintf(stderr, "probe-mask: set XCX_PROBE_MASK_IN to the file to mask\n");
        return 2;
    }
    uint64_t n = 0;
    uint8_t *text = plat_file_read(in, &n);
    PiiMask *m = bug_report_masker();
    if (!text || !m) {
        fprintf(stderr, "probe-mask: cannot read %s\n", in);
        return 2;
    }
    if (plat_getenv("XCX_PROBE_MASK_NAMES", names, sizeof names))
        for (char *p = strtok(names, ","); p; p = strtok(NULL, ",")) pii_mask_name(m, p);
    size_t k = 0;
    uint8_t *out = pii_mask_text(m, text, (size_t)n, &k);
    if (out) fwrite(out, 1, k, stdout);
    free(out);
    free(text);
    pii_mask_free(m);
    return out ? 0 : 1;
}

static int usage(const char *argv0) {
    fprintf(stderr, "usage: %s [--exefs DIR] [--romfs DIR] [--profile DIR] [--selftest | --probe-loader | "
                    "--probe-cfg | --probe-kernel | --probe-ipc | --probe-services | --probe-fs | --probe-gfx-abi | "
                    "--probe-nvn | --probe-nvn-tex | --probe-window | --probe-shader | --probe-vkmem | "
                    "--probe-vkexec | --probe-astc | --probe-movie | --probe-mask | --prepare-shaders | --warm-shaders | --boot |--seconds S [--shot PNG]]\n", argv0);
    return 2;
}

// Paths for a copied folder (tools/package_build.py). Per path: XCX_EXEFS, XCX_ROMFS, XCX_PROFILE_DIR, else the
// folder next to xcx.exe (exefs, romfs, profile) if it exists, else the path baked in at build time. A relative
// value is relative to xcx.exe's folder. A build copied to another machine can name a drive that machine lacks: a
// profile folder that cannot be made at the baked path goes next to xcx.exe instead.
static char pp_exefs[PLAT_PATH_MAX * 2], pp_romfs[PLAT_PATH_MAX * 2], pp_profile[PLAT_PATH_MAX * 2];

static const char *portable_path(const char *dir, const char *var, const char *sub, const char *must, char *out, size_t cap,
                                 const char *fallback) {
    const char *e = cfg_get(var);
    if (e && *e) {
        int abs_path = e[0] == '\\' || e[0] == '/' || e[1] == ':';
        if (abs_path) snprintf(out, cap, "%s", e);
        else snprintf(out, cap, "%s/%s", dir, e);
        return out;
    }
    char probe[PLAT_PATH_MAX * 3];
    snprintf(out, cap, "%s/%s", dir, sub);
    snprintf(probe, sizeof probe, "%s/%s", out, must);
    return plat_file_exists(probe) ? out : fallback;
}

static void portable_paths(RtConfig *c) {
    char exe[PLAT_PATH_MAX * 2];
    int n = plat_exe_path(exe, sizeof exe);
    char *cut = n && n < sizeof exe ? plat_last_sep(exe) : NULL;
    if (!cut) return;
    *cut = 0;
    c->exefs = portable_path(exe, "XCX_EXEFS", "exefs", "main.npdm", pp_exefs, sizeof pp_exefs, c->exefs);
    c->romfs = portable_path(exe, "XCX_ROMFS", "romfs", "romfs.bin", pp_romfs, sizeof pp_romfs, c->romfs);
    c->profile = portable_path(exe, "XCX_PROFILE_DIR", "profile", ".", pp_profile, sizeof pp_profile, c->profile);
}

// After the command line: a path given there or in XCX_PROFILE_DIR is the player's own and stays as it is.
static void profile_fallback(RtConfig *c) {
    if (c->profile != rt_default_profile || !*pp_profile) return;
    plat_file_mkdir_p(c->profile);
    if (plat_dir_exists(c->profile)) return;
    rt_log("cfg: cannot use the profile folder %s: using %s", c->profile, pp_profile);
    c->profile = pp_profile;
}

int main(int argc, char **argv) {
    mem_init();   // first: claim the guest address space before anything else can land there; reads xcx.ini
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#ifdef _WIN32
    const char *watch = cfg_get("XCX_WATCH");
    if (watch && *watch) {
        watch_page = strtoull(watch, NULL, 16) & ~4095ull;
        AddVectoredExceptionHandler(1, watch_handler);
        CloseHandle(CreateThread(NULL, 0, watch_thread, NULL, 0, NULL));
    }
#endif
    plat_crash_hook(crash_handler);
#ifdef _WIN32
    AddVectoredExceptionHandler(0, diag_handler);
#endif
    parse_trace();
    prof_start();
    // Timed guest waits (kernel.c block_inner) sleep in whole milliseconds; at the default 15.6 ms tick a 1 ms wait
    // costs a full tick. XCX_TIMER_1MS=0 keeps the default for comparison.
    if (cfg_bool("XCX_TIMER_1MS")) plat_timer_1ms();
    plat_power_full_speed();   // before the window exists: Windows would slow a hidden or background game
    cfg = (RtConfig){.exefs = rt_default_exefs, .romfs = rt_default_romfs, .profile = rt_default_profile,
                     .gpu = cfg_get("XCX_GPU")};
    portable_paths(&cfg);   // command-line paths below still win
    if (cfg.gpu && strcmp(cfg.gpu, "null") && strcmp(cfg.gpu, "vulkan")) {
        rt_logf("XCX_GPU must be null or vulkan, not '%s'\n", cfg.gpu);
        return 2;
    }
    const char *mode = NULL;
    for (int i = 1; i < argc; i++) {
        const char **path = !strcmp(argv[i], "--exefs") ? &cfg.exefs : !strcmp(argv[i], "--romfs") ? &cfg.romfs
                          : !strcmp(argv[i], "--profile") ? &cfg.profile : NULL;
        if (path) {
            if (++i == argc) return usage(argv[0]);
            *path = argv[i];
        } else if (!strcmp(argv[i], "--seconds")) {
            char *end;
            if (++i == argc) return usage(argv[0]);
            double s = strtod(argv[i], &end);
            if (*end || !(s > 0) || s > 1e6) return usage(argv[0]);
            cfg.seconds = s;
        } else if (!strcmp(argv[i], "--shot")) {
            if (++i == argc) return usage(argv[0]);
            cfg.shot = argv[i];
        } else if (!mode && !strncmp(argv[i], "--", 2)) {
            mode = argv[i];
        } else {
            return usage(argv[0]);
        }
    }
    if (cfg.shot && !cfg.seconds) return usage(argv[0]);
    if (cfg.seconds && mode) return usage(argv[0]);
    log_open(!mode);
    if (!mode) {
        profile_fallback(&cfg);
        cfg.boot = cfg.seconds > 0;   // --seconds starts the game like --boot, with its checkpoints
        if (!cfg.shot && !cfg.seconds) rt_mods_start();
        rt_boot();
        rt_mods_shutdown();
        return 0;
    }
    if (!strcmp(mode, "--selftest")) return selftest();
    if (!strcmp(mode, "--probe-loader")) return probe_loader();
    if (!strcmp(mode, "--probe-cfg")) return probe_cfg();
    if (!strcmp(mode, "--probe-kernel")) return probe_kernel();
    if (!strcmp(mode, "--probe-ipc")) return probe_ipc();
    if (!strcmp(mode, "--probe-services")) return probe_services();
    if (!strcmp(mode, "--probe-fs")) return probe_fs();
    if (!strcmp(mode, "--probe-gfx-abi")) return probe_gfx_abi();
    if (!strcmp(mode, "--probe-nvn")) return probe_nvn();
    if (!strcmp(mode, "--probe-nvn-tex")) return probe_nvn_tex();
    if (!strcmp(mode, "--probe-window")) return probe_window();
    if (!strcmp(mode, "--probe-shader")) return probe_shader();
    if (!strcmp(mode, "--probe-vkmem")) return probe_vkmem();
    if (!strcmp(mode, "--probe-vkexec")) return probe_vkexec();
    if (!strcmp(mode, "--probe-astc")) return probe_astc();
    if (!strcmp(mode, "--probe-vkdevice")) return probe_vkdevice();
    if (!strcmp(mode, "--probe-movie")) return probe_movie();
    if (!strcmp(mode, "--probe-mask")) return probe_mask();
    if (!strcmp(mode, "--prepare-shaders")) return vk_pipeline_prepare_shaders();
    if (!strcmp(mode, "--warm-shaders")) return vk_pipeline_warm_shaders();
    if (!strcmp(mode, "--boot")) { cfg.boot = 1; rt_boot(); return 0; }
    return usage(argv[0]);
}
