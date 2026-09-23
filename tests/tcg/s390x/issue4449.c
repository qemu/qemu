/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>

#define CASE(name, old, cst, sh)                                        \
    static void __attribute__((noinline)) case_##name(void) {           \
        unsigned long t1, got;                                          \
        __asm__("lhi  %0," #cst "\n\t"   /* low 32 known, high not */   \
                "lghi %1," #old "\n\t"   /* whole register constant */  \
                "sllk %1,%0," #sh "\n\t" /* 32-bit write = deposit */   \
                "brc 15,1f\n\t"          /* ends the translation block */ \
                "nop\n"                                                 \
                "1:"                                                    \
                : "=r"(t1), "=r"(got) : : );                            \
        unsigned long want = (unsigned)(cst) << (sh);                   \
        assert(got == want);                                            \
    }

CASE(a, 256, 128, 1);
CASE(b, 128, 128, 0);
CASE(c, 512, 128, 2);
CASE(d,   2,   1, 1);

int main()
{
    case_a();
    case_b();
    case_c();
    case_d();
    return 0;
}
