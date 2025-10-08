/*
 * QEMU ARM CP Register GCS regiters and instructions
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "exec/icount.h"
#include "hw/irq.h"
#include "cpu.h"
#include "cpu-features.h"
#include "cpregs.h"
#include "internals.h"


static CPAccessResult access_gcs(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (arm_current_el(env) < 3
        && arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.scr_el3 & SCR_GCSEN)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_gcs_el0(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    if (arm_current_el(env) == 0 && !(env->cp15.gcscr_el[0] & GCSCRE0_NTR)) {
        return CP_ACCESS_TRAP_EL1;
    }
    return access_gcs(env, ri, isread);
}

static void gcspr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    /*
     * Bits [2:0] are RES0, so we might as well clear them now,
     * rather than upon each usage a-la GetCurrentGCSPointer.
     */
    raw_write(env, ri, value & ~7);
}

static CPAccessResult access_gcspushm(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    int el = arm_current_el(env);
    if (!(env->cp15.gcscr_el[el] & GCSCR_PUSHMEN)) {
        return CP_ACCESS_TRAP_BIT | (el ? el : 1);
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_gcspushx(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    /* Trap if lock taken, and enabled. */
    if (!(env->pstate & PSTATE_EXLOCK)) {
        int el = arm_current_el(env);
        if (env->cp15.gcscr_el[el] & GCSCR_EXLOCKEN) {
            return CP_ACCESS_EXLOCK;
        }
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_gcspopcx(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    /* Trap if lock not taken, and enabled. */
    if (env->pstate & PSTATE_EXLOCK) {
        int el = arm_current_el(env);
        if (env->cp15.gcscr_el[el] & GCSCR_EXLOCKEN) {
            return CP_ACCESS_EXLOCK;
        }
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo gcs_reginfo[] = {
    { .name = "GCSCRE0_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 5, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_gcs, .fgt = FGT_NGCS_EL0,
      .fieldoffset = offsetof(CPUARMState, cp15.gcscr_el[0]) },
    { .name = "GCSCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 5, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_gcs, .fgt = FGT_NGCS_EL1,
      .nv2_redirect_offset = 0x8d0 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 2, 5, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 2, 5, 0),
      .fieldoffset = offsetof(CPUARMState, cp15.gcscr_el[1]) },
    { .name = "GCSCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 5, .opc2 = 0,
      .access = PL2_RW, .accessfn = access_gcs,
      .fieldoffset = offsetof(CPUARMState, cp15.gcscr_el[2]) },
    { .name = "GCSCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 2, .crm = 5, .opc2 = 0,
      .access = PL3_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.gcscr_el[3]) },

    { .name = "GCSPR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 2, .crm = 5, .opc2 = 1,
      .access = PL0_R | PL1_W, .accessfn = access_gcs_el0,
      .fgt = FGT_NGCS_EL0, .writefn = gcspr_write,
      .fieldoffset = offsetof(CPUARMState, cp15.gcspr_el[0]) },
    { .name = "GCSPR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 5, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_gcs,
      .fgt = FGT_NGCS_EL1, .writefn = gcspr_write,
      .nv2_redirect_offset = 0x8c0 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 2, 5, 1),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 2, 5, 1),
      .fieldoffset = offsetof(CPUARMState, cp15.gcspr_el[1]) },
    { .name = "GCSPR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 5, .opc2 = 1,
      .access = PL2_RW, .accessfn = access_gcs, .writefn = gcspr_write,
      .fieldoffset = offsetof(CPUARMState, cp15.gcspr_el[2]) },
    { .name = "GCSPR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 2, .crm = 5, .opc2 = 1,
      .access = PL3_RW, .writefn = gcspr_write,
      .fieldoffset = offsetof(CPUARMState, cp15.gcspr_el[2]) },

    { .name = "GCSPUSHM", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 7, .opc2 = 0,
      .access = PL0_W, .accessfn = access_gcspushm,
      .fgt = FGT_NGCSPUSHM_EL1, .type = ARM_CP_GCSPUSHM },
    { .name = "GCSPOPM", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 7, .opc2 = 1,
      .access = PL0_R, .type = ARM_CP_GCSPOPM },
    { .name = "GCSSS1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 7, .opc2 = 2,
      .access = PL0_W, .type = ARM_CP_GCSSS1 },
    { .name = "GCSSS2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 7, .opc2 = 3,
      .access = PL0_R, .type = ARM_CP_GCSSS2 },
    { .name = "GCSPUSHX", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 7, .opc2 = 4,
      .access = PL1_W, .accessfn = access_gcspushx, .fgt = FGT_NGCSEPP,
      .type = ARM_CP_GCSPUSHX },
    { .name = "GCSPOPCX", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 7, .opc2 = 5,
      .access = PL1_W, .accessfn = access_gcspopcx, .fgt = FGT_NGCSEPP,
      .type = ARM_CP_GCSPOPCX },
    { .name = "GCSPOPX", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 7, .opc2 = 6,
      .access = PL1_W, .type = ARM_CP_GCSPOPX },
};

void define_gcs_cpregs(ARMCPU *cpu)
{
    if (cpu_isar_feature(aa64_gcs, cpu)) {
        define_arm_cp_regs(cpu, gcs_reginfo);
    }
}
