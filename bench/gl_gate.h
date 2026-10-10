#ifndef GL_GATE_H
#define GL_GATE_H
/* Pure gate logic for bench/gl_bench.c (edit-e6x.27, review gl-1 BLOCKER 7,
 * MAJOR 9-11, 15). No display, no allocation; unit-tested by
 * tests/gl_gate_test.c. Evidence tags ([AC] [bat] [unknown]) describe the
 * run; they never switch a limit off. */
#include <elf.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* perf/01-perf-target.md G3, target A: p50 5.0 ms, p99 = T/2 with
 * T = 1e9/90 ns, i.e. p99 <= 1e9/180 ns exactly (5555555.55.. ns). */
#define GL_GATE_G3_P50_NS 5000000ull
#define GL_GATE_G3_P99_NUM 1000000000ull
#define GL_GATE_G3_P99_DEN 180ull
#define GL_GATE_MINIMAP_ALLOWANCE_NS 180000ull /* (E) bench's stated allowance */
#define GL_GATE_SAMPLES_INTERACTION 10000u      /* perf §4, per interaction scenario */

typedef struct gl_gate_limit { uint64_t p50_ns, p99_num, p99_den; } gl_gate_limit;
typedef enum gl_gate_verdict {
    GL_GATE_PASS,         /* limits met, qualifying run */
    GL_GATE_PASS_PARTIAL, /* limits met but the timed operation is partial: no gate claim */
    GL_GATE_MISS,         /* a limit missed or a sample dropped */
    GL_GATE_UNKNOWN,      /* limits met but power state unknown: not evidence */
    GL_GATE_REFUSED       /* too few samples: no verdict */
} gl_gate_verdict;

static inline const char *gl_gate_name(gl_gate_verdict v)
{
    switch (v) {
    case GL_GATE_PASS: return "PASS";
    case GL_GATE_PASS_PARTIAL: return "PASS_PARTIAL";
    case GL_GATE_MISS: return "MISS";
    case GL_GATE_UNKNOWN: return "UNKNOWN";
    case GL_GATE_REFUSED: return "REFUSED";
    }
    return "?";
}
/* Only a miss fails the process. REFUSED/UNKNOWN/PASS_PARTIAL are explicitly
 * non-qualifying and are printed as such, never as PASS. */
static inline int gl_gate_exit(gl_gate_verdict v) { return v == GL_GATE_MISS ? 1 : 0; }

/* p99 (integer ns) <= num/den exactly; no overflow for p99 < 2^64/den. */
static inline bool gl_gate_p99_within(uint64_t p99, uint64_t num, uint64_t den)
{
    if (den != 0 && p99 > UINT64_MAX / den) return false;
    return p99 * den <= num;
}

static inline bool gl_gate__unknown(const char *tag)
{ return strcmp(tag, "[AC]") != 0 && strcmp(tag, "[bat]") != 0; }

static inline gl_gate_verdict gl_gate_judge(const char *tag, size_t n, size_t dropped, size_t required,
    uint64_t p50, uint64_t p99, const gl_gate_limit *l, bool partial)
{
    if (dropped != 0 || (n != 0 && ((l->p50_ns != 0 && p50 > l->p50_ns) ||
        (l->p99_num != 0 && !gl_gate_p99_within(p99, l->p99_num, l->p99_den))))) return GL_GATE_MISS;
    if (n == 0 || n < required) return GL_GATE_REFUSED;
    if (gl_gate__unknown(tag)) return GL_GATE_UNKNOWN;
    return partial ? GL_GATE_PASS_PARTIAL : GL_GATE_PASS;
}
/* G3z: any miss, duplicate or dropped sample is a miss at any frame count. */
static inline gl_gate_verdict gl_gate_cadence(const char *tag, size_t frames, size_t required,
    uint64_t misses, uint64_t dups, size_t dropped)
{
    if (misses != 0 || dups != 0 || dropped != 0) return GL_GATE_MISS;
    if (frames == 0 || frames < required) return GL_GATE_REFUSED;
    return gl_gate__unknown(tag) ? GL_GATE_UNKNOWN : GL_GATE_PASS;
}
/* Renderer-only budget: the minimap is not rendered by this bench, so its
 * allowance is deducted from both percentiles (p99 numerator scaled by den). */
static inline gl_gate_limit gl_gate_reduce_for_minimap(const gl_gate_limit *l)
{
    gl_gate_limit r = *l;
    r.p50_ns = l->p50_ns > GL_GATE_MINIMAP_ALLOWANCE_NS ? l->p50_ns - GL_GATE_MINIMAP_ALLOWANCE_NS : 1;
    uint64_t cut = GL_GATE_MINIMAP_ALLOWANCE_NS * l->p99_den;
    r.p99_num = l->p99_num > cut ? l->p99_num - cut : 1;
    return r;
}

/* Sum of the .text section sizes of a little-endian ELF64 object. Returns
 * false (and *out = 0) on any malformed or unsupported input. All offsets are
 * widened to 64 bits and checked by subtraction. */
static inline bool gl_gate_elf_text_bytes(FILE *f, uint64_t *out)
{
    Elf64_Ehdr eh; Elf64_Shdr sh[128]; char names[4096]; uint64_t bytes = 0;
    *out = 0;
    if (fread(&eh, sizeof eh, 1, f) != 1 || memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_shnum == 0 || eh.e_shnum > 128 || eh.e_shstrndx >= eh.e_shnum ||
        eh.e_shoff > (uint64_t)LONG_MAX || eh.e_shentsize != sizeof(Elf64_Shdr)) return false;
    if (fseek(f, (long)eh.e_shoff, SEEK_SET) != 0 ||
        fread(sh, sizeof(Elf64_Shdr), eh.e_shnum, f) != eh.e_shnum) return false;
    Elf64_Shdr table = sh[eh.e_shstrndx];
    if (table.sh_size > sizeof names || table.sh_offset > (uint64_t)LONG_MAX ||
        fseek(f, (long)table.sh_offset, SEEK_SET) != 0 ||
        fread(names, 1, (size_t)table.sh_size, f) != table.sh_size) return false;
    for (uint16_t i = 0; i < eh.e_shnum; i++) {
        uint64_t off = sh[i].sh_name;                 /* widen before any arithmetic */
        if (off > table.sh_size || table.sh_size - off < 5) continue;
        if (memcmp(names + off, ".text", 5) == 0) {
            if (bytes > UINT64_MAX - sh[i].sh_size) return false;
            bytes += sh[i].sh_size;
        }
    }
    *out = bytes;
    return true;
}
#endif
