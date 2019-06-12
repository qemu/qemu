/*
 * OpenRISC gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
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
#include "exec/gdbstub.h"

int openrisc_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(cs);
    CPUOpenRISCState *env = &cpu->env;

    if (n < 32) {
        return gdb_get_reg32(mem_buf, cpu_get_gpr(env, n));
    } else {
        switch (n) {
        case 32:    /* PPC */
            return gdb_get_reg32(mem_buf, env->ppc);

        case 33:    /* NPC (equals PC) */
            return gdb_get_reg32(mem_buf, env->pc);

        case 34:    /* SR */
            return gdb_get_reg32(mem_buf, cpu_get_sr(env));

        default:
            break;
        }
    }
    return 0;
}

int openrisc_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUOpenRISCState *env = &cpu->env;
    uint32_t tmp;

    if (n > cc->gdb_num_core_regs) {
        return 0;
    }

    tmp = ldl_p(mem_buf);

    if (n < 32) {
        cpu_set_gpr(env, n, tmp);
    } else {
        switch (n) {
        case 32: /* PPC */
            env->ppc = tmp;
            break;

        case 33: /* NPC (equals PC) */
            /* If setting PC to something different,
               also clear delayed branch status.  */
            if (env->pc != tmp) {
                env->pc = tmp;
                env->dflag = 0;
            }
            break;

        case 34: /* SR */
            cpu_set_sr(env, tmp);
            break;

        default:
            break;
        }
    }
    return 4;
}
