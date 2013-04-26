/*
 * QEMU PowerPC Booke hardware System Emulator
 *
 * Copyright (c) 2011 AdaCore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw/hw.h"
#include "hw/ppc/ppc.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/timer/m48t59.h"
#include "qemu/log.h"
#include "hw/loader.h"
#include "kvm_ppc.h"


/* Timer Control Register */

#define TCR_WP_SHIFT  30        /* Watchdog Timer Period */
#define TCR_WP_MASK   (0x3 << TCR_WP_SHIFT)
#define TCR_WRC_SHIFT 28        /* Watchdog Timer Reset Control */
#define TCR_WRC_MASK  (0x3 << TCR_WRC_SHIFT)
#define TCR_WIE       (1 << 27) /* Watchdog Timer Interrupt Enable */
#define TCR_DIE       (1 << 26) /* Decrementer Interrupt Enable */
#define TCR_FP_SHIFT  24        /* Fixed-Interval Timer Period */
#define TCR_FP_MASK   (0x3 << TCR_FP_SHIFT)
#define TCR_FIE       (1 << 23) /* Fixed-Interval Timer Interrupt Enable */
#define TCR_ARE       (1 << 22) /* Auto-Reload Enable */

/* Timer Control Register (e500 specific fields) */

#define TCR_E500_FPEXT_SHIFT 13 /* Fixed-Interval Timer Period Extension */
#define TCR_E500_FPEXT_MASK  (0xf << TCR_E500_FPEXT_SHIFT)
#define TCR_E500_WPEXT_SHIFT 17 /* Watchdog Timer Period Extension */
#define TCR_E500_WPEXT_MASK  (0xf << TCR_E500_WPEXT_SHIFT)

/* Timer Status Register  */

#define TSR_FIS       (1 << 26) /* Fixed-Interval Timer Interrupt Status */
#define TSR_DIS       (1 << 27) /* Decrementer Interrupt Status */
#define TSR_WRS_SHIFT 28        /* Watchdog Timer Reset Status */
#define TSR_WRS_MASK  (0x3 << TSR_WRS_SHIFT)
#define TSR_WIS       (1 << 30) /* Watchdog Timer Interrupt Status */
#define TSR_ENW       (1 << 31) /* Enable Next Watchdog Timer */

typedef struct booke_timer_t booke_timer_t;
struct booke_timer_t {

    uint64_t fit_next;
    struct QEMUTimer *fit_timer;

    uint64_t wdt_next;
    struct QEMUTimer *wdt_timer;

    uint32_t flags;
};

static void booke_update_irq(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    ppc_set_irq(cpu, PPC_INTERRUPT_DECR,
                (env->spr[SPR_BOOKE_TSR] & TSR_DIS
                 && env->spr[SPR_BOOKE_TCR] & TCR_DIE));

    ppc_set_irq(cpu, PPC_INTERRUPT_WDT,
                (env->spr[SPR_BOOKE_TSR] & TSR_WIS
                 && env->spr[SPR_BOOKE_TCR] & TCR_WIE));

    ppc_set_irq(cpu, PPC_INTERRUPT_FIT,
                (env->spr[SPR_BOOKE_TSR] & TSR_FIS
                 && env->spr[SPR_BOOKE_TCR] & TCR_FIE));
}

/* Return the location of the bit of time base at which the FIT will raise an
   interrupt */
static uint8_t booke_get_fit_target(CPUPPCState *env, ppc_tb_t *tb_env)
{
    uint8_t fp = (env->spr[SPR_BOOKE_TCR] & TCR_FP_MASK) >> TCR_FP_SHIFT;

    if (tb_env->flags & PPC_TIMER_E500) {
        /* e500 Fixed-interval timer period extension */
        uint32_t fpext = (env->spr[SPR_BOOKE_TCR] & TCR_E500_FPEXT_MASK)
            >> TCR_E500_FPEXT_SHIFT;
        fp = 63 - (fp | fpext << 2);
    } else {
        fp = env->fit_period[fp];
    }

    return fp;
}

/* Return the location of the bit of time base at which the WDT will raise an
   interrupt */
static uint8_t booke_get_wdt_target(CPUPPCState *env, ppc_tb_t *tb_env)
{
    uint8_t wp = (env->spr[SPR_BOOKE_TCR] & TCR_WP_MASK) >> TCR_WP_SHIFT;

    if (tb_env->flags & PPC_TIMER_E500) {
        /* e500 Watchdog timer period extension */
        uint32_t wpext = (env->spr[SPR_BOOKE_TCR] & TCR_E500_WPEXT_MASK)
            >> TCR_E500_WPEXT_SHIFT;
        wp = 63 - (wp | wpext << 2);
    } else {
        wp = env->wdt_period[wp];
    }

    return wp;
}

static void booke_update_fixed_timer(CPUPPCState         *env,
                                     uint8_t           target_bit,
                                     uint64_t          *next,
                                     struct QEMUTimer *timer)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t lapse;
    uint64_t tb;
    uint64_t period = 1 << (target_bit + 1);
    uint64_t now;

    now = qemu_get_clock_ns(vm_clock);
    tb  = cpu_ppc_get_tb(tb_env, now, tb_env->tb_offset);

    lapse = period - ((tb - (1 << target_bit)) & (period - 1));

    *next = now + muldiv64(lapse, get_ticks_per_sec(), tb_env->tb_freq);

    /* XXX: If expire time is now. We can't run the callback because we don't
     * have access to it. So we just set the timer one nanosecond later.
     */

    if (*next == now) {
        (*next)++;
    }

    qemu_mod_timer(timer, *next);
}

static void booke_decr_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    env->spr[SPR_BOOKE_TSR] |= TSR_DIS;
    booke_update_irq(cpu);

    if (env->spr[SPR_BOOKE_TCR] & TCR_ARE) {
        /* Auto Reload */
        cpu_ppc_store_decr(env, env->spr[SPR_BOOKE_DECAR]);
    }
}

static void booke_fit_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    ppc_tb_t *tb_env;
    booke_timer_t *booke_timer;

    tb_env = env->tb_env;
    booke_timer = tb_env->opaque;
    env->spr[SPR_BOOKE_TSR] |= TSR_FIS;

    booke_update_irq(cpu);

    booke_update_fixed_timer(env,
                             booke_get_fit_target(env, tb_env),
                             &booke_timer->fit_next,
                             booke_timer->fit_timer);
}

static void booke_wdt_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    ppc_tb_t *tb_env;
    booke_timer_t *booke_timer;

    tb_env = env->tb_env;
    booke_timer = tb_env->opaque;

    /* TODO: There's lots of complicated stuff to do here */

    booke_update_irq(cpu);

    booke_update_fixed_timer(env,
                             booke_get_wdt_target(env, tb_env),
                             &booke_timer->wdt_next,
                             booke_timer->wdt_timer);
}

void store_booke_tsr(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    env->spr[SPR_BOOKE_TSR] &= ~val;
    kvmppc_clear_tsr_bits(cpu, val);
    booke_update_irq(cpu);
}

void store_booke_tcr(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);
    ppc_tb_t *tb_env = env->tb_env;
    booke_timer_t *booke_timer = tb_env->opaque;

    tb_env = env->tb_env;
    env->spr[SPR_BOOKE_TCR] = val;
    kvmppc_set_tcr(cpu);

    booke_update_irq(cpu);

    booke_update_fixed_timer(env,
                             booke_get_fit_target(env, tb_env),
                             &booke_timer->fit_next,
                             booke_timer->fit_timer);

    booke_update_fixed_timer(env,
                             booke_get_wdt_target(env, tb_env),
                             &booke_timer->wdt_next,
                             booke_timer->wdt_timer);
}

static void ppc_booke_timer_reset_handle(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    store_booke_tcr(env, 0);
    store_booke_tsr(env, -1);
}

/*
 * This function will be called whenever the CPU state changes.
 * CPU states are defined "typedef enum RunState".
 * Regarding timer, When CPU state changes to running after debug halt
 * or similar cases which takes time then in between final watchdog
 * expiry happenes. This will cause exit to QEMU and configured watchdog
 * action will be taken. To avoid this we always clear the watchdog state when
 * state changes to running.
 */
static void cpu_state_change_handler(void *opaque, int running, RunState state)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    if (!running) {
        return;
    }

    /*
     * Clear watchdog interrupt condition by clearing TSR.
     */
    store_booke_tsr(env, TSR_ENW | TSR_WIS | TSR_WRS_MASK);
}

void ppc_booke_timers_init(PowerPCCPU *cpu, uint32_t freq, uint32_t flags)
{
    ppc_tb_t *tb_env;
    booke_timer_t *booke_timer;
    int ret = 0;

    tb_env      = g_malloc0(sizeof(ppc_tb_t));
    booke_timer = g_malloc0(sizeof(booke_timer_t));

    cpu->env.tb_env = tb_env;
    tb_env->flags = flags | PPC_TIMER_BOOKE | PPC_DECR_ZERO_TRIGGERED;

    tb_env->tb_freq    = freq;
    tb_env->decr_freq  = freq;
    tb_env->opaque     = booke_timer;
    tb_env->decr_timer = qemu_new_timer_ns(vm_clock, &booke_decr_cb, cpu);

    booke_timer->fit_timer =
        qemu_new_timer_ns(vm_clock, &booke_fit_cb, cpu);
    booke_timer->wdt_timer =
        qemu_new_timer_ns(vm_clock, &booke_wdt_cb, cpu);

    ret = kvmppc_booke_watchdog_enable(cpu);

    if (ret) {
        /* TODO: Start the QEMU emulated watchdog if not running on KVM.
         * Also start the QEMU emulated watchdog if KVM does not support
         * emulated watchdog or somehow it is not enabled (supported but
         * not enabled is though some bug and requires debugging :)).
         */
    }

    qemu_add_vm_change_state_handler(cpu_state_change_handler, cpu);

    qemu_register_reset(ppc_booke_timer_reset_handle, cpu);
}
