// Masking of personal information in text (pii_mask.h). Three passes over the text, each into a new buffer: paths
// first (so a path keeps its root's tag or its file name only), then addresses and ids by their shape, then the
// player's names wherever they still appear. The text is UTF-8; the structure of paths and addresses is ASCII, so the
// passes step byte by byte there and code point by code point where names are compared.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pii_mask.h"
#include "platform/platform.h"

enum { MAX_NAMES = 48, MAX_ROOTS = 8, NAME_CP = 512 };
typedef struct { uint32_t cp[NAME_CP]; int n; const char *with; } Name;      // lowercase, '\' as '/'
typedef struct { uint32_t cp[PLAT_PATH_MAX]; int n; const char *tag; } Root; // lowercase, '\' as '/', no end separator
struct PiiMask { Name names[MAX_NAMES]; int nn; Root roots[MAX_ROOTS]; int nr; };

// ---- text helpers

typedef struct { uint8_t *p; size_t n, cap; int fail; } Buf;

static void put(Buf *b, const void *s, size_t k) {
    if (b->fail || !k) return;
    if (b->n + k > b->cap) {
        size_t c = b->cap ? b->cap : 4096;
        while (c < b->n + k) c *= 2;
        uint8_t *q = realloc(b->p, c);
        if (!q) {
            b->fail = 1;
            return;
        }
        b->p = q, b->cap = c;
    }
    memcpy(b->p + b->n, s, k);
    b->n += k;
}

static void puts_(Buf *b, const char *s) { put(b, s, strlen(s)); }

// The code point at s[i] and its length in bytes; a byte that starts no valid sequence is U+FFFD, one byte long.
static uint32_t dec(const uint8_t *s, size_t n, size_t i, size_t *len) {
    uint8_t c = s[i];
    *len = 1;
    if (c < 0x80) return c;
    int k = c >= 0xf0 && c < 0xf8 ? 3 : c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xc2 && c < 0xe0 ? 1 : -1;
    if (k < 0 || i + (size_t)k >= n) return 0xfffd;
    uint32_t cp = c & (0x3f >> k);
    for (int j = 1; j <= k; j++) {
        if ((s[i + j] & 0xc0) != 0x80) return 0xfffd;
        cp = cp << 6 | (s[i + j] & 0x3f);
    }
    *len = (size_t)k + 1;
    return cp;
}

static uint32_t fold(uint32_t c) { return c == '\\' ? '/' : plat_char_lower(c); }

static int word_cp(uint32_t c) { return c >= 0x80 ? c != 0xfffd : isalnum((int)c) || c == '_'; }

static int is_sep(uint8_t c) { return c == '\\' || c == '/'; }

// The code points of s, folded, into out; their count (0 when s does not fit).
static int to_cps(const char *s, uint32_t *out, int max) {
    size_t n = strlen(s), i = 0, len;
    int k = 0;
    while (i < n) {
        if (k == max) return 0;
        out[k++] = fold(dec((const uint8_t *)s, n, i, &len));
        i += len;
    }
    return k;
}

// ---- setup

static void add_one(PiiMask *m, const char *s, const char *with) {
    if (m->nn == MAX_NAMES) return;
    Name *nm = &m->names[m->nn];
    nm->n = to_cps(s, nm->cp, NAME_CP);
    while (nm->n && nm->cp[nm->n - 1] == '/') nm->n--;
    if (nm->n < 2) return;
    for (int i = 0; i < m->nn; i++)
        if (m->names[i].n == nm->n && !memcmp(m->names[i].cp, nm->cp, sizeof *nm->cp * (size_t)nm->n)) return;
    nm->with = with;
    m->nn++;
}

// s, and its URL-encoded form when that differs (with / for \ too, as a file URL writes a Windows path).
static void add_name(PiiMask *m, const char *s, const char *with) {
    if (!s || !*s) return;
    add_one(m, s, with);
    char enc[NAME_CP * 3], slash[NAME_CP];
    for (int pass = 0; pass < 2; pass++) {
        const char *src = s;
        if (pass) {
            snprintf(slash, sizeof slash, "%s", s);
            for (char *c = slash; *c; c++)
                if (*c == '\\') *c = '/';
            src = slash;
        }
        size_t k = 0;
        for (const uint8_t *c = (const uint8_t *)src; *c && k + 4 < sizeof enc; c++)
            k += (size_t)(isalnum(*c) || strchr("-._~", *c) ? snprintf(enc + k, sizeof enc - k, "%c", *c)
                                                              : snprintf(enc + k, sizeof enc - k, "%%%02X", *c));
        enc[k] = 0;
        if (strcmp(enc, s)) add_one(m, enc, with);
    }
}

// A login name of more than eight letters has an 8.3 form too: its first six and ~1 .. ~4 (JONATH~1).
static void add_short_forms(PiiMask *m, const char *name) {
    size_t n = strlen(name), k = 0;
    char six[8], f[16];
    for (size_t i = 0; i < n && k < 6; i++)
        if ((unsigned char)name[i] >= 0x80) return;   // no 8.3 form to guess for a non-ASCII name
        else if (name[i] != ' ' && name[i] != '.') six[k++] = (char)toupper((unsigned char)name[i]);
    if (n <= 8 || k < 6) return;
    six[k] = 0;
    for (int d = 1; d <= 4; d++) {
        snprintf(f, sizeof f, "%s~%d", six, d);
        add_one(m, f, "<user>");
    }
}

// Longest first: the home folder before the user name inside it, JONAT~1 before JONAT.
static int by_length(const void *a, const void *b) {
    int x = ((const Name *)a)->n, y = ((const Name *)b)->n;
    return y - x;
}

static void sort_names(PiiMask *m) { qsort(m->names, (size_t)m->nn, sizeof *m->names, by_length); }

PiiMask *pii_mask_new(void) {
    PiiMask *m = calloc(1, sizeof *m);
    PlatIdentity *id = malloc(sizeof *id);
    if (!m || !id) {
        free(m);
        free(id);
        return NULL;
    }
    plat_identity(id);
    add_name(m, id->home, "<home>");
    add_name(m, id->home_short, "<home>");
    const char *s = plat_last_sep(id->home_short);   // JONAT~1, the short form of the home folder's name
    if (s && s[1]) add_name(m, s + 1, "<user>");
    add_name(m, id->user, "<user>");
    add_short_forms(m, id->user);
    add_name(m, id->full_name, "<user>");
    char word[256];
    for (const char *p = id->full_name; *p;) {   // each word of the display name, of three letters or more
        size_t k = strcspn(p, " .,");
        if (k >= 3 && k < sizeof word) {
            memcpy(word, p, k);
            word[k] = 0;
            add_name(m, word, "<user>");
        }
        p += k + (p[k] != 0);
    }
    add_name(m, id->computer, "<computer>");
    add_name(m, id->host, "<computer>");
    free(id);
    sort_names(m);
    return m;
}

void pii_mask_free(PiiMask *m) { free(m); }

void pii_mask_name(PiiMask *m, const char *name) {
    add_name(m, name, "<user>");
    sort_names(m);
}

void pii_mask_root(PiiMask *m, const char *path, const char *tag) {
    if (!path || !*path || m->nr == MAX_ROOTS) return;
    Root *r = &m->roots[m->nr];
    r->n = to_cps(path, r->cp, PLAT_PATH_MAX);
    while (r->n && r->cp[r->n - 1] == '/') r->n--;
    if (r->n < 3) return;
    for (int i = 0; i < m->nr; i++)   // the same folder twice: the first tag stays
        if (m->roots[i].n == r->n && !memcmp(m->roots[i].cp, r->cp, sizeof *r->cp * (size_t)r->n)) return;
    r->tag = tag;
    m->nr++;
    for (int i = m->nr - 1; i > 0 && m->roots[i].n > m->roots[i - 1].n; i--) {   // longest first
        Root t = m->roots[i];
        m->roots[i] = m->roots[i - 1];
        m->roots[i - 1] = t;
    }
}

// ---- pass 1: paths

// The byte before i ends no word, number or path: a path may start at i.
static int path_bound(const uint8_t *s, size_t i) {
    if (!i) return 1;
    uint8_t p = s[i - 1];
    return !(isalnum(p) || p >= 0x80 || strchr("_\\/.%$~-:", p));
}

static int name_char(uint8_t c) { return isalnum(c) || c >= 0x80 || strchr("_.-~$", c); }

// Where a path that starts at i begins its content after any \\?\ or file:// prefix (*body), and whether it is a
// rooted Unix path (*unix: needs two parts or more). 0 when no path starts at i.
static int path_start(const uint8_t *s, size_t n, size_t i, size_t *body, int *unix_) {
    *unix_ = 0;
    if (!path_bound(s, i)) return 0;
    if (i + 7 <= n && !strncasecmp((const char *)s + i, "file://", 7)) {
        *body = i + 7;
        if (*body + 2 < n && s[*body] == '/' && isalpha(s[*body + 1]) && s[*body + 2] == ':') ++*body;
        return 1;
    }
    if (i + 2 < n && s[i] == '\\' && s[i + 1] == '\\') {
        if (i + 3 < n && (s[i + 2] == '?' || s[i + 2] == '.') && s[i + 3] == '\\') {
            *body = i + 4;
            return 1;
        }
        *body = i;
        return name_char(s[i + 2]);
    }
    *body = i;
    if (i + 2 < n && isalpha(s[i]) && s[i + 1] == ':' && is_sep(s[i + 2])) return 1;
    if (s[i] == '%') {   // %USERPROFILE%\...
        size_t k = i + 1;
        while (k < n && (isalnum(s[k]) || strchr("_()", s[k]))) k++;
        return k > i + 1 && k + 1 < n && s[k] == '%' && is_sep(s[k + 1]);
    }
    if (s[i] == '~') return i + 1 < n && is_sep(s[i + 1]);
    if (s[i] == '$') {   // $HOME/..., ${HOME}/...
        size_t k = i + 1;
        if (k < n && s[k] == '{') k++;
        while (k < n && (isalnum(s[k]) || s[k] == '_')) k++;
        if (k < n && s[k] == '}') k++;
        return k > i + 1 && k < n && is_sep(s[k]);
    }
    if (s[i] == '/' && i + 1 < n && name_char(s[i + 1])) {
        *unix_ = 1;
        return 1;
    }
    return 0;
}

// The end of a path from j. A space belongs to it when the word after the spaces holds a separator ("E:\Game
// Research\x"); a final part with a space in it ends at the space. Trailing . , : ; and an unmatched ) are not part.
static size_t path_end(const uint8_t *s, size_t n, size_t j) {
    size_t e = j;
    while (e < n) {
        uint8_t c = s[e];
        if (c < 0x20 || strchr("\"<>|*?'`", c)) break;
        if ((c == ',' || c == ';') && (e + 1 >= n || s[e + 1] <= ' ')) break;
        if (c == ' ') {   // the next words, up to three: the first that holds a separator ends the look-ahead
            size_t k = e, until = 0;
            for (int words = 0; words < 3 && !until; words++) {
                while (k < n && s[k] == ' ') k++;
                size_t b, w = k;
                int unix_, sep = 0;
                if (k >= n || s[k] < 0x20 || path_start(s, n, k, &b, &unix_)) break;   // another path: not this one
                while (w < n && s[w] > ' ' && !strchr("\"<>|*?'`", s[w])) sep |= is_sep(s[w++]);
                if (w < n && s[w] > ' ') break;   // a stop character inside the word
                if (sep) until = w;
                k = w;
            }
            if (!until) break;
            e = until;
            continue;
        }
        e++;
    }
    for (;;) {
        if (e > j && strchr(".,:;", s[e - 1])) {
            e--;
            continue;
        }
        if (e > j && s[e - 1] == ')') {
            int open = 0, close = 0;
            for (size_t k = j; k < e; k++) open += s[k] == '(', close += s[k] == ')';
            if (close > open) {
                e--;
                continue;
            }
        }
        break;
    }
    return e;
}

// The length of the root r at s[b..e) when the path continues past it with a separator or ends there; 0 otherwise.
static size_t root_match(const Root *r, const uint8_t *s, size_t b, size_t e) {
    size_t j = b, len;
    for (int k = 0; k < r->n; k++) {
        if (j >= e || fold(dec(s, e, j, &len)) != r->cp[k]) return 0;
        j += len;
    }
    return j == e || is_sep(s[j]) ? j - b : 0;
}

static void mask_path(const PiiMask *m, Buf *o, const uint8_t *s, size_t i, size_t b, size_t e) {
    for (int k = 0; k < m->nr; k++) {
        size_t r = root_match(&m->roots[k], s, b, e);
        if (r) {
            puts_(o, m->roots[k].tag);
            put(o, s + b + r, e - b - r);
            return;
        }
    }
    size_t end = e;
    while (end > i && is_sep(s[end - 1])) end--;
    size_t last = end;
    while (last > i && !is_sep(s[last - 1])) last--;
    puts_(o, "<path>");
    if (last > i && last < end) {   // the file name, behind the path's own separator
        put(o, s + last - 1, 1);
        put(o, s + last, end - last);
    }
}

static void pass_paths(const PiiMask *m, Buf *o, const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n;) {
        size_t b;
        int unix_;
        if (path_start(s, n, i, &b, &unix_)) {
            size_t e = path_end(s, n, b);
            int seps = 0;
            for (size_t k = b; k < e; k++) seps += is_sep(s[k]);
            if (e > b && (!unix_ || seps >= 2)) {
                mask_path(m, o, s, i, b, e);
                i = e;
                continue;
            }
        }
        put(o, s + i, 1);
        i++;
    }
}

// ---- pass 2: names

static size_t name_match(const Name *nm, const uint8_t *s, size_t n, size_t i) {
    size_t j = i, len;
    for (int k = 0; k < nm->n; k++) {
        if (j >= n || fold(dec(s, n, j, &len)) != nm->cp[k]) return 0;
        j += len;
    }
    if (word_cp(nm->cp[nm->n - 1]) && j < n && word_cp(dec(s, n, j, &len))) return 0;   // a whole word
    return j - i;
}

static void pass_names(const PiiMask *m, Buf *o, const uint8_t *s, size_t n) {
    int prev_word = 0;
    for (size_t i = 0; i < n;) {
        size_t len;
        uint32_t c = dec(s, n, i, &len), f = fold(c);
        const Name *hit = NULL;
        size_t k = 0;
        for (int x = 0; x < m->nn && !hit; x++) {
            const Name *nm = &m->names[x];
            if (nm->cp[0] != f || (prev_word && word_cp(nm->cp[0]))) continue;
            if ((k = name_match(nm, s, n, i))) hit = nm;
        }
        if (hit) {
            puts_(o, hit->with);
            i += k;
            if (i + 1 < n && s[i] == '~' && isdigit(s[i + 1]))   // the 8.3 tail of a short name: JONAT~1
                for (i++; i < n && isdigit(s[i]); i++) {}
            prev_word = 0;
        } else {
            put(o, s + i, len);
            i += len;
            prev_word = word_cp(c);
        }
    }
}

// ---- pass 3: addresses and ids

static int hexc(uint8_t c) { return isxdigit(c); }

// A Windows SID: S-1-<authority>-<sub>..., three dashes or more.
static size_t sid_at(const uint8_t *s, size_t n, size_t i) {
    if (i + 4 >= n || (s[i] != 'S' && s[i] != 's') || s[i + 1] != '-' || s[i + 2] != '1' || s[i + 3] != '-') return 0;
    size_t e = i + 2;
    int dashes = 1;
    while (e < n && (isdigit(s[e]) || (s[e] == '-' && e + 1 < n && isdigit(s[e + 1])))) dashes += s[e++] == '-';
    return dashes >= 3 && (e >= n || !isalnum(s[e])) ? e - i : 0;
}

static int email_local(uint8_t c) { return isalnum(c) || strchr("._%+-", c); }

static size_t email_at(const uint8_t *s, size_t n, size_t i) {
    if (i && email_local(s[i - 1])) return 0;
    size_t e = i;
    while (e < n && email_local(s[e])) e++;
    if (e == i || e >= n || s[e] != '@') return 0;
    size_t d = ++e, dots = 0, label = 0, last = 0;
    while (e < n && (isalnum(s[e]) || s[e] == '-' || (s[e] == '.' && label))) {
        if (s[e] == '.') dots++, label = 0, last = e + 1;
        else label++;
        e++;
    }
    while (e > d && s[e - 1] == '.') e--, dots--;
    if (!dots || e - last < 2) return 0;
    for (size_t k = last; k < e; k++)
        if (!isalpha(s[k])) return 0;
    return e - i;
}

static size_t mac_at(const uint8_t *s, size_t n, size_t i) {
    if (i && (isalnum(s[i - 1]) || s[i - 1] == ':' || s[i - 1] == '-')) return 0;
    if (i + 17 > n) return 0;
    uint8_t sep = s[i + 2];
    if (sep != ':' && sep != '-') return 0;
    for (int g = 0; g < 6; g++) {
        size_t p = i + (size_t)g * 3;
        if (!hexc(s[p]) || !hexc(s[p + 1]) || (g < 5 && s[p + 2] != sep)) return 0;
    }
    if (i + 17 < n && (isalnum(s[i + 17]) || s[i + 17] == sep)) return 0;
    return 17;
}

// An IPv4 address; *keep: loopback (127.x) or 0.0.0.0, left as it is.
static size_t ipv4_at(const uint8_t *s, size_t n, size_t i, int *keep) {
    if (i && (isalnum(s[i - 1]) || s[i - 1] == '.')) return 0;
    size_t e = i;
    unsigned oct[4];
    for (int g = 0; g < 4; g++) {
        if (g) {
            if (e >= n || s[e] != '.') return 0;
            e++;
        }
        size_t d = e;
        unsigned v = 0;
        while (e < n && isdigit(s[e]) && e - d < 3) v = v * 10 + (s[e++] - '0');
        if (e == d || v > 255) return 0;
        oct[g] = v;
    }
    if (e < n && (isalnum(s[e]) || (s[e] == '.' && e + 1 < n && isdigit(s[e + 1])))) return 0;
    *keep = oct[0] == 127 || (oct[0] | oct[1] | oct[2] | oct[3]) == 0;
    return e - i;
}

// An IPv6 address: hex groups of at most four digits, with "::" or seven colons; ::1 and :: kept (*keep).
static size_t ipv6_at(const uint8_t *s, size_t n, size_t i, int *keep) {
    if (i && (isalnum(s[i - 1]) || s[i - 1] == ':' || s[i - 1] == '.')) return 0;
    size_t e = i;
    int colons = 0, dbl = 0, group = 0, groups = 0;
    while (e < n && (hexc(s[e]) || s[e] == ':')) {
        if (s[e] == ':') {
            if (e > i && s[e - 1] == ':') {
                if (dbl) return 0;
                dbl = 1;
            }
            colons++;
            group = 0;
        } else {
            if (++group > 4) return 0;
            if (group == 1) groups++;
        }
        e++;
    }
    if (e < n && (isalnum(s[e]) || s[e] == '(')) return 0;
    if (colons < 2 || !groups || (!dbl && colons != 7)) return 0;
    if (s[e - 1] == ':' && !(e >= 2 && s[e - 2] == ':')) return 0;   // a trailing single colon: not an address
    *keep = (e - i == 3 && !memcmp(s + i, "::1", 3)) || (e - i == 2);
    return e - i;
}

static void pass_ids(Buf *o, const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n;) {
        size_t k;
        int keep = 0;
        const char *with = NULL;
        if ((k = sid_at(s, n, i))) with = "<sid>";
        else if ((k = email_at(s, n, i))) with = "<email>";
        else if ((k = mac_at(s, n, i))) with = "<mac>";
        else if ((k = ipv4_at(s, n, i, &keep)) || (k = ipv6_at(s, n, i, &keep))) with = keep ? NULL : "<ip>";
        if (k && with) {
            puts_(o, with);
            i += k;
        } else if (k) {
            put(o, s + i, k);
            i += k;
        } else {
            put(o, s + i, 1);
            i++;
        }
    }
}

uint8_t *pii_mask_text(const PiiMask *m, const uint8_t *text, size_t n, size_t *out_n) {
    Buf a = {0}, b = {0}, c = {0};
    pass_paths(m, &a, text, n);
    if (!a.fail) pass_ids(&b, a.p, a.n);   // before the names: an e-mail address that holds a name goes whole
    free(a.p);
    if (!a.fail && !b.fail) pass_names(m, &c, b.p, b.n);
    free(b.p);
    if (a.fail || b.fail || c.fail) {
        free(c.p);
        return NULL;
    }
    put(&c, "", 0);
    if (!c.p) c.p = malloc(1);   // an empty text: an empty buffer, not NULL
    *out_n = c.n;
    return c.p;
}
