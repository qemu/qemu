/*
 * Test the VCKSM instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "vx.h"

int main(void)
{
    S390Vector v1;
    S390Vector v2 = {
        .d[0] = 0xb2261c8140edce49ULL,
        .d[1] = 0x387bf5a433af39d1ULL,
    };
    S390Vector v3 = {
        .d[0] = 0x73b03d2c7f9e654eULL,
        .d[1] = 0x23d74e51fb479877ULL,
    };
    S390Vector exp = {.d[0] = 0xdedd7f8eULL, .d[1] = 0ULL};

    asm volatile("vcksm %[v1],%[v2],%[v3]"
                 : [v1] "=v" (v1.v)
                 : [v2] "v" (v2.v)
                 , [v3] "v" (v3.v));
    assert(memcmp(&v1, &exp, sizeof(v1)) == 0);

    return EXIT_SUCCESS;
}
