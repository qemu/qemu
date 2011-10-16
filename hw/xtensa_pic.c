/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "hw.h"
#include "qemu-log.h"
#include "qemu-timer.h"

void xtensa_advance_ccount(CPUState *env, uint32_t d)
{
    uint32_t old_ccount = env->sregs[CCOUNT];

    env->sregs[CCOUNT] += d;

    if (xtensa_option_enabled(env->config, XTENSA_OPTION_TIMER_INTERRUPT)) {
        int i;
        for (i = 0; i < env->config->nccompare; ++i) {
            if (env->sregs[CCOMPARE + i] - old_ccount <= d) {
                xtensa_timer_irq(env, i, 1);
            }
        }
    }
}

void check_interrupts(CPUState *env)
{
    int minlevel = xtensa_get_cintlevel(env);
    uint32_t int_set_enabled = env->sregs[INTSET] & env->sregs[INTENABLE];
    int level;

    /* If the CPU is halted advance CCOUNT according to the vm_clock time
     * elapsed since the moment when it was advanced last time.
     */
    if (env->halted) {
        int64_t now = qemu_get_clock_ns(vm_clock);

        xtensa_advance_ccount(env,
                muldiv64(now - env->halt_clock,
                    env->config->clock_freq_khz, 1000000));
        env->halt_clock = now;
    }
    for (level = env->config->nlevel; level > minlevel; --level) {
        if (env->config->level_mask[level] & int_set_enabled) {
            env->pending_irq_level = level;
            cpu_interrupt(env, CPU_INTERRUPT_HARD);
            qemu_log_mask(CPU_LOG_INT,
                    "%s level = %d, cintlevel = %d, "
                    "pc = %08x, a0 = %08x, ps = %08x, "
                    "intset = %08x, intenable = %08x, "
                    "ccount = %08x\n",
                    __func__, level, xtensa_get_cintlevel(env),
                    env->pc, env->regs[0], env->sregs[PS],
                    env->sregs[INTSET], env->sregs[INTENABLE],
                    env->sregs[CCOUNT]);
            return;
        }
    }
    env->pending_irq_level = 0;
    cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
}

static void xtensa_set_irq(void *opaque, int irq, int active)
{
    CPUState *env = opaque;

    if (irq >= env->config->ninterrupt) {
        qemu_log("%s: bad IRQ %d\n", __func__, irq);
    } else {
        uint32_t irq_bit = 1 << irq;

        if (active) {
            env->sregs[INTSET] |= irq_bit;
        } else if (env->config->interrupt[irq].inttype == INTTYPE_LEVEL) {
            env->sregs[INTSET] &= ~irq_bit;
        }

        check_interrupts(env);
    }
}

void xtensa_timer_irq(CPUState *env, uint32_t id, uint32_t active)
{
    qemu_set_irq(env->irq_inputs[env->config->timerint[id]], active);
}

void xtensa_rearm_ccompare_timer(CPUState *env)
{
    int i;
    uint32_t wake_ccount = env->sregs[CCOUNT] - 1;

    for (i = 0; i < env->config->nccompare; ++i) {
        if (env->sregs[CCOMPARE + i] - env->sregs[CCOUNT] <
                wake_ccount - env->sregs[CCOUNT]) {
            wake_ccount = env->sregs[CCOMPARE + i];
        }
    }
    env->wake_ccount = wake_ccount;
    qemu_mod_timer(env->ccompare_timer, env->halt_clock +
            muldiv64(wake_ccount - env->sregs[CCOUNT],
                1000000, env->config->clock_freq_khz));
}

static void xtensa_ccompare_cb(void *opaque)
{
    CPUState *env = opaque;

    if (env->halted) {
        env->halt_clock = qemu_get_clock_ns(vm_clock);
        xtensa_advance_ccount(env, env->wake_ccount - env->sregs[CCOUNT]);
        if (!cpu_has_work(env)) {
            env->sregs[CCOUNT] = env->wake_ccount + 1;
            xtensa_rearm_ccompare_timer(env);
        }
    }
}

void xtensa_irq_init(CPUState *env)
{
    env->irq_inputs = (void **)qemu_allocate_irqs(
            xtensa_set_irq, env, env->config->ninterrupt);
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_TIMER_INTERRUPT) &&
            env->config->nccompare > 0) {
        env->ccompare_timer =
            qemu_new_timer_ns(vm_clock, &xtensa_ccompare_cb, env);
    }
}

void *xtensa_get_extint(CPUState *env, unsigned extint)
{
    if (extint < env->config->nextint) {
        unsigned irq = env->config->extint[extint];
        return env->irq_inputs[irq];
    } else {
        qemu_log("%s: trying to acquire invalid external interrupt %d\n",
                __func__, extint);
        return NULL;
    }
}
