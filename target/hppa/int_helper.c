/*
 *  HPPA interrupt helper routines
 *
 *  Copyright (c) 2017 Richard Henderson
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
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qom/cpu.h"


void hppa_cpu_do_interrupt(CPUState *cs)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    int i = cs->exception_index;
    target_ureg iaoq_f = env->iaoq_f;
    target_ureg iaoq_b = env->iaoq_b;

#ifndef CONFIG_USER_ONLY
    target_ureg old_psw;

    /* As documented in pa2.0 -- interruption handling.  */
    /* step 1 */
    env->cr[CR_IPSW] = old_psw = cpu_hppa_get_psw(env);

    /* step 2 -- note PSW_W == 0 for !HPPA64.  */
    cpu_hppa_put_psw(env, PSW_W | (i == EXCP_HPMC ? PSW_M : 0));

    /* step 3 */
    env->cr[CR_IIAOQ] = iaoq_f;
    env->cr_back[1] = iaoq_b;

    if (old_psw & PSW_Q) {
        /* step 5 */
        /* ISR and IOR will be set elsewhere.  */
        switch (i) {
        case EXCP_ILL:
        case EXCP_BREAK:
        case EXCP_PRIV_REG:
        case EXCP_PRIV_OPR:
            /* IIR set via translate.c.  */
            break;

        case EXCP_OVERFLOW:
        case EXCP_COND:
        case EXCP_ASSIST:
        case EXCP_DTLB_MISS:
        case EXCP_NA_ITLB_MISS:
        case EXCP_NA_DTLB_MISS:
        case EXCP_DMAR:
        case EXCP_DMPI:
        case EXCP_UNALIGN:
        case EXCP_DMP:
        case EXCP_DMB:
        case EXCP_TLB_DIRTY:
        case EXCP_PAGE_REF:
        case EXCP_ASSIST_EMU:
            {
                /* Avoid reading directly from the virtual address, lest we
                   raise another exception from some sort of TLB issue.  */
                vaddr vaddr;
                hwaddr paddr;

                paddr = vaddr = iaoq_f & -4;
                env->cr[CR_IIR] = ldl_phys(cs->as, paddr);
            }
            break;

        default:
            /* Other exceptions do not set IIR.  */
            break;
        }

        /* step 6 */
        env->shadow[0] = env->gr[1];
        env->shadow[1] = env->gr[8];
        env->shadow[2] = env->gr[9];
        env->shadow[3] = env->gr[16];
        env->shadow[4] = env->gr[17];
        env->shadow[5] = env->gr[24];
        env->shadow[6] = env->gr[25];
    }

    /* step 7 */
    env->iaoq_f = env->cr[CR_IVA] + 32 * i;
    env->iaoq_b = env->iaoq_f + 4;
#endif

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
        char unknown[16];

        if (i >= 0 && i < ARRAY_SIZE(names)) {
            name = names[i];
        }
        if (!name) {
            snprintf(unknown, sizeof(unknown), "unknown %d", i);
            name = unknown;
        }
        qemu_log("INT %6d: %s @ " TARGET_FMT_lx "," TARGET_FMT_lx
                 " -> " TREG_FMT_lx " " TARGET_FMT_lx "\n",
                 ++count, name,
                 (target_ulong)iaoq_f,
                 (target_ulong)iaoq_b,
                 env->iaoq_f,
                 (target_ulong)env->cr[CR_IOR]);
    }
    cs->exception_index = -1;
}

bool hppa_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
#ifndef CONFIG_USER_ONLY
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;

    /* If interrupts are requested and enabled, raise them.  */
    if ((env->psw & PSW_I) && (interrupt_request & CPU_INTERRUPT_HARD)) {
        cs->exception_index = EXCP_EXT_INTERRUPT;
        hppa_cpu_do_interrupt(cs);
        return true;
    }
#endif
    return false;
}
