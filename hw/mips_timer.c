#include "hw.h"
#include "mips.h"
#include "qemu-timer.h"

void cpu_mips_irqctrl_init (void)
{
}

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
                               100 * 1000 * 1000, ticks_per_sec);
}

void cpu_mips_store_count (CPUState *env, uint32_t count)
{
    uint64_t now, next;
    uint32_t tmp;
    uint32_t compare = env->CP0_Compare;

    tmp = count;
    if (count == compare)
        tmp++;
    now = qemu_get_clock(vm_clock);
    next = now + muldiv64(compare - tmp, ticks_per_sec, 100 * 1000 * 1000);
    if (next == now)
	next++;
#if 0
    if (logfile) {
        fprintf(logfile, "%s: 0x%08" PRIx64 " %08x %08x => 0x%08" PRIx64 "\n",
                __func__, now, count, compare, next - now);
    }
#endif
    /* Store new count and compare registers */
    env->CP0_Compare = compare;
    env->CP0_Count =
        count - (uint32_t)muldiv64(now, 100 * 1000 * 1000, ticks_per_sec);
    /* Adjust timer */
    qemu_mod_timer(env->timer, next);
}

static void cpu_mips_update_count (CPUState *env, uint32_t count)
{
    if (env->CP0_Cause & (1 << CP0Ca_DC))
        return;

    cpu_mips_store_count(env, count);
}

void cpu_mips_store_compare (CPUState *env, uint32_t value)
{
    env->CP0_Compare = value;
    cpu_mips_update_count(env, cpu_mips_get_count(env));
    if ((env->CP0_Config0 & (0x7 << CP0C0_AR)) == (1 << CP0C0_AR))
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
                                         100 * 1000 * 1000, ticks_per_sec);
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

    cpu_mips_update_count(env, cpu_mips_get_count(env));
    if ((env->CP0_Config0 & (0x7 << CP0C0_AR)) == (1 << CP0C0_AR))
        env->CP0_Cause |= 1 << CP0Ca_TI;
    qemu_irq_raise(env->irq[(env->CP0_IntCtl >> CP0IntCtl_IPTI) & 0x7]);
}

void cpu_mips_clock_init (CPUState *env)
{
    env->timer = qemu_new_timer(vm_clock, &mips_timer_cb, env);
    env->CP0_Compare = 0;
    cpu_mips_update_count(env, 1);
}
