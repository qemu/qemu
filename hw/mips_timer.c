#include "hw.h"
#include "mips.h"
#include "qemu-timer.h"

#define TIMER_FREQ	100 * 1000 * 1000

/* XXX: do not use a global */
uint32_t cpu_mips_get_random (CPUState *env)
{
    static uint32_t seed = 0;
    uint32_t idx;
    seed = seed * 314159 + 1;
    idx = (seed >> 16) % (env->tlb->nb_tlb - env->CP0_Wired) + env->CP0_Wired;
    return idx;
}

/* MIPS R4K timer */
uint32_t cpu_mips_get_count (CPUState *env)
{
    if (env->CP0_Cause & (1 << CP0Ca_DC))
        return env->CP0_Count;
    else
        return env->CP0_Count +
            (uint32_t)muldiv64(qemu_get_clock(vm_clock),
                               TIMER_FREQ, ticks_per_sec);
}

static void cpu_mips_timer_update(CPUState *env)
{
    uint64_t now, next;
    uint32_t wait;

    now = qemu_get_clock(vm_clock);
    wait = env->CP0_Compare - env->CP0_Count -
	    (uint32_t)muldiv64(now, TIMER_FREQ, ticks_per_sec);
    next = now + muldiv64(wait, ticks_per_sec, TIMER_FREQ);
    qemu_mod_timer(env->timer, next);
}

void cpu_mips_store_count (CPUState *env, uint32_t count)
{
    if (env->CP0_Cause & (1 << CP0Ca_DC))
        env->CP0_Count = count;
    else {
        /* Store new count register */
        env->CP0_Count =
            count - (uint32_t)muldiv64(qemu_get_clock(vm_clock),
                                       TIMER_FREQ, ticks_per_sec);
        /* Update timer timer */
        cpu_mips_timer_update(env);
    }
}

void cpu_mips_store_compare (CPUState *env, uint32_t value)
{
    env->CP0_Compare = value;
    if (!(env->CP0_Cause & (1 << CP0Ca_DC)))
        cpu_mips_timer_update(env);
    if (env->insn_flags & ISA_MIPS32R2)
        env->CP0_Cause &= ~(1 << CP0Ca_TI);
    qemu_irq_lower(env->irq[(env->CP0_IntCtl >> CP0IntCtl_IPTI) & 0x7]);
}

void cpu_mips_start_count(CPUState *env)
{
    cpu_mips_store_count(env, env->CP0_Count);
}

void cpu_mips_stop_count(CPUState *env)
{
    /* Store the current value */
    env->CP0_Count += (uint32_t)muldiv64(qemu_get_clock(vm_clock),
                                         TIMER_FREQ, ticks_per_sec);
}

static void mips_timer_cb (void *opaque)
{
    CPUState *env;

    env = opaque;
#if 0
    if (logfile) {
        fprintf(logfile, "%s\n", __func__);
    }
#endif

    if (env->CP0_Cause & (1 << CP0Ca_DC))
        return;

    /* ??? This callback should occur when the counter is exactly equal to
       the comparator value.  Offset the count by one to avoid immediately
       retriggering the callback before any virtual time has passed.  */
    env->CP0_Count++;
    cpu_mips_timer_update(env);
    env->CP0_Count--;
    if (env->insn_flags & ISA_MIPS32R2)
        env->CP0_Cause |= 1 << CP0Ca_TI;
    qemu_irq_raise(env->irq[(env->CP0_IntCtl >> CP0IntCtl_IPTI) & 0x7]);
}

void cpu_mips_clock_init (CPUState *env)
{
    env->timer = qemu_new_timer(vm_clock, &mips_timer_cb, env);
    env->CP0_Compare = 0;
    cpu_mips_store_count(env, 1);
}
