#include <stdio.h>
#include <string.h>

int main()
{
    static const int cmp[4][4] = {
        {  110,  134,  158,  182 },
        {  390,  478,  566,  654 },
        {  670,  822,  974, 1126 },
        {  950, 1166, 1382, 1598 }
    };
    int dst[4][4];
    int *tmp = &dst[0][0];

    asm volatile(
        ".arch armv8-r+sme\n\t"
        "smstart\n\t"
        "index z0.b, #0, #1\n\t"
        "movprfx z1, z0\n\t"
        "add z1.b, z1.b, #16\n\t"
        "ptrue p0.b\n\t"
        "smopa za0.s, p0/m, p0/m, z0.b, z1.b\n\t"
        "ptrue p0.s, vl4\n\t"
        "mov w12, #0\n\t"
        "st1w { za0h.s[w12, #0] }, p0, [%0]\n\t"
        "add %0, %0, #16\n\t"
        "st1w { za0h.s[w12, #1] }, p0, [%0]\n\t"
        "add %0, %0, #16\n\t"
        "st1w { za0h.s[w12, #2] }, p0, [%0]\n\t"
        "add %0, %0, #16\n\t"
        "st1w { za0h.s[w12, #3] }, p0, [%0]\n\t"
        "smstop"
        : "+r"(tmp) : : "memory");

    if (memcmp(cmp, dst, sizeof(dst)) == 0) {
        return 0;
    }

    /* See above for correct results. */
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            printf("%6d", dst[i][j]);
        }
        printf("\n");
    }
    return 1;
}
