/*
 * m68k gdb server stub
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
#include "exec/gdbstub.h"

int m68k_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    M68kCPU *cpu = M68K_CPU(cs);
    CPUM68KState *env = &cpu->env;

    if (n < 8) {
        /* D0-D7 */
        return gdb_get_reg32(mem_buf, env->dregs[n]);
    } else if (n < 16) {
        /* A0-A7 */
        return gdb_get_reg32(mem_buf, env->aregs[n - 8]);
    } else {
        switch (n) {
        case 16:
            return gdb_get_reg32(mem_buf, env->sr);
        case 17:
            return gdb_get_reg32(mem_buf, env->pc);
        }
    }
    /* FP registers not included here because they vary between
       ColdFire and m68k.  Use XML bits for these.  */
    return 0;
}

int m68k_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    M68kCPU *cpu = M68K_CPU(cs);
    CPUM68KState *env = &cpu->env;
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    if (n < 8) {
        /* D0-D7 */
        env->dregs[n] = tmp;
    } else if (n < 16) {
        /* A0-A7 */
        env->aregs[n - 8] = tmp;
    } else {
        switch (n) {
        case 16:
            env->sr = tmp;
            break;
        case 17:
            env->pc = tmp;
            break;
        default:
            return 0;
        }
    }
    return 4;
}
