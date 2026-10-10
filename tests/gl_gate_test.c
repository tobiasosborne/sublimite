/* gl_bench honesty checks (edit-e6x.27, review gl-1 BLOCKER 7, MAJOR 9-11, 15).
 * Tests the pure gate logic shared with bench/gl_bench.c; no display needed. */
#include "../bench/gl_gate.h"
#include <elf.h>
#include <stdio.h>
#include <string.h>
#define T(c) do { if (!(c)) { fprintf(stderr,"gl_gate_test:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)

/* Minimal relocatable ELF64: null, .text (size 100), .shstrtab. */
static size_t build_elf(unsigned char *img, size_t cap, uint32_t text_name, uint64_t strtab_size, uint8_t cls)
{
    memset(img, 0, cap);
    Elf64_Ehdr *eh = (Elf64_Ehdr *)img;
    memcpy(eh->e_ident, ELFMAG, SELFMAG);
    eh->e_ident[EI_CLASS] = cls; eh->e_ident[EI_DATA] = ELFDATA2LSB;
    eh->e_shoff = sizeof *eh; eh->e_shentsize = sizeof(Elf64_Shdr);
    eh->e_shnum = 3; eh->e_shstrndx = 2;
    Elf64_Shdr *sh = (Elf64_Shdr *)(img + sizeof *eh);
    sh[1].sh_name = text_name; sh[1].sh_size = 100; sh[1].sh_type = SHT_PROGBITS;
    sh[2].sh_name = 7; sh[2].sh_offset = sizeof *eh + 3 * sizeof(Elf64_Shdr);
    sh[2].sh_size = strtab_size; sh[2].sh_type = SHT_STRTAB;
    memcpy(img + sh[2].sh_offset + 1, ".text", 6);
    memcpy(img + sh[2].sh_offset + 7, ".shstrtab", 10);
    return (size_t)sh[2].sh_offset + 32;
}
static bool run_elf(unsigned char *img, size_t len, uint64_t *bytes)
{
    FILE *f = fmemopen(img, len, "rb");
    if (!f) return false;
    bool ok = gl_gate_elf_text_bytes(f, bytes);
    fclose(f);
    return ok;
}
int main(void)
{
    unsigned char img[1024]; uint64_t bytes = 0;
    /* ELF: well-formed input still works. */
    size_t len = build_elf(img, sizeof img, 1, 32, ELFCLASS64);
    T(run_elf(img, len, &bytes) && bytes == 100);
    /* BLOCKER 7: sh_name + 5 wraps in 32 bits to 1 and used to pass the check. */
    len = build_elf(img, sizeof img, 0xfffffffcu, 32, ELFCLASS64);
    bytes = 77; T(!run_elf(img, len, &bytes) || bytes == 0);
    len = build_elf(img, sizeof img, 28, 32, ELFCLASS64);   /* name ends past table */
    bytes = 77; T(!run_elf(img, len, &bytes) || bytes == 0);
    len = build_elf(img, sizeof img, 1, 32, ELFCLASS32);    /* wrong class */
    T(!run_elf(img, len, &bytes));
    /* MAJOR 15: exact p99 limit is 1e9/90/2 = 5555555.5.. ns. */
    T(gl_gate_p99_within(5555555u, GL_GATE_G3_P99_NUM, GL_GATE_G3_P99_DEN));
    T(!gl_gate_p99_within(5555556u, GL_GATE_G3_P99_NUM, GL_GATE_G3_P99_DEN));
    T(!gl_gate_p99_within(5558000u, GL_GATE_G3_P99_NUM, GL_GATE_G3_P99_DEN)); /* old rounded 5560000 passed this */
    gl_gate_limit g3 = {GL_GATE_G3_P50_NS, GL_GATE_G3_P99_NUM, GL_GATE_G3_P99_DEN};
    T(gl_gate_judge("[AC]", 10000, 0, 10000, 4000000, 5558000, &g3, false) == GL_GATE_MISS);
    T(gl_gate_judge("[AC]", 10000, 0, 10000, 4000000, 5555555, &g3, false) == GL_GATE_PASS);
    /* MAJOR 9: battery and unknown power still enforce the limits. */
    T(gl_gate_judge("[bat]", 10000, 0, 10000, 100000000, 100000000, &g3, false) == GL_GATE_MISS);
    T(gl_gate_judge("[unknown]", 10000, 0, 10000, 100000000, 100000000, &g3, false) == GL_GATE_MISS);
    T(gl_gate_judge("[unknown]", 10000, 0, 10000, 1000000, 1000000, &g3, false) == GL_GATE_UNKNOWN);
    T(gl_gate_judge("[bat]", 10000, 0, 10000, 1000000, 1000000, &g3, false) == GL_GATE_PASS);
    T(gl_gate_judge("[AC]", 10000, 3, 10000, 1, 1, &g3, false) == GL_GATE_MISS); /* dropped samples */
    T(gl_gate_cadence("[bat]", 10000, 10000, 5000, 0, 0) == GL_GATE_MISS);
    T(gl_gate_cadence("[unknown]", 10000, 10000, 0, 0, 0) == GL_GATE_UNKNOWN);
    T(gl_gate_cadence("[AC]", 10000, 10000, 0, 0, 0) == GL_GATE_PASS);
    T(gl_gate_cadence("[AC]", 10000, 10000, 0, 1, 0) == GL_GATE_MISS);
    /* MAJOR 11: too few samples refuses a verdict (quick mode used to print GATE pass=1). */
    T(gl_gate_judge("[AC]", 100, 0, 10000, 1, 1, &g3, false) == GL_GATE_REFUSED);
    T(gl_gate_cadence("[AC]", 100, 10000, 0, 0, 0) == GL_GATE_REFUSED);
    T(gl_gate_cadence("[AC]", 100, 10000, 7, 0, 0) == GL_GATE_MISS); /* a miss is a miss at any n */
    /* MAJOR 10: partial-operation evidence never claims a G3 pass; the
     * minimap allowance is deducted from the p50 budget (and p99 numerator). */
    gl_gate_limit r = gl_gate_reduce_for_minimap(&g3);
    T(r.p50_ns == 5000000u - GL_GATE_MINIMAP_ALLOWANCE_NS);
    T(!gl_gate_p99_within(5500000u, r.p99_num, r.p99_den));
    T(gl_gate_judge("[AC]", 10000, 0, 10000, 4900000, 5000000, &g3, true) == GL_GATE_PASS_PARTIAL);
    T(gl_gate_judge("[AC]", 10000, 0, 10000, 4900000, 5000000, &r, true) == GL_GATE_MISS);
    puts("gl_gate_test: ok");
    return 0;
}
