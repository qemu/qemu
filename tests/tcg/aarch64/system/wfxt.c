/*
 * WFIT/WFET timeout test
 *
 * Copyright (c) 2026 Google LLC
 * Author: Fuad Tabba <fuad.tabba@linux.dev>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <minilib.h>

/* from Linux's include/linux/stringify.h */
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

/* .inst forms of WFIT x0 and WFET x0, for assemblers without FEAT_WFxT */
static inline void wfit(uint64_t timeout)
{
    register uint64_t x0 asm("x0") = timeout;

    asm volatile(".inst 0xd5031020" : : "r" (x0) : "memory");
}

static inline void wfet(uint64_t timeout)
{
    register uint64_t x0 asm("x0") = timeout;

    asm volatile(".inst 0xd5031000" : : "r" (x0) : "memory");
}

/* virt machine, GICv2 */
#define GICD_BASE 0x08000000UL
#define GICC_BASE 0x08010000UL
#define GICD_CTLR 0x000
#define GICD_ISENABLER0 0x100
#define GICC_CTLR 0x000
#define GICC_PMR 0x004
#define VTIMER_PPI 27

static inline void mmio_write32(uintptr_t addr, uint32_t val)
{
    /* GIC registers: MMIO, MMU off at EL2 */
    *(volatile uint32_t *)addr = val;
}

static int test_one(const char *name, void (*wait)(uint64_t), uint64_t freq)
{
    uint64_t now, timeout, elapsed;
    int early = 0;

    /*
     * The virtual timer interrupt, 1s out, wakes the CPU even with
     * interrupts masked. The timeout, 10ms out, must be what wakes it.
     */
    now = read_sysreg(cntvct_el0);
    write_sysreg(cntv_cval_el0, now + freq);
    write_sysreg(cntv_ctl_el0, 1);
    timeout = now + freq / 100;

    do {
        wait(timeout);
        early++;
    } while (read_sysreg(cntvct_el0) < timeout && early < 1000);

    elapsed = read_sysreg(cntvct_el0) - now;
    write_sysreg(cntv_ctl_el0, 0);

    ml_printf("%s: woke after %ld ticks (%d wakes)\n", name, elapsed, early);
    if (early >= 1000) {
        ml_printf("FAIL: %s kept waking before its timeout\n", name);
        return 1;
    }
    if (elapsed > freq / 2) {
        ml_printf("FAIL: %s woke on the timer interrupt, not its timeout\n",
                  name);
        return 1;
    }
    return 0;
}

int main(void)
{
    uint64_t freq;
    int ret;

    ml_printf("WFxT Test\n");

    mmio_write32(GICD_BASE + GICD_ISENABLER0, 1u << VTIMER_PPI);
    mmio_write32(GICD_BASE + GICD_CTLR, 1);
    mmio_write32(GICC_BASE + GICC_PMR, 0xff);
    mmio_write32(GICC_BASE + GICC_CTLR, 1);

    /* Put the virtual count ahead of the physical count */
    write_sysreg(cntvoff_el2, -(1ULL << 60));
    asm volatile("isb");
    freq = read_sysreg(cntfrq_el0);

    ml_printf("cntvoff_el2=%lx cntfrq_el0=%ld\n",
              read_sysreg(cntvoff_el2), freq);

    ret = test_one("wfit", wfit, freq);
    ret |= test_one("wfet", wfet, freq);

    return ret;
}
