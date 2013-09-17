/*
 *  LatticeMico32 helper routines.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
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

int cpu_lm32_handle_mmu_fault(CPULM32State *env, target_ulong address, int rw,
                              int mmu_idx)
{
    int prot;

    address &= TARGET_PAGE_MASK;
    prot = PAGE_BITS;
    if (env->flags & LM32_FLAG_IGNORE_MSB) {
        tlb_set_page(env, address, address & 0x7fffffff, prot, mmu_idx,
                TARGET_PAGE_SIZE);
    } else {
        tlb_set_page(env, address, address, prot, mmu_idx, TARGET_PAGE_SIZE);
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

void lm32_cpu_do_interrupt(CPUState *cs)
{
    LM32CPU *cpu = LM32_CPU(cs);
    CPULM32State *env = &cpu->env;

    qemu_log_mask(CPU_LOG_INT,
            "exception at pc=%x type=%x\n", env->pc, env->exception_index);

    switch (env->exception_index) {
    case EXCP_INSN_BUS_ERROR:
    case EXCP_DATA_BUS_ERROR:
    case EXCP_DIVIDE_BY_ZERO:
    case EXCP_IRQ:
    case EXCP_SYSTEMCALL:
        /* non-debug exceptions */
        env->regs[R_EA] = env->pc;
        env->ie |= (env->ie & IE_IE) ? IE_EIE : 0;
        env->ie &= ~IE_IE;
        if (env->dc & DC_RE) {
            env->pc = env->deba + (env->exception_index * 32);
        } else {
            env->pc = env->eba + (env->exception_index * 32);
        }
        log_cpu_state_mask(CPU_LOG_INT, cs, 0);
        break;
    case EXCP_BREAKPOINT:
    case EXCP_WATCHPOINT:
        /* debug exceptions */
        env->regs[R_BA] = env->pc;
        env->ie |= (env->ie & IE_IE) ? IE_BIE : 0;
        env->ie &= ~IE_IE;
        env->pc = env->deba + (env->exception_index * 32);
        log_cpu_state_mask(CPU_LOG_INT, cs, 0);
        break;
    default:
        cpu_abort(env, "unhandled exception type=%d\n",
                  env->exception_index);
        break;
    }
}

LM32CPU *cpu_lm32_init(const char *cpu_model)
{
    LM32CPU *cpu;
    ObjectClass *oc;

    oc = cpu_class_by_name(TYPE_LM32_CPU, cpu_model);
    if (oc == NULL) {
        return NULL;
    }
    cpu = LM32_CPU(object_new(object_class_get_name(oc)));

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
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
