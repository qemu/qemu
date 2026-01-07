/*
 * QEMU S/390 debug routines
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/breakpoint.h"
#include "exec/watchpoint.h"
#include "target/s390x/cpu.h"
#include "target/s390x/s390x-internal.h"
#include "tcg_s390x.h"

void s390_cpu_recompute_watchpoints(CPUState *cs)
{
    const int wp_flags = BP_CPU | BP_MEM_WRITE | BP_STOP_BEFORE_ACCESS;
    CPUS390XState *env = cpu_env(cs);

    /* We are called when the watchpoints have changed. First
       remove them all.  */
    cpu_watchpoint_remove_all(cs, BP_CPU);

    /* Return if PER is not enabled */
    if (!(env->psw.mask & PSW_MASK_PER)) {
        return;
    }

    /* Return if storage-alteration event is not enabled.  */
    if (!(env->cregs[9] & PER_CR9_EVENT_STORE)) {
        return;
    }

    if (env->cregs[10] == 0 && env->cregs[11] == -1LL) {
        /* We can't create a watchoint spanning the whole memory range, so
           split it in two parts.   */
        cpu_watchpoint_insert(cs, 0, 1ULL << 63, wp_flags, NULL);
        cpu_watchpoint_insert(cs, 1ULL << 63, 1ULL << 63, wp_flags, NULL);
    } else if (env->cregs[10] > env->cregs[11]) {
        /* The address range loops, create two watchpoints.  */
        cpu_watchpoint_insert(cs, env->cregs[10], -env->cregs[10],
                              wp_flags, NULL);
        cpu_watchpoint_insert(cs, 0, env->cregs[11] + 1, wp_flags, NULL);

    } else {
        /* Default case, create a single watchpoint.  */
        cpu_watchpoint_insert(cs, env->cregs[10],
                              env->cregs[11] - env->cregs[10] + 1,
                              wp_flags, NULL);
    }
}

void s390x_cpu_debug_excp_handler(CPUState *cs)
{
    CPUS390XState *env = cpu_env(cs);
    CPUWatchpoint *wp_hit = cs->watchpoint_hit;

    if (wp_hit && wp_hit->flags & BP_CPU) {
        /*
         * FIXME: When the storage-alteration-space control bit is set,
         * the exception should only be triggered if the memory access
         * is done using an address space with the storage-alteration-event
         * bit set.  We have no way to detect that with the current
         * watchpoint code.
         */
        cs->watchpoint_hit = NULL;

        env->per_address = env->psw.addr;
        env->per_perc_atmid |= PER_CODE_EVENT_STORE | get_per_atmid(env);
        /*
         * FIXME: We currently no way to detect the address space used
         * to trigger the watchpoint.  For now just consider it is the
         * current default ASC. This turn to be true except when MVCP
         * and MVCS instructions are not used.
         */
        env->per_perc_atmid |= env->psw.mask & (PSW_MASK_ASC) >> 46;

        /*
         * Remove all watchpoints to re-execute the code.  A PER exception
         * will be triggered, it will call s390_cpu_set_psw which will
         * recompute the watchpoints.
         */
        cpu_watchpoint_remove_all(cs, BP_CPU);
        cpu_loop_exit_noexc(cs);
    }
}
