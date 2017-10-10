/*
 *  HPPA emulation cpu helpers for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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
#include "exec/exec-all.h"
#include "fpu/softfloat.h"
#include "exec/helper-proto.h"

target_ureg cpu_hppa_get_psw(CPUHPPAState *env)
{
    target_ureg psw;

    /* Fold carry bits down to 8 consecutive bits.  */
    /* ??? Needs tweaking for hppa64.  */
    /* .......b...c...d...e...f...g...h */
    psw = (env->psw_cb >> 4) & 0x01111111;
    /* .......b..bc..cd..de..ef..fg..gh */
    psw |= psw >> 3;
    /* .............bcd............efgh */
    psw |= (psw >> 6) & 0x000f000f;
    /* .........................bcdefgh */
    psw |= (psw >> 12) & 0xf;
    psw |= env->psw_cb_msb << 7;
    psw = (psw & 0xff) << 8;

    psw |= env->psw_n * PSW_N;
    psw |= (env->psw_v < 0) * PSW_V;
    psw |= env->psw;

    return psw;
}

void cpu_hppa_put_psw(CPUHPPAState *env, target_ureg psw)
{
    target_ureg cb = 0;

    env->psw = psw & ~(PSW_N | PSW_V | PSW_CB);
    env->psw_n = (psw / PSW_N) & 1;
    env->psw_v = -((psw / PSW_V) & 1);
    env->psw_cb_msb = (psw >> 15) & 1;

    cb |= ((psw >> 14) & 1) << 28;
    cb |= ((psw >> 13) & 1) << 24;
    cb |= ((psw >> 12) & 1) << 20;
    cb |= ((psw >> 11) & 1) << 16;
    cb |= ((psw >> 10) & 1) << 12;
    cb |= ((psw >>  9) & 1) <<  8;
    cb |= ((psw >>  8) & 1) <<  4;
    env->psw_cb = cb;
}

void hppa_cpu_do_interrupt(CPUState *cs)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    int i = cs->exception_index;

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        static const char * const names[] = {
            [EXCP_HPMC]          = "high priority machine check",
            [EXCP_POWER_FAIL]    = "power fail interrupt",
            [EXCP_RC]            = "recovery counter trap",
            [EXCP_EXT_INTERRUPT] = "external interrupt",
            [EXCP_LPMC]          = "low priority machine check",
            [EXCP_ITLB_MISS]     = "instruction tlb miss fault",
            [EXCP_IMP]           = "instruction memory protection trap",
            [EXCP_ILL]           = "illegal instruction trap",
            [EXCP_BREAK]         = "break instruction trap",
            [EXCP_PRIV_OPR]      = "privileged operation trap",
            [EXCP_PRIV_REG]      = "privileged register trap",
            [EXCP_OVERFLOW]      = "overflow trap",
            [EXCP_COND]          = "conditional trap",
            [EXCP_ASSIST]        = "assist exception trap",
            [EXCP_DTLB_MISS]     = "data tlb miss fault",
            [EXCP_NA_ITLB_MISS]  = "non-access instruction tlb miss",
            [EXCP_NA_DTLB_MISS]  = "non-access data tlb miss",
            [EXCP_DMP]           = "data memory protection trap",
            [EXCP_DMB]           = "data memory break trap",
            [EXCP_TLB_DIRTY]     = "tlb dirty bit trap",
            [EXCP_PAGE_REF]      = "page reference trap",
            [EXCP_ASSIST_EMU]    = "assist emulation trap",
            [EXCP_HPT]           = "high-privilege transfer trap",
            [EXCP_LPT]           = "low-privilege transfer trap",
            [EXCP_TB]            = "taken branch trap",
            [EXCP_DMAR]          = "data memory access rights trap",
            [EXCP_DMPI]          = "data memory protection id trap",
            [EXCP_UNALIGN]       = "unaligned data reference trap",
            [EXCP_PER_INTERRUPT] = "performance monitor interrupt",
            [EXCP_SYSCALL]       = "syscall",
            [EXCP_SYSCALL_LWS]   = "syscall-lws",
        };
        static int count;
        const char *name = NULL;

        if (i >= 0 && i < ARRAY_SIZE(names)) {
            name = names[i];
        }
        if (name) {
            qemu_log("INT %6d: %s ia_f=" TARGET_FMT_lx "\n",
                     ++count, name, env->iaoq_f);
        } else {
            qemu_log("INT %6d: unknown %d ia_f=" TARGET_FMT_lx "\n",
                     ++count, i, env->iaoq_f);
        }
    }
    cs->exception_index = -1;
}

bool hppa_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    abort();
    return false;
}

void hppa_cpu_dump_state(CPUState *cs, FILE *f,
                         fprintf_function cpu_fprintf, int flags)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    target_ureg psw = cpu_hppa_get_psw(env);
    target_ureg psw_cb;
    char psw_c[20];
    int i;

    cpu_fprintf(f, "IA_F " TARGET_FMT_lx " IA_B " TARGET_FMT_lx "\n",
                (target_ulong)env->iaoq_f, (target_ulong)env->iaoq_b);

    psw_c[0]  = (psw & PSW_W ? 'W' : '-');
    psw_c[1]  = (psw & PSW_E ? 'E' : '-');
    psw_c[2]  = (psw & PSW_S ? 'S' : '-');
    psw_c[3]  = (psw & PSW_T ? 'T' : '-');
    psw_c[4]  = (psw & PSW_H ? 'H' : '-');
    psw_c[5]  = (psw & PSW_L ? 'L' : '-');
    psw_c[6]  = (psw & PSW_N ? 'N' : '-');
    psw_c[7]  = (psw & PSW_X ? 'X' : '-');
    psw_c[8]  = (psw & PSW_B ? 'B' : '-');
    psw_c[9]  = (psw & PSW_C ? 'C' : '-');
    psw_c[10] = (psw & PSW_V ? 'V' : '-');
    psw_c[11] = (psw & PSW_M ? 'M' : '-');
    psw_c[12] = (psw & PSW_F ? 'F' : '-');
    psw_c[13] = (psw & PSW_R ? 'R' : '-');
    psw_c[14] = (psw & PSW_Q ? 'Q' : '-');
    psw_c[15] = (psw & PSW_P ? 'P' : '-');
    psw_c[16] = (psw & PSW_D ? 'D' : '-');
    psw_c[17] = (psw & PSW_I ? 'I' : '-');
    psw_c[18] = '\0';
    psw_cb = ((env->psw_cb >> 4) & 0x01111111) | (env->psw_cb_msb << 28);

    cpu_fprintf(f, "PSW  " TREG_FMT_lx " CB   " TREG_FMT_lx " %s\n",
                psw, psw_cb, psw_c);

    for (i = 0; i < 32; i++) {
        cpu_fprintf(f, "GR%02d " TREG_FMT_lx "%c", i, env->gr[i],
                    (i & 3) == 3 ? '\n' : ' ');
    }
#ifndef CONFIG_USER_ONLY
    for (i = 0; i < 8; i++) {
        cpu_fprintf(f, "SR%02d %08x%c", i, (uint32_t)(env->sr[i] >> 32),
                    (i & 3) == 3 ? '\n' : ' ');
    }
#endif
     cpu_fprintf(f, "\n");

    /* ??? FR */
}
