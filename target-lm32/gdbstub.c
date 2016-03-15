/*
 * LM32 gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
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
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "hw/lm32/lm32_pic.h"

int lm32_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    LM32CPU *cpu = LM32_CPU(cs);
    CPULM32State *env = &cpu->env;

    if (n < 32) {
        return gdb_get_reg32(mem_buf, env->regs[n]);
    } else {
        switch (n) {
        case 32:
            return gdb_get_reg32(mem_buf, env->pc);
        /* FIXME: put in right exception ID */
        case 33:
            return gdb_get_reg32(mem_buf, 0);
        case 34:
            return gdb_get_reg32(mem_buf, env->eba);
        case 35:
            return gdb_get_reg32(mem_buf, env->deba);
        case 36:
            return gdb_get_reg32(mem_buf, env->ie);
        case 37:
            return gdb_get_reg32(mem_buf, lm32_pic_get_im(env->pic_state));
        case 38:
            return gdb_get_reg32(mem_buf, lm32_pic_get_ip(env->pic_state));
        }
    }
    return 0;
}

int lm32_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    LM32CPU *cpu = LM32_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPULM32State *env = &cpu->env;
    uint32_t tmp;

    if (n > cc->gdb_num_core_regs) {
        return 0;
    }

    tmp = ldl_p(mem_buf);

    if (n < 32) {
        env->regs[n] = tmp;
    } else {
        switch (n) {
        case 32:
            env->pc = tmp;
            break;
        case 34:
            env->eba = tmp;
            break;
        case 35:
            env->deba = tmp;
            break;
        case 36:
            env->ie = tmp;
            break;
        case 37:
            lm32_pic_set_im(env->pic_state, tmp);
            break;
        case 38:
            lm32_pic_set_ip(env->pic_state, tmp);
            break;
        }
    }
    return 4;
}
