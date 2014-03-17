/*
 *  Helpers for system instructions.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "helper.h"
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
    return (uint32_t)cpu_get_real_ticks();
#endif
}

/* PALcode support special instructions */
#ifndef CONFIG_USER_ONLY
void helper_hw_ret(CPUAlphaState *env, uint64_t a)
{
    env->pc = a & ~3;
    env->intr_flag = 0;
    env->lock_addr = -1;
    if ((a & 1) == 0) {
        env->pal_mode = 0;
        swap_shadow_regs(env);
    }
}

void helper_call_pal(CPUAlphaState *env, uint64_t pc, uint64_t entry_ofs)
{
    int pal_mode = env->pal_mode;
    env->exc_addr = pc | pal_mode;
    env->pc = env->palbr + entry_ofs;
    if (!pal_mode) {
        env->pal_mode = 1;
        swap_shadow_regs(env);
    }
}

void helper_tbia(CPUAlphaState *env)
{
    tlb_flush(CPU(alpha_env_get_cpu(env)), 1);
}

void helper_tbis(CPUAlphaState *env, uint64_t p)
{
    tlb_flush_page(CPU(alpha_env_get_cpu(env)), p);
}

void helper_tb_flush(CPUAlphaState *env)
{
    tb_flush(env);
}

void helper_halt(uint64_t restart)
{
    if (restart) {
        qemu_system_reset_request();
    } else {
        qemu_system_shutdown_request();
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
    AlphaCPU *cpu = alpha_env_get_cpu(env);

    if (expire) {
        env->alarm_expire = expire;
        timer_mod(cpu->alarm_timer, expire);
    } else {
        timer_del(cpu->alarm_timer);
    }
}

#endif /* CONFIG_USER_ONLY */
