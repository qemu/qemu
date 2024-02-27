/*
 * MicroBlaze gdb server stub
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
#include "gdbstub/helpers.h"

/*
 * GDB expects SREGs in the following order:
 * PC, MSR, EAR, ESR, FSR, BTR, EDR, PID, ZPR, TLBX, TLBSX, TLBLO, TLBHI.
 *
 * PID, ZPR, TLBx, TLBsx, TLBLO, and TLBHI aren't modeled, so we don't
 * map them to anything and return a value of 0 instead.
 */

enum {
    GDB_PC    = 32 + 0,
    GDB_MSR   = 32 + 1,
    GDB_EAR   = 32 + 2,
    GDB_ESR   = 32 + 3,
    GDB_FSR   = 32 + 4,
    GDB_BTR   = 32 + 5,
    GDB_PVR0  = 32 + 6,
    GDB_PVR11 = 32 + 17,
    GDB_EDR   = 32 + 18,
};

enum {
    GDB_SP_SHL,
    GDB_SP_SHR,
};

int mb_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    uint32_t val;

    switch (n) {
    case 1 ... 31:
        val = env->regs[n];
        break;
    case GDB_PC:
        val = env->pc;
        break;
    case GDB_MSR:
        val = mb_cpu_read_msr(env);
        break;
    case GDB_EAR:
        val = env->ear;
        break;
    case GDB_ESR:
        val = env->esr;
        break;
    case GDB_FSR:
        val = env->fsr;
        break;
    case GDB_BTR:
        val = env->btr;
        break;
    case GDB_PVR0 ... GDB_PVR11:
        /* PVR12 is intentionally skipped */
        val = cpu->cfg.pvr_regs[n - GDB_PVR0];
        break;
    case GDB_EDR:
        val = env->edr;
        break;
    default:
        /* Other SRegs aren't modeled, so report a value of 0 */
        val = 0;
        break;
    }
    return gdb_get_reg32(mem_buf, val);
}

int mb_cpu_gdb_read_stack_protect(CPUState *cs, GByteArray *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    uint32_t val;

    switch (n) {
    case GDB_SP_SHL:
        val = env->slr;
        break;
    case GDB_SP_SHR:
        val = env->shr;
        break;
    default:
        return 0;
    }
    return gdb_get_reg32(mem_buf, val);
}

int mb_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUMBState *env = &cpu->env;
    uint32_t tmp;

    if (n > cc->gdb_num_core_regs) {
        return 0;
    }

    tmp = ldl_p(mem_buf);

    switch (n) {
    case 1 ... 31:
        env->regs[n] = tmp;
        break;
    case GDB_PC:
        env->pc = tmp;
        break;
    case GDB_MSR:
        mb_cpu_write_msr(env, tmp);
        break;
    case GDB_EAR:
        env->ear = tmp;
        break;
    case GDB_ESR:
        env->esr = tmp;
        break;
    case GDB_FSR:
        env->fsr = tmp;
        break;
    case GDB_BTR:
        env->btr = tmp;
        break;
    case GDB_EDR:
        env->edr = tmp;
        break;
    }
    return 4;
}

int mb_cpu_gdb_write_stack_protect(CPUState *cs, uint8_t *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;

    switch (n) {
    case GDB_SP_SHL:
        env->slr = ldl_p(mem_buf);
        break;
    case GDB_SP_SHR:
        env->shr = ldl_p(mem_buf);
        break;
    default:
        return 0;
    }
    return 4;
}
