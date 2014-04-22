/*
 *  LatticeMico32 helper routines.
 *
 *  Copyright (c) 2010-2014 Michael Walle <michael@walle.cc>
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
#include "qemu/host-utils.h"
#include "sysemu/sysemu.h"

int lm32_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                              int mmu_idx)
{
    LM32CPU *cpu = LM32_CPU(cs);
    CPULM32State *env = &cpu->env;
    int prot;

    address &= TARGET_PAGE_MASK;
    prot = PAGE_BITS;
    if (env->flags & LM32_FLAG_IGNORE_MSB) {
        tlb_set_page(cs, address, address & 0x7fffffff, prot, mmu_idx,
                     TARGET_PAGE_SIZE);
    } else {
        tlb_set_page(cs, address, address, prot, mmu_idx, TARGET_PAGE_SIZE);
    }

    return 0;
}

hwaddr lm32_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    LM32CPU *cpu = LM32_CPU(cs);

    addr &= TARGET_PAGE_MASK;
    if (cpu->env.flags & LM32_FLAG_IGNORE_MSB) {
        return addr & 0x7fffffff;
    } else {
        return addr;
    }
}

void lm32_breakpoint_insert(CPULM32State *env, int idx, target_ulong address)
{
    LM32CPU *cpu = lm32_env_get_cpu(env);

    cpu_breakpoint_insert(CPU(cpu), address, BP_CPU,
                          &env->cpu_breakpoint[idx]);
}

void lm32_breakpoint_remove(CPULM32State *env, int idx)
{
    LM32CPU *cpu = lm32_env_get_cpu(env);

    if (!env->cpu_breakpoint[idx]) {
        return;
    }

    cpu_breakpoint_remove_by_ref(CPU(cpu), env->cpu_breakpoint[idx]);
    env->cpu_breakpoint[idx] = NULL;
}

void lm32_watchpoint_insert(CPULM32State *env, int idx, target_ulong address,
                            lm32_wp_t wp_type)
{
    LM32CPU *cpu = lm32_env_get_cpu(env);
    int flags = 0;

    switch (wp_type) {
    case LM32_WP_DISABLED:
        /* nothing to to */
        break;
    case LM32_WP_READ:
        flags = BP_CPU | BP_STOP_BEFORE_ACCESS | BP_MEM_READ;
        break;
    case LM32_WP_WRITE:
        flags = BP_CPU | BP_STOP_BEFORE_ACCESS | BP_MEM_WRITE;
        break;
    case LM32_WP_READ_WRITE:
        flags = BP_CPU | BP_STOP_BEFORE_ACCESS | BP_MEM_ACCESS;
        break;
    }

    if (flags != 0) {
        cpu_watchpoint_insert(CPU(cpu), address, 1, flags,
                &env->cpu_watchpoint[idx]);
    }
}

void lm32_watchpoint_remove(CPULM32State *env, int idx)
{
    LM32CPU *cpu = lm32_env_get_cpu(env);

    if (!env->cpu_watchpoint[idx]) {
        return;
    }

    cpu_watchpoint_remove_by_ref(CPU(cpu), env->cpu_watchpoint[idx]);
    env->cpu_watchpoint[idx] = NULL;
}

static bool check_watchpoints(CPULM32State *env)
{
    LM32CPU *cpu = lm32_env_get_cpu(env);
    int i;

    for (i = 0; i < cpu->num_watchpoints; i++) {
        if (env->cpu_watchpoint[i] &&
                env->cpu_watchpoint[i]->flags & BP_WATCHPOINT_HIT) {
            return true;
        }
    }
    return false;
}

void lm32_debug_excp_handler(CPULM32State *env)
{
    CPUState *cs = CPU(lm32_env_get_cpu(env));
    CPUBreakpoint *bp;

    if (cs->watchpoint_hit) {
        if (cs->watchpoint_hit->flags & BP_CPU) {
            cs->watchpoint_hit = NULL;
            if (check_watchpoints(env)) {
                raise_exception(env, EXCP_WATCHPOINT);
            } else {
                cpu_resume_from_signal(cs, NULL);
            }
        }
    } else {
        QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
            if (bp->pc == env->pc) {
                if (bp->flags & BP_CPU) {
                    raise_exception(env, EXCP_BREAKPOINT);
                }
                break;
            }
        }
    }
}

void lm32_cpu_do_interrupt(CPUState *cs)
{
    LM32CPU *cpu = LM32_CPU(cs);
    CPULM32State *env = &cpu->env;

    qemu_log_mask(CPU_LOG_INT,
            "exception at pc=%x type=%x\n", env->pc, cs->exception_index);

    switch (cs->exception_index) {
    case EXCP_SYSTEMCALL:
        if (unlikely(semihosting_enabled)) {
            /* do_semicall() returns true if call was handled. Otherwise
             * do the normal exception handling. */
            if (lm32_cpu_do_semihosting(cs)) {
                env->pc += 4;
                break;
            }
        }
        /* fall through */
    case EXCP_INSN_BUS_ERROR:
    case EXCP_DATA_BUS_ERROR:
    case EXCP_DIVIDE_BY_ZERO:
    case EXCP_IRQ:
        /* non-debug exceptions */
        env->regs[R_EA] = env->pc;
        env->ie |= (env->ie & IE_IE) ? IE_EIE : 0;
        env->ie &= ~IE_IE;
        if (env->dc & DC_RE) {
            env->pc = env->deba + (cs->exception_index * 32);
        } else {
            env->pc = env->eba + (cs->exception_index * 32);
        }
        log_cpu_state_mask(CPU_LOG_INT, cs, 0);
        break;
    case EXCP_BREAKPOINT:
    case EXCP_WATCHPOINT:
        /* debug exceptions */
        env->regs[R_BA] = env->pc;
        env->ie |= (env->ie & IE_IE) ? IE_BIE : 0;
        env->ie &= ~IE_IE;
        env->pc = env->deba + (cs->exception_index * 32);
        log_cpu_state_mask(CPU_LOG_INT, cs, 0);
        break;
    default:
        cpu_abort(cs, "unhandled exception type=%d\n",
                  cs->exception_index);
        break;
    }
}

LM32CPU *cpu_lm32_init(const char *cpu_model)
{
    return LM32_CPU(cpu_generic_init(TYPE_LM32_CPU, cpu_model));
}

/* Some soc ignores the MSB on the address bus. Thus creating a shadow memory
 * area. As a general rule, 0x00000000-0x7fffffff is cached, whereas
 * 0x80000000-0xffffffff is not cached and used to access IO devices. */
void cpu_lm32_set_phys_msb_ignore(CPULM32State *env, int value)
{
    if (value) {
        env->flags |= LM32_FLAG_IGNORE_MSB;
    } else {
        env->flags &= ~LM32_FLAG_IGNORE_MSB;
    }
}
