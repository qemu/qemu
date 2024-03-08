#include <stdio.h>
#include <string.h>

int main()
{
    static const long cmp[4][4] = {
        {  110,  134,  158,  182 },
        {  390,  478,  566,  654 },
        {  670,  822,  974, 1126 },
        {  950, 1166, 1382, 1598 }
    };
    long dst[4][4];
    long *tmp = &dst[0][0];
    long svl;

    /* Validate that we have a wide enough vector for 4 elements. */
    asm(".arch armv8-r+sme-i64\n\trdsvl %0, #1" : "=r"(svl));
    if (svl < 32) {
        return 0;
    }

    asm volatile(
        "smstart\n\t"
        "index z0.h, #0, #1\n\t"
        "movprfx z1, z0\n\t"
        "add z1.h, z1.h, #16\n\t"
        "ptrue p0.b\n\t"
        "smopa za0.d, p0/m, p0/m, z0.h, z1.h\n\t"
        "ptrue p0.d, vl4\n\t"
        "mov w12, #0\n\t"
        "st1d { za0h.d[w12, #0] }, p0, [%0]\n\t"
        "add %0, %0, #32\n\t"
        "st1d { za0h.d[w12, #1] }, p0, [%0]\n\t"
        "mov w12, #2\n\t"
        "add %0, %0, #32\n\t"
        "st1d { za0h.d[w12, #0] }, p0, [%0]\n\t"
        "add %0, %0, #32\n\t"
        "st1d { za0h.d[w12, #1] }, p0, [%0]\n\t"
        "smstop"
        : "+r"(tmp) : : "memory");

    if (memcmp(cmp, dst, sizeof(dst)) == 0) {
        return 0;
    }

    /* See above for correct results. */
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            printf("%6ld", dst[i][j]);
        }
        printf("\n");
    }
    return 1;
}
