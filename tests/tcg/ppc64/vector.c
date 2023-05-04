#include <assert.h>
#include <stdint.h>
#include "qemu/compiler.h"

int main(void)
{
    unsigned int result_wi;
    vector unsigned char vbc_bi_src = { 0xFF, 0xFF, 0, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0,
                                        0, 0xFF, 0xFF};
    vector unsigned short vbc_hi_src = { 0xFFFF, 0, 0, 0xFFFF,
                                         0, 0, 0xFFFF, 0xFFFF};
    vector unsigned int vbc_wi_src = {0, 0, 0xFFFFFFFF, 0xFFFFFFFF};
    vector unsigned long long vbc_di_src = {0xFFFFFFFFFFFFFFFF, 0};
    vector __uint128_t vbc_qi_src;

    asm("vextractbm %0, %1" : "=r" (result_wi) : "v" (vbc_bi_src));
#if HOST_BIG_ENDIAN
    assert(result_wi == 0b1101111111000011);
#else
    assert(result_wi == 0b1100001111111011);
#endif

    asm("vextracthm %0, %1" : "=r" (result_wi) : "v" (vbc_hi_src));
#if HOST_BIG_ENDIAN
    assert(result_wi == 0b10010011);
#else
    assert(result_wi == 0b11001001);
#endif

    asm("vextractwm %0, %1" : "=r" (result_wi) : "v" (vbc_wi_src));
#if HOST_BIG_ENDIAN
    assert(result_wi == 0b0011);
#else
    assert(result_wi == 0b1100);
#endif

    asm("vextractdm %0, %1" : "=r" (result_wi) : "v" (vbc_di_src));
#if HOST_BIG_ENDIAN
    assert(result_wi == 0b10);
#else
    assert(result_wi == 0b01);
#endif

    vbc_qi_src[0] = 0x1;
    vbc_qi_src[0] = vbc_qi_src[0] << 127;
    asm("vextractqm %0, %1" : "=r" (result_wi) : "v" (vbc_qi_src));
    assert(result_wi == 0b1);

    return 0;
}
