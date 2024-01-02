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

uint64_t cpu_loongarch_get_constant_timer_counter(LoongArchCPU *cpu)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / TIMER_PERIOD;
}

uint64_t cpu_loongarch_get_constant_timer_ticks(LoongArchCPU *cpu)
{
    uint64_t now, expire;

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    expire = timer_expire_time_ns(&cpu->timer);

    return (expire - now) / TIMER_PERIOD;
}

void cpu_loongarch_store_constant_timer_config(LoongArchCPU *cpu,
                                               uint64_t value)
{
    CPULoongArchState *env = &cpu->env;
    uint64_t now, next;

    env->CSR_TCFG = value;
    if (value & CONSTANT_TIMER_ENABLE) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        next = now + (value & CONSTANT_TIMER_TICK_MASK) * TIMER_PERIOD;
        timer_mod(&cpu->timer, next);
    } else {
        timer_del(&cpu->timer);
    }
}

void loongarch_constant_timer_cb(void *opaque)
{
    LoongArchCPU *cpu  = opaque;
    CPULoongArchState *env = &cpu->env;
    uint64_t now, next;

    if (FIELD_EX64(env->CSR_TCFG, CSR_TCFG, PERIODIC)) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        next = now + (env->CSR_TCFG & CONSTANT_TIMER_TICK_MASK) * TIMER_PERIOD;
        timer_mod(&cpu->timer, next);
    } else {
        env->CSR_TCFG = FIELD_DP64(env->CSR_TCFG, CSR_TCFG, EN, 0);
    }

    loongarch_cpu_set_irq(opaque, IRQ_TIMER, 1);
}
