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
    uint64_t freq, now;
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

    /*
     * An offset that puts the virtual count ahead of the physical one,
     * so cval + cntvoff wraps for every future cval. The timer must
     * still fire. ISTATUS is set by the expiry, so poll it with a bound.
     */
    write_sysreg(cntv_ctl_el0, 0);
    write_sysreg(cntvoff_el2, -(1ULL << 60));
    asm volatile("isb");

    freq = read_sysreg(cntfrq_el0);
    now = read_sysreg(cntvct_el0);
    write_sysreg(cntv_cval_el0, now + freq / 100);
    write_sysreg(cntv_ctl_el0, 1);

    ml_printf("cntvoff_el2=%lx\n", read_sysreg(cntvoff_el2));
    ml_printf("cntvct_el0=%lx\n", now);
    ml_printf("cntv_cval_el0=%lx\n", read_sysreg(cntv_cval_el0));

    while (!(read_sysreg(cntv_ctl_el0) & 4)) {
        if (read_sysreg(cntvct_el0) - now > freq) {
            ml_printf("FAIL: ISTATUS not set within 1s: cntv_ctl_el0=%lx\n",
                      read_sysreg(cntv_ctl_el0));
            return 1;
        }
    }
    ml_printf("ISTATUS set at cntvct_el0=%lx\n", read_sysreg(cntvct_el0));

    return 0;
}
