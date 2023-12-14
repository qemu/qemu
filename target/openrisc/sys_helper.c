/*
 * OpenRISC system instructions helper routines
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Zhizhou Zhang <etouzh@gmail.com>
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
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exception.h"
#ifndef CONFIG_USER_ONLY
#include "hw/boards.h"
#endif
#include "tcg/insn-start-words.h"

#define TO_SPR(group, number) (((group) << 11) + (number))

static inline bool is_user(CPUOpenRISCState *env)
{
#ifdef CONFIG_USER_ONLY
    return true;
#else
    return (env->sr & SR_SM) == 0;
#endif
}

void HELPER(mtspr)(CPUOpenRISCState *env, target_ulong spr, target_ulong rb)
{
    OpenRISCCPU *cpu = env_archcpu(env);
#ifndef CONFIG_USER_ONLY
    CPUState *cs = env_cpu(env);
    target_ulong mr;
    int idx;
#endif

    /* Handle user accessible SPRs first.  */
    switch (spr) {
    case TO_SPR(0, 20): /* FPCSR */
        cpu_set_fpcsr(env, rb);
        return;
    }

    if (is_user(env)) {
        raise_exception(cpu, EXCP_ILLEGAL);
    }

#ifndef CONFIG_USER_ONLY
    switch (spr) {
    case TO_SPR(0, 11): /* EVBAR */
        env->evbar = rb;
        break;

    case TO_SPR(0, 16): /* NPC */
        cpu_restore_state(cs, GETPC());
        /* ??? Mirror or1ksim in not trashing delayed branch state
           when "jumping" to the current instruction.  */
        if (env->pc != rb) {
            env->pc = rb;
            env->dflag = 0;
        }
        cpu_loop_exit(cs);
        break;

    case TO_SPR(0, 17): /* SR */
        cpu_set_sr(env, rb);
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

    case TO_SPR(0, 1024) ... TO_SPR(0, 1024 + (16 * 32)): /* Shadow GPRs */
        idx = (spr - 1024);
        env->shadow_gpr[idx / 32][idx % 32] = rb;
        break;

    case TO_SPR(1, 512) ... TO_SPR(1, 512 + TLB_SIZE - 1): /* DTLBW0MR 0-127 */
        idx = spr - TO_SPR(1, 512);
        mr = env->tlb.dtlb[idx].mr;
        if (mr & 1) {
            tlb_flush_page(cs, mr & TARGET_PAGE_MASK);
        }
        if (rb & 1) {
            tlb_flush_page(cs, rb & TARGET_PAGE_MASK);
        }
        env->tlb.dtlb[idx].mr = rb;
        break;
    case TO_SPR(1, 640) ... TO_SPR(1, 640 + TLB_SIZE - 1): /* DTLBW0TR 0-127 */
        idx = spr - TO_SPR(1, 640);
        env->tlb.dtlb[idx].tr = rb;
        break;
    case TO_SPR(1, 768) ... TO_SPR(1, 895):   /* DTLBW1MR 0-127 */
    case TO_SPR(1, 896) ... TO_SPR(1, 1023):  /* DTLBW1TR 0-127 */
    case TO_SPR(1, 1024) ... TO_SPR(1, 1151): /* DTLBW2MR 0-127 */
    case TO_SPR(1, 1152) ... TO_SPR(1, 1279): /* DTLBW2TR 0-127 */
    case TO_SPR(1, 1280) ... TO_SPR(1, 1407): /* DTLBW3MR 0-127 */
    case TO_SPR(1, 1408) ... TO_SPR(1, 1535): /* DTLBW3TR 0-127 */
        break;

    case TO_SPR(2, 512) ... TO_SPR(2, 512 + TLB_SIZE - 1): /* ITLBW0MR 0-127 */
        idx = spr - TO_SPR(2, 512);
        mr = env->tlb.itlb[idx].mr;
        if (mr & 1) {
            tlb_flush_page(cs, mr & TARGET_PAGE_MASK);
        }
        if (rb & 1) {
            tlb_flush_page(cs, rb & TARGET_PAGE_MASK);
        }
        env->tlb.itlb[idx].mr = rb;
        break;
    case TO_SPR(2, 640) ... TO_SPR(2, 640 + TLB_SIZE - 1): /* ITLBW0TR 0-127 */
        idx = spr - TO_SPR(2, 640);
        env->tlb.itlb[idx].tr = rb;
        break;
    case TO_SPR(2, 768) ... TO_SPR(2, 895):   /* ITLBW1MR 0-127 */
    case TO_SPR(2, 896) ... TO_SPR(2, 1023):  /* ITLBW1TR 0-127 */
    case TO_SPR(2, 1024) ... TO_SPR(2, 1151): /* ITLBW2MR 0-127 */
    case TO_SPR(2, 1152) ... TO_SPR(2, 1279): /* ITLBW2TR 0-127 */
    case TO_SPR(2, 1280) ... TO_SPR(2, 1407): /* ITLBW3MR 0-127 */
    case TO_SPR(2, 1408) ... TO_SPR(2, 1535): /* ITLBW3TR 0-127 */
        break;

    case TO_SPR(5, 1):  /* MACLO */
        env->mac = deposit64(env->mac, 0, 32, rb);
        break;
    case TO_SPR(5, 2):  /* MACHI */
        env->mac = deposit64(env->mac, 32, 32, rb);
        break;
    case TO_SPR(8, 0):  /* PMR */
        env->pmr = rb;
        if (env->pmr & PMR_DME || env->pmr & PMR_SME) {
            cpu_restore_state(cs, GETPC());
            env->pc += 4;
            cs->halted = 1;
            raise_exception(cpu, EXCP_HALTED);
        }
        break;
    case TO_SPR(9, 0):  /* PICMR */
        env->picmr = rb;
        qemu_mutex_lock_iothread();
        if (env->picsr & env->picmr) {
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
        qemu_mutex_unlock_iothread();
        break;
    case TO_SPR(9, 2):  /* PICSR */
        env->picsr &= ~rb;
        break;
    case TO_SPR(10, 0): /* TTMR */
        {
            qemu_mutex_lock_iothread();
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
            qemu_mutex_unlock_iothread();
        }
        break;

    case TO_SPR(10, 1): /* TTCR */
        qemu_mutex_lock_iothread();
        cpu_openrisc_count_set(cpu, rb);
        cpu_openrisc_timer_update(cpu);
        qemu_mutex_unlock_iothread();
        break;
    }
#endif
}

target_ulong HELPER(mfspr)(CPUOpenRISCState *env, target_ulong rd,
                           target_ulong spr)
{
    OpenRISCCPU *cpu = env_archcpu(env);
#ifndef CONFIG_USER_ONLY
    uint64_t data[TARGET_INSN_START_WORDS];
    MachineState *ms = MACHINE(qdev_get_machine());
    CPUState *cs = env_cpu(env);
    int idx;
#endif

    /* Handle user accessible SPRs first.  */
    switch (spr) {
    case TO_SPR(0, 20): /* FPCSR */
        return env->fpcsr;
    }

    if (is_user(env)) {
        raise_exception(cpu, EXCP_ILLEGAL);
    }

#ifndef CONFIG_USER_ONLY
    switch (spr) {
    case TO_SPR(0, 0): /* VR */
        return env->vr;

    case TO_SPR(0, 1): /* UPR */
        return env->upr;

    case TO_SPR(0, 2): /* CPUCFGR */
        return env->cpucfgr;

    case TO_SPR(0, 3): /* DMMUCFGR */
        return env->dmmucfgr;

    case TO_SPR(0, 4): /* IMMUCFGR */
        return env->immucfgr;

    case TO_SPR(0, 9): /* VR2 */
        return env->vr2;

    case TO_SPR(0, 10): /* AVR */
        return env->avr;

    case TO_SPR(0, 11): /* EVBAR */
        return env->evbar;

    case TO_SPR(0, 16): /* NPC (equals PC) */
        if (cpu_unwind_state_data(cs, GETPC(), data)) {
            return data[0];
        }
        return env->pc;

    case TO_SPR(0, 17): /* SR */
        return cpu_get_sr(env);

    case TO_SPR(0, 18): /* PPC */
        if (cpu_unwind_state_data(cs, GETPC(), data)) {
            if (data[1] & 2) {
                return data[0] - 4;
            }
        }
        return env->ppc;

    case TO_SPR(0, 32): /* EPCR */
        return env->epcr;

    case TO_SPR(0, 48): /* EEAR */
        return env->eear;

    case TO_SPR(0, 64): /* ESR */
        return env->esr;

    case TO_SPR(0, 128): /* COREID */
        return cpu->parent_obj.cpu_index;

    case TO_SPR(0, 129): /* NUMCORES */
        return ms->smp.max_cpus;

    case TO_SPR(0, 1024) ... TO_SPR(0, 1024 + (16 * 32)): /* Shadow GPRs */
        idx = (spr - 1024);
        return env->shadow_gpr[idx / 32][idx % 32];

    case TO_SPR(1, 512) ... TO_SPR(1, 512 + TLB_SIZE - 1): /* DTLBW0MR 0-127 */
        idx = spr - TO_SPR(1, 512);
        return env->tlb.dtlb[idx].mr;

    case TO_SPR(1, 640) ... TO_SPR(1, 640 + TLB_SIZE - 1): /* DTLBW0TR 0-127 */
        idx = spr - TO_SPR(1, 640);
        return env->tlb.dtlb[idx].tr;

    case TO_SPR(1, 768) ... TO_SPR(1, 895):   /* DTLBW1MR 0-127 */
    case TO_SPR(1, 896) ... TO_SPR(1, 1023):  /* DTLBW1TR 0-127 */
    case TO_SPR(1, 1024) ... TO_SPR(1, 1151): /* DTLBW2MR 0-127 */
    case TO_SPR(1, 1152) ... TO_SPR(1, 1279): /* DTLBW2TR 0-127 */
    case TO_SPR(1, 1280) ... TO_SPR(1, 1407): /* DTLBW3MR 0-127 */
    case TO_SPR(1, 1408) ... TO_SPR(1, 1535): /* DTLBW3TR 0-127 */
        break;

    case TO_SPR(2, 512) ... TO_SPR(2, 512 + TLB_SIZE - 1): /* ITLBW0MR 0-127 */
        idx = spr - TO_SPR(2, 512);
        return env->tlb.itlb[idx].mr;

    case TO_SPR(2, 640) ... TO_SPR(2, 640 + TLB_SIZE - 1): /* ITLBW0TR 0-127 */
        idx = spr - TO_SPR(2, 640);
        return env->tlb.itlb[idx].tr;

    case TO_SPR(2, 768) ... TO_SPR(2, 895):   /* ITLBW1MR 0-127 */
    case TO_SPR(2, 896) ... TO_SPR(2, 1023):  /* ITLBW1TR 0-127 */
    case TO_SPR(2, 1024) ... TO_SPR(2, 1151): /* ITLBW2MR 0-127 */
    case TO_SPR(2, 1152) ... TO_SPR(2, 1279): /* ITLBW2TR 0-127 */
    case TO_SPR(2, 1280) ... TO_SPR(2, 1407): /* ITLBW3MR 0-127 */
    case TO_SPR(2, 1408) ... TO_SPR(2, 1535): /* ITLBW3TR 0-127 */
        break;

    case TO_SPR(5, 1):  /* MACLO */
        return (uint32_t)env->mac;
        break;
    case TO_SPR(5, 2):  /* MACHI */
        return env->mac >> 32;
        break;

    case TO_SPR(8, 0):  /* PMR */
        return env->pmr;

    case TO_SPR(9, 0):  /* PICMR */
        return env->picmr;

    case TO_SPR(9, 2):  /* PICSR */
        return env->picsr;

    case TO_SPR(10, 0): /* TTMR */
        return env->ttmr;

    case TO_SPR(10, 1): /* TTCR */
        qemu_mutex_lock_iothread();
        cpu_openrisc_count_update(cpu);
        qemu_mutex_unlock_iothread();
        return cpu_openrisc_count_get(cpu);
    }
#endif

    /* for rd is passed in, if rd unchanged, just keep it back.  */
    return rd;
}
