/*
 * Test the VSTL instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>
#include "vx.h"

static inline void vstl(S390Vector *v1, void *db2, size_t r3)
{
    asm("vstl %[v1],%[r3],%[db2]"
        : [db2] "=Q" (*(char *)db2)
        : [v1] "v" (v1->v), [r3] "r" (r3)
        : "memory");
}

int main(void)
{
    uint64_t buf[3] = {0x1122334455667788ULL, 0x99aabbccddeeffULL,
                       0x5a5a5a5a5a5a5a5aULL};
    S390Vector v = {.d[0] = 0x1234567887654321ULL,
                    .d[1] = 0x9abcdef00fedcba9ULL};

    vstl(&v, buf, 0);
    assert(buf[0] == 0x1222334455667788ULL);

    vstl(&v, buf, 1);
    assert(buf[0] == 0x1234334455667788ULL);

    vstl(&v, buf, -1);
    assert(buf[0] == 0x1234567887654321ULL);
    assert(buf[1] == 0x9abcdef00fedcba9ULL);
    assert(buf[2] == 0x5a5a5a5a5a5a5a5aULL);

    return EXIT_SUCCESS;
}
