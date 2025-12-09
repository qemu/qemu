/*
 * QEMU Hypervisor.framework support for Apple Silicon

 * Copyright 2020 Alexander Graf <agraf@csgraf.de>
 * Copyright 2020 Google LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"

#include "system/runstate.h"
#include "system/hvf.h"
#include "system/hvf_int.h"
#include "system/hw_accel.h"
#include "hvf_arm.h"
#include "cpregs.h"
#include "cpu-sysregs.h"

#include <mach/mach_time.h>

#include "system/address-spaces.h"
#include "system/memory.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "qemu/main-loop.h"
#include "system/cpus.h"
#include "arm-powerctl.h"
#include "target/arm/cpu.h"
#include "target/arm/internals.h"
#include "target/arm/multiprocessing.h"
#include "target/arm/gtimer.h"
#include "target/arm/trace.h"
#include "trace.h"
#include "migration/vmstate.h"

#include "gdbstub/enums.h"

#define MDSCR_EL1_SS_SHIFT  0
#define MDSCR_EL1_MDE_SHIFT 15

static const uint16_t dbgbcr_regs[] = {
    HV_SYS_REG_DBGBCR0_EL1,
    HV_SYS_REG_DBGBCR1_EL1,
    HV_SYS_REG_DBGBCR2_EL1,
    HV_SYS_REG_DBGBCR3_EL1,
    HV_SYS_REG_DBGBCR4_EL1,
    HV_SYS_REG_DBGBCR5_EL1,
    HV_SYS_REG_DBGBCR6_EL1,
    HV_SYS_REG_DBGBCR7_EL1,
    HV_SYS_REG_DBGBCR8_EL1,
    HV_SYS_REG_DBGBCR9_EL1,
    HV_SYS_REG_DBGBCR10_EL1,
    HV_SYS_REG_DBGBCR11_EL1,
    HV_SYS_REG_DBGBCR12_EL1,
    HV_SYS_REG_DBGBCR13_EL1,
    HV_SYS_REG_DBGBCR14_EL1,
    HV_SYS_REG_DBGBCR15_EL1,
};

static const uint16_t dbgbvr_regs[] = {
    HV_SYS_REG_DBGBVR0_EL1,
    HV_SYS_REG_DBGBVR1_EL1,
    HV_SYS_REG_DBGBVR2_EL1,
    HV_SYS_REG_DBGBVR3_EL1,
    HV_SYS_REG_DBGBVR4_EL1,
    HV_SYS_REG_DBGBVR5_EL1,
    HV_SYS_REG_DBGBVR6_EL1,
    HV_SYS_REG_DBGBVR7_EL1,
    HV_SYS_REG_DBGBVR8_EL1,
    HV_SYS_REG_DBGBVR9_EL1,
    HV_SYS_REG_DBGBVR10_EL1,
    HV_SYS_REG_DBGBVR11_EL1,
    HV_SYS_REG_DBGBVR12_EL1,
    HV_SYS_REG_DBGBVR13_EL1,
    HV_SYS_REG_DBGBVR14_EL1,
    HV_SYS_REG_DBGBVR15_EL1,
};

static const uint16_t dbgwcr_regs[] = {
    HV_SYS_REG_DBGWCR0_EL1,
    HV_SYS_REG_DBGWCR1_EL1,
    HV_SYS_REG_DBGWCR2_EL1,
    HV_SYS_REG_DBGWCR3_EL1,
    HV_SYS_REG_DBGWCR4_EL1,
    HV_SYS_REG_DBGWCR5_EL1,
    HV_SYS_REG_DBGWCR6_EL1,
    HV_SYS_REG_DBGWCR7_EL1,
    HV_SYS_REG_DBGWCR8_EL1,
    HV_SYS_REG_DBGWCR9_EL1,
    HV_SYS_REG_DBGWCR10_EL1,
    HV_SYS_REG_DBGWCR11_EL1,
    HV_SYS_REG_DBGWCR12_EL1,
    HV_SYS_REG_DBGWCR13_EL1,
    HV_SYS_REG_DBGWCR14_EL1,
    HV_SYS_REG_DBGWCR15_EL1,
};

static const uint16_t dbgwvr_regs[] = {
    HV_SYS_REG_DBGWVR0_EL1,
    HV_SYS_REG_DBGWVR1_EL1,
    HV_SYS_REG_DBGWVR2_EL1,
    HV_SYS_REG_DBGWVR3_EL1,
    HV_SYS_REG_DBGWVR4_EL1,
    HV_SYS_REG_DBGWVR5_EL1,
    HV_SYS_REG_DBGWVR6_EL1,
    HV_SYS_REG_DBGWVR7_EL1,
    HV_SYS_REG_DBGWVR8_EL1,
    HV_SYS_REG_DBGWVR9_EL1,
    HV_SYS_REG_DBGWVR10_EL1,
    HV_SYS_REG_DBGWVR11_EL1,
    HV_SYS_REG_DBGWVR12_EL1,
    HV_SYS_REG_DBGWVR13_EL1,
    HV_SYS_REG_DBGWVR14_EL1,
    HV_SYS_REG_DBGWVR15_EL1,
};

static inline int hvf_arm_num_brps(hv_vcpu_config_t config)
{
    uint64_t val;
    hv_return_t ret;
    ret = hv_vcpu_config_get_feature_reg(config, HV_FEATURE_REG_ID_AA64DFR0_EL1,
                                         &val);
    assert_hvf_ok(ret);
    return FIELD_EX64(val, ID_AA64DFR0, BRPS) + 1;
}

static inline int hvf_arm_num_wrps(hv_vcpu_config_t config)
{
    uint64_t val;
    hv_return_t ret;
    ret = hv_vcpu_config_get_feature_reg(config, HV_FEATURE_REG_ID_AA64DFR0_EL1,
                                         &val);
    assert_hvf_ok(ret);
    return FIELD_EX64(val, ID_AA64DFR0, WRPS) + 1;
}

void hvf_arm_init_debug(void)
{
    hv_vcpu_config_t config;
    config = hv_vcpu_config_create();

    max_hw_bps = hvf_arm_num_brps(config);
    hw_breakpoints =
        g_array_sized_new(true, true, sizeof(HWBreakpoint), max_hw_bps);

    max_hw_wps = hvf_arm_num_wrps(config);
    hw_watchpoints =
        g_array_sized_new(true, true, sizeof(HWWatchpoint), max_hw_wps);

    os_release(config);
}

#define SYSREG_OP0_SHIFT      20
#define SYSREG_OP0_MASK       0x3
#define SYSREG_OP0(sysreg)    ((sysreg >> SYSREG_OP0_SHIFT) & SYSREG_OP0_MASK)
#define SYSREG_OP1_SHIFT      14
#define SYSREG_OP1_MASK       0x7
#define SYSREG_OP1(sysreg)    ((sysreg >> SYSREG_OP1_SHIFT) & SYSREG_OP1_MASK)
#define SYSREG_CRN_SHIFT      10
#define SYSREG_CRN_MASK       0xf
#define SYSREG_CRN(sysreg)    ((sysreg >> SYSREG_CRN_SHIFT) & SYSREG_CRN_MASK)
#define SYSREG_CRM_SHIFT      1
#define SYSREG_CRM_MASK       0xf
#define SYSREG_CRM(sysreg)    ((sysreg >> SYSREG_CRM_SHIFT) & SYSREG_CRM_MASK)
#define SYSREG_OP2_SHIFT      17
#define SYSREG_OP2_MASK       0x7
#define SYSREG_OP2(sysreg)    ((sysreg >> SYSREG_OP2_SHIFT) & SYSREG_OP2_MASK)

#define SYSREG(op0, op1, crn, crm, op2) \
    ((op0 << SYSREG_OP0_SHIFT) | \
     (op1 << SYSREG_OP1_SHIFT) | \
     (crn << SYSREG_CRN_SHIFT) | \
     (crm << SYSREG_CRM_SHIFT) | \
     (op2 << SYSREG_OP2_SHIFT))
#define SYSREG_MASK \
    SYSREG(SYSREG_OP0_MASK, \
           SYSREG_OP1_MASK, \
           SYSREG_CRN_MASK, \
           SYSREG_CRM_MASK, \
           SYSREG_OP2_MASK)
#define SYSREG_OSLAR_EL1      SYSREG(2, 0, 1, 0, 4)
#define SYSREG_OSLSR_EL1      SYSREG(2, 0, 1, 1, 4)
#define SYSREG_OSDLR_EL1      SYSREG(2, 0, 1, 3, 4)
#define SYSREG_LORC_EL1       SYSREG(3, 0, 10, 4, 3)
#define SYSREG_CNTPCT_EL0     SYSREG(3, 3, 14, 0, 1)
#define SYSREG_CNTP_CTL_EL0   SYSREG(3, 3, 14, 2, 1)
#define SYSREG_PMCR_EL0       SYSREG(3, 3, 9, 12, 0)
#define SYSREG_PMUSERENR_EL0  SYSREG(3, 3, 9, 14, 0)
#define SYSREG_PMCNTENSET_EL0 SYSREG(3, 3, 9, 12, 1)
#define SYSREG_PMCNTENCLR_EL0 SYSREG(3, 3, 9, 12, 2)
#define SYSREG_PMINTENCLR_EL1 SYSREG(3, 0, 9, 14, 2)
#define SYSREG_PMOVSCLR_EL0   SYSREG(3, 3, 9, 12, 3)
#define SYSREG_PMSWINC_EL0    SYSREG(3, 3, 9, 12, 4)
#define SYSREG_PMSELR_EL0     SYSREG(3, 3, 9, 12, 5)
#define SYSREG_PMCEID0_EL0    SYSREG(3, 3, 9, 12, 6)
#define SYSREG_PMCEID1_EL0    SYSREG(3, 3, 9, 12, 7)
#define SYSREG_PMCCNTR_EL0    SYSREG(3, 3, 9, 13, 0)
#define SYSREG_PMCCFILTR_EL0  SYSREG(3, 3, 14, 15, 7)

#define SYSREG_ICC_AP0R0_EL1     SYSREG(3, 0, 12, 8, 4)
#define SYSREG_ICC_AP0R1_EL1     SYSREG(3, 0, 12, 8, 5)
#define SYSREG_ICC_AP0R2_EL1     SYSREG(3, 0, 12, 8, 6)
#define SYSREG_ICC_AP0R3_EL1     SYSREG(3, 0, 12, 8, 7)
#define SYSREG_ICC_AP1R0_EL1     SYSREG(3, 0, 12, 9, 0)
#define SYSREG_ICC_AP1R1_EL1     SYSREG(3, 0, 12, 9, 1)
#define SYSREG_ICC_AP1R2_EL1     SYSREG(3, 0, 12, 9, 2)
#define SYSREG_ICC_AP1R3_EL1     SYSREG(3, 0, 12, 9, 3)
#define SYSREG_ICC_ASGI1R_EL1    SYSREG(3, 0, 12, 11, 6)
#define SYSREG_ICC_BPR0_EL1      SYSREG(3, 0, 12, 8, 3)
#define SYSREG_ICC_BPR1_EL1      SYSREG(3, 0, 12, 12, 3)
#define SYSREG_ICC_CTLR_EL1      SYSREG(3, 0, 12, 12, 4)
#define SYSREG_ICC_DIR_EL1       SYSREG(3, 0, 12, 11, 1)
#define SYSREG_ICC_EOIR0_EL1     SYSREG(3, 0, 12, 8, 1)
#define SYSREG_ICC_EOIR1_EL1     SYSREG(3, 0, 12, 12, 1)
#define SYSREG_ICC_HPPIR0_EL1    SYSREG(3, 0, 12, 8, 2)
#define SYSREG_ICC_HPPIR1_EL1    SYSREG(3, 0, 12, 12, 2)
#define SYSREG_ICC_IAR0_EL1      SYSREG(3, 0, 12, 8, 0)
#define SYSREG_ICC_IAR1_EL1      SYSREG(3, 0, 12, 12, 0)
#define SYSREG_ICC_IGRPEN0_EL1   SYSREG(3, 0, 12, 12, 6)
#define SYSREG_ICC_IGRPEN1_EL1   SYSREG(3, 0, 12, 12, 7)
#define SYSREG_ICC_PMR_EL1       SYSREG(3, 0, 4, 6, 0)
#define SYSREG_ICC_RPR_EL1       SYSREG(3, 0, 12, 11, 3)
#define SYSREG_ICC_SGI0R_EL1     SYSREG(3, 0, 12, 11, 7)
#define SYSREG_ICC_SGI1R_EL1     SYSREG(3, 0, 12, 11, 5)
#define SYSREG_ICC_SRE_EL1       SYSREG(3, 0, 12, 12, 5)

#define SYSREG_MDSCR_EL1      SYSREG(2, 0, 0, 2, 2)
#define SYSREG_DBGBVR0_EL1    SYSREG(2, 0, 0, 0, 4)
#define SYSREG_DBGBCR0_EL1    SYSREG(2, 0, 0, 0, 5)
#define SYSREG_DBGWVR0_EL1    SYSREG(2, 0, 0, 0, 6)
#define SYSREG_DBGWCR0_EL1    SYSREG(2, 0, 0, 0, 7)
#define SYSREG_DBGBVR1_EL1    SYSREG(2, 0, 0, 1, 4)
#define SYSREG_DBGBCR1_EL1    SYSREG(2, 0, 0, 1, 5)
#define SYSREG_DBGWVR1_EL1    SYSREG(2, 0, 0, 1, 6)
#define SYSREG_DBGWCR1_EL1    SYSREG(2, 0, 0, 1, 7)
#define SYSREG_DBGBVR2_EL1    SYSREG(2, 0, 0, 2, 4)
#define SYSREG_DBGBCR2_EL1    SYSREG(2, 0, 0, 2, 5)
#define SYSREG_DBGWVR2_EL1    SYSREG(2, 0, 0, 2, 6)
#define SYSREG_DBGWCR2_EL1    SYSREG(2, 0, 0, 2, 7)
#define SYSREG_DBGBVR3_EL1    SYSREG(2, 0, 0, 3, 4)
#define SYSREG_DBGBCR3_EL1    SYSREG(2, 0, 0, 3, 5)
#define SYSREG_DBGWVR3_EL1    SYSREG(2, 0, 0, 3, 6)
#define SYSREG_DBGWCR3_EL1    SYSREG(2, 0, 0, 3, 7)
#define SYSREG_DBGBVR4_EL1    SYSREG(2, 0, 0, 4, 4)
#define SYSREG_DBGBCR4_EL1    SYSREG(2, 0, 0, 4, 5)
#define SYSREG_DBGWVR4_EL1    SYSREG(2, 0, 0, 4, 6)
#define SYSREG_DBGWCR4_EL1    SYSREG(2, 0, 0, 4, 7)
#define SYSREG_DBGBVR5_EL1    SYSREG(2, 0, 0, 5, 4)
#define SYSREG_DBGBCR5_EL1    SYSREG(2, 0, 0, 5, 5)
#define SYSREG_DBGWVR5_EL1    SYSREG(2, 0, 0, 5, 6)
#define SYSREG_DBGWCR5_EL1    SYSREG(2, 0, 0, 5, 7)
#define SYSREG_DBGBVR6_EL1    SYSREG(2, 0, 0, 6, 4)
#define SYSREG_DBGBCR6_EL1    SYSREG(2, 0, 0, 6, 5)
#define SYSREG_DBGWVR6_EL1    SYSREG(2, 0, 0, 6, 6)
#define SYSREG_DBGWCR6_EL1    SYSREG(2, 0, 0, 6, 7)
#define SYSREG_DBGBVR7_EL1    SYSREG(2, 0, 0, 7, 4)
#define SYSREG_DBGBCR7_EL1    SYSREG(2, 0, 0, 7, 5)
#define SYSREG_DBGWVR7_EL1    SYSREG(2, 0, 0, 7, 6)
#define SYSREG_DBGWCR7_EL1    SYSREG(2, 0, 0, 7, 7)
#define SYSREG_DBGBVR8_EL1    SYSREG(2, 0, 0, 8, 4)
#define SYSREG_DBGBCR8_EL1    SYSREG(2, 0, 0, 8, 5)
#define SYSREG_DBGWVR8_EL1    SYSREG(2, 0, 0, 8, 6)
#define SYSREG_DBGWCR8_EL1    SYSREG(2, 0, 0, 8, 7)
#define SYSREG_DBGBVR9_EL1    SYSREG(2, 0, 0, 9, 4)
#define SYSREG_DBGBCR9_EL1    SYSREG(2, 0, 0, 9, 5)
#define SYSREG_DBGWVR9_EL1    SYSREG(2, 0, 0, 9, 6)
#define SYSREG_DBGWCR9_EL1    SYSREG(2, 0, 0, 9, 7)
#define SYSREG_DBGBVR10_EL1   SYSREG(2, 0, 0, 10, 4)
#define SYSREG_DBGBCR10_EL1   SYSREG(2, 0, 0, 10, 5)
#define SYSREG_DBGWVR10_EL1   SYSREG(2, 0, 0, 10, 6)
#define SYSREG_DBGWCR10_EL1   SYSREG(2, 0, 0, 10, 7)
#define SYSREG_DBGBVR11_EL1   SYSREG(2, 0, 0, 11, 4)
#define SYSREG_DBGBCR11_EL1   SYSREG(2, 0, 0, 11, 5)
#define SYSREG_DBGWVR11_EL1   SYSREG(2, 0, 0, 11, 6)
#define SYSREG_DBGWCR11_EL1   SYSREG(2, 0, 0, 11, 7)
#define SYSREG_DBGBVR12_EL1   SYSREG(2, 0, 0, 12, 4)
#define SYSREG_DBGBCR12_EL1   SYSREG(2, 0, 0, 12, 5)
#define SYSREG_DBGWVR12_EL1   SYSREG(2, 0, 0, 12, 6)
#define SYSREG_DBGWCR12_EL1   SYSREG(2, 0, 0, 12, 7)
#define SYSREG_DBGBVR13_EL1   SYSREG(2, 0, 0, 13, 4)
#define SYSREG_DBGBCR13_EL1   SYSREG(2, 0, 0, 13, 5)
#define SYSREG_DBGWVR13_EL1   SYSREG(2, 0, 0, 13, 6)
#define SYSREG_DBGWCR13_EL1   SYSREG(2, 0, 0, 13, 7)
#define SYSREG_DBGBVR14_EL1   SYSREG(2, 0, 0, 14, 4)
#define SYSREG_DBGBCR14_EL1   SYSREG(2, 0, 0, 14, 5)
#define SYSREG_DBGWVR14_EL1   SYSREG(2, 0, 0, 14, 6)
#define SYSREG_DBGWCR14_EL1   SYSREG(2, 0, 0, 14, 7)
#define SYSREG_DBGBVR15_EL1   SYSREG(2, 0, 0, 15, 4)
#define SYSREG_DBGBCR15_EL1   SYSREG(2, 0, 0, 15, 5)
#define SYSREG_DBGWVR15_EL1   SYSREG(2, 0, 0, 15, 6)
#define SYSREG_DBGWCR15_EL1   SYSREG(2, 0, 0, 15, 7)

#define WFX_IS_WFE (1 << 0)

#define TMR_CTL_ENABLE  (1 << 0)
#define TMR_CTL_IMASK   (1 << 1)
#define TMR_CTL_ISTATUS (1 << 2)

static void hvf_wfi(CPUState *cpu);

static uint32_t chosen_ipa_bit_size;

typedef struct HVFVTimer {
    /* Vtimer value during migration and paused state */
    uint64_t vtimer_val;
} HVFVTimer;

static HVFVTimer vtimer;

typedef struct ARMHostCPUFeatures {
    ARMISARegisters isar;
    uint64_t features;
    uint64_t midr;
    uint32_t reset_sctlr;
    const char *dtb_compatible;
} ARMHostCPUFeatures;

static ARMHostCPUFeatures arm_host_cpu_features;

struct hvf_reg_match {
    int reg;
    uint64_t offset;
};

static const struct hvf_reg_match hvf_reg_match[] = {
    { HV_REG_X0,   offsetof(CPUARMState, xregs[0]) },
    { HV_REG_X1,   offsetof(CPUARMState, xregs[1]) },
    { HV_REG_X2,   offsetof(CPUARMState, xregs[2]) },
    { HV_REG_X3,   offsetof(CPUARMState, xregs[3]) },
    { HV_REG_X4,   offsetof(CPUARMState, xregs[4]) },
    { HV_REG_X5,   offsetof(CPUARMState, xregs[5]) },
    { HV_REG_X6,   offsetof(CPUARMState, xregs[6]) },
    { HV_REG_X7,   offsetof(CPUARMState, xregs[7]) },
    { HV_REG_X8,   offsetof(CPUARMState, xregs[8]) },
    { HV_REG_X9,   offsetof(CPUARMState, xregs[9]) },
    { HV_REG_X10,  offsetof(CPUARMState, xregs[10]) },
    { HV_REG_X11,  offsetof(CPUARMState, xregs[11]) },
    { HV_REG_X12,  offsetof(CPUARMState, xregs[12]) },
    { HV_REG_X13,  offsetof(CPUARMState, xregs[13]) },
    { HV_REG_X14,  offsetof(CPUARMState, xregs[14]) },
    { HV_REG_X15,  offsetof(CPUARMState, xregs[15]) },
    { HV_REG_X16,  offsetof(CPUARMState, xregs[16]) },
    { HV_REG_X17,  offsetof(CPUARMState, xregs[17]) },
    { HV_REG_X18,  offsetof(CPUARMState, xregs[18]) },
    { HV_REG_X19,  offsetof(CPUARMState, xregs[19]) },
    { HV_REG_X20,  offsetof(CPUARMState, xregs[20]) },
    { HV_REG_X21,  offsetof(CPUARMState, xregs[21]) },
    { HV_REG_X22,  offsetof(CPUARMState, xregs[22]) },
    { HV_REG_X23,  offsetof(CPUARMState, xregs[23]) },
    { HV_REG_X24,  offsetof(CPUARMState, xregs[24]) },
    { HV_REG_X25,  offsetof(CPUARMState, xregs[25]) },
    { HV_REG_X26,  offsetof(CPUARMState, xregs[26]) },
    { HV_REG_X27,  offsetof(CPUARMState, xregs[27]) },
    { HV_REG_X28,  offsetof(CPUARMState, xregs[28]) },
    { HV_REG_X29,  offsetof(CPUARMState, xregs[29]) },
    { HV_REG_X30,  offsetof(CPUARMState, xregs[30]) },
    { HV_REG_PC,   offsetof(CPUARMState, pc) },
};

static const struct hvf_reg_match hvf_fpreg_match[] = {
    { HV_SIMD_FP_REG_Q0,  offsetof(CPUARMState, vfp.zregs[0]) },
    { HV_SIMD_FP_REG_Q1,  offsetof(CPUARMState, vfp.zregs[1]) },
    { HV_SIMD_FP_REG_Q2,  offsetof(CPUARMState, vfp.zregs[2]) },
    { HV_SIMD_FP_REG_Q3,  offsetof(CPUARMState, vfp.zregs[3]) },
    { HV_SIMD_FP_REG_Q4,  offsetof(CPUARMState, vfp.zregs[4]) },
    { HV_SIMD_FP_REG_Q5,  offsetof(CPUARMState, vfp.zregs[5]) },
    { HV_SIMD_FP_REG_Q6,  offsetof(CPUARMState, vfp.zregs[6]) },
    { HV_SIMD_FP_REG_Q7,  offsetof(CPUARMState, vfp.zregs[7]) },
    { HV_SIMD_FP_REG_Q8,  offsetof(CPUARMState, vfp.zregs[8]) },
    { HV_SIMD_FP_REG_Q9,  offsetof(CPUARMState, vfp.zregs[9]) },
    { HV_SIMD_FP_REG_Q10, offsetof(CPUARMState, vfp.zregs[10]) },
    { HV_SIMD_FP_REG_Q11, offsetof(CPUARMState, vfp.zregs[11]) },
    { HV_SIMD_FP_REG_Q12, offsetof(CPUARMState, vfp.zregs[12]) },
    { HV_SIMD_FP_REG_Q13, offsetof(CPUARMState, vfp.zregs[13]) },
    { HV_SIMD_FP_REG_Q14, offsetof(CPUARMState, vfp.zregs[14]) },
    { HV_SIMD_FP_REG_Q15, offsetof(CPUARMState, vfp.zregs[15]) },
    { HV_SIMD_FP_REG_Q16, offsetof(CPUARMState, vfp.zregs[16]) },
    { HV_SIMD_FP_REG_Q17, offsetof(CPUARMState, vfp.zregs[17]) },
    { HV_SIMD_FP_REG_Q18, offsetof(CPUARMState, vfp.zregs[18]) },
    { HV_SIMD_FP_REG_Q19, offsetof(CPUARMState, vfp.zregs[19]) },
    { HV_SIMD_FP_REG_Q20, offsetof(CPUARMState, vfp.zregs[20]) },
    { HV_SIMD_FP_REG_Q21, offsetof(CPUARMState, vfp.zregs[21]) },
    { HV_SIMD_FP_REG_Q22, offsetof(CPUARMState, vfp.zregs[22]) },
    { HV_SIMD_FP_REG_Q23, offsetof(CPUARMState, vfp.zregs[23]) },
    { HV_SIMD_FP_REG_Q24, offsetof(CPUARMState, vfp.zregs[24]) },
    { HV_SIMD_FP_REG_Q25, offsetof(CPUARMState, vfp.zregs[25]) },
    { HV_SIMD_FP_REG_Q26, offsetof(CPUARMState, vfp.zregs[26]) },
    { HV_SIMD_FP_REG_Q27, offsetof(CPUARMState, vfp.zregs[27]) },
    { HV_SIMD_FP_REG_Q28, offsetof(CPUARMState, vfp.zregs[28]) },
    { HV_SIMD_FP_REG_Q29, offsetof(CPUARMState, vfp.zregs[29]) },
    { HV_SIMD_FP_REG_Q30, offsetof(CPUARMState, vfp.zregs[30]) },
    { HV_SIMD_FP_REG_Q31, offsetof(CPUARMState, vfp.zregs[31]) },
};

/*
 * QEMU uses KVM system register ids in the migration format.
 * Conveniently, HVF uses the same encoding of the op* and cr* parameters
 * within the low 16 bits of the ids.  Thus conversion between the
 * formats is trivial.
 */

#define KVMID_TO_HVF(KVM)  ((KVM) & 0xffff)
#define HVF_TO_KVMID(HVF)  \
    (CP_REG_ARM64 | CP_REG_SIZE_U64 | CP_REG_ARM64_SYSREG | (HVF))

/* Verify this at compile-time. */

#define DEF_SYSREG(HVF_ID, ...) \
  QEMU_BUILD_BUG_ON(HVF_ID != KVMID_TO_HVF(KVMID_AA64_SYS_REG64(__VA_ARGS__)));

#include "sysreg.c.inc"

#undef DEF_SYSREG

#define DEF_SYSREG(HVF_ID, op0, op1, crn, crm, op2)  HVF_ID,

static const hv_sys_reg_t hvf_sreg_list[] = {
#include "sysreg.c.inc"
};

#undef DEF_SYSREG

int hvf_arch_get_registers(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    hv_return_t ret;
    uint64_t val;
    hv_simd_fp_uchar16_t fpval;
    int i, n;

    for (i = 0; i < ARRAY_SIZE(hvf_reg_match); i++) {
        ret = hv_vcpu_get_reg(cpu->accel->fd, hvf_reg_match[i].reg, &val);
        *(uint64_t *)((void *)env + hvf_reg_match[i].offset) = val;
        assert_hvf_ok(ret);
    }

    for (i = 0; i < ARRAY_SIZE(hvf_fpreg_match); i++) {
        ret = hv_vcpu_get_simd_fp_reg(cpu->accel->fd, hvf_fpreg_match[i].reg,
                                      &fpval);
        memcpy((void *)env + hvf_fpreg_match[i].offset, &fpval, sizeof(fpval));
        assert_hvf_ok(ret);
    }

    val = 0;
    ret = hv_vcpu_get_reg(cpu->accel->fd, HV_REG_FPCR, &val);
    assert_hvf_ok(ret);
    vfp_set_fpcr(env, val);

    val = 0;
    ret = hv_vcpu_get_reg(cpu->accel->fd, HV_REG_FPSR, &val);
    assert_hvf_ok(ret);
    vfp_set_fpsr(env, val);

    ret = hv_vcpu_get_reg(cpu->accel->fd, HV_REG_CPSR, &val);
    assert_hvf_ok(ret);
    pstate_write(env, val);

    for (i = 0, n = arm_cpu->cpreg_array_len; i < n; i++) {
        uint64_t kvm_id = arm_cpu->cpreg_indexes[i];
        int hvf_id = KVMID_TO_HVF(kvm_id);

        if (cpu->accel->guest_debug_enabled) {
            /* Handle debug registers */
            switch (hvf_id) {
            case HV_SYS_REG_DBGBVR0_EL1:
            case HV_SYS_REG_DBGBCR0_EL1:
            case HV_SYS_REG_DBGWVR0_EL1:
            case HV_SYS_REG_DBGWCR0_EL1:
            case HV_SYS_REG_DBGBVR1_EL1:
            case HV_SYS_REG_DBGBCR1_EL1:
            case HV_SYS_REG_DBGWVR1_EL1:
            case HV_SYS_REG_DBGWCR1_EL1:
            case HV_SYS_REG_DBGBVR2_EL1:
            case HV_SYS_REG_DBGBCR2_EL1:
            case HV_SYS_REG_DBGWVR2_EL1:
            case HV_SYS_REG_DBGWCR2_EL1:
            case HV_SYS_REG_DBGBVR3_EL1:
            case HV_SYS_REG_DBGBCR3_EL1:
            case HV_SYS_REG_DBGWVR3_EL1:
            case HV_SYS_REG_DBGWCR3_EL1:
            case HV_SYS_REG_DBGBVR4_EL1:
            case HV_SYS_REG_DBGBCR4_EL1:
            case HV_SYS_REG_DBGWVR4_EL1:
            case HV_SYS_REG_DBGWCR4_EL1:
            case HV_SYS_REG_DBGBVR5_EL1:
            case HV_SYS_REG_DBGBCR5_EL1:
            case HV_SYS_REG_DBGWVR5_EL1:
            case HV_SYS_REG_DBGWCR5_EL1:
            case HV_SYS_REG_DBGBVR6_EL1:
            case HV_SYS_REG_DBGBCR6_EL1:
            case HV_SYS_REG_DBGWVR6_EL1:
            case HV_SYS_REG_DBGWCR6_EL1:
            case HV_SYS_REG_DBGBVR7_EL1:
            case HV_SYS_REG_DBGBCR7_EL1:
            case HV_SYS_REG_DBGWVR7_EL1:
            case HV_SYS_REG_DBGWCR7_EL1:
            case HV_SYS_REG_DBGBVR8_EL1:
            case HV_SYS_REG_DBGBCR8_EL1:
            case HV_SYS_REG_DBGWVR8_EL1:
            case HV_SYS_REG_DBGWCR8_EL1:
            case HV_SYS_REG_DBGBVR9_EL1:
            case HV_SYS_REG_DBGBCR9_EL1:
            case HV_SYS_REG_DBGWVR9_EL1:
            case HV_SYS_REG_DBGWCR9_EL1:
            case HV_SYS_REG_DBGBVR10_EL1:
            case HV_SYS_REG_DBGBCR10_EL1:
            case HV_SYS_REG_DBGWVR10_EL1:
            case HV_SYS_REG_DBGWCR10_EL1:
            case HV_SYS_REG_DBGBVR11_EL1:
            case HV_SYS_REG_DBGBCR11_EL1:
            case HV_SYS_REG_DBGWVR11_EL1:
            case HV_SYS_REG_DBGWCR11_EL1:
            case HV_SYS_REG_DBGBVR12_EL1:
            case HV_SYS_REG_DBGBCR12_EL1:
            case HV_SYS_REG_DBGWVR12_EL1:
            case HV_SYS_REG_DBGWCR12_EL1:
            case HV_SYS_REG_DBGBVR13_EL1:
            case HV_SYS_REG_DBGBCR13_EL1:
            case HV_SYS_REG_DBGWVR13_EL1:
            case HV_SYS_REG_DBGWCR13_EL1:
            case HV_SYS_REG_DBGBVR14_EL1:
            case HV_SYS_REG_DBGBCR14_EL1:
            case HV_SYS_REG_DBGWVR14_EL1:
            case HV_SYS_REG_DBGWCR14_EL1:
            case HV_SYS_REG_DBGBVR15_EL1:
            case HV_SYS_REG_DBGBCR15_EL1:
            case HV_SYS_REG_DBGWVR15_EL1:
            case HV_SYS_REG_DBGWCR15_EL1: {
                /*
                 * If the guest is being debugged, the vCPU's debug registers
                 * are holding the gdbstub's view of the registers (set in
                 * hvf_arch_update_guest_debug()).
                 * Since the environment is used to store only the guest's view
                 * of the registers, don't update it with the values from the
                 * vCPU but simply keep the values from the previous
                 * environment.
                 */
                uint32_t key = kvm_to_cpreg_id(kvm_id);
                const ARMCPRegInfo *ri =
                    get_arm_cp_reginfo(arm_cpu->cp_regs, key);

                val = read_raw_cp_reg(env, ri);

                arm_cpu->cpreg_values[i] = val;
                continue;
            }
            }
        }

        ret = hv_vcpu_get_sys_reg(cpu->accel->fd, hvf_id, &val);
        assert_hvf_ok(ret);

        arm_cpu->cpreg_values[i] = val;
    }
    assert(write_list_to_cpustate(arm_cpu));

    aarch64_restore_sp(env, arm_current_el(env));

    return 0;
}

int hvf_arch_put_registers(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    hv_return_t ret;
    uint64_t val;
    hv_simd_fp_uchar16_t fpval;
    int i, n;

    for (i = 0; i < ARRAY_SIZE(hvf_reg_match); i++) {
        val = *(uint64_t *)((void *)env + hvf_reg_match[i].offset);
        ret = hv_vcpu_set_reg(cpu->accel->fd, hvf_reg_match[i].reg, val);
        assert_hvf_ok(ret);
    }

    for (i = 0; i < ARRAY_SIZE(hvf_fpreg_match); i++) {
        memcpy(&fpval, (void *)env + hvf_fpreg_match[i].offset, sizeof(fpval));
        ret = hv_vcpu_set_simd_fp_reg(cpu->accel->fd, hvf_fpreg_match[i].reg,
                                      fpval);
        assert_hvf_ok(ret);
    }

    ret = hv_vcpu_set_reg(cpu->accel->fd, HV_REG_FPCR, vfp_get_fpcr(env));
    assert_hvf_ok(ret);

    ret = hv_vcpu_set_reg(cpu->accel->fd, HV_REG_FPSR, vfp_get_fpsr(env));
    assert_hvf_ok(ret);

    ret = hv_vcpu_set_reg(cpu->accel->fd, HV_REG_CPSR, pstate_read(env));
    assert_hvf_ok(ret);

    aarch64_save_sp(env, arm_current_el(env));

    assert(write_cpustate_to_list(arm_cpu, false));
    for (i = 0, n = arm_cpu->cpreg_array_len; i < n; i++) {
        uint64_t kvm_id = arm_cpu->cpreg_indexes[i];
        int hvf_id = KVMID_TO_HVF(kvm_id);

        if (cpu->accel->guest_debug_enabled) {
            /* Handle debug registers */
            switch (hvf_id) {
            case HV_SYS_REG_DBGBVR0_EL1:
            case HV_SYS_REG_DBGBCR0_EL1:
            case HV_SYS_REG_DBGWVR0_EL1:
            case HV_SYS_REG_DBGWCR0_EL1:
            case HV_SYS_REG_DBGBVR1_EL1:
            case HV_SYS_REG_DBGBCR1_EL1:
            case HV_SYS_REG_DBGWVR1_EL1:
            case HV_SYS_REG_DBGWCR1_EL1:
            case HV_SYS_REG_DBGBVR2_EL1:
            case HV_SYS_REG_DBGBCR2_EL1:
            case HV_SYS_REG_DBGWVR2_EL1:
            case HV_SYS_REG_DBGWCR2_EL1:
            case HV_SYS_REG_DBGBVR3_EL1:
            case HV_SYS_REG_DBGBCR3_EL1:
            case HV_SYS_REG_DBGWVR3_EL1:
            case HV_SYS_REG_DBGWCR3_EL1:
            case HV_SYS_REG_DBGBVR4_EL1:
            case HV_SYS_REG_DBGBCR4_EL1:
            case HV_SYS_REG_DBGWVR4_EL1:
            case HV_SYS_REG_DBGWCR4_EL1:
            case HV_SYS_REG_DBGBVR5_EL1:
            case HV_SYS_REG_DBGBCR5_EL1:
            case HV_SYS_REG_DBGWVR5_EL1:
            case HV_SYS_REG_DBGWCR5_EL1:
            case HV_SYS_REG_DBGBVR6_EL1:
            case HV_SYS_REG_DBGBCR6_EL1:
            case HV_SYS_REG_DBGWVR6_EL1:
            case HV_SYS_REG_DBGWCR6_EL1:
            case HV_SYS_REG_DBGBVR7_EL1:
            case HV_SYS_REG_DBGBCR7_EL1:
            case HV_SYS_REG_DBGWVR7_EL1:
            case HV_SYS_REG_DBGWCR7_EL1:
            case HV_SYS_REG_DBGBVR8_EL1:
            case HV_SYS_REG_DBGBCR8_EL1:
            case HV_SYS_REG_DBGWVR8_EL1:
            case HV_SYS_REG_DBGWCR8_EL1:
            case HV_SYS_REG_DBGBVR9_EL1:
            case HV_SYS_REG_DBGBCR9_EL1:
            case HV_SYS_REG_DBGWVR9_EL1:
            case HV_SYS_REG_DBGWCR9_EL1:
            case HV_SYS_REG_DBGBVR10_EL1:
            case HV_SYS_REG_DBGBCR10_EL1:
            case HV_SYS_REG_DBGWVR10_EL1:
            case HV_SYS_REG_DBGWCR10_EL1:
            case HV_SYS_REG_DBGBVR11_EL1:
            case HV_SYS_REG_DBGBCR11_EL1:
            case HV_SYS_REG_DBGWVR11_EL1:
            case HV_SYS_REG_DBGWCR11_EL1:
            case HV_SYS_REG_DBGBVR12_EL1:
            case HV_SYS_REG_DBGBCR12_EL1:
            case HV_SYS_REG_DBGWVR12_EL1:
            case HV_SYS_REG_DBGWCR12_EL1:
            case HV_SYS_REG_DBGBVR13_EL1:
            case HV_SYS_REG_DBGBCR13_EL1:
            case HV_SYS_REG_DBGWVR13_EL1:
            case HV_SYS_REG_DBGWCR13_EL1:
            case HV_SYS_REG_DBGBVR14_EL1:
            case HV_SYS_REG_DBGBCR14_EL1:
            case HV_SYS_REG_DBGWVR14_EL1:
            case HV_SYS_REG_DBGWCR14_EL1:
            case HV_SYS_REG_DBGBVR15_EL1:
            case HV_SYS_REG_DBGBCR15_EL1:
            case HV_SYS_REG_DBGWVR15_EL1:
            case HV_SYS_REG_DBGWCR15_EL1:
                /*
                 * If the guest is being debugged, the vCPU's debug registers
                 * are already holding the gdbstub's view of the registers (set
                 * in hvf_arch_update_guest_debug()).
                 */
                continue;
            }
        }

        val = arm_cpu->cpreg_values[i];
        ret = hv_vcpu_set_sys_reg(cpu->accel->fd, hvf_id, val);
        assert_hvf_ok(ret);
    }

    ret = hv_vcpu_set_vtimer_offset(cpu->accel->fd, hvf_state->vtimer_offset);
    assert_hvf_ok(ret);

    return 0;
}

/* Must be called by the owning thread */
static void flush_cpu_state(CPUState *cpu)
{
    if (cpu->vcpu_dirty) {
        hvf_arch_put_registers(cpu);
        cpu->vcpu_dirty = false;
    }
}

/* Must be called by the owning thread */
static void hvf_set_reg(CPUState *cpu, int rt, uint64_t val)
{
    hv_return_t r;

    flush_cpu_state(cpu);

    if (rt < 31) {
        r = hv_vcpu_set_reg(cpu->accel->fd, HV_REG_X0 + rt, val);
        assert_hvf_ok(r);
    }
}

/* Must be called by the owning thread */
static uint64_t hvf_get_reg(CPUState *cpu, int rt)
{
    uint64_t val = 0;
    hv_return_t r;

    flush_cpu_state(cpu);

    if (rt < 31) {
        r = hv_vcpu_get_reg(cpu->accel->fd, HV_REG_X0 + rt, &val);
        assert_hvf_ok(r);
    }

    return val;
}

static void clamp_id_aa64mmfr0_parange_to_ipa_size(ARMISARegisters *isar)
{
    uint32_t ipa_size = chosen_ipa_bit_size ?
            chosen_ipa_bit_size : hvf_arm_get_max_ipa_bit_size();
    uint64_t id_aa64mmfr0;

    /* Clamp down the PARange to the IPA size the kernel supports. */
    uint8_t index = round_down_to_parange_index(ipa_size);
    id_aa64mmfr0 = GET_IDREG(isar, ID_AA64MMFR0);
    id_aa64mmfr0 = (id_aa64mmfr0 & ~R_ID_AA64MMFR0_PARANGE_MASK) | index;
    SET_IDREG(isar, ID_AA64MMFR0, id_aa64mmfr0);
}

static bool hvf_arm_get_host_cpu_features(ARMHostCPUFeatures *ahcf)
{
    ARMISARegisters host_isar = {};
    static const struct isar_regs {
        hv_feature_reg_t reg;
        ARMIDRegisterIdx index;
    } regs[] = {
        { HV_FEATURE_REG_ID_AA64PFR0_EL1, ID_AA64PFR0_EL1_IDX },
        { HV_FEATURE_REG_ID_AA64PFR1_EL1, ID_AA64PFR1_EL1_IDX },
        /* Add ID_AA64PFR2_EL1 here when HVF supports it */
        { HV_FEATURE_REG_ID_AA64DFR0_EL1, ID_AA64DFR0_EL1_IDX },
        { HV_FEATURE_REG_ID_AA64DFR1_EL1, ID_AA64DFR1_EL1_IDX },
        { HV_FEATURE_REG_ID_AA64ISAR0_EL1, ID_AA64ISAR0_EL1_IDX },
        { HV_FEATURE_REG_ID_AA64ISAR1_EL1, ID_AA64ISAR1_EL1_IDX },
        /* Add ID_AA64ISAR2_EL1 here when HVF supports it */
        { HV_FEATURE_REG_ID_AA64MMFR0_EL1, ID_AA64MMFR0_EL1_IDX },
        { HV_FEATURE_REG_ID_AA64MMFR1_EL1, ID_AA64MMFR1_EL1_IDX },
        { HV_FEATURE_REG_ID_AA64MMFR2_EL1, ID_AA64MMFR2_EL1_IDX },
        /* Add ID_AA64MMFR3_EL1 here when HVF supports it */
    };
    hv_return_t r = HV_SUCCESS;
    hv_vcpu_config_t config = hv_vcpu_config_create();
    uint64_t t;
    int i;

    ahcf->dtb_compatible = "arm,armv8";
    ahcf->features = (1ULL << ARM_FEATURE_V8) |
                     (1ULL << ARM_FEATURE_NEON) |
                     (1ULL << ARM_FEATURE_AARCH64) |
                     (1ULL << ARM_FEATURE_PMU) |
                     (1ULL << ARM_FEATURE_GENERIC_TIMER);

    for (i = 0; i < ARRAY_SIZE(regs); i++) {
        r |= hv_vcpu_config_get_feature_reg(config, regs[i].reg,
                                            &host_isar.idregs[regs[i].index]);
    }
    os_release(config);

    /*
     * Hardcode MIDR because Apple deliberately doesn't expose a divergent
     * MIDR across systems.
     */
    t = FIELD_DP64(0, MIDR_EL1, IMPLEMENTER, 0x61); /* Apple */
    t = FIELD_DP64(t, MIDR_EL1, ARCHITECTURE, 0xf); /* v7 or later */
    t = FIELD_DP64(t, MIDR_EL1, PARTNUM, 0);
    t = FIELD_DP64(t, MIDR_EL1, VARIANT, 0);
    t = FIELD_DP64(t, MIDR_EL1, REVISION, 0);
    ahcf->midr = t;

    clamp_id_aa64mmfr0_parange_to_ipa_size(&host_isar);

    /*
     * Disable SME, which is not properly handled by QEMU hvf yet.
     * To allow this through we would need to:
     * - make sure that the SME state is correctly handled in the
     *   get_registers/put_registers functions
     * - get the SME-specific CPU properties to work with accelerators
     *   other than TCG
     * - fix any assumptions we made that SME implies SVE (since
     *   on the M4 there is SME but not SVE)
     */
    SET_IDREG(&host_isar, ID_AA64PFR1,
              GET_IDREG(&host_isar, ID_AA64PFR1) & ~R_ID_AA64PFR1_SME_MASK);

    ahcf->isar = host_isar;

    /*
     * A scratch vCPU returns SCTLR 0, so let's fill our default with the M1
     * boot SCTLR from https://github.com/AsahiLinux/m1n1/issues/97
     */
    ahcf->reset_sctlr = 0x30100180;
    /*
     * SPAN is disabled by default when SCTLR.SPAN=1. To improve compatibility,
     * let's disable it on boot and then allow guest software to turn it on by
     * setting it to 0.
     */
    ahcf->reset_sctlr |= 0x00800000;

    /* Make sure we don't advertise AArch32 support for EL0/EL1 */
    if ((GET_IDREG(&host_isar, ID_AA64PFR0) & 0xff) != 0x11) {
        return false;
    }

    return r == HV_SUCCESS;
}

uint32_t hvf_arm_get_default_ipa_bit_size(void)
{
    uint32_t default_ipa_size;
    hv_return_t ret = hv_vm_config_get_default_ipa_size(&default_ipa_size);
    assert_hvf_ok(ret);

    return default_ipa_size;
}

uint32_t hvf_arm_get_max_ipa_bit_size(void)
{
    uint32_t max_ipa_size;
    hv_return_t ret = hv_vm_config_get_max_ipa_size(&max_ipa_size);
    assert_hvf_ok(ret);

    /*
     * We clamp any IPA size we want to back the VM with to a valid PARange
     * value so the guest doesn't try and map memory outside of the valid range.
     * This logic just clamps the passed in IPA bit size to the first valid
     * PARange value <= to it.
     */
    return round_down_to_parange_bit_size(max_ipa_size);
}

void hvf_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    if (!arm_host_cpu_features.dtb_compatible) {
        if (!hvf_enabled() ||
            !hvf_arm_get_host_cpu_features(&arm_host_cpu_features)) {
            /*
             * We can't report this error yet, so flag that we need to
             * in arm_cpu_realizefn().
             */
            cpu->host_cpu_probe_failed = true;
            return;
        }
    }

    cpu->dtb_compatible = arm_host_cpu_features.dtb_compatible;
    cpu->isar = arm_host_cpu_features.isar;
    cpu->env.features = arm_host_cpu_features.features;
    cpu->midr = arm_host_cpu_features.midr;
    cpu->reset_sctlr = arm_host_cpu_features.reset_sctlr;
}

void hvf_arch_vcpu_destroy(CPUState *cpu)
{
    hv_return_t ret;

    ret = hv_vcpu_destroy(cpu->accel->fd);
    assert_hvf_ok(ret);
}

hv_return_t hvf_arch_vm_create(MachineState *ms, uint32_t pa_range)
{
    hv_return_t ret;
    hv_vm_config_t config = hv_vm_config_create();

    ret = hv_vm_config_set_ipa_size(config, pa_range);
    if (ret != HV_SUCCESS) {
        goto cleanup;
    }
    chosen_ipa_bit_size = pa_range;

    ret = hv_vm_create(config);

cleanup:
    os_release(config);

    return ret;
}

int hvf_arch_init_vcpu(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    uint32_t sregs_match_len = ARRAY_SIZE(hvf_sreg_list);
    uint32_t sregs_cnt = 0;
    uint64_t pfr;
    hv_return_t ret;
    int i;

    env->aarch64 = true;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(arm_cpu->gt_cntfrq_hz));

    /* Allocate enough space for our sysreg sync */
    arm_cpu->cpreg_indexes = g_renew(uint64_t, arm_cpu->cpreg_indexes,
                                     sregs_match_len);
    arm_cpu->cpreg_values = g_renew(uint64_t, arm_cpu->cpreg_values,
                                    sregs_match_len);
    arm_cpu->cpreg_vmstate_indexes = g_renew(uint64_t,
                                             arm_cpu->cpreg_vmstate_indexes,
                                             sregs_match_len);
    arm_cpu->cpreg_vmstate_values = g_renew(uint64_t,
                                            arm_cpu->cpreg_vmstate_values,
                                            sregs_match_len);

    memset(arm_cpu->cpreg_values, 0, sregs_match_len * sizeof(uint64_t));

    /* Populate cp list for all known sysregs */
    for (i = 0; i < sregs_match_len; i++) {
        hv_sys_reg_t hvf_id = hvf_sreg_list[i];
        uint64_t kvm_id = HVF_TO_KVMID(hvf_id);
        uint32_t key = kvm_to_cpreg_id(kvm_id);
        const ARMCPRegInfo *ri = get_arm_cp_reginfo(arm_cpu->cp_regs, key);

        if (ri) {
            assert(!(ri->type & ARM_CP_NO_RAW));
            arm_cpu->cpreg_indexes[sregs_cnt++] = kvm_id;
        }
    }
    arm_cpu->cpreg_array_len = sregs_cnt;
    arm_cpu->cpreg_vmstate_array_len = sregs_cnt;

    /* cpreg tuples must be in strictly ascending order */
    qsort(arm_cpu->cpreg_indexes, sregs_cnt, sizeof(uint64_t), compare_u64);

    assert(write_cpustate_to_list(arm_cpu, false));

    /* Set CP_NO_RAW system registers on init */
    ret = hv_vcpu_set_sys_reg(cpu->accel->fd, HV_SYS_REG_MIDR_EL1,
                              arm_cpu->midr);
    assert_hvf_ok(ret);

    ret = hv_vcpu_set_sys_reg(cpu->accel->fd, HV_SYS_REG_MPIDR_EL1,
                              arm_cpu->mp_affinity);
    assert_hvf_ok(ret);

    ret = hv_vcpu_get_sys_reg(cpu->accel->fd, HV_SYS_REG_ID_AA64PFR0_EL1, &pfr);
    assert_hvf_ok(ret);
    pfr |= env->gicv3state ? (1 << 24) : 0;
    ret = hv_vcpu_set_sys_reg(cpu->accel->fd, HV_SYS_REG_ID_AA64PFR0_EL1, pfr);
    assert_hvf_ok(ret);

    /* We're limited to underlying hardware caps, override internal versions */
    ret = hv_vcpu_get_sys_reg(cpu->accel->fd, HV_SYS_REG_ID_AA64MMFR0_EL1,
                              &arm_cpu->isar.idregs[ID_AA64MMFR0_EL1_IDX]);
    assert_hvf_ok(ret);

    clamp_id_aa64mmfr0_parange_to_ipa_size(&arm_cpu->isar);
    ret = hv_vcpu_set_sys_reg(cpu->accel->fd, HV_SYS_REG_ID_AA64MMFR0_EL1,
                              arm_cpu->isar.idregs[ID_AA64MMFR0_EL1_IDX]);
    assert_hvf_ok(ret);

    return 0;
}

void hvf_kick_vcpu_thread(CPUState *cpu)
{
    hv_return_t ret;
    trace_hvf_kick_vcpu_thread(cpu->cpu_index, cpu->stop);
    cpus_kick_thread(cpu);
    ret = hv_vcpus_exit(&cpu->accel->fd, 1);
    assert_hvf_ok(ret);
}

static void hvf_raise_exception(CPUState *cpu, uint32_t excp,
                                uint32_t syndrome, int target_el)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    cpu->exception_index = excp;
    env->exception.target_el = target_el;
    env->exception.syndrome = syndrome;

    arm_cpu_do_interrupt(cpu);
}

static void hvf_psci_cpu_off(ARMCPU *arm_cpu)
{
    int32_t ret = arm_set_cpu_off(arm_cpu_mp_affinity(arm_cpu));
    assert(ret == QEMU_ARM_POWERCTL_RET_SUCCESS);
}

/*
 * Handle a PSCI call.
 *
 * Returns 0 on success
 *         -1 when the PSCI call is unknown,
 */
static bool hvf_handle_psci_call(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    uint64_t param[4] = {
        env->xregs[0],
        env->xregs[1],
        env->xregs[2],
        env->xregs[3]
    };
    uint64_t context_id, mpidr;
    bool target_aarch64 = true;
    CPUState *target_cpu_state;
    ARMCPU *target_cpu;
    target_ulong entry;
    int target_el = 1;
    int32_t ret = 0;

    trace_arm_psci_call(param[0], param[1], param[2], param[3],
                        arm_cpu_mp_affinity(arm_cpu));

    switch (param[0]) {
    case QEMU_PSCI_0_2_FN_PSCI_VERSION:
        ret = QEMU_PSCI_VERSION_1_1;
        break;
    case QEMU_PSCI_0_2_FN_MIGRATE_INFO_TYPE:
        ret = QEMU_PSCI_0_2_RET_TOS_MIGRATION_NOT_REQUIRED; /* No trusted OS */
        break;
    case QEMU_PSCI_0_2_FN_AFFINITY_INFO:
    case QEMU_PSCI_0_2_FN64_AFFINITY_INFO:
        mpidr = param[1];

        switch (param[2]) {
        case 0:
            target_cpu_state = arm_get_cpu_by_id(mpidr);
            if (!target_cpu_state) {
                ret = QEMU_PSCI_RET_INVALID_PARAMS;
                break;
            }
            target_cpu = ARM_CPU(target_cpu_state);

            ret = target_cpu->power_state;
            break;
        default:
            /* Everything above affinity level 0 is always on. */
            ret = 0;
        }
        break;
    case QEMU_PSCI_0_2_FN_SYSTEM_RESET:
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        /*
         * QEMU reset and shutdown are async requests, but PSCI
         * mandates that we never return from the reset/shutdown
         * call, so power the CPU off now so it doesn't execute
         * anything further.
         */
        hvf_psci_cpu_off(arm_cpu);
        break;
    case QEMU_PSCI_0_2_FN_SYSTEM_OFF:
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        hvf_psci_cpu_off(arm_cpu);
        break;
    case QEMU_PSCI_0_1_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN64_CPU_ON:
        mpidr = param[1];
        entry = param[2];
        context_id = param[3];
        ret = arm_set_cpu_on(mpidr, entry, context_id,
                             target_el, target_aarch64);
        break;
    case QEMU_PSCI_0_1_FN_CPU_OFF:
    case QEMU_PSCI_0_2_FN_CPU_OFF:
        hvf_psci_cpu_off(arm_cpu);
        break;
    case QEMU_PSCI_0_1_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN64_CPU_SUSPEND:
        /* Affinity levels are not supported in QEMU */
        if (param[1] & 0xfffe0000) {
            ret = QEMU_PSCI_RET_INVALID_PARAMS;
            break;
        }
        /* Powerdown is not supported, we always go into WFI */
        env->xregs[0] = 0;
        hvf_wfi(cpu);
        break;
    case QEMU_PSCI_0_1_FN_MIGRATE:
    case QEMU_PSCI_0_2_FN_MIGRATE:
        ret = QEMU_PSCI_RET_NOT_SUPPORTED;
        break;
    case QEMU_PSCI_1_0_FN_PSCI_FEATURES:
        switch (param[1]) {
        case QEMU_PSCI_0_2_FN_PSCI_VERSION:
        case QEMU_PSCI_0_2_FN_MIGRATE_INFO_TYPE:
        case QEMU_PSCI_0_2_FN_AFFINITY_INFO:
        case QEMU_PSCI_0_2_FN64_AFFINITY_INFO:
        case QEMU_PSCI_0_2_FN_SYSTEM_RESET:
        case QEMU_PSCI_0_2_FN_SYSTEM_OFF:
        case QEMU_PSCI_0_1_FN_CPU_ON:
        case QEMU_PSCI_0_2_FN_CPU_ON:
        case QEMU_PSCI_0_2_FN64_CPU_ON:
        case QEMU_PSCI_0_1_FN_CPU_OFF:
        case QEMU_PSCI_0_2_FN_CPU_OFF:
        case QEMU_PSCI_0_1_FN_CPU_SUSPEND:
        case QEMU_PSCI_0_2_FN_CPU_SUSPEND:
        case QEMU_PSCI_0_2_FN64_CPU_SUSPEND:
        case QEMU_PSCI_1_0_FN_PSCI_FEATURES:
            ret = 0;
            break;
        case QEMU_PSCI_0_1_FN_MIGRATE:
        case QEMU_PSCI_0_2_FN_MIGRATE:
        default:
            ret = QEMU_PSCI_RET_NOT_SUPPORTED;
        }
        break;
    default:
        return false;
    }

    env->xregs[0] = ret;
    return true;
}

static bool is_id_sysreg(uint32_t reg)
{
    return SYSREG_OP0(reg) == 3 &&
           SYSREG_OP1(reg) == 0 &&
           SYSREG_CRN(reg) == 0 &&
           SYSREG_CRM(reg) >= 1 &&
           SYSREG_CRM(reg) < 8;
}

static uint32_t hvf_reg2cp_reg(uint32_t reg)
{
    return ENCODE_AA64_CP_REG((reg >> SYSREG_OP0_SHIFT) & SYSREG_OP0_MASK,
                              (reg >> SYSREG_OP1_SHIFT) & SYSREG_OP1_MASK,
                              (reg >> SYSREG_CRN_SHIFT) & SYSREG_CRN_MASK,
                              (reg >> SYSREG_CRM_SHIFT) & SYSREG_CRM_MASK,
                              (reg >> SYSREG_OP2_SHIFT) & SYSREG_OP2_MASK);
}

static bool hvf_sysreg_read_cp(CPUState *cpu, const char *cpname,
                               uint32_t reg, uint64_t *val)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    const ARMCPRegInfo *ri;

    ri = get_arm_cp_reginfo(arm_cpu->cp_regs, hvf_reg2cp_reg(reg));
    if (ri) {
        if (!cp_access_ok(1, ri, true)) {
            return false;
        }
        if (ri->accessfn) {
            if (ri->accessfn(env, ri, true) != CP_ACCESS_OK) {
                return false;
            }
        }
        if (ri->type & ARM_CP_CONST) {
            *val = ri->resetvalue;
        } else if (ri->readfn) {
            *val = ri->readfn(env, ri);
        } else {
            *val = raw_read(env, ri);
        }
        trace_hvf_emu_reginfo_read(cpname, ri->name, *val);
        return true;
    }

    return false;
}

static int hvf_sysreg_read(CPUState *cpu, uint32_t reg, uint64_t *val)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    if (arm_feature(env, ARM_FEATURE_PMU)) {
        switch (reg) {
        case SYSREG_PMCR_EL0:
            *val = env->cp15.c9_pmcr;
            return 0;
        case SYSREG_PMCCNTR_EL0:
            pmu_op_start(env);
            *val = env->cp15.c15_ccnt;
            pmu_op_finish(env);
            return 0;
        case SYSREG_PMCNTENCLR_EL0:
            *val = env->cp15.c9_pmcnten;
            return 0;
        case SYSREG_PMOVSCLR_EL0:
            *val = env->cp15.c9_pmovsr;
            return 0;
        case SYSREG_PMSELR_EL0:
            *val = env->cp15.c9_pmselr;
            return 0;
        case SYSREG_PMINTENCLR_EL1:
            *val = env->cp15.c9_pminten;
            return 0;
        case SYSREG_PMCCFILTR_EL0:
            *val = env->cp15.pmccfiltr_el0;
            return 0;
        case SYSREG_PMCNTENSET_EL0:
            *val = env->cp15.c9_pmcnten;
            return 0;
        case SYSREG_PMUSERENR_EL0:
            *val = env->cp15.c9_pmuserenr;
            return 0;
        case SYSREG_PMCEID0_EL0:
        case SYSREG_PMCEID1_EL0:
            /* We can't really count anything yet, declare all events invalid */
            *val = 0;
            return 0;
        }
    }

    switch (reg) {
    case SYSREG_CNTPCT_EL0:
        *val = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) /
              gt_cntfrq_period_ns(arm_cpu);
        return 0;
    case SYSREG_OSLSR_EL1:
        *val = env->cp15.oslsr_el1;
        return 0;
    case SYSREG_OSDLR_EL1:
        /* Dummy register */
        return 0;
    case SYSREG_ICC_AP0R0_EL1:
    case SYSREG_ICC_AP0R1_EL1:
    case SYSREG_ICC_AP0R2_EL1:
    case SYSREG_ICC_AP0R3_EL1:
    case SYSREG_ICC_AP1R0_EL1:
    case SYSREG_ICC_AP1R1_EL1:
    case SYSREG_ICC_AP1R2_EL1:
    case SYSREG_ICC_AP1R3_EL1:
    case SYSREG_ICC_ASGI1R_EL1:
    case SYSREG_ICC_BPR0_EL1:
    case SYSREG_ICC_BPR1_EL1:
    case SYSREG_ICC_DIR_EL1:
    case SYSREG_ICC_EOIR0_EL1:
    case SYSREG_ICC_EOIR1_EL1:
    case SYSREG_ICC_HPPIR0_EL1:
    case SYSREG_ICC_HPPIR1_EL1:
    case SYSREG_ICC_IAR0_EL1:
    case SYSREG_ICC_IAR1_EL1:
    case SYSREG_ICC_IGRPEN0_EL1:
    case SYSREG_ICC_IGRPEN1_EL1:
    case SYSREG_ICC_PMR_EL1:
    case SYSREG_ICC_RPR_EL1:
    case SYSREG_ICC_SGI0R_EL1:
    case SYSREG_ICC_SGI1R_EL1:
    case SYSREG_ICC_SRE_EL1:
    case SYSREG_ICC_CTLR_EL1:
        /* Call the TCG sysreg handler. This is only safe for GICv3 regs. */
        if (hvf_sysreg_read_cp(cpu, "GICv3", reg, val)) {
            return 0;
        }
        break;
    case SYSREG_DBGBVR0_EL1:
    case SYSREG_DBGBVR1_EL1:
    case SYSREG_DBGBVR2_EL1:
    case SYSREG_DBGBVR3_EL1:
    case SYSREG_DBGBVR4_EL1:
    case SYSREG_DBGBVR5_EL1:
    case SYSREG_DBGBVR6_EL1:
    case SYSREG_DBGBVR7_EL1:
    case SYSREG_DBGBVR8_EL1:
    case SYSREG_DBGBVR9_EL1:
    case SYSREG_DBGBVR10_EL1:
    case SYSREG_DBGBVR11_EL1:
    case SYSREG_DBGBVR12_EL1:
    case SYSREG_DBGBVR13_EL1:
    case SYSREG_DBGBVR14_EL1:
    case SYSREG_DBGBVR15_EL1:
        *val = env->cp15.dbgbvr[SYSREG_CRM(reg)];
        return 0;
    case SYSREG_DBGBCR0_EL1:
    case SYSREG_DBGBCR1_EL1:
    case SYSREG_DBGBCR2_EL1:
    case SYSREG_DBGBCR3_EL1:
    case SYSREG_DBGBCR4_EL1:
    case SYSREG_DBGBCR5_EL1:
    case SYSREG_DBGBCR6_EL1:
    case SYSREG_DBGBCR7_EL1:
    case SYSREG_DBGBCR8_EL1:
    case SYSREG_DBGBCR9_EL1:
    case SYSREG_DBGBCR10_EL1:
    case SYSREG_DBGBCR11_EL1:
    case SYSREG_DBGBCR12_EL1:
    case SYSREG_DBGBCR13_EL1:
    case SYSREG_DBGBCR14_EL1:
    case SYSREG_DBGBCR15_EL1:
        *val = env->cp15.dbgbcr[SYSREG_CRM(reg)];
        return 0;
    case SYSREG_DBGWVR0_EL1:
    case SYSREG_DBGWVR1_EL1:
    case SYSREG_DBGWVR2_EL1:
    case SYSREG_DBGWVR3_EL1:
    case SYSREG_DBGWVR4_EL1:
    case SYSREG_DBGWVR5_EL1:
    case SYSREG_DBGWVR6_EL1:
    case SYSREG_DBGWVR7_EL1:
    case SYSREG_DBGWVR8_EL1:
    case SYSREG_DBGWVR9_EL1:
    case SYSREG_DBGWVR10_EL1:
    case SYSREG_DBGWVR11_EL1:
    case SYSREG_DBGWVR12_EL1:
    case SYSREG_DBGWVR13_EL1:
    case SYSREG_DBGWVR14_EL1:
    case SYSREG_DBGWVR15_EL1:
        *val = env->cp15.dbgwvr[SYSREG_CRM(reg)];
        return 0;
    case SYSREG_DBGWCR0_EL1:
    case SYSREG_DBGWCR1_EL1:
    case SYSREG_DBGWCR2_EL1:
    case SYSREG_DBGWCR3_EL1:
    case SYSREG_DBGWCR4_EL1:
    case SYSREG_DBGWCR5_EL1:
    case SYSREG_DBGWCR6_EL1:
    case SYSREG_DBGWCR7_EL1:
    case SYSREG_DBGWCR8_EL1:
    case SYSREG_DBGWCR9_EL1:
    case SYSREG_DBGWCR10_EL1:
    case SYSREG_DBGWCR11_EL1:
    case SYSREG_DBGWCR12_EL1:
    case SYSREG_DBGWCR13_EL1:
    case SYSREG_DBGWCR14_EL1:
    case SYSREG_DBGWCR15_EL1:
        *val = env->cp15.dbgwcr[SYSREG_CRM(reg)];
        return 0;
    default:
        if (is_id_sysreg(reg)) {
            /* ID system registers read as RES0 */
            *val = 0;
            return 0;
        }
    }

    cpu_synchronize_state(cpu);
    trace_hvf_unhandled_sysreg_read(env->pc, reg,
                                    SYSREG_OP0(reg),
                                    SYSREG_OP1(reg),
                                    SYSREG_CRN(reg),
                                    SYSREG_CRM(reg),
                                    SYSREG_OP2(reg));
    hvf_raise_exception(cpu, EXCP_UDEF, syn_uncategorized(), 1);
    return 1;
}

static void pmu_update_irq(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    qemu_set_irq(cpu->pmu_interrupt, (env->cp15.c9_pmcr & PMCRE) &&
            (env->cp15.c9_pminten & env->cp15.c9_pmovsr));
}

static bool pmu_event_supported(uint16_t number)
{
    return false;
}

/* Returns true if the counter (pass 31 for PMCCNTR) should count events using
 * the current EL, security state, and register configuration.
 */
static bool pmu_counter_enabled(CPUARMState *env, uint8_t counter)
{
    uint64_t filter;
    bool enabled, filtered = true;
    int el = arm_current_el(env);

    enabled = (env->cp15.c9_pmcr & PMCRE) &&
              (env->cp15.c9_pmcnten & (1 << counter));

    if (counter == 31) {
        filter = env->cp15.pmccfiltr_el0;
    } else {
        filter = env->cp15.c14_pmevtyper[counter];
    }

    if (el == 0) {
        filtered = filter & PMXEVTYPER_U;
    } else if (el == 1) {
        filtered = filter & PMXEVTYPER_P;
    }

    if (counter != 31) {
        /*
         * If not checking PMCCNTR, ensure the counter is setup to an event we
         * support
         */
        uint16_t event = filter & PMXEVTYPER_EVTCOUNT;
        if (!pmu_event_supported(event)) {
            return false;
        }
    }

    return enabled && !filtered;
}

static void pmswinc_write(CPUARMState *env, uint64_t value)
{
    unsigned int i;
    for (i = 0; i < pmu_num_counters(env); i++) {
        /* Increment a counter's count iff: */
        if ((value & (1 << i)) && /* counter's bit is set */
                /* counter is enabled and not filtered */
                pmu_counter_enabled(env, i) &&
                /* counter is SW_INCR */
                (env->cp15.c14_pmevtyper[i] & PMXEVTYPER_EVTCOUNT) == 0x0) {
            /*
             * Detect if this write causes an overflow since we can't predict
             * PMSWINC overflows like we can for other events
             */
            uint32_t new_pmswinc = env->cp15.c14_pmevcntr[i] + 1;

            if (env->cp15.c14_pmevcntr[i] & ~new_pmswinc & INT32_MIN) {
                env->cp15.c9_pmovsr |= (1 << i);
                pmu_update_irq(env);
            }

            env->cp15.c14_pmevcntr[i] = new_pmswinc;
        }
    }
}

static bool hvf_sysreg_write_cp(CPUState *cpu, const char *cpname,
                                uint32_t reg, uint64_t val)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    const ARMCPRegInfo *ri;

    ri = get_arm_cp_reginfo(arm_cpu->cp_regs, hvf_reg2cp_reg(reg));

    if (ri) {
        if (!cp_access_ok(1, ri, false)) {
            return false;
        }
        if (ri->accessfn) {
            if (ri->accessfn(env, ri, false) != CP_ACCESS_OK) {
                return false;
            }
        }
        if (ri->writefn) {
            ri->writefn(env, ri, val);
        } else {
            raw_write(env, ri, val);
        }

        trace_hvf_emu_reginfo_write(cpname, ri->name, val);
        return true;
    }

    return false;
}

static int hvf_sysreg_write(CPUState *cpu, uint32_t reg, uint64_t val)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    trace_hvf_sysreg_write(reg,
                           SYSREG_OP0(reg),
                           SYSREG_OP1(reg),
                           SYSREG_CRN(reg),
                           SYSREG_CRM(reg),
                           SYSREG_OP2(reg),
                           val);

    if (arm_feature(env, ARM_FEATURE_PMU)) {
        switch (reg) {
        case SYSREG_PMCCNTR_EL0:
            pmu_op_start(env);
            env->cp15.c15_ccnt = val;
            pmu_op_finish(env);
            return 0;
        case SYSREG_PMCR_EL0:
            pmu_op_start(env);

            if (val & PMCRC) {
                /* The counter has been reset */
                env->cp15.c15_ccnt = 0;
            }

            if (val & PMCRP) {
                unsigned int i;
                for (i = 0; i < pmu_num_counters(env); i++) {
                    env->cp15.c14_pmevcntr[i] = 0;
                }
            }

            env->cp15.c9_pmcr &= ~PMCR_WRITABLE_MASK;
            env->cp15.c9_pmcr |= (val & PMCR_WRITABLE_MASK);

            pmu_op_finish(env);
            return 0;
        case SYSREG_PMUSERENR_EL0:
            env->cp15.c9_pmuserenr = val & 0xf;
            return 0;
        case SYSREG_PMCNTENSET_EL0:
            env->cp15.c9_pmcnten |= (val & pmu_counter_mask(env));
            return 0;
        case SYSREG_PMCNTENCLR_EL0:
            env->cp15.c9_pmcnten &= ~(val & pmu_counter_mask(env));
            return 0;
        case SYSREG_PMINTENCLR_EL1:
            pmu_op_start(env);
            env->cp15.c9_pminten |= val;
            pmu_op_finish(env);
            return 0;
        case SYSREG_PMOVSCLR_EL0:
            pmu_op_start(env);
            env->cp15.c9_pmovsr &= ~val;
            pmu_op_finish(env);
            return 0;
        case SYSREG_PMSWINC_EL0:
            pmu_op_start(env);
            pmswinc_write(env, val);
            pmu_op_finish(env);
            return 0;
        case SYSREG_PMSELR_EL0:
            env->cp15.c9_pmselr = val & 0x1f;
            return 0;
        case SYSREG_PMCCFILTR_EL0:
            pmu_op_start(env);
            env->cp15.pmccfiltr_el0 = val & PMCCFILTR_EL0;
            pmu_op_finish(env);
            return 0;
        }
    }

    switch (reg) {
    case SYSREG_OSLAR_EL1:
        env->cp15.oslsr_el1 = val & 1;
        return 0;
    case SYSREG_CNTP_CTL_EL0:
        /*
         * Guests should not rely on the physical counter, but macOS emits
         * disable writes to it. Let it do so, but ignore the requests.
         */
        qemu_log_mask(LOG_UNIMP, "Unsupported write to CNTP_CTL_EL0\n");
        return 0;
    case SYSREG_OSDLR_EL1:
        /* Dummy register */
        return 0;
    case SYSREG_LORC_EL1:
        /* Dummy register */
        return 0;
    case SYSREG_ICC_AP0R0_EL1:
    case SYSREG_ICC_AP0R1_EL1:
    case SYSREG_ICC_AP0R2_EL1:
    case SYSREG_ICC_AP0R3_EL1:
    case SYSREG_ICC_AP1R0_EL1:
    case SYSREG_ICC_AP1R1_EL1:
    case SYSREG_ICC_AP1R2_EL1:
    case SYSREG_ICC_AP1R3_EL1:
    case SYSREG_ICC_ASGI1R_EL1:
    case SYSREG_ICC_BPR0_EL1:
    case SYSREG_ICC_BPR1_EL1:
    case SYSREG_ICC_CTLR_EL1:
    case SYSREG_ICC_DIR_EL1:
    case SYSREG_ICC_EOIR0_EL1:
    case SYSREG_ICC_EOIR1_EL1:
    case SYSREG_ICC_HPPIR0_EL1:
    case SYSREG_ICC_HPPIR1_EL1:
    case SYSREG_ICC_IAR0_EL1:
    case SYSREG_ICC_IAR1_EL1:
    case SYSREG_ICC_IGRPEN0_EL1:
    case SYSREG_ICC_IGRPEN1_EL1:
    case SYSREG_ICC_PMR_EL1:
    case SYSREG_ICC_RPR_EL1:
    case SYSREG_ICC_SGI0R_EL1:
    case SYSREG_ICC_SGI1R_EL1:
    case SYSREG_ICC_SRE_EL1:
        /* Call the TCG sysreg handler. This is only safe for GICv3 regs. */
        if (hvf_sysreg_write_cp(cpu, "GICv3", reg, val)) {
            return 0;
        }
        break;
    case SYSREG_MDSCR_EL1:
        env->cp15.mdscr_el1 = val;
        return 0;
    case SYSREG_DBGBVR0_EL1:
    case SYSREG_DBGBVR1_EL1:
    case SYSREG_DBGBVR2_EL1:
    case SYSREG_DBGBVR3_EL1:
    case SYSREG_DBGBVR4_EL1:
    case SYSREG_DBGBVR5_EL1:
    case SYSREG_DBGBVR6_EL1:
    case SYSREG_DBGBVR7_EL1:
    case SYSREG_DBGBVR8_EL1:
    case SYSREG_DBGBVR9_EL1:
    case SYSREG_DBGBVR10_EL1:
    case SYSREG_DBGBVR11_EL1:
    case SYSREG_DBGBVR12_EL1:
    case SYSREG_DBGBVR13_EL1:
    case SYSREG_DBGBVR14_EL1:
    case SYSREG_DBGBVR15_EL1:
        env->cp15.dbgbvr[SYSREG_CRM(reg)] = val;
        return 0;
    case SYSREG_DBGBCR0_EL1:
    case SYSREG_DBGBCR1_EL1:
    case SYSREG_DBGBCR2_EL1:
    case SYSREG_DBGBCR3_EL1:
    case SYSREG_DBGBCR4_EL1:
    case SYSREG_DBGBCR5_EL1:
    case SYSREG_DBGBCR6_EL1:
    case SYSREG_DBGBCR7_EL1:
    case SYSREG_DBGBCR8_EL1:
    case SYSREG_DBGBCR9_EL1:
    case SYSREG_DBGBCR10_EL1:
    case SYSREG_DBGBCR11_EL1:
    case SYSREG_DBGBCR12_EL1:
    case SYSREG_DBGBCR13_EL1:
    case SYSREG_DBGBCR14_EL1:
    case SYSREG_DBGBCR15_EL1:
        env->cp15.dbgbcr[SYSREG_CRM(reg)] = val;
        return 0;
    case SYSREG_DBGWVR0_EL1:
    case SYSREG_DBGWVR1_EL1:
    case SYSREG_DBGWVR2_EL1:
    case SYSREG_DBGWVR3_EL1:
    case SYSREG_DBGWVR4_EL1:
    case SYSREG_DBGWVR5_EL1:
    case SYSREG_DBGWVR6_EL1:
    case SYSREG_DBGWVR7_EL1:
    case SYSREG_DBGWVR8_EL1:
    case SYSREG_DBGWVR9_EL1:
    case SYSREG_DBGWVR10_EL1:
    case SYSREG_DBGWVR11_EL1:
    case SYSREG_DBGWVR12_EL1:
    case SYSREG_DBGWVR13_EL1:
    case SYSREG_DBGWVR14_EL1:
    case SYSREG_DBGWVR15_EL1:
        env->cp15.dbgwvr[SYSREG_CRM(reg)] = val;
        return 0;
    case SYSREG_DBGWCR0_EL1:
    case SYSREG_DBGWCR1_EL1:
    case SYSREG_DBGWCR2_EL1:
    case SYSREG_DBGWCR3_EL1:
    case SYSREG_DBGWCR4_EL1:
    case SYSREG_DBGWCR5_EL1:
    case SYSREG_DBGWCR6_EL1:
    case SYSREG_DBGWCR7_EL1:
    case SYSREG_DBGWCR8_EL1:
    case SYSREG_DBGWCR9_EL1:
    case SYSREG_DBGWCR10_EL1:
    case SYSREG_DBGWCR11_EL1:
    case SYSREG_DBGWCR12_EL1:
    case SYSREG_DBGWCR13_EL1:
    case SYSREG_DBGWCR14_EL1:
    case SYSREG_DBGWCR15_EL1:
        env->cp15.dbgwcr[SYSREG_CRM(reg)] = val;
        return 0;
    }

    cpu_synchronize_state(cpu);
    trace_hvf_unhandled_sysreg_write(env->pc, reg,
                                     SYSREG_OP0(reg),
                                     SYSREG_OP1(reg),
                                     SYSREG_CRN(reg),
                                     SYSREG_CRM(reg),
                                     SYSREG_OP2(reg));
    hvf_raise_exception(cpu, EXCP_UDEF, syn_uncategorized(), 1);
    return 1;
}

/* Must be called by the owning thread */
static int hvf_inject_interrupts(CPUState *cpu)
{
    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_FIQ)) {
        trace_hvf_inject_fiq();
        hv_vcpu_set_pending_interrupt(cpu->accel->fd, HV_INTERRUPT_TYPE_FIQ,
                                      true);
    }

    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD)) {
        trace_hvf_inject_irq();
        hv_vcpu_set_pending_interrupt(cpu->accel->fd, HV_INTERRUPT_TYPE_IRQ,
                                      true);
    }

    return 0;
}

static uint64_t hvf_vtimer_val_raw(void)
{
    /*
     * mach_absolute_time() returns the vtimer value without the VM
     * offset that we define. Add our own offset on top.
     */
    return mach_absolute_time() - hvf_state->vtimer_offset;
}

static uint64_t hvf_vtimer_val(void)
{
    if (!runstate_is_running()) {
        /* VM is paused, the vtimer value is in vtimer.vtimer_val */
        return vtimer.vtimer_val;
    }

    return hvf_vtimer_val_raw();
}

static void hvf_wait_for_ipi(CPUState *cpu, struct timespec *ts)
{
    /*
     * Use pselect to sleep so that other threads can IPI us while we're
     * sleeping.
     */
    qatomic_set_mb(&cpu->thread_kicked, false);
    bql_unlock();
    pselect(0, 0, 0, 0, ts, &cpu->accel->unblock_ipi_mask);
    bql_lock();
}

static void hvf_wfi(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    struct timespec ts;
    hv_return_t r;
    uint64_t ctl;
    uint64_t cval;
    int64_t ticks_to_sleep;
    uint64_t seconds;
    uint64_t nanos;
    uint32_t cntfrq;

    if (cpu_test_interrupt(cpu, CPU_INTERRUPT_HARD | CPU_INTERRUPT_FIQ)) {
        /* Interrupt pending, no need to wait */
        return;
    }

    r = hv_vcpu_get_sys_reg(cpu->accel->fd, HV_SYS_REG_CNTV_CTL_EL0, &ctl);
    assert_hvf_ok(r);

    if (!(ctl & 1) || (ctl & 2)) {
        /* Timer disabled or masked, just wait for an IPI. */
        hvf_wait_for_ipi(cpu, NULL);
        return;
    }

    r = hv_vcpu_get_sys_reg(cpu->accel->fd, HV_SYS_REG_CNTV_CVAL_EL0, &cval);
    assert_hvf_ok(r);

    ticks_to_sleep = cval - hvf_vtimer_val();
    if (ticks_to_sleep < 0) {
        return;
    }

    cntfrq = gt_cntfrq_period_ns(arm_cpu);
    seconds = muldiv64(ticks_to_sleep, cntfrq, NANOSECONDS_PER_SECOND);
    ticks_to_sleep -= muldiv64(seconds, NANOSECONDS_PER_SECOND, cntfrq);
    nanos = ticks_to_sleep * cntfrq;

    /*
     * Don't sleep for less than the time a context switch would take,
     * so that we can satisfy fast timer requests on the same CPU.
     * Measurements on M1 show the sweet spot to be ~2ms.
     */
    if (!seconds && nanos < (2 * SCALE_MS)) {
        return;
    }

    ts = (struct timespec) { seconds, nanos };
    hvf_wait_for_ipi(cpu, &ts);
}

/* Must be called by the owning thread */
static void hvf_sync_vtimer(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    hv_return_t r;
    uint64_t ctl;
    bool irq_state;

    if (!cpu->accel->vtimer_masked) {
        /* We will get notified on vtimer changes by hvf, nothing to do */
        return;
    }

    r = hv_vcpu_get_sys_reg(cpu->accel->fd, HV_SYS_REG_CNTV_CTL_EL0, &ctl);
    assert_hvf_ok(r);

    irq_state = (ctl & (TMR_CTL_ENABLE | TMR_CTL_IMASK | TMR_CTL_ISTATUS)) ==
                (TMR_CTL_ENABLE | TMR_CTL_ISTATUS);
    qemu_set_irq(arm_cpu->gt_timer_outputs[GTIMER_VIRT], irq_state);

    if (!irq_state) {
        /* Timer no longer asserting, we can unmask it */
        r = hv_vcpu_set_vtimer_mask(cpu->accel->fd, false);
        assert_hvf_ok(r);
        cpu->accel->vtimer_masked = false;
    }
}

static int hvf_handle_exception(CPUState *cpu, hv_vcpu_exit_exception_t *excp)
{
    CPUARMState *env = cpu_env(cpu);
    ARMCPU *arm_cpu = env_archcpu(env);
    uint64_t syndrome = excp->syndrome;
    uint32_t ec = syn_get_ec(syndrome);
    bool advance_pc = false;
    hv_return_t r;
    int ret = 0;

    switch (ec) {
    case EC_SOFTWARESTEP: {
        ret = EXCP_DEBUG;

        if (!cpu->singlestep_enabled) {
            error_report("EC_SOFTWARESTEP but single-stepping not enabled");
        }
        break;
    }
    case EC_AA64_BKPT: {
        ret = EXCP_DEBUG;

        cpu_synchronize_state(cpu);

        if (!hvf_find_sw_breakpoint(cpu, env->pc)) {
            /* Re-inject into the guest */
            ret = 0;
            hvf_raise_exception(cpu, EXCP_BKPT, syn_aa64_bkpt(0), 1);
        }
        break;
    }
    case EC_BREAKPOINT: {
        ret = EXCP_DEBUG;

        cpu_synchronize_state(cpu);

        if (!find_hw_breakpoint(cpu, env->pc)) {
            error_report("EC_BREAKPOINT but unknown hw breakpoint");
        }
        break;
    }
    case EC_WATCHPOINT: {
        ret = EXCP_DEBUG;

        cpu_synchronize_state(cpu);

        CPUWatchpoint *wp =
            find_hw_watchpoint(cpu, excp->virtual_address);
        if (!wp) {
            error_report("EXCP_DEBUG but unknown hw watchpoint");
        }
        cpu->watchpoint_hit = wp;
        break;
    }
    case EC_DATAABORT: {
        bool isv = syndrome & ARM_EL_ISV;
        bool iswrite = (syndrome >> 6) & 1;
        bool s1ptw = (syndrome >> 7) & 1;
        bool sse = (syndrome >> 21) & 1;
        uint32_t sas = (syndrome >> 22) & 3;
        uint32_t len = 1 << sas;
        uint32_t srt = (syndrome >> 16) & 0x1f;
        uint32_t cm = (syndrome >> 8) & 0x1;
        uint64_t val = 0;

        trace_hvf_data_abort(excp->virtual_address,
                             excp->physical_address, isv,
                             iswrite, s1ptw, len, srt);

        if (cm) {
            /* We don't cache MMIO regions */
            advance_pc = true;
            break;
        }

        assert(isv);

        if (iswrite) {
            val = hvf_get_reg(cpu, srt);
            address_space_write(&address_space_memory,
                                excp->physical_address,
                                MEMTXATTRS_UNSPECIFIED, &val, len);
        } else {
            address_space_read(&address_space_memory,
                               excp->physical_address,
                               MEMTXATTRS_UNSPECIFIED, &val, len);
            if (sse) {
                val = sextract64(val, 0, len * 8);
            }
            hvf_set_reg(cpu, srt, val);
        }

        advance_pc = true;
        break;
    }
    case EC_SYSTEMREGISTERTRAP: {
        bool isread = (syndrome >> 0) & 1;
        uint32_t rt = (syndrome >> 5) & 0x1f;
        uint32_t reg = syndrome & SYSREG_MASK;
        uint64_t val;
        int sysreg_ret = 0;

        if (isread) {
            sysreg_ret = hvf_sysreg_read(cpu, reg, &val);
            if (!sysreg_ret) {
                trace_hvf_sysreg_read(reg,
                                      SYSREG_OP0(reg),
                                      SYSREG_OP1(reg),
                                      SYSREG_CRN(reg),
                                      SYSREG_CRM(reg),
                                      SYSREG_OP2(reg),
                                      val);
                hvf_set_reg(cpu, rt, val);
            }
        } else {
            val = hvf_get_reg(cpu, rt);
            sysreg_ret = hvf_sysreg_write(cpu, reg, val);
        }

        advance_pc = !sysreg_ret;
        break;
    }
    case EC_WFX_TRAP:
        advance_pc = true;
        if (!(syndrome & WFX_IS_WFE)) {
            hvf_wfi(cpu);
        }
        break;
    case EC_AA64_HVC:
        cpu_synchronize_state(cpu);
        if (arm_cpu->psci_conduit == QEMU_PSCI_CONDUIT_HVC) {
            /* Do NOT advance $pc for HVC */
            if (!hvf_handle_psci_call(cpu)) {
                trace_hvf_unknown_hvc(env->pc, env->xregs[0]);
                /* SMCCC 1.3 section 5.2 says every unknown SMCCC call returns -1 */
                env->xregs[0] = -1;
            }
            cpu->vcpu_dirty = true;
        } else {
            trace_hvf_unknown_hvc(env->pc, env->xregs[0]);
            hvf_raise_exception(cpu, EXCP_UDEF, syn_uncategorized(), 1);
        }
        break;
    case EC_AA64_SMC:
        cpu_synchronize_state(cpu);
        if (arm_cpu->psci_conduit == QEMU_PSCI_CONDUIT_SMC) {
            /* Secure Monitor Call exception, we need to advance $pc */
            advance_pc = true;

            if (!hvf_handle_psci_call(cpu)) {
                trace_hvf_unknown_smc(env->xregs[0]);
                /* SMCCC 1.3 section 5.2 says every unknown SMCCC call returns -1 */
                env->xregs[0] = -1;
            }
            cpu->vcpu_dirty = true;
        } else {
            trace_hvf_unknown_smc(env->xregs[0]);
            hvf_raise_exception(cpu, EXCP_UDEF, syn_uncategorized(), 1);
        }
        break;
    case EC_INSNABORT: {
        uint32_t set = (syndrome >> 12) & 3;
        bool fnv = (syndrome >> 10) & 1;
        bool ea = (syndrome >> 9) & 1;
        bool s1ptw = (syndrome >> 7) & 1;
        uint32_t ifsc = (syndrome >> 0) & 0x3f;

        trace_hvf_insn_abort(env->pc, set, fnv, ea, s1ptw, ifsc);

        /* fall through */
    }
    default:
        cpu_synchronize_state(cpu);
        trace_hvf_exit(syndrome, ec, env->pc);
        error_report("0x%llx: unhandled exception ec=0x%x", env->pc, ec);
    }

    /* flush any changed cpu state back to HVF */
    flush_cpu_state(cpu);

    if (advance_pc) {
        uint64_t pc;


        r = hv_vcpu_get_reg(cpu->accel->fd, HV_REG_PC, &pc);
        assert_hvf_ok(r);
        pc += 4;
        r = hv_vcpu_set_reg(cpu->accel->fd, HV_REG_PC, pc);
        assert_hvf_ok(r);

        /* Handle single-stepping over instructions which trigger a VM exit */
        if (cpu->singlestep_enabled) {
            ret = EXCP_DEBUG;
        }
    }

    return ret;
}

static int hvf_handle_vmexit(CPUState *cpu, hv_vcpu_exit_t *exit)
{
    ARMCPU *arm_cpu = env_archcpu(cpu_env(cpu));
    int ret = 0;

    switch (exit->reason) {
    case HV_EXIT_REASON_EXCEPTION:
        hvf_sync_vtimer(cpu);
        ret = hvf_handle_exception(cpu, &exit->exception);
        break;
    case HV_EXIT_REASON_VTIMER_ACTIVATED:
        qemu_set_irq(arm_cpu->gt_timer_outputs[GTIMER_VIRT], 1);
        cpu->accel->vtimer_masked = true;
        break;
    case HV_EXIT_REASON_CANCELED:
        /* we got kicked, no exit to process */
        ret = -1;
        break;
    default:
        g_assert_not_reached();
    }

    return ret;
}

int hvf_arch_vcpu_exec(CPUState *cpu)
{
    int ret;
    hv_return_t r;

    if (cpu->halted) {
        return EXCP_HLT;
    }

    flush_cpu_state(cpu);

    do {
        if (!(cpu->singlestep_enabled & SSTEP_NOIRQ) &&
            hvf_inject_interrupts(cpu)) {
            return EXCP_INTERRUPT;
        }

        bql_unlock();
        cpu_exec_start(cpu);
        r = hv_vcpu_run(cpu->accel->fd);
        cpu_exec_end(cpu);
        bql_lock();
        switch (r) {
        case HV_SUCCESS:
            ret = hvf_handle_vmexit(cpu, cpu->accel->exit);
            break;
        case HV_ILLEGAL_GUEST_STATE:
            trace_hvf_illegal_guest_state();
            /* fall through */
        default:
            g_assert_not_reached();
        }
    } while (ret == 0);

    return ret;
}

static const VMStateDescription vmstate_hvf_vtimer = {
    .name = "hvf-vtimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(vtimer_val, HVFVTimer),
        VMSTATE_END_OF_LIST()
    },
};

static void hvf_vm_state_change(void *opaque, bool running, RunState state)
{
    HVFVTimer *s = opaque;

    if (running) {
        /* Update vtimer offset on all CPUs */
        hvf_state->vtimer_offset = mach_absolute_time() - s->vtimer_val;
        cpu_synchronize_all_states();
    } else {
        /* Remember vtimer value on every pause */
        s->vtimer_val = hvf_vtimer_val_raw();
    }
}

int hvf_arch_init(void)
{
    hvf_state->vtimer_offset = mach_absolute_time();
    vmstate_register(NULL, 0, &vmstate_hvf_vtimer, &vtimer);
    qemu_add_vm_change_state_handler(hvf_vm_state_change, &vtimer);

    hvf_arm_init_debug();

    return 0;
}

static const uint32_t brk_insn = 0xd4200000;

int hvf_arch_insert_sw_breakpoint(CPUState *cpu, struct hvf_sw_breakpoint *bp)
{
    if (cpu_memory_rw_debug(cpu, bp->pc, (uint8_t *)&bp->saved_insn, 4, 0) ||
        cpu_memory_rw_debug(cpu, bp->pc, (uint8_t *)&brk_insn, 4, 1)) {
        return -EINVAL;
    }
    return 0;
}

int hvf_arch_remove_sw_breakpoint(CPUState *cpu, struct hvf_sw_breakpoint *bp)
{
    static uint32_t brk;

    if (cpu_memory_rw_debug(cpu, bp->pc, (uint8_t *)&brk, 4, 0) ||
        brk != brk_insn ||
        cpu_memory_rw_debug(cpu, bp->pc, (uint8_t *)&bp->saved_insn, 4, 1)) {
        return -EINVAL;
    }
    return 0;
}

int hvf_arch_insert_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_HW:
        return insert_hw_breakpoint(addr);
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_ACCESS:
        return insert_hw_watchpoint(addr, len, type);
    default:
        return -ENOSYS;
    }
}

int hvf_arch_remove_hw_breakpoint(vaddr addr, vaddr len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_HW:
        return delete_hw_breakpoint(addr);
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_ACCESS:
        return delete_hw_watchpoint(addr, len, type);
    default:
        return -ENOSYS;
    }
}

void hvf_arch_remove_all_hw_breakpoints(void)
{
    if (cur_hw_wps > 0) {
        g_array_remove_range(hw_watchpoints, 0, cur_hw_wps);
    }
    if (cur_hw_bps > 0) {
        g_array_remove_range(hw_breakpoints, 0, cur_hw_bps);
    }
}

/*
 * Update the vCPU with the gdbstub's view of debug registers. This view
 * consists of all hardware breakpoints and watchpoints inserted so far while
 * debugging the guest.
 * Must be called by the owning thread.
 */
static void hvf_put_gdbstub_debug_registers(CPUState *cpu)
{
    hv_return_t r = HV_SUCCESS;
    int i;

    for (i = 0; i < cur_hw_bps; i++) {
        HWBreakpoint *bp = get_hw_bp(i);
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgbcr_regs[i], bp->bcr);
        assert_hvf_ok(r);
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgbvr_regs[i], bp->bvr);
        assert_hvf_ok(r);
    }
    for (i = cur_hw_bps; i < max_hw_bps; i++) {
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgbcr_regs[i], 0);
        assert_hvf_ok(r);
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgbvr_regs[i], 0);
        assert_hvf_ok(r);
    }

    for (i = 0; i < cur_hw_wps; i++) {
        HWWatchpoint *wp = get_hw_wp(i);
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgwcr_regs[i], wp->wcr);
        assert_hvf_ok(r);
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgwvr_regs[i], wp->wvr);
        assert_hvf_ok(r);
    }
    for (i = cur_hw_wps; i < max_hw_wps; i++) {
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgwcr_regs[i], 0);
        assert_hvf_ok(r);
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgwvr_regs[i], 0);
        assert_hvf_ok(r);
    }
}

/*
 * Update the vCPU with the guest's view of debug registers. This view is kept
 * in the environment at all times.
 * Must be called by the owning thread.
 */
static void hvf_put_guest_debug_registers(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    hv_return_t r = HV_SUCCESS;
    int i;

    for (i = 0; i < max_hw_bps; i++) {
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgbcr_regs[i],
                                env->cp15.dbgbcr[i]);
        assert_hvf_ok(r);
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgbvr_regs[i],
                                env->cp15.dbgbvr[i]);
        assert_hvf_ok(r);
    }

    for (i = 0; i < max_hw_wps; i++) {
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgwcr_regs[i],
                                env->cp15.dbgwcr[i]);
        assert_hvf_ok(r);
        r = hv_vcpu_set_sys_reg(cpu->accel->fd, dbgwvr_regs[i],
                                env->cp15.dbgwvr[i]);
        assert_hvf_ok(r);
    }
}

static inline bool hvf_arm_hw_debug_active(CPUState *cpu)
{
    return ((cur_hw_wps > 0) || (cur_hw_bps > 0));
}

/* Must be called by the owning thread */
static void hvf_arch_set_traps(CPUState *cpu)
{
    bool should_enable_traps = false;
    hv_return_t r = HV_SUCCESS;

    /* Check whether guest debugging is enabled for at least one vCPU; if it
     * is, enable exiting the guest on all vCPUs */
    should_enable_traps |= cpu->accel->guest_debug_enabled;
    /* Set whether debug exceptions exit the guest */
    r = hv_vcpu_set_trap_debug_exceptions(cpu->accel->fd,
                                            should_enable_traps);
    assert_hvf_ok(r);

    /* Set whether accesses to debug registers exit the guest */
    r = hv_vcpu_set_trap_debug_reg_accesses(cpu->accel->fd,
                                            should_enable_traps);
    assert_hvf_ok(r);
}

void hvf_arch_update_guest_debug(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    /* Check whether guest debugging is enabled */
    cpu->accel->guest_debug_enabled = cpu->singlestep_enabled ||
                                    hvf_sw_breakpoints_active(cpu) ||
                                    hvf_arm_hw_debug_active(cpu);

    /* Update debug registers */
    if (cpu->accel->guest_debug_enabled) {
        hvf_put_gdbstub_debug_registers(cpu);
    } else {
        hvf_put_guest_debug_registers(cpu);
    }

    cpu_synchronize_state(cpu);

    /* Enable/disable single-stepping */
    if (cpu->singlestep_enabled) {
        env->cp15.mdscr_el1 =
            deposit64(env->cp15.mdscr_el1, MDSCR_EL1_SS_SHIFT, 1, 1);
        pstate_write(env, pstate_read(env) | PSTATE_SS);
    } else {
        env->cp15.mdscr_el1 =
            deposit64(env->cp15.mdscr_el1, MDSCR_EL1_SS_SHIFT, 1, 0);
    }

    /* Enable/disable Breakpoint exceptions */
    if (hvf_arm_hw_debug_active(cpu)) {
        env->cp15.mdscr_el1 =
            deposit64(env->cp15.mdscr_el1, MDSCR_EL1_MDE_SHIFT, 1, 1);
    } else {
        env->cp15.mdscr_el1 =
            deposit64(env->cp15.mdscr_el1, MDSCR_EL1_MDE_SHIFT, 1, 0);
    }

    hvf_arch_set_traps(cpu);
}

bool hvf_arch_supports_guest_debug(void)
{
    return true;
}
