/*
 * Simple Virtual Timer Test
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <minilib.h>

/* grabbed from Linux */
#define __stringify_1(x...) #x
#define __stringify(x...)   __stringify_1(x)

#define read_sysreg(r) ({                                           \
            uint64_t __val;                                         \
            asm volatile("mrs %0, " __stringify(r) : "=r" (__val)); \
            __val;                                                  \
})

#define write_sysreg(r, v) do {                     \
        uint64_t __val = (uint64_t)(v);             \
        asm volatile("msr " __stringify(r) ", %x0"  \
                 : : "rZ" (__val));                 \
} while (0)

int main(void)
{
    int i;

    ml_printf("VTimer Test\n");

    write_sysreg(cntvoff_el2, 1);
    write_sysreg(cntv_cval_el0, -1);
    write_sysreg(cntv_ctl_el0, 1);

    ml_printf("cntvoff_el2=%lx\n", read_sysreg(cntvoff_el2));
    ml_printf("cntv_cval_el0=%lx\n", read_sysreg(cntv_cval_el0));
    ml_printf("cntv_ctl_el0=%lx\n", read_sysreg(cntv_ctl_el0));

    /* Now read cval a few times */
    for (i = 0; i < 10; i++) {
        ml_printf("%d: cntv_cval_el0=%lx\n", i, read_sysreg(cntv_cval_el0));
    }

    return 0;
}
