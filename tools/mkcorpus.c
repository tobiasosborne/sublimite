/* mkcorpus: deterministic perf corpus generator (perf/01-perf-target.md section 4).
 * Usage: mkcorpus [--small] [--huge] [DIR]
 *   DIR defaults to /tmp/edit-corpus.
 *   --small scales every size (except needle.txt) by 1/64.
 *   --huge  also writes oneline_10g.txt (10 GiB, one line, no newline).
 * Output is a pure function of the file name and size (seeded xorshift64).
 * Existing files with the expected size are skipped. Standard C only.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MIB ((uint64_t)1 << 20)
#define GIB ((uint64_t)1 << 30)
#define CHUNK_CAP (64u * 1024u)
#define BUF_SZ (1u << 20)
#define GEN_SOFT 4096u  /* soft limit per generator call, keeps padding waste small */
#define COUNT(a) ((unsigned)(sizeof(a) / sizeof((a)[0])))

enum { MODE_TEXT, MODE_CRLF, MODE_RAW };

struct sink {
    FILE *f;
    uint64_t target;
    uint64_t written;
    size_t len;
    char buf[BUF_SZ];
};

struct gstate {
    uint64_t rng;
    uint64_t idx;
};

typedef size_t (*gen_fn)(struct gstate *g, char *out, size_t cap);

static uint64_t xs_next(uint64_t *s) {
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static unsigned rnd(struct gstate *g, unsigned n) {
    return (unsigned)(xs_next(&g->rng) % n);
}

static size_t app(char *out, size_t cap, size_t n, const char *s, size_t len) {
    if (len > cap - n) return n;
    memcpy(out + n, s, len);
    return n + len;
}

/* ---- sink: 1 MiB buffer, never writes past target ---- */

static int sink_flush(struct sink *s) {
    if (s->len == 0) return 0;
    if (fwrite(s->buf, 1, s->len, s->f) != s->len) return -1;
    s->len = 0;
    return 0;
}

static int sink_put(struct sink *s, const char *p, size_t n) {
    while (n > 0) {
        uint64_t room64 = s->target - s->written;
        size_t room = BUF_SZ - s->len;
        size_t take = n;
        if (room64 == 0) break;
        if (take > room) take = room;
        if ((uint64_t)take > room64) take = (size_t)room64;
        memcpy(s->buf + s->len, p, take);
        s->len += take;
        s->written += take;
        p += take;
        n -= take;
        if (s->len == BUF_SZ && sink_flush(s) != 0) return -1;
    }
    return 0;
}

/* Pad the remaining bytes so the file ends on a line terminator. */
static int sink_pad(struct sink *s, int crlf) {
    uint64_t rem = s->target - s->written;
    const char *term = crlf ? "\r\n" : "\n";
    uint64_t tl = crlf ? 2u : 1u;
    uint64_t spaces;
    char sp = ' ';
    if (rem == 0) return 0;
    if (rem >= tl) {
        spaces = rem - tl;
    } else {
        spaces = rem;
        tl = 0;
    }
    for (uint64_t i = 0; i < spaces; i++)
        if (sink_put(s, &sp, 1) != 0) return -1;
    if (tl > 0 && sink_put(s, term, (size_t)tl) != 0) return -1;
    return 0;
}

/* Text: whole generator chunks (each ends with a terminator); the last one is padded. */
static int fill_text(struct sink *s, struct gstate *g, gen_fn gen, int crlf) {
    char *chunk = malloc(CHUNK_CAP);
    int rc = 0;
    if (!chunk) return -1;
    while (s->written < s->target) {
        size_t n = gen(g, chunk, CHUNK_CAP);
        if ((uint64_t)n <= s->target - s->written) {
            if (sink_put(s, chunk, n) != 0) { rc = -1; break; }
        } else if (sink_pad(s, crlf) != 0) {
            rc = -1;
            break;
        }
    }
    free(chunk);
    return rc;
}

/* Raw: bytes exactly as generated, truncated at target. */
static int fill_raw(struct sink *s, struct gstate *g, gen_fn gen) {
    char *chunk = malloc(CHUNK_CAP);
    int rc = 0;
    if (!chunk) return -1;
    while (s->written < s->target) {
        size_t n = gen(g, chunk, CHUNK_CAP);
        if (n == 0 || sink_put(s, chunk, n) != 0) { rc = -1; break; }
    }
    free(chunk);
    return rc;
}

/* ---- generators ---- */

static const char *const cwords[] = {
    "buffer", "count", "index", "value", "node", "offset", "length", "ptr",
    "result", "state", "cursor", "line", "col", "flags", "size", "span"};

static const char *cword(struct gstate *g) { return cwords[rnd(g, COUNT(cwords))]; }

/* Emit one indented line; pad with a comment so its width lands in [60, 100]. */
static size_t emit(char *out, size_t cap, size_t n, unsigned indent,
                   const char *text, unsigned target) {
    char line[256];
    size_t len = 0;
    const char *fill = "review pending, check bounds ";
    size_t fl = strlen(fill);
    size_t tl = strlen(text);
    size_t i = 0;
    for (unsigned k = 0; k < indent * 4u; k++) line[len++] = ' ';
    if (tl > 150) tl = 150;
    memcpy(line + len, text, tl);
    len += tl;
    if (target > 0 && len + 8 < target) {
        memcpy(line + len, " /* ", 4);
        len += 4;
        while (len + 3 < target) line[len++] = fill[i++ % fl];
        memcpy(line + len, " */", 3);
        len += 3;
    }
    line[len++] = '\n';
    return app(out, cap, n, line, len);
}

static size_t gen_c(struct gstate *g, char *out, size_t cap) {
    char text[200];
    size_t n = 0;
    while (n < GEN_SOFT) {
        unsigned fid = (unsigned)(g->idx++ % 100000u);
        unsigned target = 60u + rnd(g, 41);
        int cur = 0;
        unsigned nst = 4u + rnd(g, 26);
        snprintf(text, sizeof text, "static int fn_%u(int a, int b, int c)", fid);
        n = emit(out, cap, n, 0, text, target);
        n = emit(out, cap, n, 0, "{", 0);
        for (unsigned s = 0; s < nst; s++) {
            unsigned r = rnd(g, 10);
            unsigned ind = (unsigned)cur + 1u;
            unsigned k = rnd(g, 1000);
            if (r < 2 && cur < 3) {
                snprintf(text, sizeof text, "if (%s > %u) {", cword(g), k);
                n = emit(out, cap, n, ind, text, target);
                cur++;
            } else if (r == 2 && cur < 3) {
                snprintf(text, sizeof text, "for (%s = 0; %s < %u; %s++) {", cword(g), cword(g), k, cword(g));
                n = emit(out, cap, n, ind, text, target);
                cur++;
            } else if (r == 3 && cur > 0) {
                cur--;
                n = emit(out, cap, n, (unsigned)cur + 1u, "}", 0);
            } else {
                switch (rnd(g, 6)) {
                case 0: snprintf(text, sizeof text, "%s = %s + %u;", cword(g), cword(g), k); break;
                case 1: snprintf(text, sizeof text, "%s[%s] = (char)%u;", cword(g), cword(g), k & 0x7fu); break;
                case 2: snprintf(text, sizeof text, "if (%s < %u) return -1;", cword(g), k); break;
                case 3: snprintf(text, sizeof text, "%s->%s = %s * %u;", cword(g), cword(g), cword(g), k); break;
                case 4: snprintf(text, sizeof text, "fn_%u(a, b, %s);", k % 97u, cword(g)); break;
                default: snprintf(text, sizeof text, "/* %s update: %s */", cword(g), cword(g)); break;
                }
                n = emit(out, cap, n, ind, text, target);
            }
        }
        while (cur > 0) {
            cur--;
            n = emit(out, cap, n, (unsigned)cur + 1u, "}", 0);
        }
        n = emit(out, cap, n, 1, "return a + b;", 0);
        n = emit(out, cap, n, 0, "}", 0);
        n = emit(out, cap, n, 0, "", 0);
    }
    return n;
}

static const char *const ufrag[] = {
    "café", "naïve", "Ångström", "Zürich", "señor", "crème brûlée", "Łódź", "Ærø",
    "café", "naïve", "über", "ño",
    "漢字", "日本語のテキスト", "中文字符", "한국어",
    "👨‍👩‍👧", "🏳️‍🌈", "👍🏽", "👩‍💻", "🇯🇵", "😀😃😄",
    "مرحبا بالعالم", "שלום עולם", "‮abc‬", "⁧שלום⁩",
    "text مرحبا 123 שלום", "a​b", "﻿BOM", "x⁠y", "‎text‏",
    "Lorem ipsum dolor sit amet"};

static const char *const mfrag[] = {
    "valid text", "café", "漢字", "👍🏽", "مرحبا", "שלום", "plain ascii", "‍zw"};

static const char *const mbad[] = {
    "\xE6\x97", "\xC0\xAF", "\xE0\x80\xAF", "\x80", "\xED\xA0\x80", "\xED\xBF\xBF",
    "\xFF", "\xFE", "\xF0\x9F\x98", "\xC3", "\xF4\x90\x80\x80", "\xBF\xBF"};

static const char *const asciiwords[] = {
    "the quick brown fox", "lorem ipsum dolor", "sit amet consectetur",
    "buffer offset length", "editor piece tree", "line column cursor",
    "hello world", "0123456789 abcdef", "ENDLINE CRLF TEST"};

static size_t gen_lines(struct gstate *g, char *out, size_t cap,
                        const char *const *a, unsigned na,
                        const char *const *b, unsigned nb, const char *eol) {
    size_t n = 0;
    size_t el = strlen(eol);
    while (n < GEN_SOFT) {
        unsigned k = 1u + rnd(g, 6);
        for (unsigned i = 0; i < k; i++) {
            const char *f;
            if (nb > 0 && rnd(g, 4) == 0) f = b[rnd(g, nb)];
            else f = a[rnd(g, na)];
            if (i > 0 && rnd(g, 3) != 0) n = app(out, cap, n, " ", 1);
            n = app(out, cap, n, f, strlen(f));
        }
        n = app(out, cap, n, eol, el);
    }
    return n;
}

static size_t gen_uni(struct gstate *g, char *out, size_t cap) {
    return gen_lines(g, out, cap, ufrag, COUNT(ufrag), NULL, 0, "\n");
}

static size_t gen_mal(struct gstate *g, char *out, size_t cap) {
    return gen_lines(g, out, cap, mfrag, COUNT(mfrag), mbad, COUNT(mbad), "\n");
}

static size_t gen_crlf(struct gstate *g, char *out, size_t cap) {
    return gen_lines(g, out, cap, asciiwords, COUNT(asciiwords), NULL, 0, "\r\n");
}

static size_t gen_dense(struct gstate *g, char *out, size_t cap) {
    (void)cap;
    size_t n = 0;
    while (n + 8 <= GEN_SOFT) {
        unsigned len = rnd(g, 4);
        for (unsigned i = 0; i < len; i++) {
            if (rnd(g, 2) == 0) out[n++] = ' ';
            else out[n++] = (char)('a' + (int)rnd(g, 26));
        }
        out[n++] = '\n';
    }
    return n;
}

static size_t gen_log(struct gstate *g, char *out, size_t cap) {
    static const char *const lv[] = {"INFO", "WARN", "DEBUG", "ERROR", "TRACE"};
    static const char *const sv[] = {"editor-core", "piece-tree", "render", "input", "fs-watch", "search"};
    size_t n = 0;
    for (unsigned k = 0; k < 16u && n + 120u <= cap; k++) {
        char hdr[160];
        uint64_t ms = g->idx++;
        unsigned h = (unsigned)((ms / 3600000u) % 24u);
        unsigned mi = (unsigned)((ms / 60000u) % 60u);
        unsigned se = (unsigned)((ms / 1000u) % 60u);
        unsigned msec = (unsigned)(ms % 1000u);
        unsigned usec = rnd(g, 1000);
        const char *lvl = lv[rnd(g, COUNT(lv))];
        const char *svc = sv[rnd(g, COUNT(sv))];
        uint64_t req = xs_next(&g->rng);
        unsigned lat = rnd(g, 90000);
        int r = snprintf(hdr, sizeof hdr,
                         "2026-10-08T%02u:%02u:%02u.%03u%03uZ %-5s %-10s req=%016" PRIx64 " lat=%uus ",
                         h, mi, se, msec, usec, lvl, svc, req, lat);
        size_t hl = r > 0 ? (size_t)r : 0;
        if (hl > 118) hl = 118;
        memcpy(out + n, hdr, hl);
        for (size_t j = hl; j < 119; j++)
            out[n + j] = (rnd(g, 6) == 0) ? ' ' : (char)('a' + (int)rnd(g, 26));
        out[n + 119] = '\n';
        n += 120;
    }
    return n;
}

static size_t gen_oneline(struct gstate *g, char *out, size_t cap) {
    for (size_t j = 0; j < cap; j++) {
        if (rnd(g, 8) == 0) out[j] = ' ';
        else out[j] = (char)('a' + (int)rnd(g, 26));
    }
    return cap;
}

/* b at offsets p % 32 == 37 % 32 (a prime shift), a elsewhere. */
static size_t gen_periodic(struct gstate *g, char *out, size_t cap) {
    for (size_t j = 0; j < cap; j++) {
        uint64_t p = g->idx + j;
        out[j] = (p % 32u == 37u % 32u) ? 'b' : 'a';
    }
    g->idx += cap;
    return cap;
}

static size_t gen_needle(struct gstate *g, char *out, size_t cap) {
    (void)g;
    if (cap < 32) return 0;
    memset(out, 'a', 31);
    out[31] = 'b';
    return 32;
}

/* ---- file table ---- */

struct spec {
    const char *name;
    uint64_t size;
    gen_fn gen;
    int mode;
    uint64_t seed;
    int scale;   /* scaled by 1/64 under --small */
    int huge;    /* only with --huge */
};

static const struct spec specs[] = {
    {"ascii_code.c", MIB, gen_c, MODE_TEXT, 0x0123456789abcdefULL, 1, 0},
    {"unicode.txt", MIB, gen_uni, MODE_TEXT, 0x2545f4914f6cdd1dULL, 1, 0},
    {"malformed.txt", MIB, gen_mal, MODE_TEXT, 0x9e3779b97f4a7c15ULL, 1, 0},
    {"dense_short.txt", 64 * MIB, gen_dense, MODE_TEXT, 0xdeadbeefcafef00dULL, 1, 0},
    {"crlf.txt", MIB, gen_crlf, MODE_CRLF, 0x123456789ULL, 1, 0},
    {"log_1g.txt", GIB, gen_log, MODE_TEXT, 0xfeedfacefeedfaceULL, 1, 0},
    {"oneline_1g.txt", GIB, gen_oneline, MODE_RAW, 0x0badc0ffee123457ULL, 1, 0},
    {"oneline_10g.txt", 10 * GIB, gen_oneline, MODE_RAW, 0x5deece66dULL, 1, 1},
    {"periodic.txt", 256 * MIB, gen_periodic, MODE_RAW, 0x1ULL, 1, 0},
    {"needle.txt", 32, gen_needle, MODE_RAW, 0x1ULL, 0, 0},
};

static int make_file(const char *dir, const struct spec *sp, int small) {
    char path[4096];
    struct stat st;
    uint64_t size = (small && sp->scale) ? sp->size / 64u : sp->size;
    struct sink *s;
    struct gstate g;
    FILE *f;
    int rc;

    snprintf(path, sizeof path, "%s/%s", dir, sp->name);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && (uint64_t)st.st_size == size) {
        printf("skip   %-16s %12" PRIu64 " bytes (exists)\n", sp->name, size);
        return 0;
    }
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "mkcorpus: cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }
    s = malloc(sizeof *s);
    if (!s) {
        fclose(f);
        fprintf(stderr, "mkcorpus: out of memory\n");
        return -1;
    }
    s->f = f;
    s->target = size;
    s->written = 0;
    s->len = 0;
    g.rng = sp->seed ? sp->seed : 1u;
    g.idx = 0;
    if (sp->mode == MODE_RAW) rc = fill_raw(s, &g, sp->gen);
    else rc = fill_text(s, &g, sp->gen, sp->mode == MODE_CRLF);
    if (rc == 0 && sink_flush(s) != 0) rc = -1;
    if (fclose(f) != 0) rc = -1;
    free(s);
    if (rc != 0) {
        fprintf(stderr, "mkcorpus: write error on %s: %s\n", path, strerror(errno));
        return -1;
    }
    printf("wrote  %-16s %12" PRIu64 " bytes\n", sp->name, size);
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = "/tmp/edit-corpus";
    int small = 0, huge = 0;
    size_t i;

    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--small") == 0) small = 1;
        else if (strcmp(argv[a], "--huge") == 0) huge = 1;
        else if (strncmp(argv[a], "--", 2) == 0) {
            fprintf(stderr, "usage: mkcorpus [--small] [--huge] [DIR]\n");
            return 2;
        } else dir = argv[a];
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkcorpus: cannot create %s: %s\n", dir, strerror(errno));
        return 1;
    }
    for (i = 0; i < sizeof specs / sizeof specs[0]; i++) {
        if (specs[i].huge && !huge) continue;
        if (make_file(dir, &specs[i], small) != 0) return 1;
    }
    return 0;
}
