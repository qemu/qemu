/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/gdbstub.h"
#include "cpu.h"
#include "internal.h"

int hexagon_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;

    if (n < TOTAL_PER_THREAD_REGS) {
        return gdb_get_regl(mem_buf, env->gpr[n]);
    }

    g_assert_not_reached();
}

int hexagon_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;

    if (n < TOTAL_PER_THREAD_REGS) {
        env->gpr[n] = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    }

    g_assert_not_reached();
}
