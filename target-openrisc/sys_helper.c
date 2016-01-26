/*
 * OpenRISC system instructions helper routines
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Zhizhou Zhang <etouzh@gmail.com>
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
#include "exec/helper-proto.h"

#define TO_SPR(group, number) (((group) << 11) + (number))

void HELPER(mtspr)(CPUOpenRISCState *env,
                   target_ulong ra, target_ulong rb, target_ulong offset)
{
#ifndef CONFIG_USER_ONLY
    int spr = (ra | offset);
    int idx;

    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    switch (spr) {
    case TO_SPR(0, 0): /* VR */
        env->vr = rb;
        break;

    case TO_SPR(0, 16): /* NPC */
        env->npc = rb;
        break;

    case TO_SPR(0, 17): /* SR */
        if ((env->sr & (SR_IME | SR_DME | SR_SM)) ^
            (rb & (SR_IME | SR_DME | SR_SM))) {
            tlb_flush(cs, 1);
        }
        env->sr = rb;
        env->sr |= SR_FO;      /* FO is const equal to 1 */
        if (env->sr & SR_DME) {
            env->tlb->cpu_openrisc_map_address_data =
                &cpu_openrisc_get_phys_data;
        } else {
            env->tlb->cpu_openrisc_map_address_data =
                &cpu_openrisc_get_phys_nommu;
        }

        if (env->sr & SR_IME) {
            env->tlb->cpu_openrisc_map_address_code =
                &cpu_openrisc_get_phys_code;
        } else {
            env->tlb->cpu_openrisc_map_address_code =
                &cpu_openrisc_get_phys_nommu;
        }
        break;

    case TO_SPR(0, 18): /* PPC */
        env->ppc = rb;
        break;

    case TO_SPR(0, 32): /* EPCR */
        env->epcr = rb;
        break;

    case TO_SPR(0, 48): /* EEAR */
        env->eear = rb;
        break;

    case TO_SPR(0, 64): /* ESR */
        env->esr = rb;
        break;
    case TO_SPR(1, 512) ... TO_SPR(1, 512+DTLB_SIZE-1): /* DTLBW0MR 0-127 */
        idx = spr - TO_SPR(1, 512);
        if (!(rb & 1)) {
            tlb_flush_page(cs, env->tlb->dtlb[0][idx].mr & TARGET_PAGE_MASK);
        }
        env->tlb->dtlb[0][idx].mr = rb;
        break;

    case TO_SPR(1, 640) ... TO_SPR(1, 640+DTLB_SIZE-1): /* DTLBW0TR 0-127 */
        idx = spr - TO_SPR(1, 640);
        env->tlb->dtlb[0][idx].tr = rb;
        break;
    case TO_SPR(1, 768) ... TO_SPR(1, 895):   /* DTLBW1MR 0-127 */
    case TO_SPR(1, 896) ... TO_SPR(1, 1023):  /* DTLBW1TR 0-127 */
    case TO_SPR(1, 1024) ... TO_SPR(1, 1151): /* DTLBW2MR 0-127 */
    case TO_SPR(1, 1152) ... TO_SPR(1, 1279): /* DTLBW2TR 0-127 */
    case TO_SPR(1, 1280) ... TO_SPR(1, 1407): /* DTLBW3MR 0-127 */
    case TO_SPR(1, 1408) ... TO_SPR(1, 1535): /* DTLBW3TR 0-127 */
        break;
    case TO_SPR(2, 512) ... TO_SPR(2, 512+ITLB_SIZE-1):   /* ITLBW0MR 0-127 */
        idx = spr - TO_SPR(2, 512);
        if (!(rb & 1)) {
            tlb_flush_page(cs, env->tlb->itlb[0][idx].mr & TARGET_PAGE_MASK);
        }
        env->tlb->itlb[0][idx].mr = rb;
        break;

    case TO_SPR(2, 640) ... TO_SPR(2, 640+ITLB_SIZE-1): /* ITLBW0TR 0-127 */
        idx = spr - TO_SPR(2, 640);
        env->tlb->itlb[0][idx].tr = rb;
        break;
    case TO_SPR(2, 768) ... TO_SPR(2, 895):   /* ITLBW1MR 0-127 */
    case TO_SPR(2, 896) ... TO_SPR(2, 1023):  /* ITLBW1TR 0-127 */
    case TO_SPR(2, 1024) ... TO_SPR(2, 1151): /* ITLBW2MR 0-127 */
    case TO_SPR(2, 1152) ... TO_SPR(2, 1279): /* ITLBW2TR 0-127 */
    case TO_SPR(2, 1280) ... TO_SPR(2, 1407): /* ITLBW3MR 0-127 */
    case TO_SPR(2, 1408) ... TO_SPR(2, 1535): /* ITLBW3TR 0-127 */
        break;
    case TO_SPR(9, 0):  /* PICMR */
        env->picmr |= rb;
        break;
    case TO_SPR(9, 2):  /* PICSR */
        env->picsr &= ~rb;
        break;
    case TO_SPR(10, 0): /* TTMR */
        {
            if ((env->ttmr & TTMR_M) ^ (rb & TTMR_M)) {
                switch (rb & TTMR_M) {
                case TIMER_NONE:
                    cpu_openrisc_count_stop(cpu);
                    break;
                case TIMER_INTR:
                case TIMER_SHOT:
                case TIMER_CONT:
                    cpu_openrisc_count_start(cpu);
                    break;
                default:
                    break;
                }
            }

            int ip = env->ttmr & TTMR_IP;

            if (rb & TTMR_IP) {    /* Keep IP bit.  */
                env->ttmr = (rb & ~TTMR_IP) | ip;
            } else {    /* Clear IP bit.  */
                env->ttmr = rb & ~TTMR_IP;
                cs->interrupt_request &= ~CPU_INTERRUPT_TIMER;
            }

            cpu_openrisc_timer_update(cpu);
        }
        break;

    case TO_SPR(10, 1): /* TTCR */
        env->ttcr = rb;
        if (env->ttmr & TIMER_NONE) {
            return;
        }
        cpu_openrisc_timer_update(cpu);
        break;
    default:

        break;
    }
#endif
}

target_ulong HELPER(mfspr)(CPUOpenRISCState *env,
                           target_ulong rd, target_ulong ra, uint32_t offset)
{
#ifndef CONFIG_USER_ONLY
    int spr = (ra | offset);
    int idx;

    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);

    switch (spr) {
    case TO_SPR(0, 0): /* VR */
        return env->vr & SPR_VR;

    case TO_SPR(0, 1): /* UPR */
        return env->upr;    /* TT, DM, IM, UP present */

    case TO_SPR(0, 2): /* CPUCFGR */
        return env->cpucfgr;

    case TO_SPR(0, 3): /* DMMUCFGR */
        return env->dmmucfgr;    /* 1Way, 64 entries */

    case TO_SPR(0, 4): /* IMMUCFGR */
        return env->immucfgr;

    case TO_SPR(0, 16): /* NPC */
        return env->npc;

    case TO_SPR(0, 17): /* SR */
        return env->sr;

    case TO_SPR(0, 18): /* PPC */
        return env->ppc;

    case TO_SPR(0, 32): /* EPCR */
        return env->epcr;

    case TO_SPR(0, 48): /* EEAR */
        return env->eear;

    case TO_SPR(0, 64): /* ESR */
        return env->esr;

    case TO_SPR(1, 512) ... TO_SPR(1, 512+DTLB_SIZE-1): /* DTLBW0MR 0-127 */
        idx = spr - TO_SPR(1, 512);
        return env->tlb->dtlb[0][idx].mr;

    case TO_SPR(1, 640) ... TO_SPR(1, 640+DTLB_SIZE-1): /* DTLBW0TR 0-127 */
        idx = spr - TO_SPR(1, 640);
        return env->tlb->dtlb[0][idx].tr;

    case TO_SPR(1, 768) ... TO_SPR(1, 895):   /* DTLBW1MR 0-127 */
    case TO_SPR(1, 896) ... TO_SPR(1, 1023):  /* DTLBW1TR 0-127 */
    case TO_SPR(1, 1024) ... TO_SPR(1, 1151): /* DTLBW2MR 0-127 */
    case TO_SPR(1, 1152) ... TO_SPR(1, 1279): /* DTLBW2TR 0-127 */
    case TO_SPR(1, 1280) ... TO_SPR(1, 1407): /* DTLBW3MR 0-127 */
    case TO_SPR(1, 1408) ... TO_SPR(1, 1535): /* DTLBW3TR 0-127 */
        break;

    case TO_SPR(2, 512) ... TO_SPR(2, 512+ITLB_SIZE-1): /* ITLBW0MR 0-127 */
        idx = spr - TO_SPR(2, 512);
        return env->tlb->itlb[0][idx].mr;

    case TO_SPR(2, 640) ... TO_SPR(2, 640+ITLB_SIZE-1): /* ITLBW0TR 0-127 */
        idx = spr - TO_SPR(2, 640);
        return env->tlb->itlb[0][idx].tr;

    case TO_SPR(2, 768) ... TO_SPR(2, 895):   /* ITLBW1MR 0-127 */
    case TO_SPR(2, 896) ... TO_SPR(2, 1023):  /* ITLBW1TR 0-127 */
    case TO_SPR(2, 1024) ... TO_SPR(2, 1151): /* ITLBW2MR 0-127 */
    case TO_SPR(2, 1152) ... TO_SPR(2, 1279): /* ITLBW2TR 0-127 */
    case TO_SPR(2, 1280) ... TO_SPR(2, 1407): /* ITLBW3MR 0-127 */
    case TO_SPR(2, 1408) ... TO_SPR(2, 1535): /* ITLBW3TR 0-127 */
        break;

    case TO_SPR(9, 0):  /* PICMR */
        return env->picmr;

    case TO_SPR(9, 2):  /* PICSR */
        return env->picsr;

    case TO_SPR(10, 0): /* TTMR */
        return env->ttmr;

    case TO_SPR(10, 1): /* TTCR */
        cpu_openrisc_count_update(cpu);
        return env->ttcr;

    default:
        break;
    }
#endif

/*If we later need to add tracepoints (or debug printfs) for the return
value, it may be useful to structure the code like this:

target_ulong ret = 0;

switch() {
case x:
 ret = y;
 break;
case z:
 ret = 42;
 break;
...
}

later something like trace_spr_read(ret);

return ret;*/

    /* for rd is passed in, if rd unchanged, just keep it back.  */
    return rd;
}
