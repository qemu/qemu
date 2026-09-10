/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch constant timer support
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-csr.h"

#define TIMER_PERIOD                10 /* 10 ns period for 100 MHz frequency */
#define CONSTANT_TIMER_TICK_MASK    0xfffffffffffcUL
#define CONSTANT_TIMER_ENABLE       0x1UL

uint64_t cpu_loongarch_get_timer_counter(CPUTimerState *timer)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / TIMER_PERIOD;
}

uint64_t cpu_loongarch_get_timer_ticks(CPUTimerState *timer)
{
    CPUSysState *sys = container_of(timer, CPUSysState, timer_state);
    uint64_t now, expire;

    if ((sys->CSR_TCFG & CONSTANT_TIMER_ENABLE) &&
        (sys->CSR_TVAL < sys->CSR_TCFG)) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        expire = timer_expire_time_ns(&timer->timer);
        sys->CSR_TVAL = (expire - now) / TIMER_PERIOD;
    }

    return sys->CSR_TVAL;
}

void cpu_loongarch_set_timer_config(CPUTimerState *timer, uint64_t value)
{
    CPUSysState *sys = container_of(timer, CPUSysState, timer_state);
    uint64_t now, next;

    sys->CSR_TCFG = value;
    if (value & CONSTANT_TIMER_ENABLE) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        next = now + (value & CONSTANT_TIMER_TICK_MASK) * TIMER_PERIOD;
        timer_mod(&timer->timer, next);
        sys->CSR_TVAL = sys->CSR_TCFG & CONSTANT_TIMER_TICK_MASK;
    } else {
        timer_del(&timer->timer);
        sys->CSR_TVAL = 0;
    }
}

void cpu_loongarch_timer_cb(void *opaque)
{
    CPUTimerState *timer = opaque;
    CPUSysState *sys = container_of(timer, CPUSysState, timer_state);
    uint64_t now, next;

    if (FIELD_EX64(sys->CSR_TCFG, CSR_TCFG, PERIODIC)) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        next = now + (sys->CSR_TCFG & CONSTANT_TIMER_TICK_MASK) * TIMER_PERIOD;
        timer_mod(&timer->timer, next);
        sys->CSR_TVAL = sys->CSR_TCFG & CONSTANT_TIMER_TICK_MASK;
    } else {
        sys->CSR_TVAL = CONSTANT_TIMER_TICK_MASK;
    }

    loongarch_cpu_set_irq(LOONGARCH_CPU(timer->cs), timer->irq, 1);
}
