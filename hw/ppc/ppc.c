/*
 * QEMU generic PowerPC hardware System Emulator
 *
 * Copyright (c) 2003-2007 Jocelyn Mayer
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

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/ppc_e500.h"
#include "qemu/timer.h"
#include "sysemu/cpus.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "kvm_ppc.h"
#include "migration/vmstate.h"
#include "trace.h"

static void cpu_ppc_tb_stop (CPUPPCState *env);
static void cpu_ppc_tb_start (CPUPPCState *env);

void ppc_set_irq(PowerPCCPU *cpu, int n_IRQ, int level)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    unsigned int old_pending;
    bool locked = false;

    /* We may already have the BQL if coming from the reset path */
    if (!qemu_mutex_iothread_locked()) {
        locked = true;
        qemu_mutex_lock_iothread();
    }

    old_pending = env->pending_interrupts;

    if (level) {
        env->pending_interrupts |= 1 << n_IRQ;
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        env->pending_interrupts &= ~(1 << n_IRQ);
        if (env->pending_interrupts == 0) {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }

    if (old_pending != env->pending_interrupts) {
        kvmppc_set_interrupt(cpu, n_IRQ, level);
    }


    trace_ppc_irq_set_exit(env, n_IRQ, level, env->pending_interrupts,
                           CPU(cpu)->interrupt_request);

    if (locked) {
        qemu_mutex_unlock_iothread();
    }
}

/* PowerPC 6xx / 7xx internal IRQ controller */
static void ppc6xx_set_irq(void *opaque, int pin, int level)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int cur_level;

    trace_ppc_irq_set(env, pin, level);

    cur_level = (env->irq_input_state >> pin) & 1;
    /* Don't generate spurious events */
    if ((cur_level == 1 && level == 0) || (cur_level == 0 && level != 0)) {
        CPUState *cs = CPU(cpu);

        switch (pin) {
        case PPC6xx_INPUT_TBEN:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("time base", level);
            if (level) {
                cpu_ppc_tb_start(env);
            } else {
                cpu_ppc_tb_stop(env);
            }
            break;
        case PPC6xx_INPUT_INT:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("external IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_EXT, level);
            break;
        case PPC6xx_INPUT_SMI:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("SMI IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_SMI, level);
            break;
        case PPC6xx_INPUT_MCP:
            /* Negative edge sensitive */
            /* XXX: TODO: actual reaction may depends on HID0 status
             *            603/604/740/750: check HID0[EMCP]
             */
            if (cur_level == 1 && level == 0) {
                trace_ppc_irq_set_state("machine check", 1);
                ppc_set_irq(cpu, PPC_INTERRUPT_MCK, 1);
            }
            break;
        case PPC6xx_INPUT_CKSTP_IN:
            /* Level sensitive - active low */
            /* XXX: TODO: relay the signal to CKSTP_OUT pin */
            /* XXX: Note that the only way to restart the CPU is to reset it */
            if (level) {
                trace_ppc_irq_cpu("stop");
                cs->halted = 1;
            }
            break;
        case PPC6xx_INPUT_HRESET:
            /* Level sensitive - active low */
            if (level) {
                trace_ppc_irq_reset("CPU");
                cpu_interrupt(cs, CPU_INTERRUPT_RESET);
            }
            break;
        case PPC6xx_INPUT_SRESET:
            trace_ppc_irq_set_state("RESET IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_RESET, level);
            break;
        default:
            g_assert_not_reached();
        }
        if (level)
            env->irq_input_state |= 1 << pin;
        else
            env->irq_input_state &= ~(1 << pin);
    }
}

void ppc6xx_irq_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    env->irq_inputs = (void **)qemu_allocate_irqs(&ppc6xx_set_irq, cpu,
                                                  PPC6xx_INPUT_NB);
}

#if defined(TARGET_PPC64)
/* PowerPC 970 internal IRQ controller */
static void ppc970_set_irq(void *opaque, int pin, int level)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int cur_level;

    trace_ppc_irq_set(env, pin, level);

    cur_level = (env->irq_input_state >> pin) & 1;
    /* Don't generate spurious events */
    if ((cur_level == 1 && level == 0) || (cur_level == 0 && level != 0)) {
        CPUState *cs = CPU(cpu);

        switch (pin) {
        case PPC970_INPUT_INT:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("external IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_EXT, level);
            break;
        case PPC970_INPUT_THINT:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("SMI IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_THERM, level);
            break;
        case PPC970_INPUT_MCP:
            /* Negative edge sensitive */
            /* XXX: TODO: actual reaction may depends on HID0 status
             *            603/604/740/750: check HID0[EMCP]
             */
            if (cur_level == 1 && level == 0) {
                trace_ppc_irq_set_state("machine check", 1);
                ppc_set_irq(cpu, PPC_INTERRUPT_MCK, 1);
            }
            break;
        case PPC970_INPUT_CKSTP:
            /* Level sensitive - active low */
            /* XXX: TODO: relay the signal to CKSTP_OUT pin */
            if (level) {
                trace_ppc_irq_cpu("stop");
                cs->halted = 1;
            } else {
                trace_ppc_irq_cpu("restart");
                cs->halted = 0;
                qemu_cpu_kick(cs);
            }
            break;
        case PPC970_INPUT_HRESET:
            /* Level sensitive - active low */
            if (level) {
                cpu_interrupt(cs, CPU_INTERRUPT_RESET);
            }
            break;
        case PPC970_INPUT_SRESET:
            trace_ppc_irq_set_state("RESET IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_RESET, level);
            break;
        case PPC970_INPUT_TBEN:
            trace_ppc_irq_set_state("TBEN IRQ", level);
            /* XXX: TODO */
            break;
        default:
            g_assert_not_reached();
        }
        if (level)
            env->irq_input_state |= 1 << pin;
        else
            env->irq_input_state &= ~(1 << pin);
    }
}

void ppc970_irq_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    env->irq_inputs = (void **)qemu_allocate_irqs(&ppc970_set_irq, cpu,
                                                  PPC970_INPUT_NB);
}

/* POWER7 internal IRQ controller */
static void power7_set_irq(void *opaque, int pin, int level)
{
    PowerPCCPU *cpu = opaque;

    trace_ppc_irq_set(&cpu->env, pin, level);

    switch (pin) {
    case POWER7_INPUT_INT:
        /* Level sensitive - active high */
        trace_ppc_irq_set_state("external IRQ", level);
        ppc_set_irq(cpu, PPC_INTERRUPT_EXT, level);
        break;
    default:
        g_assert_not_reached();
    }
}

void ppcPOWER7_irq_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    env->irq_inputs = (void **)qemu_allocate_irqs(&power7_set_irq, cpu,
                                                  POWER7_INPUT_NB);
}

/* POWER9 internal IRQ controller */
static void power9_set_irq(void *opaque, int pin, int level)
{
    PowerPCCPU *cpu = opaque;

    trace_ppc_irq_set(&cpu->env, pin, level);

    switch (pin) {
    case POWER9_INPUT_INT:
        /* Level sensitive - active high */
        trace_ppc_irq_set_state("external IRQ", level);
        ppc_set_irq(cpu, PPC_INTERRUPT_EXT, level);
        break;
    case POWER9_INPUT_HINT:
        /* Level sensitive - active high */
        trace_ppc_irq_set_state("HV external IRQ", level);
        ppc_set_irq(cpu, PPC_INTERRUPT_HVIRT, level);
        break;
    default:
        g_assert_not_reached();
        return;
    }
}

void ppcPOWER9_irq_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    env->irq_inputs = (void **)qemu_allocate_irqs(&power9_set_irq, cpu,
                                                  POWER9_INPUT_NB);
}
#endif /* defined(TARGET_PPC64) */

void ppc40x_core_reset(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    target_ulong dbsr;

    qemu_log_mask(CPU_LOG_RESET, "Reset PowerPC core\n");
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_RESET);
    dbsr = env->spr[SPR_40x_DBSR];
    dbsr &= ~0x00000300;
    dbsr |= 0x00000100;
    env->spr[SPR_40x_DBSR] = dbsr;
}

void ppc40x_chip_reset(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    target_ulong dbsr;

    qemu_log_mask(CPU_LOG_RESET, "Reset PowerPC chip\n");
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_RESET);
    /* XXX: TODO reset all internal peripherals */
    dbsr = env->spr[SPR_40x_DBSR];
    dbsr &= ~0x00000300;
    dbsr |= 0x00000200;
    env->spr[SPR_40x_DBSR] = dbsr;
}

void ppc40x_system_reset(PowerPCCPU *cpu)
{
    qemu_log_mask(CPU_LOG_RESET, "Reset PowerPC system\n");
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}

void store_40x_dbcr0(CPUPPCState *env, uint32_t val)
{
    PowerPCCPU *cpu = env_archcpu(env);

    qemu_mutex_lock_iothread();

    switch ((val >> 28) & 0x3) {
    case 0x0:
        /* No action */
        break;
    case 0x1:
        /* Core reset */
        ppc40x_core_reset(cpu);
        break;
    case 0x2:
        /* Chip reset */
        ppc40x_chip_reset(cpu);
        break;
    case 0x3:
        /* System reset */
        ppc40x_system_reset(cpu);
        break;
    }

    qemu_mutex_unlock_iothread();
}

/* PowerPC 40x internal IRQ controller */
static void ppc40x_set_irq(void *opaque, int pin, int level)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int cur_level;

    trace_ppc_irq_set(env, pin, level);

    cur_level = (env->irq_input_state >> pin) & 1;
    /* Don't generate spurious events */
    if ((cur_level == 1 && level == 0) || (cur_level == 0 && level != 0)) {
        CPUState *cs = CPU(cpu);

        switch (pin) {
        case PPC40x_INPUT_RESET_SYS:
            if (level) {
                trace_ppc_irq_reset("system");
                ppc40x_system_reset(cpu);
            }
            break;
        case PPC40x_INPUT_RESET_CHIP:
            if (level) {
                trace_ppc_irq_reset("chip");
                ppc40x_chip_reset(cpu);
            }
            break;
        case PPC40x_INPUT_RESET_CORE:
            /* XXX: TODO: update DBSR[MRR] */
            if (level) {
                trace_ppc_irq_reset("core");
                ppc40x_core_reset(cpu);
            }
            break;
        case PPC40x_INPUT_CINT:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("critical IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_CEXT, level);
            break;
        case PPC40x_INPUT_INT:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("external IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_EXT, level);
            break;
        case PPC40x_INPUT_HALT:
            /* Level sensitive - active low */
            if (level) {
                trace_ppc_irq_cpu("stop");
                cs->halted = 1;
            } else {
                trace_ppc_irq_cpu("restart");
                cs->halted = 0;
                qemu_cpu_kick(cs);
            }
            break;
        case PPC40x_INPUT_DEBUG:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("debug pin", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_DEBUG, level);
            break;
        default:
            g_assert_not_reached();
        }
        if (level)
            env->irq_input_state |= 1 << pin;
        else
            env->irq_input_state &= ~(1 << pin);
    }
}

void ppc40x_irq_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    env->irq_inputs = (void **)qemu_allocate_irqs(&ppc40x_set_irq,
                                                  cpu, PPC40x_INPUT_NB);
}

/* PowerPC E500 internal IRQ controller */
static void ppce500_set_irq(void *opaque, int pin, int level)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int cur_level;

    trace_ppc_irq_set(env, pin, level);

    cur_level = (env->irq_input_state >> pin) & 1;
    /* Don't generate spurious events */
    if ((cur_level == 1 && level == 0) || (cur_level == 0 && level != 0)) {
        switch (pin) {
        case PPCE500_INPUT_MCK:
            if (level) {
                trace_ppc_irq_reset("system");
                qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            }
            break;
        case PPCE500_INPUT_RESET_CORE:
            if (level) {
                trace_ppc_irq_reset("core");
                ppc_set_irq(cpu, PPC_INTERRUPT_MCK, level);
            }
            break;
        case PPCE500_INPUT_CINT:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("critical IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_CEXT, level);
            break;
        case PPCE500_INPUT_INT:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("core IRQ", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_EXT, level);
            break;
        case PPCE500_INPUT_DEBUG:
            /* Level sensitive - active high */
            trace_ppc_irq_set_state("debug pin", level);
            ppc_set_irq(cpu, PPC_INTERRUPT_DEBUG, level);
            break;
        default:
            g_assert_not_reached();
        }
        if (level)
            env->irq_input_state |= 1 << pin;
        else
            env->irq_input_state &= ~(1 << pin);
    }
}

void ppce500_irq_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    env->irq_inputs = (void **)qemu_allocate_irqs(&ppce500_set_irq,
                                                  cpu, PPCE500_INPUT_NB);
}

/* Enable or Disable the E500 EPR capability */
void ppce500_set_mpic_proxy(bool enabled)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        cpu->env.mpic_proxy = enabled;
        if (kvm_enabled()) {
            kvmppc_set_mpic_proxy(cpu, enabled);
        }
    }
}

/*****************************************************************************/
/* PowerPC time base and decrementer emulation */

uint64_t cpu_ppc_get_tb(ppc_tb_t *tb_env, uint64_t vmclk, int64_t tb_offset)
{
    /* TB time in tb periods */
    return muldiv64(vmclk, tb_env->tb_freq, NANOSECONDS_PER_SECOND) + tb_offset;
}

uint64_t cpu_ppc_load_tbl (CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    if (kvm_enabled()) {
        return env->spr[SPR_TBL];
    }

    tb = cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tb_env->tb_offset);
    trace_ppc_tb_load(tb);

    return tb;
}

static inline uint32_t _cpu_ppc_load_tbu(CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tb_env->tb_offset);
    trace_ppc_tb_load(tb);

    return tb >> 32;
}

uint32_t cpu_ppc_load_tbu (CPUPPCState *env)
{
    if (kvm_enabled()) {
        return env->spr[SPR_TBU];
    }

    return _cpu_ppc_load_tbu(env);
}

static inline void cpu_ppc_store_tb(ppc_tb_t *tb_env, uint64_t vmclk,
                                    int64_t *tb_offsetp, uint64_t value)
{
    *tb_offsetp = value -
        muldiv64(vmclk, tb_env->tb_freq, NANOSECONDS_PER_SECOND);

    trace_ppc_tb_store(value, *tb_offsetp);
}

void cpu_ppc_store_tbl (CPUPPCState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tb_env->tb_offset);
    tb &= 0xFFFFFFFF00000000ULL;
    cpu_ppc_store_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                     &tb_env->tb_offset, tb | (uint64_t)value);
}

static inline void _cpu_ppc_store_tbu(CPUPPCState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tb_env->tb_offset);
    tb &= 0x00000000FFFFFFFFULL;
    cpu_ppc_store_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                     &tb_env->tb_offset, ((uint64_t)value << 32) | tb);
}

void cpu_ppc_store_tbu (CPUPPCState *env, uint32_t value)
{
    _cpu_ppc_store_tbu(env, value);
}

uint64_t cpu_ppc_load_atbl (CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tb_env->atb_offset);
    trace_ppc_tb_load(tb);

    return tb;
}

uint32_t cpu_ppc_load_atbu (CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tb_env->atb_offset);
    trace_ppc_tb_load(tb);

    return tb >> 32;
}

void cpu_ppc_store_atbl (CPUPPCState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tb_env->atb_offset);
    tb &= 0xFFFFFFFF00000000ULL;
    cpu_ppc_store_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                     &tb_env->atb_offset, tb | (uint64_t)value);
}

void cpu_ppc_store_atbu (CPUPPCState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), tb_env->atb_offset);
    tb &= 0x00000000FFFFFFFFULL;
    cpu_ppc_store_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                     &tb_env->atb_offset, ((uint64_t)value << 32) | tb);
}

uint64_t cpu_ppc_load_vtb(CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;

    return cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                          tb_env->vtb_offset);
}

void cpu_ppc_store_vtb(CPUPPCState *env, uint64_t value)
{
    ppc_tb_t *tb_env = env->tb_env;

    cpu_ppc_store_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                     &tb_env->vtb_offset, value);
}

void cpu_ppc_store_tbu40(CPUPPCState *env, uint64_t value)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                        tb_env->tb_offset);
    tb &= 0xFFFFFFUL;
    tb |= (value & ~0xFFFFFFUL);
    cpu_ppc_store_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                     &tb_env->tb_offset, tb);
}

static void cpu_ppc_tb_stop (CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb, atb, vmclk;

    /* If the time base is already frozen, do nothing */
    if (tb_env->tb_freq != 0) {
        vmclk = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        /* Get the time base */
        tb = cpu_ppc_get_tb(tb_env, vmclk, tb_env->tb_offset);
        /* Get the alternate time base */
        atb = cpu_ppc_get_tb(tb_env, vmclk, tb_env->atb_offset);
        /* Store the time base value (ie compute the current offset) */
        cpu_ppc_store_tb(tb_env, vmclk, &tb_env->tb_offset, tb);
        /* Store the alternate time base value (compute the current offset) */
        cpu_ppc_store_tb(tb_env, vmclk, &tb_env->atb_offset, atb);
        /* Set the time base frequency to zero */
        tb_env->tb_freq = 0;
        /* Now, the time bases are frozen to tb_offset / atb_offset value */
    }
}

static void cpu_ppc_tb_start (CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb, atb, vmclk;

    /* If the time base is not frozen, do nothing */
    if (tb_env->tb_freq == 0) {
        vmclk = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        /* Get the time base from tb_offset */
        tb = tb_env->tb_offset;
        /* Get the alternate time base from atb_offset */
        atb = tb_env->atb_offset;
        /* Restore the tb frequency from the decrementer frequency */
        tb_env->tb_freq = tb_env->decr_freq;
        /* Store the time base value */
        cpu_ppc_store_tb(tb_env, vmclk, &tb_env->tb_offset, tb);
        /* Store the alternate time base value */
        cpu_ppc_store_tb(tb_env, vmclk, &tb_env->atb_offset, atb);
    }
}

bool ppc_decr_clear_on_delivery(CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    int flags = PPC_DECR_UNDERFLOW_TRIGGERED | PPC_DECR_UNDERFLOW_LEVEL;
    return ((tb_env->flags & flags) == PPC_DECR_UNDERFLOW_TRIGGERED);
}

static inline int64_t _cpu_ppc_load_decr(CPUPPCState *env, uint64_t next)
{
    ppc_tb_t *tb_env = env->tb_env;
    int64_t decr, diff;

    diff = next - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (diff >= 0) {
        decr = muldiv64(diff, tb_env->decr_freq, NANOSECONDS_PER_SECOND);
    } else if (tb_env->flags & PPC_TIMER_BOOKE) {
        decr = 0;
    }  else {
        decr = -muldiv64(-diff, tb_env->decr_freq, NANOSECONDS_PER_SECOND);
    }
    trace_ppc_decr_load(decr);

    return decr;
}

target_ulong cpu_ppc_load_decr(CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t decr;

    if (kvm_enabled()) {
        return env->spr[SPR_DECR];
    }

    decr = _cpu_ppc_load_decr(env, tb_env->decr_next);

    /*
     * If large decrementer is enabled then the decrementer is signed extened
     * to 64 bits, otherwise it is a 32 bit value.
     */
    if (env->spr[SPR_LPCR] & LPCR_LD) {
        return decr;
    }
    return (uint32_t) decr;
}

target_ulong cpu_ppc_load_hdecr(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t hdecr;

    hdecr =  _cpu_ppc_load_decr(env, tb_env->hdecr_next);

    /*
     * If we have a large decrementer (POWER9 or later) then hdecr is sign
     * extended to 64 bits, otherwise it is 32 bits.
     */
    if (pcc->lrg_decr_bits > 32) {
        return hdecr;
    }
    return (uint32_t) hdecr;
}

uint64_t cpu_ppc_load_purr (CPUPPCState *env)
{
    ppc_tb_t *tb_env = env->tb_env;

    return cpu_ppc_get_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                          tb_env->purr_offset);
}

/* When decrementer expires,
 * all we need to do is generate or queue a CPU exception
 */
static inline void cpu_ppc_decr_excp(PowerPCCPU *cpu)
{
    /* Raise it */
    trace_ppc_decr_excp("raise");
    ppc_set_irq(cpu, PPC_INTERRUPT_DECR, 1);
}

static inline void cpu_ppc_decr_lower(PowerPCCPU *cpu)
{
    ppc_set_irq(cpu, PPC_INTERRUPT_DECR, 0);
}

static inline void cpu_ppc_hdecr_excp(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    /* Raise it */
    trace_ppc_decr_excp("raise HV");

    /* The architecture specifies that we don't deliver HDEC
     * interrupts in a PM state. Not only they don't cause a
     * wakeup but they also get effectively discarded.
     */
    if (!env->resume_as_sreset) {
        ppc_set_irq(cpu, PPC_INTERRUPT_HDECR, 1);
    }
}

static inline void cpu_ppc_hdecr_lower(PowerPCCPU *cpu)
{
    ppc_set_irq(cpu, PPC_INTERRUPT_HDECR, 0);
}

static void __cpu_ppc_store_decr(PowerPCCPU *cpu, uint64_t *nextp,
                                 QEMUTimer *timer,
                                 void (*raise_excp)(void *),
                                 void (*lower_excp)(PowerPCCPU *),
                                 target_ulong decr, target_ulong value,
                                 int nr_bits)
{
    CPUPPCState *env = &cpu->env;
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t now, next;
    int64_t signed_value;
    int64_t signed_decr;

    /* Truncate value to decr_width and sign extend for simplicity */
    signed_value = sextract64(value, 0, nr_bits);
    signed_decr = sextract64(decr, 0, nr_bits);

    trace_ppc_decr_store(nr_bits, decr, value);

    if (kvm_enabled()) {
        /* KVM handles decrementer exceptions, we don't need our own timer */
        return;
    }

    /*
     * Going from 2 -> 1, 1 -> 0 or 0 -> -1 is the event to generate a DEC
     * interrupt.
     *
     * If we get a really small DEC value, we can assume that by the time we
     * handled it we should inject an interrupt already.
     *
     * On MSB level based DEC implementations the MSB always means the interrupt
     * is pending, so raise it on those.
     *
     * On MSB edge based DEC implementations the MSB going from 0 -> 1 triggers
     * an edge interrupt, so raise it here too.
     */
    if ((value < 3) ||
        ((tb_env->flags & PPC_DECR_UNDERFLOW_LEVEL) && signed_value < 0) ||
        ((tb_env->flags & PPC_DECR_UNDERFLOW_TRIGGERED) && signed_value < 0
          && signed_decr >= 0)) {
        (*raise_excp)(cpu);
        return;
    }

    /* On MSB level based systems a 0 for the MSB stops interrupt delivery */
    if (signed_value >= 0 && (tb_env->flags & PPC_DECR_UNDERFLOW_LEVEL)) {
        (*lower_excp)(cpu);
    }

    /* Calculate the next timer event */
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    next = now + muldiv64(value, NANOSECONDS_PER_SECOND, tb_env->decr_freq);
    *nextp = next;

    /* Adjust timer */
    timer_mod(timer, next);
}

static inline void _cpu_ppc_store_decr(PowerPCCPU *cpu, target_ulong decr,
                                       target_ulong value, int nr_bits)
{
    ppc_tb_t *tb_env = cpu->env.tb_env;

    __cpu_ppc_store_decr(cpu, &tb_env->decr_next, tb_env->decr_timer,
                         tb_env->decr_timer->cb, &cpu_ppc_decr_lower, decr,
                         value, nr_bits);
}

void cpu_ppc_store_decr(CPUPPCState *env, target_ulong value)
{
    PowerPCCPU *cpu = env_archcpu(env);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    int nr_bits = 32;

    if (env->spr[SPR_LPCR] & LPCR_LD) {
        nr_bits = pcc->lrg_decr_bits;
    }

    _cpu_ppc_store_decr(cpu, cpu_ppc_load_decr(env), value, nr_bits);
}

static void cpu_ppc_decr_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_ppc_decr_excp(cpu);
}

static inline void _cpu_ppc_store_hdecr(PowerPCCPU *cpu, target_ulong hdecr,
                                        target_ulong value, int nr_bits)
{
    ppc_tb_t *tb_env = cpu->env.tb_env;

    if (tb_env->hdecr_timer != NULL) {
        __cpu_ppc_store_decr(cpu, &tb_env->hdecr_next, tb_env->hdecr_timer,
                             tb_env->hdecr_timer->cb, &cpu_ppc_hdecr_lower,
                             hdecr, value, nr_bits);
    }
}

void cpu_ppc_store_hdecr(CPUPPCState *env, target_ulong value)
{
    PowerPCCPU *cpu = env_archcpu(env);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    _cpu_ppc_store_hdecr(cpu, cpu_ppc_load_hdecr(env), value,
                         pcc->lrg_decr_bits);
}

static void cpu_ppc_hdecr_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_ppc_hdecr_excp(cpu);
}

void cpu_ppc_store_purr(CPUPPCState *env, uint64_t value)
{
    ppc_tb_t *tb_env = env->tb_env;

    cpu_ppc_store_tb(tb_env, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                     &tb_env->purr_offset, value);
}

static void cpu_ppc_set_tb_clk (void *opaque, uint32_t freq)
{
    CPUPPCState *env = opaque;
    PowerPCCPU *cpu = env_archcpu(env);
    ppc_tb_t *tb_env = env->tb_env;

    tb_env->tb_freq = freq;
    tb_env->decr_freq = freq;
    /* There is a bug in Linux 2.4 kernels:
     * if a decrementer exception is pending when it enables msr_ee at startup,
     * it's not ready to handle it...
     */
    _cpu_ppc_store_decr(cpu, 0xFFFFFFFF, 0xFFFFFFFF, 32);
    _cpu_ppc_store_hdecr(cpu, 0xFFFFFFFF, 0xFFFFFFFF, 32);
    cpu_ppc_store_purr(env, 0x0000000000000000ULL);
}

static void timebase_save(PPCTimebase *tb)
{
    uint64_t ticks = cpu_get_host_ticks();
    PowerPCCPU *first_ppc_cpu = POWERPC_CPU(first_cpu);

    if (!first_ppc_cpu->env.tb_env) {
        error_report("No timebase object");
        return;
    }

    /* not used anymore, we keep it for compatibility */
    tb->time_of_the_day_ns = qemu_clock_get_ns(QEMU_CLOCK_HOST);
    /*
     * tb_offset is only expected to be changed by QEMU so
     * there is no need to update it from KVM here
     */
    tb->guest_timebase = ticks + first_ppc_cpu->env.tb_env->tb_offset;

    tb->runstate_paused =
        runstate_check(RUN_STATE_PAUSED) || runstate_check(RUN_STATE_SAVE_VM);
}

static void timebase_load(PPCTimebase *tb)
{
    CPUState *cpu;
    PowerPCCPU *first_ppc_cpu = POWERPC_CPU(first_cpu);
    int64_t tb_off_adj, tb_off;
    unsigned long freq;

    if (!first_ppc_cpu->env.tb_env) {
        error_report("No timebase object");
        return;
    }

    freq = first_ppc_cpu->env.tb_env->tb_freq;

    tb_off_adj = tb->guest_timebase - cpu_get_host_ticks();

    tb_off = first_ppc_cpu->env.tb_env->tb_offset;
    trace_ppc_tb_adjust(tb_off, tb_off_adj, tb_off_adj - tb_off,
                        (tb_off_adj - tb_off) / freq);

    /* Set new offset to all CPUs */
    CPU_FOREACH(cpu) {
        PowerPCCPU *pcpu = POWERPC_CPU(cpu);
        pcpu->env.tb_env->tb_offset = tb_off_adj;
        kvmppc_set_reg_tb_offset(pcpu, pcpu->env.tb_env->tb_offset);
    }
}

void cpu_ppc_clock_vm_state_change(void *opaque, bool running,
                                   RunState state)
{
    PPCTimebase *tb = opaque;

    if (running) {
        timebase_load(tb);
    } else {
        timebase_save(tb);
    }
}

/*
 * When migrating a running guest, read the clock just
 * before migration, so that the guest clock counts
 * during the events between:
 *
 *  * vm_stop()
 *  *
 *  * pre_save()
 *
 *  This reduces clock difference on migration from 5s
 *  to 0.1s (when max_downtime == 5s), because sending the
 *  final pages of memory (which happens between vm_stop()
 *  and pre_save()) takes max_downtime.
 */
static int timebase_pre_save(void *opaque)
{
    PPCTimebase *tb = opaque;

    /* guest_timebase won't be overridden in case of paused guest or savevm */
    if (!tb->runstate_paused) {
        timebase_save(tb);
    }

    return 0;
}

const VMStateDescription vmstate_ppc_timebase = {
    .name = "timebase",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = timebase_pre_save,
    .fields      = (VMStateField []) {
        VMSTATE_UINT64(guest_timebase, PPCTimebase),
        VMSTATE_INT64(time_of_the_day_ns, PPCTimebase),
        VMSTATE_END_OF_LIST()
    },
};

/* Set up (once) timebase frequency (in Hz) */
clk_setup_cb cpu_ppc_tb_init (CPUPPCState *env, uint32_t freq)
{
    PowerPCCPU *cpu = env_archcpu(env);
    ppc_tb_t *tb_env;

    tb_env = g_new0(ppc_tb_t, 1);
    env->tb_env = tb_env;
    tb_env->flags = PPC_DECR_UNDERFLOW_TRIGGERED;
    if (is_book3s_arch2x(env)) {
        /* All Book3S 64bit CPUs implement level based DEC logic */
        tb_env->flags |= PPC_DECR_UNDERFLOW_LEVEL;
    }
    /* Create new timer */
    tb_env->decr_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &cpu_ppc_decr_cb, cpu);
    if (env->has_hv_mode && !cpu->vhyp) {
        tb_env->hdecr_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &cpu_ppc_hdecr_cb,
                                                cpu);
    } else {
        tb_env->hdecr_timer = NULL;
    }
    cpu_ppc_set_tb_clk(env, freq);

    return &cpu_ppc_set_tb_clk;
}

void cpu_ppc_tb_free(CPUPPCState *env)
{
    timer_free(env->tb_env->decr_timer);
    timer_free(env->tb_env->hdecr_timer);
    g_free(env->tb_env);
}

/* cpu_ppc_hdecr_init may be used if the timer is not used by HDEC emulation */
void cpu_ppc_hdecr_init(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);

    assert(env->tb_env->hdecr_timer == NULL);

    env->tb_env->hdecr_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                            &cpu_ppc_hdecr_cb, cpu);
}

void cpu_ppc_hdecr_exit(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);

    timer_free(env->tb_env->hdecr_timer);
    env->tb_env->hdecr_timer = NULL;

    cpu_ppc_hdecr_lower(cpu);
}

/*****************************************************************************/
/* PowerPC 40x timers */

/* PIT, FIT & WDT */
typedef struct ppc40x_timer_t ppc40x_timer_t;
struct ppc40x_timer_t {
    uint64_t pit_reload;  /* PIT auto-reload value        */
    uint64_t fit_next;    /* Tick for next FIT interrupt  */
    QEMUTimer *fit_timer;
    uint64_t wdt_next;    /* Tick for next WDT interrupt  */
    QEMUTimer *wdt_timer;

    /* 405 have the PIT, 440 have a DECR.  */
    unsigned int decr_excp;
};

/* Fixed interval timer */
static void cpu_4xx_fit_cb (void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;
    uint64_t now, next;

    tb_env = env->tb_env;
    ppc40x_timer = tb_env->opaque;
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    switch ((env->spr[SPR_40x_TCR] >> 24) & 0x3) {
    case 0:
        next = 1 << 9;
        break;
    case 1:
        next = 1 << 13;
        break;
    case 2:
        next = 1 << 17;
        break;
    case 3:
        next = 1 << 21;
        break;
    default:
        /* Cannot occur, but makes gcc happy */
        return;
    }
    next = now + muldiv64(next, NANOSECONDS_PER_SECOND, tb_env->tb_freq);
    if (next == now)
        next++;
    timer_mod(ppc40x_timer->fit_timer, next);
    env->spr[SPR_40x_TSR] |= 1 << 26;
    if ((env->spr[SPR_40x_TCR] >> 23) & 0x1) {
        ppc_set_irq(cpu, PPC_INTERRUPT_FIT, 1);
    }
    trace_ppc4xx_fit((int)((env->spr[SPR_40x_TCR] >> 23) & 0x1),
                         env->spr[SPR_40x_TCR], env->spr[SPR_40x_TSR]);
}

/* Programmable interval timer */
static void start_stop_pit (CPUPPCState *env, ppc_tb_t *tb_env, int is_excp)
{
    ppc40x_timer_t *ppc40x_timer;
    uint64_t now, next;

    ppc40x_timer = tb_env->opaque;
    if (ppc40x_timer->pit_reload <= 1 ||
        !((env->spr[SPR_40x_TCR] >> 26) & 0x1) ||
        (is_excp && !((env->spr[SPR_40x_TCR] >> 22) & 0x1))) {
        /* Stop PIT */
        trace_ppc4xx_pit_stop();
        timer_del(tb_env->decr_timer);
    } else {
        trace_ppc4xx_pit_start(ppc40x_timer->pit_reload);
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        next = now + muldiv64(ppc40x_timer->pit_reload,
                              NANOSECONDS_PER_SECOND, tb_env->decr_freq);
        if (is_excp)
            next += tb_env->decr_next - now;
        if (next == now)
            next++;
        timer_mod(tb_env->decr_timer, next);
        tb_env->decr_next = next;
    }
}

static void cpu_4xx_pit_cb (void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;

    tb_env = env->tb_env;
    ppc40x_timer = tb_env->opaque;
    env->spr[SPR_40x_TSR] |= 1 << 27;
    if ((env->spr[SPR_40x_TCR] >> 26) & 0x1) {
        ppc_set_irq(cpu, ppc40x_timer->decr_excp, 1);
    }
    start_stop_pit(env, tb_env, 1);
    trace_ppc4xx_pit((int)((env->spr[SPR_40x_TCR] >> 22) & 0x1),
           (int)((env->spr[SPR_40x_TCR] >> 26) & 0x1),
           env->spr[SPR_40x_TCR], env->spr[SPR_40x_TSR],
           ppc40x_timer->pit_reload);
}

/* Watchdog timer */
static void cpu_4xx_wdt_cb (void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;
    uint64_t now, next;

    tb_env = env->tb_env;
    ppc40x_timer = tb_env->opaque;
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    switch ((env->spr[SPR_40x_TCR] >> 30) & 0x3) {
    case 0:
        next = 1 << 17;
        break;
    case 1:
        next = 1 << 21;
        break;
    case 2:
        next = 1 << 25;
        break;
    case 3:
        next = 1 << 29;
        break;
    default:
        /* Cannot occur, but makes gcc happy */
        return;
    }
    next = now + muldiv64(next, NANOSECONDS_PER_SECOND, tb_env->decr_freq);
    if (next == now)
        next++;
    trace_ppc4xx_wdt(env->spr[SPR_40x_TCR], env->spr[SPR_40x_TSR]);
    switch ((env->spr[SPR_40x_TSR] >> 30) & 0x3) {
    case 0x0:
    case 0x1:
        timer_mod(ppc40x_timer->wdt_timer, next);
        ppc40x_timer->wdt_next = next;
        env->spr[SPR_40x_TSR] |= 1U << 31;
        break;
    case 0x2:
        timer_mod(ppc40x_timer->wdt_timer, next);
        ppc40x_timer->wdt_next = next;
        env->spr[SPR_40x_TSR] |= 1 << 30;
        if ((env->spr[SPR_40x_TCR] >> 27) & 0x1) {
            ppc_set_irq(cpu, PPC_INTERRUPT_WDT, 1);
        }
        break;
    case 0x3:
        env->spr[SPR_40x_TSR] &= ~0x30000000;
        env->spr[SPR_40x_TSR] |= env->spr[SPR_40x_TCR] & 0x30000000;
        switch ((env->spr[SPR_40x_TCR] >> 28) & 0x3) {
        case 0x0:
            /* No reset */
            break;
        case 0x1: /* Core reset */
            ppc40x_core_reset(cpu);
            break;
        case 0x2: /* Chip reset */
            ppc40x_chip_reset(cpu);
            break;
        case 0x3: /* System reset */
            ppc40x_system_reset(cpu);
            break;
        }
    }
}

void store_40x_pit (CPUPPCState *env, target_ulong val)
{
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;

    tb_env = env->tb_env;
    ppc40x_timer = tb_env->opaque;
    trace_ppc40x_store_pit(val);
    ppc40x_timer->pit_reload = val;
    start_stop_pit(env, tb_env, 0);
}

target_ulong load_40x_pit (CPUPPCState *env)
{
    return cpu_ppc_load_decr(env);
}

void store_40x_tsr(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = env_archcpu(env);

    trace_ppc40x_store_tcr(val);

    env->spr[SPR_40x_TSR] &= ~(val & 0xFC000000);
    if (val & 0x80000000) {
        ppc_set_irq(cpu, PPC_INTERRUPT_PIT, 0);
    }
}

void store_40x_tcr(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = env_archcpu(env);
    ppc_tb_t *tb_env;

    trace_ppc40x_store_tsr(val);

    tb_env = env->tb_env;
    env->spr[SPR_40x_TCR] = val & 0xFFC00000;
    start_stop_pit(env, tb_env, 1);
    cpu_4xx_wdt_cb(cpu);
}

static void ppc_40x_set_tb_clk (void *opaque, uint32_t freq)
{
    CPUPPCState *env = opaque;
    ppc_tb_t *tb_env = env->tb_env;

    trace_ppc40x_set_tb_clk(freq);
    tb_env->tb_freq = freq;
    tb_env->decr_freq = freq;
    /* XXX: we should also update all timers */
}

clk_setup_cb ppc_40x_timers_init (CPUPPCState *env, uint32_t freq,
                                  unsigned int decr_excp)
{
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;
    PowerPCCPU *cpu = env_archcpu(env);

    trace_ppc40x_timers_init(freq);

    tb_env = g_new0(ppc_tb_t, 1);
    ppc40x_timer = g_new0(ppc40x_timer_t, 1);

    env->tb_env = tb_env;
    tb_env->flags = PPC_DECR_UNDERFLOW_TRIGGERED;
    tb_env->tb_freq = freq;
    tb_env->decr_freq = freq;
    tb_env->opaque = ppc40x_timer;

    /* We use decr timer for PIT */
    tb_env->decr_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &cpu_4xx_pit_cb, cpu);
    ppc40x_timer->fit_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, &cpu_4xx_fit_cb, cpu);
    ppc40x_timer->wdt_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, &cpu_4xx_wdt_cb, cpu);
    ppc40x_timer->decr_excp = decr_excp;

    return &ppc_40x_set_tb_clk;
}

/*****************************************************************************/
/* Embedded PowerPC Device Control Registers */
typedef struct ppc_dcrn_t ppc_dcrn_t;
struct ppc_dcrn_t {
    dcr_read_cb dcr_read;
    dcr_write_cb dcr_write;
    void *opaque;
};

/* XXX: on 460, DCR addresses are 32 bits wide,
 *      using DCRIPR to get the 22 upper bits of the DCR address
 */
#define DCRN_NB 1024
struct ppc_dcr_t {
    ppc_dcrn_t dcrn[DCRN_NB];
    int (*read_error)(int dcrn);
    int (*write_error)(int dcrn);
};

int ppc_dcr_read (ppc_dcr_t *dcr_env, int dcrn, uint32_t *valp)
{
    ppc_dcrn_t *dcr;

    if (dcrn < 0 || dcrn >= DCRN_NB)
        goto error;
    dcr = &dcr_env->dcrn[dcrn];
    if (dcr->dcr_read == NULL)
        goto error;
    *valp = (*dcr->dcr_read)(dcr->opaque, dcrn);
    trace_ppc_dcr_read(dcrn, *valp);

    return 0;

 error:
    if (dcr_env->read_error != NULL)
        return (*dcr_env->read_error)(dcrn);

    return -1;
}

int ppc_dcr_write (ppc_dcr_t *dcr_env, int dcrn, uint32_t val)
{
    ppc_dcrn_t *dcr;

    if (dcrn < 0 || dcrn >= DCRN_NB)
        goto error;
    dcr = &dcr_env->dcrn[dcrn];
    if (dcr->dcr_write == NULL)
        goto error;
    trace_ppc_dcr_write(dcrn, val);
    (*dcr->dcr_write)(dcr->opaque, dcrn, val);

    return 0;

 error:
    if (dcr_env->write_error != NULL)
        return (*dcr_env->write_error)(dcrn);

    return -1;
}

int ppc_dcr_register (CPUPPCState *env, int dcrn, void *opaque,
                      dcr_read_cb dcr_read, dcr_write_cb dcr_write)
{
    ppc_dcr_t *dcr_env;
    ppc_dcrn_t *dcr;

    dcr_env = env->dcr_env;
    if (dcr_env == NULL)
        return -1;
    if (dcrn < 0 || dcrn >= DCRN_NB)
        return -1;
    dcr = &dcr_env->dcrn[dcrn];
    if (dcr->opaque != NULL ||
        dcr->dcr_read != NULL ||
        dcr->dcr_write != NULL)
        return -1;
    dcr->opaque = opaque;
    dcr->dcr_read = dcr_read;
    dcr->dcr_write = dcr_write;

    return 0;
}

int ppc_dcr_init (CPUPPCState *env, int (*read_error)(int dcrn),
                  int (*write_error)(int dcrn))
{
    ppc_dcr_t *dcr_env;

    dcr_env = g_new0(ppc_dcr_t, 1);
    dcr_env->read_error = read_error;
    dcr_env->write_error = write_error;
    env->dcr_env = dcr_env;

    return 0;
}

/*****************************************************************************/

int ppc_cpu_pir(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    return env->spr_cb[SPR_PIR].default_value;
}

PowerPCCPU *ppc_get_vcpu_by_pir(int pir)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        if (ppc_cpu_pir(cpu) == pir) {
            return cpu;
        }
    }

    return NULL;
}

void ppc_irq_reset(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    env->irq_input_state = 0;
    kvmppc_set_interrupt(cpu, PPC_INTERRUPT_EXT, 0);
}
