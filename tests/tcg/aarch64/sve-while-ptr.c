/* SPDX-License-Identifier: GPL-2.0-or-later */
/* WHILEWR / WHILERW regression test */

#include <sys/prctl.h>
#include <assert.h>

int main(int argc, char **argv)
{
    unsigned short p;
    int set_vl_ret;

    set_vl_ret = prctl(PR_SVE_SET_VL, 16, 0, 0, 0, 0);
    assert(set_vl_ret == 16);

    p = 0xdead;
    asm("whilewr p0.s, %0, %1\n\t"
        "str p0, [%2]"
        : : "r"(8), "r"(11), "r"(&p) : "memory", "p0");
    assert(p == 0x1111);

    p = 0xdead;
    asm("whilerw p0.s, %0, %1\n\t"
        "str p0, [%2]"
        : : "r"(8), "r"(11), "r"(&p) : "memory", "p0");
    assert(p == 0x1111);

    return 0;
}
