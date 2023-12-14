/*
 * Test the VECTOR ISOLATE STRING (vistr) instruction
 */
#include <stdint.h>
#include <stdio.h>
#include "vx.h"

static inline void vistr(S390Vector *v1, S390Vector *v2,
                         const uint8_t m3, const uint8_t m5)
{
    asm volatile("vistr %[v1], %[v2], %[m3], %[m5]\n"
                 : [v1] "=v" (v1->v)
                 : [v2]  "v" (v2->v)
                 , [m3]  "i" (m3)
                 , [m5]  "i" (m5)
                 : "cc");
}

int main(int argc, char *argv[])
{
    S390Vector vd = {};
    S390Vector vs16 = {
        .h[0] = 0x1234, .h[1] = 0x0056, .h[2] = 0x7800, .h[3] = 0x0000,
        .h[4] = 0x0078, .h[5] = 0x0000, .h[6] = 0x6543, .h[7] = 0x2100
    };
    S390Vector vs32 = {
        .w[0] = 0x12340000, .w[1] = 0x78654300,
        .w[2] = 0x0, .w[3] = 0x12,
    };

    vistr(&vd, &vs16, 1, 0);
    if (vd.h[0] != 0x1234 || vd.h[1] != 0x0056 || vd.h[2] != 0x7800 ||
        vd.h[3] || vd.h[4] || vd.h[5] || vd.h[6] || vd.h[7]) {
        puts("ERROR: vitrh failed!");
        return 1;
    }

    vistr(&vd, &vs32, 2, 0);
    if (vd.w[0] != 0x12340000 || vd.w[1] != 0x78654300 || vd.w[2] || vd.w[3]) {
        puts("ERROR: vitrf failed!");
        return 1;
    }

    return 0;
}
