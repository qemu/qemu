/*
 * Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "qemu/osdep.h"
#include "cpu.h"
#ifdef CONFIG_USER_ONLY
#include "exec/helper-proto.h"
#include "qemu.h"
#endif
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "arch.h"
#include "internal.h"
#include "macros.h"
#include "sys_macros.h"
#include "tcg/tcg-op.h"
#ifndef CONFIG_USER_ONLY
#include "hex_mmu.h"
#endif

#ifndef CONFIG_USER_ONLY


static void set_addresses(CPUHexagonState *env, target_ulong pc_offset,
                          target_ulong exception_index)

{
    ARCH_SET_SYSTEM_REG(env, HEX_SREG_ELR,
                        ARCH_GET_THREAD_REG(env, HEX_REG_PC) + pc_offset);
    ARCH_SET_THREAD_REG(env, HEX_REG_PC,
                        ARCH_GET_SYSTEM_REG(env, HEX_SREG_EVB) |
                            (exception_index << 2));
}

static const char *event_name[] = {
    [HEX_EVENT_RESET] = "HEX_EVENT_RESET",
    [HEX_EVENT_IMPRECISE] = "HEX_EVENT_IMPRECISE",
    [HEX_EVENT_TLB_MISS_X] = "HEX_EVENT_TLB_MISS_X",
    [HEX_EVENT_TLB_MISS_RW] = "HEX_EVENT_TLB_MISS_RW",
    [HEX_EVENT_TRAP0] = "HEX_EVENT_TRAP0",
    [HEX_EVENT_TRAP1] = "HEX_EVENT_TRAP1",
    [HEX_EVENT_FPTRAP] = "HEX_EVENT_FPTRAP",
    [HEX_EVENT_DEBUG] = "HEX_EVENT_DEBUG",
    [HEX_EVENT_INT0] = "HEX_EVENT_INT0",
    [HEX_EVENT_INT1] = "HEX_EVENT_INT1",
    [HEX_EVENT_INT2] = "HEX_EVENT_INT2",
    [HEX_EVENT_INT3] = "HEX_EVENT_INT3",
    [HEX_EVENT_INT4] = "HEX_EVENT_INT4",
    [HEX_EVENT_INT5] = "HEX_EVENT_INT5",
    [HEX_EVENT_INT6] = "HEX_EVENT_INT6",
    [HEX_EVENT_INT7] = "HEX_EVENT_INT7",
    [HEX_EVENT_INT8] = "HEX_EVENT_INT8",
    [HEX_EVENT_INT9] = "HEX_EVENT_INT9",
    [HEX_EVENT_INTA] = "HEX_EVENT_INTA",
    [HEX_EVENT_INTB] = "HEX_EVENT_INTB",
    [HEX_EVENT_INTC] = "HEX_EVENT_INTC",
    [HEX_EVENT_INTD] = "HEX_EVENT_INTD",
    [HEX_EVENT_INTE] = "HEX_EVENT_INTE",
    [HEX_EVENT_INTF] = "HEX_EVENT_INTF"
};

void hexagon_cpu_do_interrupt(CPUState *cs)

{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;
    BQL_LOCK_GUARD();

    qemu_log_mask(CPU_LOG_INT, "\t%s: event 0x%x:%s, cause 0x%x(%d)\n",
                  __func__, cs->exception_index,
                  event_name[cs->exception_index], env->cause_code,
                  env->cause_code);

    env->llsc_addr = ~0;

    uint32_t ssr = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SSR);
    if (GET_SSR_FIELD(SSR_EX, ssr) == 1) {
        ARCH_SET_SYSTEM_REG(env, HEX_SREG_DIAG, env->cause_code);
        env->cause_code = HEX_CAUSE_DOUBLE_EXCEPT;
        cs->exception_index = HEX_EVENT_PRECISE;
    }

    switch (cs->exception_index) {
    case HEX_EVENT_TRAP0:
        if (env->cause_code == 0) {
            qemu_log_mask(LOG_UNIMP,
                          "trap0 is unhandled, no semihosting available\n");
        }

        hexagon_ssr_set_cause(env, env->cause_code);
        set_addresses(env, 4, cs->exception_index);
        break;

    case HEX_EVENT_TRAP1:
        hexagon_ssr_set_cause(env, env->cause_code);
        set_addresses(env, 4, cs->exception_index);
        break;

    case HEX_EVENT_TLB_MISS_X:
        switch (env->cause_code) {
        case HEX_CAUSE_TLBMISSX_CAUSE_NORMAL:
        case HEX_CAUSE_TLBMISSX_CAUSE_NEXTPAGE:
            qemu_log_mask(CPU_LOG_MMU,
                          "TLB miss EX exception (0x%x) caught: "
                          "Cause code (0x%x) "
                          "TID = 0x%" PRIx32 ", PC = 0x%" PRIx32
                          ", BADVA = 0x%" PRIx32 "\n",
                          cs->exception_index, env->cause_code, env->threadId,
                          ARCH_GET_THREAD_REG(env, HEX_REG_PC),
                          ARCH_GET_SYSTEM_REG(env, HEX_SREG_BADVA));

            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            break;

        default:
            cpu_abort(cs,
                      "1:Hexagon exception %d/0x%x: "
                      "Unknown cause code %d/0x%x\n",
                      cs->exception_index, cs->exception_index, env->cause_code,
                      env->cause_code);
            break;
        }
        break;

    case HEX_EVENT_TLB_MISS_RW:
        switch (env->cause_code) {
        case HEX_CAUSE_TLBMISSRW_CAUSE_READ:
        case HEX_CAUSE_TLBMISSRW_CAUSE_WRITE:
            qemu_log_mask(CPU_LOG_MMU,
                          "TLB miss RW exception (0x%x) caught: "
                          "Cause code (0x%x) "
                          "TID = 0x%" PRIx32 ", PC = 0x%" PRIx32
                          ", BADVA = 0x%" PRIx32 "\n",
                          cs->exception_index, env->cause_code, env->threadId,
                          env->gpr[HEX_REG_PC],
                          ARCH_GET_SYSTEM_REG(env, HEX_SREG_BADVA));

            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            /* env->sreg[HEX_SREG_BADVA] is set when the exception is raised */
            break;

        default:
            cpu_abort(cs,
                      "2:Hexagon exception %d/0x%x: "
                      "Unknown cause code %d/0x%x\n",
                      cs->exception_index, cs->exception_index, env->cause_code,
                      env->cause_code);
            break;
        }
        break;

    case HEX_EVENT_FPTRAP:
        hexagon_ssr_set_cause(env, env->cause_code);
        ARCH_SET_THREAD_REG(env, HEX_REG_PC,
                            ARCH_GET_SYSTEM_REG(env, HEX_SREG_EVB) |
                                (cs->exception_index << 2));
        break;

    case HEX_EVENT_DEBUG:
        hexagon_ssr_set_cause(env, env->cause_code);
        set_addresses(env, 0, cs->exception_index);
        qemu_log_mask(LOG_UNIMP, "single-step exception is not handled\n");
        break;

    case HEX_EVENT_PRECISE:
        switch (env->cause_code) {
        case HEX_CAUSE_FETCH_NO_XPAGE:
        case HEX_CAUSE_FETCH_NO_UPAGE:
        case HEX_CAUSE_PRIV_NO_READ:
        case HEX_CAUSE_PRIV_NO_UREAD:
        case HEX_CAUSE_PRIV_NO_WRITE:
        case HEX_CAUSE_PRIV_NO_UWRITE:
        case HEX_CAUSE_MISALIGNED_LOAD:
        case HEX_CAUSE_MISALIGNED_STORE:
        case HEX_CAUSE_PC_NOT_ALIGNED:
            qemu_log_mask(CPU_LOG_MMU,
                          "MMU permission exception (0x%x) caught: "
                          "Cause code (0x%x) "
                          "TID = 0x%" PRIx32 ", PC = 0x%" PRIx32
                          ", BADVA = 0x%" PRIx32 "\n",
                          cs->exception_index, env->cause_code, env->threadId,
                          env->gpr[HEX_REG_PC],
                          ARCH_GET_SYSTEM_REG(env, HEX_SREG_BADVA));


            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            /* env->sreg[HEX_SREG_BADVA] is set when the exception is raised */
            break;

        case HEX_CAUSE_DOUBLE_EXCEPT:
        case HEX_CAUSE_PRIV_USER_NO_SINSN:
        case HEX_CAUSE_PRIV_USER_NO_GINSN:
        case HEX_CAUSE_INVALID_OPCODE:
        case HEX_CAUSE_NO_COPROC_ENABLE:
        case HEX_CAUSE_NO_COPROC2_ENABLE:
        case HEX_CAUSE_UNSUPORTED_HVX_64B:
        case HEX_CAUSE_REG_WRITE_CONFLICT:
        case HEX_CAUSE_VWCTRL_WINDOW_MISS:
            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            break;

        case HEX_CAUSE_COPROC_LDST:
            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            break;

        case HEX_CAUSE_STACK_LIMIT:
            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            break;

        default:
            cpu_abort(cs,
                      "3:Hexagon exception %d/0x%x: "
                      "Unknown cause code %d/0x%x\n",
                      cs->exception_index, cs->exception_index, env->cause_code,
                      env->cause_code);
            break;
        }
        break;

    case HEX_EVENT_IMPRECISE:
        qemu_log_mask(LOG_UNIMP,
                "Imprecise exception: this case is not yet handled");
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                "Hexagon Unsupported exception 0x%x/0x%x\n",
                  cs->exception_index, env->cause_code);
        break;
    }

    cs->exception_index = HEX_EVENT_NONE;
}

void register_trap_exception(CPUHexagonState *env, int traptype, int imm,
                             target_ulong PC)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = (traptype == 0) ? HEX_EVENT_TRAP0 : HEX_EVENT_TRAP1;
    ASSERT_DIRECT_TO_GUEST_UNSET(env, cs->exception_index);

    env->cause_code = imm;
    env->gpr[HEX_REG_PC] = PC;
    cpu_loop_exit(cs);
}
#endif
