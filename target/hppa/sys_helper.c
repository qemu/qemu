/*
 * Helpers for HPPA system instructions.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/timer.h"
#include "sysemu/runstate.h"

void HELPER(write_interval_timer)(CPUHPPAState *env, target_ulong val)
{
    HPPACPU *cpu = env_archcpu(env);
    uint64_t current = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t timeout;

    /*
     * Even in 64-bit mode, the comparator is always 32-bit.  But the
     * value we expose to the guest is 1/4 of the speed of the clock,
     * so moosh in 34 bits.
     */
    timeout = deposit64(current, 0, 34, (uint64_t)val << 2);

    /* If the mooshing puts the clock in the past, advance to next round.  */
    if (timeout < current + 1000) {
        timeout += 1ULL << 34;
    }

    cpu->env.cr[CR_IT] = timeout;
    timer_mod(cpu->alarm_timer, timeout);
}

void HELPER(halt)(CPUHPPAState *env)
{
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    helper_excp(env, EXCP_HLT);
}

void HELPER(reset)(CPUHPPAState *env)
{
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    helper_excp(env, EXCP_HLT);
}

target_ulong HELPER(swap_system_mask)(CPUHPPAState *env, target_ulong nsm)
{
    target_ulong psw = env->psw;
    /*
     * Setting the PSW Q bit to 1, if it was not already 1, is an
     * undefined operation.
     *
     * However, HP-UX 10.20 does this with the SSM instruction.
     * Tested this on HP9000/712 and HP9000/785/C3750 and both
     * machines set the Q bit from 0 to 1 without an exception,
     * so let this go without comment.
     */
    env->psw = (psw & ~PSW_SM) | (nsm & PSW_SM);
    return psw & PSW_SM;
}

void HELPER(rfi)(CPUHPPAState *env)
{
    env->iasq_f = (uint64_t)env->cr[CR_IIASQ] << 32;
    env->iasq_b = (uint64_t)env->cr_back[0] << 32;
    env->iaoq_f = env->cr[CR_IIAOQ];
    env->iaoq_b = env->cr_back[1];

    /*
     * For pa2.0, IIASQ is the top bits of the virtual address.
     * To recreate the space identifier, remove the offset bits.
     */
    if (hppa_is_pa20(env)) {
        env->iasq_f &= ~env->iaoq_f;
        env->iasq_b &= ~env->iaoq_b;
    }

    cpu_hppa_put_psw(env, env->cr[CR_IPSW]);
}

void HELPER(getshadowregs)(CPUHPPAState *env)
{
    env->gr[1] = env->shadow[0];
    env->gr[8] = env->shadow[1];
    env->gr[9] = env->shadow[2];
    env->gr[16] = env->shadow[3];
    env->gr[17] = env->shadow[4];
    env->gr[24] = env->shadow[5];
    env->gr[25] = env->shadow[6];
}

void HELPER(rfi_r)(CPUHPPAState *env)
{
    helper_getshadowregs(env);
    helper_rfi(env);
}
