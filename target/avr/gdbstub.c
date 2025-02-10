/*
 * QEMU AVR gdbstub
 *
 * Copyright (c) 2016-2020 Michael Rolnik
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "gdbstub/helpers.h"
#include "cpu.h"

int avr_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CPUAVRState *env = cpu_env(cs);

    /*  R */
    if (n < 32) {
        return gdb_get_reg8(mem_buf, env->r[n]);
    }

    /*  SREG */
    if (n == 32) {
        uint8_t sreg = cpu_get_sreg(env);

        return gdb_get_reg8(mem_buf, sreg);
    }

    /*  SP */
    if (n == 33) {
        return gdb_get_reg16(mem_buf, env->sp & 0x0000ffff);
    }

    /*  PC */
    if (n == 34) {
        return gdb_get_reg32(mem_buf, env->pc_w * 2);
    }

    return 0;
}

int avr_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPUAVRState *env = cpu_env(cs);

    /*  R */
    if (n < 32) {
        env->r[n] = *mem_buf;
        return 1;
    }

    /*  SREG */
    if (n == 32) {
        cpu_set_sreg(env, *mem_buf);
        return 1;
    }

    /*  SP */
    if (n == 33) {
        env->sp = lduw_le_p(mem_buf);
        return 2;
    }

    /*  PC */
    if (n == 34) {
        env->pc_w = ldl_le_p(mem_buf) / 2;
        return 4;
    }

    return 0;
}

vaddr avr_cpu_gdb_adjust_breakpoint(CPUState *cpu, vaddr addr)
{
    /*
     * This is due to some strange GDB behavior
     * Let's assume main has address 0x100:
     * b main   - sets breakpoint at address 0x00000100 (code)
     * b *0x100 - sets breakpoint at address 0x00800100 (data)
     *
     * Force all breakpoints into code space.
     */
    return addr % OFFSET_DATA;
}
