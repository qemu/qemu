/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "exec/helper-proto.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "arch.h"
#include "internal.h"
#include "macros.h"
#include "sys_macros.h"
#include "accel/tcg/cpu-loop.h"
#include "tcg/tcg-op.h"
#include "hex_mmu.h"
#include "hexswi.h"
#include "hw/hexagon/hexagon_globalreg.h"

#ifdef CONFIG_USER_ONLY
#error "This file is only used in system emulation"
#endif

static void set_addresses(CPUHexagonState *env, uint32_t pc_offset,
                          uint32_t exception_index)

{
    HexagonCPU *cpu = env_archcpu(env);
    uint32_t evb = cpu->globalregs ?
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_EVB,
                               env->threadId) :
        cpu->boot_addr;
    env->t_sreg[HEX_SREG_ELR] = env->gpr[HEX_REG_PC] + pc_offset;
    env->gpr[HEX_REG_PC] = evb | (exception_index << 2);
}

static const char *event_name[] = {
    [HEX_EVENT_RESET] = "HEX_EVENT_RESET",
    [HEX_EVENT_IMPRECISE] = "HEX_EVENT_IMPRECISE",
    [HEX_EVENT_PRECISE] = "HEX_EVENT_PRECISE",
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
    CPUHexagonState *env = cpu_env(cs);
    uint32_t ssr;

    BQL_LOCK_GUARD();

    qemu_log_mask(CPU_LOG_INT,
                  "\t%s: event 0x%02x:%s, cause 0x%" PRIx32 "(%" PRIu32 ")\n",
                  __func__, (unsigned)cs->exception_index,
                  event_name[cs->exception_index], env->cause_code,
                  env->cause_code);

    env->llsc_addr = ~0;

    ssr = env->t_sreg[HEX_SREG_SSR];
    if (GET_SSR_FIELD(SSR_EX, ssr) == 1) {
        HexagonCPU *cpu = env_archcpu(env);
        if (cpu->globalregs) {
            hexagon_globalreg_write(cpu->globalregs, HEX_SREG_DIAG,
                                    env->cause_code, env->threadId);
        }
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
                          "TLB miss EX exception (0x%02" PRIx32 ") caught: "
                          "Cause code (0x%" PRIx32 ") "
                          "TID = 0x%" PRIx32 ", PC = 0x%" PRIx32
                          ", BADVA = 0x%" PRIx32 "\n",
                          (uint32_t)cs->exception_index,
                          env->cause_code, env->threadId,
                          env->gpr[HEX_REG_PC],
                          env->t_sreg[HEX_SREG_BADVA]);

            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            break;

        default:
            cpu_abort(cs,
                      "1:Hexagon exception %" PRId32 "/0x%02" PRIx32 ": "
                      "Unknown cause code %" PRIu32 "/0x%" PRIx32 "\n",
                      (uint32_t)cs->exception_index,
                      (uint32_t)cs->exception_index,
                      env->cause_code,
                      env->cause_code);
            break;
        }
        break;

    case HEX_EVENT_TLB_MISS_RW:
        switch (env->cause_code) {
        case HEX_CAUSE_TLBMISSRW_CAUSE_READ:
        case HEX_CAUSE_TLBMISSRW_CAUSE_WRITE:
            qemu_log_mask(CPU_LOG_MMU,
                          "TLB miss RW exception (0x%02" PRIx32 ") caught: "
                          "Cause code (0x%" PRIx32 ") "
                          "TID = 0x%" PRIx32 ", PC = 0x%" PRIx32
                          ", BADVA = 0x%" PRIx32 "\n",
                          (uint32_t)cs->exception_index,
                          env->cause_code, env->threadId,
                          env->gpr[HEX_REG_PC],
                          env->t_sreg[HEX_SREG_BADVA]);

            hexagon_ssr_set_cause(env, env->cause_code);
            set_addresses(env, 0, cs->exception_index);
            /* env->sreg[HEX_SREG_BADVA] is set when the exception is raised */
            break;

        default:
            cpu_abort(cs,
                      "2:Hexagon exception %" PRId32 "/0x%02" PRIx32 ": "
                      "Unknown cause code %" PRIu32 "/0x%" PRIx32 "\n",
                      (uint32_t)cs->exception_index,
                      (uint32_t)cs->exception_index,
                      env->cause_code,
                      env->cause_code);
            break;
        }
        break;

    case HEX_EVENT_FPTRAP:
        hexagon_ssr_set_cause(env, env->cause_code);
        set_addresses(env, 0, cs->exception_index);
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
                          "MMU permission exception (0x%02" PRIx32 ") caught: "
                          "Cause code (0x%" PRIx32 ") "
                          "TID = 0x%" PRIx32 ", PC = 0x%" PRIx32
                          ", BADVA = 0x%" PRIx32 "\n",
                          (uint32_t)cs->exception_index,
                          env->cause_code, env->threadId,
                          env->gpr[HEX_REG_PC],
                          env->t_sreg[HEX_SREG_BADVA]);


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
        case HEX_CAUSE_UNSUPPORTED_HVX_64B:
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
                      "3:Hexagon exception %" PRId32 "/0x%02" PRIx32 ": "
                      "Unknown cause code %" PRIu32 "/0x%" PRIx32 "\n",
                      (uint32_t)cs->exception_index,
                      (uint32_t)cs->exception_index,
                      env->cause_code,
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
                "Hexagon Unsupported exception 0x%02x/0x%" PRIx32 "\n",
                (unsigned)cs->exception_index, env->cause_code);
        break;
    }

    cs->exception_index = HEX_EVENT_NONE;
}

void register_trap_exception(CPUHexagonState *env, int traptype, int imm,
                             uint32_t PC)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = (traptype == 0) ? HEX_EVENT_TRAP0 : HEX_EVENT_TRAP1;
    ASSERT_DIRECT_TO_GUEST_UNSET(env, cs->exception_index);

    env->cause_code = imm;
    env->gpr[HEX_REG_PC] = PC;
    cpu_loop_exit(cs);
}
