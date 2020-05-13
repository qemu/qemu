/*
 * MicroBlaze gdb server stub
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
#include "cpu.h"
#include "exec/gdbstub.h"

int mb_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    /*
     * GDB expects SREGs in the following order:
     * PC, MSR, EAR, ESR, FSR, BTR, EDR, PID, ZPR, TLBX, TLBSX, TLBLO, TLBHI.
     * They aren't stored in this order, so make a map.
     * PID, ZPR, TLBx, TLBsx, TLBLO, and TLBHI aren't modeled, so we don't
     * map them to anything and return a value of 0 instead.
     */
    static const uint8_t sreg_map[6] = {
        SR_PC,
        SR_MSR,
        SR_EAR,
        SR_ESR,
        SR_FSR,
        SR_BTR
    };

    /*
     * GDB expects registers to be reported in this order:
     * R0-R31
     * PC-BTR
     * PVR0-PVR11
     * EDR-TLBHI
     * SLR-SHR
     */
    if (n < 32) {
        return gdb_get_reg32(mem_buf, env->regs[n]);
    } else {
        n -= 32;
        switch (n) {
        case 0 ... 5:
            return gdb_get_reg32(mem_buf, env->sregs[sreg_map[n]]);
        /* PVR12 is intentionally skipped */
        case 6 ... 17:
            n -= 6;
            return gdb_get_reg32(mem_buf, env->pvr.regs[n]);
        case 18:
            return gdb_get_reg32(mem_buf, env->sregs[SR_EDR]);
        /* Other SRegs aren't modeled, so report a value of 0 */
        case 19 ... 24:
            return gdb_get_reg32(mem_buf, 0);
        case 25:
            return gdb_get_reg32(mem_buf, env->slr);
        case 26:
            return gdb_get_reg32(mem_buf, env->shr);
        default:
            return 0;
        }
    }
}

int mb_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUMBState *env = &cpu->env;
    uint32_t tmp;

    /*
     * GDB expects SREGs in the following order:
     * PC, MSR, EAR, ESR, FSR, BTR, EDR, PID, ZPR, TLBX, TLBSX, TLBLO, TLBHI.
     * They aren't stored in this order, so make a map.
     * PID, ZPR, TLBx, TLBsx, TLBLO, and TLBHI aren't modeled, so we don't
     * map them to anything.
     */
    static const uint8_t sreg_map[6] = {
        SR_PC,
        SR_MSR,
        SR_EAR,
        SR_ESR,
        SR_FSR,
        SR_BTR
    };

    if (n > cc->gdb_num_core_regs) {
        return 0;
    }

    tmp = ldl_p(mem_buf);

    /*
     * GDB expects registers to be reported in this order:
     * R0-R31
     * PC-BTR
     * PVR0-PVR11
     * EDR-TLBHI
     * SLR-SHR
     */
    if (n < 32) {
        env->regs[n] = tmp;
    } else {
        n -= 32;
        switch (n) {
        case 0 ... 5:
            env->sregs[sreg_map[n]] = tmp;
            break;
        /* PVR12 is intentionally skipped */
        case 6 ... 17:
            n -= 6;
            env->pvr.regs[n] = tmp;
            break;
        /* Only EDR is modeled in these indeces, so ignore the rest */
        case 18:
            env->sregs[SR_EDR] = tmp;
            break;
        case 25:
            env->slr = tmp;
            break;
        case 26:
            env->shr = tmp;
            break;
        }
    }
    return 4;
}
