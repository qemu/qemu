/*
 *  Helpers for system instructions.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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
#include "exec/tb-flush.h"
#include "exec/helper-proto.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "qemu/timer.h"


uint64_t helper_load_pcc(CPUAlphaState *env)
{
#ifndef CONFIG_USER_ONLY
    /* In system mode we have access to a decent high-resolution clock.
       In order to make OS-level time accounting work with the RPCC,
       present it with a well-timed clock fixed at 250MHz.  */
    return (((uint64_t)env->pcc_ofs << 32)
            | (uint32_t)(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >> 2));
#else
    /* In user-mode, QEMU_CLOCK_VIRTUAL doesn't exist.  Just pass through the host cpu
       clock ticks.  Also, don't bother taking PCC_OFS into account.  */
    return (uint32_t)cpu_get_host_ticks();
#endif
}

/* PALcode support special instructions */
#ifndef CONFIG_USER_ONLY
void helper_tbia(CPUAlphaState *env)
{
    tlb_flush(env_cpu(env));
}

void helper_tbis(CPUAlphaState *env, uint64_t p)
{
    tlb_flush_page(env_cpu(env), p);
}

void helper_tb_flush(CPUAlphaState *env)
{
    tb_flush(env_cpu(env));
}

void helper_halt(uint64_t restart)
{
    if (restart) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    } else {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

uint64_t helper_get_vmtime(void)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

uint64_t helper_get_walltime(void)
{
    return qemu_clock_get_ns(rtc_clock);
}

void helper_set_alarm(CPUAlphaState *env, uint64_t expire)
{
    AlphaCPU *cpu = env_archcpu(env);

    if (expire) {
        env->alarm_expire = expire;
        timer_mod(cpu->alarm_timer, expire);
    } else {
        timer_del(cpu->alarm_timer);
    }
}

#endif /* CONFIG_USER_ONLY */
