/*
 * OpenRISC exception.
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
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
#include "qemu/cutils.h"
#include "accel/tcg/cpu-loop.h"
#include "cpu.h"
#include "exception.h"

const char *openrisc_excp_name(int excp)
{
    static const char * const excp_name[EXCP_NR] = {
        [EXCP_RESET]    = "RESET",
        [EXCP_BUSERR]   = "BUSERR (bus error)",
        [EXCP_DPF]      = "DFP (data protection fault)",
        [EXCP_IPF]      = "IPF (code protection fault)",
        [EXCP_TICK]     = "TICK (timer interrupt)",
        [EXCP_ALIGN]    = "ALIGN",
        [EXCP_ILLEGAL]  = "ILLEGAL",
        [EXCP_INT]      = "INT (device interrupt)",
        [EXCP_DTLBMISS] = "DTLBMISS (data tlb miss)",
        [EXCP_ITLBMISS] = "ITLBMISS (code tlb miss)",
        [EXCP_RANGE]    = "RANGE",
        [EXCP_SYSCALL]  = "SYSCALL",
        [EXCP_FPE]      = "FPE",
        [EXCP_TRAP]     = "TRAP",
    };
    return STR_ARRAY_ELEM_OR_NULL(excp_name, excp) ?: "unknown";
}

G_NORETURN void or1k_raise_exception(OpenRISCCPU *cpu, uint32_t excp)
{
    CPUState *cs = CPU(cpu);

    cs->exception_index = excp;
    cpu_loop_exit(cs);
}
