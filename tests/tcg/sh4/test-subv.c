/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void subv(const int a, const int b, const int res, const int carry)
{
    int o = a, c;

    asm volatile("subv %2,%0\n"
                 "movt %1\n"
                 : "+r"(o), "=r"(c) : "r"(b) : );

    if (c != carry || o != res) {
        printf("SUBV %d, %d = %d/%d [T = %d/%d]\n", a, b, o, res, c, carry);
        abort();
    }
}

int main(void)
{
    subv(INT_MIN, 1, INT_MAX, 1);
    subv(INT_MAX, -1, INT_MIN, 1);
    subv(INT_MAX, 1, INT_MAX - 1, 0);
    subv(0, 1, -1, 0);
    subv(-1, -1, 0, 0);

    return 0;
}
