/*
 * Definitions for Arm ID system registers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ARM_CPU_SYSREGS_H
#define ARM_CPU_SYSREGS_H

/*
 * Following is similar to the coprocessor regs encodings, but with an argument
 * ordering that matches the ARM ARM. We also reuse the various CP_REG_ defines
 * that actually are the same as the equivalent KVM_REG_ values.
 */
#define ENCODE_ID_REG(op0, op1, crn, crm, op2)          \
    (((op0) << CP_REG_ARM64_SYSREG_OP0_SHIFT) |         \
     ((op1) << CP_REG_ARM64_SYSREG_OP1_SHIFT) |         \
     ((crn) << CP_REG_ARM64_SYSREG_CRN_SHIFT) |         \
     ((crm) << CP_REG_ARM64_SYSREG_CRM_SHIFT) |         \
     ((op2) << CP_REG_ARM64_SYSREG_OP2_SHIFT))

#define DEF(NAME, OP0, OP1, CRN, CRM, OP2) NAME##_IDX,

typedef enum ARMIDRegisterIdx {
#include "cpu-sysregs.h.inc"
    NUM_ID_IDX,
} ARMIDRegisterIdx;

#undef DEF
#define DEF(NAME, OP0, OP1, CRN, CRM, OP2) \
    SYS_##NAME = ENCODE_ID_REG(OP0, OP1, CRN, CRM, OP2),

typedef enum ARMSysRegs {
#include "cpu-sysregs.h.inc"
} ARMSysRegs;

#undef DEF

extern const uint32_t id_register_sysreg[NUM_ID_IDX];

int get_sysreg_idx(ARMSysRegs sysreg);

#endif /* ARM_CPU_SYSREGS_H */
