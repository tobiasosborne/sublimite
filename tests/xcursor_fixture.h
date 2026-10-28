#ifndef TEST_XCURSOR_FIXTURE_H
#define TEST_XCURSOR_FIXTURE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static inline void test_xcursor_word(uint8_t *b, uint32_t n)
{ for (unsigned i=0;i<4;i++) b[i]=(uint8_t)(n>>(8u*i)); }
static inline size_t test_xcursor_fixture(uint8_t *b)
{
    memset(b,0,256); memcpy(b,"Xcur",4); test_xcursor_word(b+4,16); test_xcursor_word(b+8,0x10000); test_xcursor_word(b+12,3);
    size_t at=16u+3u*12u;
    const uint32_t sizes[]={16,32,32};
    for (size_t i=0;i<3;i++) {
        test_xcursor_word(b+16+i*12,0xfffd0002); test_xcursor_word(b+20+i*12,sizes[i]); test_xcursor_word(b+24+i*12,(uint32_t)at);
        test_xcursor_word(b+at,36); test_xcursor_word(b+at+4,0xfffd0002); test_xcursor_word(b+at+8,sizes[i]); test_xcursor_word(b+at+12,1);
        test_xcursor_word(b+at+16,2); test_xcursor_word(b+at+20,2); test_xcursor_word(b+at+24,1); test_xcursor_word(b+at+28,1);
        for (size_t p=0;p<4;p++) test_xcursor_word(b+at+36+p*4,UINT32_C(0xff000000)+(uint32_t)i);
        at+=36u+2u*2u*4u;
    }
    return at;
}
#endif
