// Mod zips: the central directory (PKWARE APPNOTE 4.3.12 and 4.3.7), stored and deflated entries, CRC-32 checks, and
// an inflater written from RFC 1951; a small deflater (fixed codes) for the bug report's zip. Zip64 archives are refused; a mod that large is not something to fetch in game.
#include "mod_zip.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "platform/platform.h"

// ---- CRC-32 (the zip polynomial, 0xedb88320 reflected)

uint32_t mod_crc32(const uint8_t *p, size_t n) {
    static uint32_t table[256];
    static int ready;
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
    }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

// ---- inflate

typedef struct {
    const uint8_t *src; size_t n, pos;   // input, and the byte the bit buffer reads next
    uint64_t bits; int nbits;
    uint8_t *dst; size_t want, out;
} Inflate;

typedef struct { uint16_t count[16]; uint16_t symbol[320]; } Huffman;

static int need(Inflate *s, int k) {
    while (s->nbits < k) {
        if (s->pos >= s->n) return 0;
        s->bits |= (uint64_t)s->src[s->pos++] << s->nbits;
        s->nbits += 8;
    }
    return 1;
}

static int getbits(Inflate *s, int k, unsigned *v) {
    if (!need(s, k)) return 0;
    *v = (unsigned)(s->bits & ((1ull << k) - 1));
    s->bits >>= k;
    s->nbits -= k;
    return 1;
}

// Canonical Huffman table from code lengths; 0 when the lengths over-subscribe the code.
static int build(Huffman *h, const uint8_t *lengths, int n) {
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left = left * 2 - h->count[len];
        if (left < 0) return 0;
    }
    uint16_t offs[16] = {0};
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (uint16_t)i;
    return 1;
}

// One symbol, read a bit at a time (the codes are stored most significant bit first); -1 on a bad or short stream.
static int decode(Inflate *s, const Huffman *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        unsigned b;
        if (!getbits(s, 1, &b)) return -1;
        code |= (int)b;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                                   99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DEXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
                                   12, 13, 13};

static int codes(Inflate *s, const Huffman *lit, const Huffman *dist) {
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0) return 0;
        if (sym < 256) {
            if (s->out >= s->want) return 0;
            s->dst[s->out++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 1;
        } else {
            sym -= 257;
            if (sym >= 29) return 0;
            unsigned e;
            if (!getbits(s, LEXTRA[sym], &e)) return 0;
            size_t len = LBASE[sym] + e;
            int d = decode(s, dist);
            if (d < 0 || d >= 30 || !getbits(s, DEXTRA[d], &e)) return 0;
            size_t back = DBASE[d] + e;
            if (back > s->out || len > s->want - s->out) return 0;
            for (size_t i = 0; i < len; i++, s->out++) s->dst[s->out] = s->dst[s->out - back];
        }
    }
}

static int fixed_block(Inflate *s) {
    static Huffman lit, dist;
    static int ready;
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) {
        uint8_t l[288];
        for (int i = 0; i < 288; i++) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        build(&lit, l, 288);
        for (int i = 0; i < 30; i++) l[i] = 5;
        build(&dist, l, 30);
        __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
    }
    return codes(s, &lit, &dist);
}

static int dynamic_block(Inflate *s) {
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    unsigned hlit, hdist, hclen;
    if (!getbits(s, 5, &hlit) || !getbits(s, 5, &hdist) || !getbits(s, 4, &hclen)) return 0;
    hlit += 257; hdist += 1; hclen += 4;
    if (hlit > 286 || hdist > 30) return 0;
    uint8_t lengths[320] = {0};
    for (unsigned i = 0; i < hclen; i++) {
        unsigned v;
        if (!getbits(s, 3, &v)) return 0;
        lengths[ORDER[i]] = (uint8_t)v;
    }
    Huffman lencode, lit, dist;
    if (!build(&lencode, lengths, 19)) return 0;
    memset(lengths, 0, sizeof lengths);
    for (unsigned i = 0; i < hlit + hdist;) {
        int sym = decode(s, &lencode);
        if (sym < 0) return 0;
        if (sym < 16) {
            lengths[i++] = (uint8_t)sym;
            continue;
        }
        unsigned rep, len = 0;
        if (sym == 16) {
            if (!i || !getbits(s, 2, &rep)) return 0;
            len = lengths[i - 1];
            rep += 3;
        } else if (sym == 17) {
            if (!getbits(s, 3, &rep)) return 0;
            rep += 3;
        } else {
            if (!getbits(s, 7, &rep)) return 0;
            rep += 11;
        }
        if (i + rep > hlit + hdist) return 0;
        while (rep--) lengths[i++] = (uint8_t)len;
    }
    if (!lengths[256]) return 0;   // a block needs its end code
    if (!build(&lit, lengths, (int)hlit) || !build(&dist, lengths + hlit, (int)hdist)) return 0;
    return codes(s, &lit, &dist);
}

// ---- deflate: greedy LZ77 over a 32 KiB window with hash chains, one block with the fixed Huffman codes (RFC 1951
// 3.2.6). Logs, the bulk of a bug report, shrink to about a fifth; no dynamic codes, as speed matters more here.

typedef struct { uint8_t *dst; size_t cap, at; uint64_t bits; int nbits; int full; } Deflate;

static void put_bits(Deflate *d, uint32_t v, int k) {
    d->bits |= (uint64_t)v << d->nbits;
    d->nbits += k;
    while (d->nbits >= 8) {
        if (d->at >= d->cap) { d->full = 1; d->nbits = 0; return; }
        d->dst[d->at++] = (uint8_t)d->bits;
        d->bits >>= 8;
        d->nbits -= 8;
    }
}

// A Huffman code goes out most significant bit first, so reversed into the bit buffer.
static void put_code(Deflate *d, uint32_t code, int len) {
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1) << (len - 1 - i);
    put_bits(d, r, len);
}

static void put_lit(Deflate *d, unsigned v) {
    if (v < 144) put_code(d, 0x30 + v, 8);
    else if (v < 256) put_code(d, 0x190 + v - 144, 9);
    else if (v < 280) put_code(d, v - 256, 7);
    else put_code(d, 0xc0 + v - 280, 8);
}

static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                                      99, 115, 131, 163, 195, 227, 258};
static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                       12, 12, 13, 13};

static void put_match(Deflate *d, unsigned len, unsigned dist) {
    int l = 28;
    while (len_base[l] > len) l--;
    put_lit(d, 257 + (unsigned)l);
    if (len_extra[l]) put_bits(d, len - len_base[l], len_extra[l]);
    int k = 29;
    while (dist_base[k] > dist) k--;
    put_code(d, (uint32_t)k, 5);
    if (dist_extra[k]) put_bits(d, dist - dist_base[k], dist_extra[k]);
}

size_t mod_deflate(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    enum { WIN = 32768, HASH = 1 << 15, CHAIN = 24, MAX_LEN = 258 };
    int32_t *head = malloc(HASH * sizeof *head), *prev = malloc(WIN * sizeof *prev);
    if (!head || !prev) { free(head); free(prev); return 0; }
    for (int i = 0; i < HASH; i++) head[i] = -1;
    Deflate d = {dst, cap, 0, 0, 0, 0};
    put_bits(&d, 1, 1);   // BFINAL
    put_bits(&d, 1, 2);   // BTYPE 01: fixed codes
    size_t i = 0;
    while (i < n && !d.full) {
        unsigned best = 0, best_dist = 0;
        if (i + 3 <= n) {
            uint32_t h = ((uint32_t)src[i] << 16 | (uint32_t)src[i + 1] << 8 | src[i + 2]) * 2654435761u >> 17;
            size_t max = n - i < MAX_LEN ? n - i : MAX_LEN;
            int32_t cand = head[h];
            for (int c = 0; c < CHAIN && cand >= 0 && i - (size_t)cand <= WIN - 1; c++) {
                const uint8_t *a = src + cand, *b = src + i;
                if (a[best] == b[best]) {
                    unsigned k = 0;
                    while (k < max && a[k] == b[k]) k++;
                    if (k > best) { best = k; best_dist = (unsigned)(i - (size_t)cand); if (k == max) break; }
                }
                int32_t next = prev[cand & (WIN - 1)];
                if (next >= cand) break;
                cand = next;
            }
            prev[i & (WIN - 1)] = head[h];
            head[h] = (int32_t)i;
        }
        if (best >= 3) {
            put_match(&d, best, best_dist);
            for (size_t e = i + best, j = i + 1; j < e && j + 3 <= n; j++) {   // the match's other positions
                uint32_t h = ((uint32_t)src[j] << 16 | (uint32_t)src[j + 1] << 8 | src[j + 2]) * 2654435761u >> 17;
                prev[j & (WIN - 1)] = head[h];
                head[h] = (int32_t)j;
            }
            i += best;
        } else {
            put_lit(&d, src[i++]);
        }
    }
    put_lit(&d, 256);
    if (d.nbits) put_bits(&d, 0, 8 - d.nbits);
    free(head);
    free(prev);
    return d.full ? 0 : d.at;
}

int mod_inflate(const uint8_t *src, size_t n, uint8_t *dst, size_t want) {
    Inflate s = {src, n, 0, 0, 0, dst, want, 0};
    unsigned last, type;
    do {
        if (!getbits(&s, 1, &last) || !getbits(&s, 2, &type)) return 0;
        if (type == 0) {   // stored: to a byte boundary, then LEN and its complement
            s.bits = 0;
            s.nbits = 0;
            if (s.pos + 4 > n) return 0;
            unsigned len = src[s.pos] | src[s.pos + 1] << 8, nlen = src[s.pos + 2] | src[s.pos + 3] << 8;
            s.pos += 4;
            if ((len ^ 0xffff) != nlen || s.pos + len > n || len > want - s.out) return 0;
            memcpy(dst + s.out, src + s.pos, len);
            s.pos += len;
            s.out += len;
        } else if (type == 1) {
            if (!fixed_block(&s)) return 0;
        } else if (type == 2) {
            if (!dynamic_block(&s)) return 0;
        } else {
            return 0;
        }
    } while (!last);
    return s.out == want;
}

// ---- the zip

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

// The game path of an entry name, into out; 0: not a game file (skip); -1: unsafe (refuse the zip).
static int game_path(const char *name, char *out, size_t cap) {
    char tmp[1024];
    size_t n = strlen(name);
    if (!n || n >= sizeof tmp) return n ? -1 : 0;
    for (size_t i = 0; i <= n; i++) tmp[i] = name[i] == '\\' ? '/' : name[i];
    if (tmp[0] == '/' || tmp[n - 1] == '/') return tmp[0] == '/' ? -1 : 0;   // absolute, or a directory
    // the parts; any ".." or a drive letter in the first refuses
    char *parts[128];
    int np = 0;
    for (char *p = tmp, *e; p && np < 128; p = e ? e + 1 : NULL) {
        e = strchr(p, '/');
        if (e) *e = 0;
        if (!strcmp(p, "..") || (!np && strchr(p, ':'))) return -1;
        if (*p && strcmp(p, ".")) parts[np++] = p;
    }
    int start = -1;
    for (int i = 0; i < np; i++)
        if (!strcasecmp(parts[i], "romfs")) start = i + 1;
    if (start < 0 || start >= np) return 0;
    if (!strcasecmp(parts[start], "mod") && start + 1 < np) start++;
    size_t len = 0;
    for (int i = start; i < np; i++) {
        size_t pl = strlen(parts[i]);
        if (len + pl + 2 > cap) return -1;
        if (len) out[len++] = '/';
        memcpy(out + len, parts[i], pl);
        len += pl;
    }
    out[len] = 0;
    return 1;
}

static int by_lower(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

const char *mod_zip_unpack_romfs(const uint8_t *zip, size_t size, const char *folder) {
    if (size < 22) return "the download is not a zip";
    size_t eocd = 0;
    int found = 0;
    for (size_t back = 22; back <= size && back <= 22 + 65535; back++)
        if (rd32(zip + size - back) == 0x06054b50) {
            eocd = size - back;
            found = 1;
            break;
        }
    if (!found) return "the download is not a zip";
    uint32_t entries = rd16(zip + eocd + 10), cd_size = rd32(zip + eocd + 12), cd_off = rd32(zip + eocd + 16);
    if (entries == 0xffff || cd_off == 0xffffffffu) return "the zip is a Zip64 archive, which is not supported in game";
    if ((uint64_t)cd_off + cd_size > eocd) return "the zip is damaged";
    // pass 1: check every entry and collect the game paths; pass 2: write
    char **paths = calloc(entries ? entries : 1, sizeof *paths);
    const char *err = NULL;
    int ngame = 0;
    uint64_t total = 0;
    if (!paths) return "out of memory";
    for (int pass = 0; pass < 2 && !err; pass++) {
        size_t p = cd_off;

        for (uint32_t i = 0; i < entries && !err; i++) {
            if (p + 46 > eocd || rd32(zip + p) != 0x02014b50) { err = "the zip is damaged"; break; }
            uint32_t method = rd16(zip + p + 10), crc = rd32(zip + p + 16), csize = rd32(zip + p + 20),
                     usize = rd32(zip + p + 24), nlen = rd16(zip + p + 28), xlen = rd16(zip + p + 30),
                     clen = rd16(zip + p + 32), attr = rd32(zip + p + 38), loff = rd32(zip + p + 42);
            uint32_t flags = rd16(zip + p + 8);
            if (p + 46 + nlen > eocd) { err = "the zip is damaged"; break; }
            char name[1024];
            if (nlen >= sizeof name) { err = "the zip has a path that is too long"; break; }
            memcpy(name, zip + p + 46, nlen);
            name[nlen] = 0;
            p += 46 + nlen + xlen + clen;
            char gp[1024];
            int kind = game_path(name, gp, sizeof gp);
            if (kind < 0 || ((attr >> 16) & 0170000) == 0120000 || memchr(name, 0, nlen)) {
                err = "the zip has an unsafe entry";
                break;
            }
            if (!kind) continue;
            if (pass == 0) {
                if (csize == 0xffffffffu || usize == 0xffffffffu) { err = "the zip is a Zip64 archive"; break; }
                if (flags & 1) { err = "the zip is encrypted"; break; }
                if (method != 0 && method != 8) { err = "the zip uses a compression the game cannot unpack"; break; }
                total += usize;
                if (total > MOD_ZIP_LIMIT) { err = "the zip unpacks to more than 16 GB"; break; }
                paths[ngame++] = strdup(gp);
                continue;
            }
            // pass 2: the data after the local header
            if ((uint64_t)loff + 30 > eocd || rd32(zip + loff) != 0x04034b50) { err = "the zip is damaged"; break; }
            uint64_t data = (uint64_t)loff + 30 + rd16(zip + loff + 26) + rd16(zip + loff + 28);
            if (data + csize > eocd) { err = "the zip is damaged"; break; }
            uint8_t *out = malloc(usize ? usize : 1);
            if (!out) { err = "out of memory"; break; }
            int ok = method == 0 ? (csize == usize && (memcpy(out, zip + data, usize), 1))
                                 : mod_inflate(zip + data, csize, out, usize);
            if (!ok || mod_crc32(out, usize) != crc) {
                free(out);
                err = "the zip is damaged (a file does not unpack to what it should)";
                break;
            }
            char host[4200];
            snprintf(host, sizeof host, "%s/romfs/%s", folder, gp);
            char *slash = strrchr(host, '/');
            *slash = 0;
            plat_file_mkdir_p(host);
            *slash = '/';
            if (!plat_file_write(host, out, usize)) err = "a file could not be written to the mods folder";
            free(out);

        }
        if (!err && pass == 0) {
            if (!ngame) err = "the zip has no romfs folder, so it holds no game files";
            qsort(paths, (size_t)ngame, sizeof *paths, by_lower);
            for (int i = 1; i < ngame && !err; i++)
                if (!strcasecmp(paths[i - 1], paths[i])) err = "the zip has the same file twice";
        }
    }
    for (int i = 0; i < ngame; i++) free(paths[i]);
    free(paths);
    return err;
}
