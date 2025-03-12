/*
 * QEMU ARM CP Register access and descriptions
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#ifndef TARGET_ARM_CPREGS_H
#define TARGET_ARM_CPREGS_H

#include "hw/registerfields.h"
#include "target/arm/kvm-consts.h"

/*
 * ARMCPRegInfo type field bits:
 */
enum {
    /*
     * Register must be handled specially during translation.
     * The method is one of the values below:
     */
    ARM_CP_SPECIAL_MASK          = 0x000f,
    /* Special: no change to PE state: writes ignored, reads ignored. */
    ARM_CP_NOP                   = 0x0001,
    /* Special: sysreg is WFI, for v5 and v6. */
    ARM_CP_WFI                   = 0x0002,
    /* Special: sysreg is NZCV. */
    ARM_CP_NZCV                  = 0x0003,
    /* Special: sysreg is CURRENTEL. */
    ARM_CP_CURRENTEL             = 0x0004,
    /* Special: sysreg is DC ZVA or similar. */
    ARM_CP_DC_ZVA                = 0x0005,
    ARM_CP_DC_GVA                = 0x0006,
    ARM_CP_DC_GZVA               = 0x0007,

    /* Flag: reads produce resetvalue; writes ignored. */
    ARM_CP_CONST                 = 1 << 4,
    /* Flag: For ARM_CP_STATE_AA32, sysreg is 64-bit. */
    ARM_CP_64BIT                 = 1 << 5,
    /*
     * Flag: TB should not be ended after a write to this register
     * (the default is that the TB ends after cp writes).
     */
    ARM_CP_SUPPRESS_TB_END       = 1 << 6,
    /*
     * Flag: Permit a register definition to override a previous definition
     * for the same (cp, is64, crn, crm, opc1, opc2) tuple: either the new
     * or the old must have the ARM_CP_OVERRIDE bit set.
     */
    ARM_CP_OVERRIDE              = 1 << 7,
    /*
     * Flag: Register is an alias view of some underlying state which is also
     * visible via another register, and that the other register is handling
     * migration and reset; registers marked ARM_CP_ALIAS will not be migrated
     * but may have their state set by syncing of register state from KVM.
     */
    ARM_CP_ALIAS                 = 1 << 8,
    /*
     * Flag: Register does I/O and therefore its accesses need to be marked
     * with translator_io_start() and also end the TB. In particular,
     * registers which implement clocks or timers require this.
     */
    ARM_CP_IO                    = 1 << 9,
    /*
     * Flag: Register has no underlying state and does not support raw access
     * for state saving/loading; it will not be used for either migration or
     * KVM state synchronization. Typically this is for "registers" which are
     * actually used as instructions for cache maintenance and so on.
     */
    ARM_CP_NO_RAW                = 1 << 10,
    /*
     * Flag: The read or write hook might raise an exception; the generated
     * code will synchronize the CPU state before calling the hook so that it
     * is safe for the hook to call raise_exception().
     */
    ARM_CP_RAISES_EXC            = 1 << 11,
    /*
     * Flag: Writes to the sysreg might change the exception level - typically
     * on older ARM chips. For those cases we need to re-read the new el when
     * recomputing the translation flags.
     */
    ARM_CP_NEWEL                 = 1 << 12,
    /*
     * Flag: Access check for this sysreg is identical to accessing FPU state
     * from an instruction: use translation fp_access_check().
     */
    ARM_CP_FPU                   = 1 << 13,
    /*
     * Flag: Access check for this sysreg is identical to accessing SVE state
     * from an instruction: use translation sve_access_check().
     */
    ARM_CP_SVE                   = 1 << 14,
    /* Flag: Do not expose in gdb sysreg xml. */
    ARM_CP_NO_GDB                = 1 << 15,
    /*
     * Flags: If EL3 but not EL2...
     *   - UNDEF: discard the cpreg,
     *   -  KEEP: retain the cpreg as is,
     *   -  C_NZ: set const on the cpreg, but retain resetvalue,
     *   -  else: set const on the cpreg, zero resetvalue, aka RES0.
     * See rule RJFFP in section D1.1.3 of DDI0487H.a.
     */
    ARM_CP_EL3_NO_EL2_UNDEF      = 1 << 16,
    ARM_CP_EL3_NO_EL2_KEEP       = 1 << 17,
    ARM_CP_EL3_NO_EL2_C_NZ       = 1 << 18,
    /*
     * Flag: Access check for this sysreg is constrained by the
     * ARM pseudocode function CheckSMEAccess().
     */
    ARM_CP_SME                   = 1 << 19,
    /*
     * Flag: one of the four EL2 registers which redirect to the
     * equivalent EL1 register when FEAT_NV2 is enabled.
     */
    ARM_CP_NV2_REDIRECT          = 1 << 20,
    /*
     * Flag: this is a TLBI insn which (when FEAT_XS is present) also has
     * an NXS variant at the same encoding except that crn is 1 greater,
     * so when registering this cpreg automatically also register one
     * for the TLBI NXS variant. (For QEMU the NXS variant behaves
     * identically to the normal one, other than FGT trapping handling.)
     */
    ARM_CP_ADD_TLBI_NXS          = 1 << 21,
};

/*
 * Interface for defining coprocessor registers.
 * Registers are defined in tables of arm_cp_reginfo structs
 * which are passed to define_arm_cp_regs().
 */

/*
 * When looking up a coprocessor register we look for it
 * via an integer which encodes all of:
 *  coprocessor number
 *  Crn, Crm, opc1, opc2 fields
 *  32 or 64 bit register (ie is it accessed via MRC/MCR
 *    or via MRRC/MCRR?)
 *  non-secure/secure bank (AArch32 only)
 * We allow 4 bits for opc1 because MRRC/MCRR have a 4 bit field.
 * (In this case crn and opc2 should be zero.)
 * For AArch64, there is no 32/64 bit size distinction;
 * instead all registers have a 2 bit op0, 3 bit op1 and op2,
 * and 4 bit CRn and CRm. The encoding patterns are chosen
 * to be easy to convert to and from the KVM encodings, and also
 * so that the hashtable can contain both AArch32 and AArch64
 * registers (to allow for interprocessing where we might run
 * 32 bit code on a 64 bit core).
 */
/*
 * This bit is private to our hashtable cpreg; in KVM register
 * IDs the AArch64/32 distinction is the KVM_REG_ARM/ARM64
 * in the upper bits of the 64 bit ID.
 */
#define CP_REG_AA64_SHIFT 28
#define CP_REG_AA64_MASK (1 << CP_REG_AA64_SHIFT)

/*
 * To enable banking of coprocessor registers depending on ns-bit we
 * add a bit to distinguish between secure and non-secure cpregs in the
 * hashtable.
 */
#define CP_REG_NS_SHIFT 29
#define CP_REG_NS_MASK (1 << CP_REG_NS_SHIFT)

#define ENCODE_CP_REG(cp, is64, ns, crn, crm, opc1, opc2)   \
    ((ns) << CP_REG_NS_SHIFT | ((cp) << 16) | ((is64) << 15) |   \
     ((crn) << 11) | ((crm) << 7) | ((opc1) << 3) | (opc2))

#define ENCODE_AA64_CP_REG(cp, crn, crm, op0, op1, op2) \
    (CP_REG_AA64_MASK |                                 \
     ((cp) << CP_REG_ARM_COPROC_SHIFT) |                \
     ((op0) << CP_REG_ARM64_SYSREG_OP0_SHIFT) |         \
     ((op1) << CP_REG_ARM64_SYSREG_OP1_SHIFT) |         \
     ((crn) << CP_REG_ARM64_SYSREG_CRN_SHIFT) |         \
     ((crm) << CP_REG_ARM64_SYSREG_CRM_SHIFT) |         \
     ((op2) << CP_REG_ARM64_SYSREG_OP2_SHIFT))

/*
 * Convert a full 64 bit KVM register ID to the truncated 32 bit
 * version used as a key for the coprocessor register hashtable
 */
static inline uint32_t kvm_to_cpreg_id(uint64_t kvmid)
{
    uint32_t cpregid = kvmid;
    if ((kvmid & CP_REG_ARCH_MASK) == CP_REG_ARM64) {
        cpregid |= CP_REG_AA64_MASK;
    } else {
        if ((kvmid & CP_REG_SIZE_MASK) == CP_REG_SIZE_U64) {
            cpregid |= (1 << 15);
        }

        /*
         * KVM is always non-secure so add the NS flag on AArch32 register
         * entries.
         */
         cpregid |= 1 << CP_REG_NS_SHIFT;
    }
    return cpregid;
}

/*
 * Convert a truncated 32 bit hashtable key into the full
 * 64 bit KVM register ID.
 */
static inline uint64_t cpreg_to_kvm_id(uint32_t cpregid)
{
    uint64_t kvmid;

    if (cpregid & CP_REG_AA64_MASK) {
        kvmid = cpregid & ~CP_REG_AA64_MASK;
        kvmid |= CP_REG_SIZE_U64 | CP_REG_ARM64;
    } else {
        kvmid = cpregid & ~(1 << 15);
        if (cpregid & (1 << 15)) {
            kvmid |= CP_REG_SIZE_U64 | CP_REG_ARM;
        } else {
            kvmid |= CP_REG_SIZE_U32 | CP_REG_ARM;
        }
    }
    return kvmid;
}

/*
 * Valid values for ARMCPRegInfo state field, indicating which of
 * the AArch32 and AArch64 execution states this register is visible in.
 * If the reginfo doesn't explicitly specify then it is AArch32 only.
 * If the reginfo is declared to be visible in both states then a second
 * reginfo is synthesised for the AArch32 view of the AArch64 register,
 * such that the AArch32 view is the lower 32 bits of the AArch64 one.
 * Note that we rely on the values of these enums as we iterate through
 * the various states in some places.
 */
typedef enum {
    ARM_CP_STATE_AA32 = 0,
    ARM_CP_STATE_AA64 = 1,
    ARM_CP_STATE_BOTH = 2,
} CPState;

/*
 * ARM CP register secure state flags.  These flags identify security state
 * attributes for a given CP register entry.
 * The existence of both or neither secure and non-secure flags indicates that
 * the register has both a secure and non-secure hash entry.  A single one of
 * these flags causes the register to only be hashed for the specified
 * security state.
 * Although definitions may have any combination of the S/NS bits, each
 * registered entry will only have one to identify whether the entry is secure
 * or non-secure.
 */
typedef enum {
    ARM_CP_SECSTATE_BOTH = 0,       /* define one cpreg for each secstate */
    ARM_CP_SECSTATE_S =   (1 << 0), /* bit[0]: Secure state register */
    ARM_CP_SECSTATE_NS =  (1 << 1), /* bit[1]: Non-secure state register */
} CPSecureState;

/*
 * Access rights:
 * We define bits for Read and Write access for what rev C of the v7-AR ARM ARM
 * defines as PL0 (user), PL1 (fiq/irq/svc/abt/und/sys, ie privileged), and
 * PL2 (hyp). The other level which has Read and Write bits is Secure PL1
 * (ie any of the privileged modes in Secure state, or Monitor mode).
 * If a register is accessible in one privilege level it's always accessible
 * in higher privilege levels too. Since "Secure PL1" also follows this rule
 * (ie anything visible in PL2 is visible in S-PL1, some things are only
 * visible in S-PL1) but "Secure PL1" is a bit of a mouthful, we bend the
 * terminology a little and call this PL3.
 * In AArch64 things are somewhat simpler as the PLx bits line up exactly
 * with the ELx exception levels.
 *
 * If access permissions for a register are more complex than can be
 * described with these bits, then use a laxer set of restrictions, and
 * do the more restrictive/complex check inside a helper function.
 */
typedef enum {
    PL3_R = 0x80,
    PL3_W = 0x40,
    PL2_R = 0x20 | PL3_R,
    PL2_W = 0x10 | PL3_W,
    PL1_R = 0x08 | PL2_R,
    PL1_W = 0x04 | PL2_W,
    PL0_R = 0x02 | PL1_R,
    PL0_W = 0x01 | PL1_W,

    /*
     * For user-mode some registers are accessible to EL0 via a kernel
     * trap-and-emulate ABI. In this case we define the read permissions
     * as actually being PL0_R. However some bits of any given register
     * may still be masked.
     */
#ifdef CONFIG_USER_ONLY
    PL0U_R = PL0_R,
#else
    PL0U_R = PL1_R,
#endif

    PL3_RW = PL3_R | PL3_W,
    PL2_RW = PL2_R | PL2_W,
    PL1_RW = PL1_R | PL1_W,
    PL0_RW = PL0_R | PL0_W,
} CPAccessRights;

typedef enum CPAccessResult {
    /* Access is permitted */
    CP_ACCESS_OK = 0,

    /*
     * Combined with one of the following, the low 2 bits indicate the
     * target exception level.  If 0, the exception is taken to the usual
     * target EL (EL1 or PL1 if in EL0, otherwise to the current EL).
     */
    CP_ACCESS_EL_MASK = 3,

    /*
     * Access fails due to a configurable trap or enable which would
     * result in a categorized exception syndrome giving information about
     * the failing instruction (ie syndrome category 0x3, 0x4, 0x5, 0x6,
     * 0xc or 0x18). These traps are always to a specified target EL,
     * never to the usual target EL.
     */
    CP_ACCESS_TRAP_BIT = (1 << 2),
    CP_ACCESS_TRAP_EL1 = CP_ACCESS_TRAP_BIT | 1,
    CP_ACCESS_TRAP_EL2 = CP_ACCESS_TRAP_BIT | 2,
    CP_ACCESS_TRAP_EL3 = CP_ACCESS_TRAP_BIT | 3,

    /*
     * Access fails with UNDEFINED, i.e. an exception syndrome 0x0
     * ("uncategorized"), which is what an undefined insn produces.
     * Note that this is not a catch-all case -- the set of cases which may
     * result in this failure is specifically defined by the architecture.
     * This trap is always to the usual target EL, never directly to a
     * specified target EL.
     */
    CP_ACCESS_UNDEFINED = (2 << 2),
} CPAccessResult;

/* Indexes into fgt_read[] */
#define FGTREG_HFGRTR 0
#define FGTREG_HDFGRTR 1
/* Indexes into fgt_write[] */
#define FGTREG_HFGWTR 0
#define FGTREG_HDFGWTR 1
/* Indexes into fgt_exec[] */
#define FGTREG_HFGITR 0

FIELD(HFGRTR_EL2, AFSR0_EL1, 0, 1)
FIELD(HFGRTR_EL2, AFSR1_EL1, 1, 1)
FIELD(HFGRTR_EL2, AIDR_EL1, 2, 1)
FIELD(HFGRTR_EL2, AMAIR_EL1, 3, 1)
FIELD(HFGRTR_EL2, APDAKEY, 4, 1)
FIELD(HFGRTR_EL2, APDBKEY, 5, 1)
FIELD(HFGRTR_EL2, APGAKEY, 6, 1)
FIELD(HFGRTR_EL2, APIAKEY, 7, 1)
FIELD(HFGRTR_EL2, APIBKEY, 8, 1)
FIELD(HFGRTR_EL2, CCSIDR_EL1, 9, 1)
FIELD(HFGRTR_EL2, CLIDR_EL1, 10, 1)
FIELD(HFGRTR_EL2, CONTEXTIDR_EL1, 11, 1)
FIELD(HFGRTR_EL2, CPACR_EL1, 12, 1)
FIELD(HFGRTR_EL2, CSSELR_EL1, 13, 1)
FIELD(HFGRTR_EL2, CTR_EL0, 14, 1)
FIELD(HFGRTR_EL2, DCZID_EL0, 15, 1)
FIELD(HFGRTR_EL2, ESR_EL1, 16, 1)
FIELD(HFGRTR_EL2, FAR_EL1, 17, 1)
FIELD(HFGRTR_EL2, ISR_EL1, 18, 1)
FIELD(HFGRTR_EL2, LORC_EL1, 19, 1)
FIELD(HFGRTR_EL2, LOREA_EL1, 20, 1)
FIELD(HFGRTR_EL2, LORID_EL1, 21, 1)
FIELD(HFGRTR_EL2, LORN_EL1, 22, 1)
FIELD(HFGRTR_EL2, LORSA_EL1, 23, 1)
FIELD(HFGRTR_EL2, MAIR_EL1, 24, 1)
FIELD(HFGRTR_EL2, MIDR_EL1, 25, 1)
FIELD(HFGRTR_EL2, MPIDR_EL1, 26, 1)
FIELD(HFGRTR_EL2, PAR_EL1, 27, 1)
FIELD(HFGRTR_EL2, REVIDR_EL1, 28, 1)
FIELD(HFGRTR_EL2, SCTLR_EL1, 29, 1)
FIELD(HFGRTR_EL2, SCXTNUM_EL1, 30, 1)
FIELD(HFGRTR_EL2, SCXTNUM_EL0, 31, 1)
FIELD(HFGRTR_EL2, TCR_EL1, 32, 1)
FIELD(HFGRTR_EL2, TPIDR_EL1, 33, 1)
FIELD(HFGRTR_EL2, TPIDRRO_EL0, 34, 1)
FIELD(HFGRTR_EL2, TPIDR_EL0, 35, 1)
FIELD(HFGRTR_EL2, TTBR0_EL1, 36, 1)
FIELD(HFGRTR_EL2, TTBR1_EL1, 37, 1)
FIELD(HFGRTR_EL2, VBAR_EL1, 38, 1)
FIELD(HFGRTR_EL2, ICC_IGRPENN_EL1, 39, 1)
FIELD(HFGRTR_EL2, ERRIDR_EL1, 40, 1)
FIELD(HFGRTR_EL2, ERRSELR_EL1, 41, 1)
FIELD(HFGRTR_EL2, ERXFR_EL1, 42, 1)
FIELD(HFGRTR_EL2, ERXCTLR_EL1, 43, 1)
FIELD(HFGRTR_EL2, ERXSTATUS_EL1, 44, 1)
FIELD(HFGRTR_EL2, ERXMISCN_EL1, 45, 1)
FIELD(HFGRTR_EL2, ERXPFGF_EL1, 46, 1)
FIELD(HFGRTR_EL2, ERXPFGCTL_EL1, 47, 1)
FIELD(HFGRTR_EL2, ERXPFGCDN_EL1, 48, 1)
FIELD(HFGRTR_EL2, ERXADDR_EL1, 49, 1)
FIELD(HFGRTR_EL2, NACCDATA_EL1, 50, 1)
/* 51-53: RES0 */
FIELD(HFGRTR_EL2, NSMPRI_EL1, 54, 1)
FIELD(HFGRTR_EL2, NTPIDR2_EL0, 55, 1)
/* 56-63: RES0 */

/* These match HFGRTR but bits for RO registers are RES0 */
FIELD(HFGWTR_EL2, AFSR0_EL1, 0, 1)
FIELD(HFGWTR_EL2, AFSR1_EL1, 1, 1)
FIELD(HFGWTR_EL2, AMAIR_EL1, 3, 1)
FIELD(HFGWTR_EL2, APDAKEY, 4, 1)
FIELD(HFGWTR_EL2, APDBKEY, 5, 1)
FIELD(HFGWTR_EL2, APGAKEY, 6, 1)
FIELD(HFGWTR_EL2, APIAKEY, 7, 1)
FIELD(HFGWTR_EL2, APIBKEY, 8, 1)
FIELD(HFGWTR_EL2, CONTEXTIDR_EL1, 11, 1)
FIELD(HFGWTR_EL2, CPACR_EL1, 12, 1)
FIELD(HFGWTR_EL2, CSSELR_EL1, 13, 1)
FIELD(HFGWTR_EL2, ESR_EL1, 16, 1)
FIELD(HFGWTR_EL2, FAR_EL1, 17, 1)
FIELD(HFGWTR_EL2, LORC_EL1, 19, 1)
FIELD(HFGWTR_EL2, LOREA_EL1, 20, 1)
FIELD(HFGWTR_EL2, LORN_EL1, 22, 1)
FIELD(HFGWTR_EL2, LORSA_EL1, 23, 1)
FIELD(HFGWTR_EL2, MAIR_EL1, 24, 1)
FIELD(HFGWTR_EL2, PAR_EL1, 27, 1)
FIELD(HFGWTR_EL2, SCTLR_EL1, 29, 1)
FIELD(HFGWTR_EL2, SCXTNUM_EL1, 30, 1)
FIELD(HFGWTR_EL2, SCXTNUM_EL0, 31, 1)
FIELD(HFGWTR_EL2, TCR_EL1, 32, 1)
FIELD(HFGWTR_EL2, TPIDR_EL1, 33, 1)
FIELD(HFGWTR_EL2, TPIDRRO_EL0, 34, 1)
FIELD(HFGWTR_EL2, TPIDR_EL0, 35, 1)
FIELD(HFGWTR_EL2, TTBR0_EL1, 36, 1)
FIELD(HFGWTR_EL2, TTBR1_EL1, 37, 1)
FIELD(HFGWTR_EL2, VBAR_EL1, 38, 1)
FIELD(HFGWTR_EL2, ICC_IGRPENN_EL1, 39, 1)
FIELD(HFGWTR_EL2, ERRSELR_EL1, 41, 1)
FIELD(HFGWTR_EL2, ERXCTLR_EL1, 43, 1)
FIELD(HFGWTR_EL2, ERXSTATUS_EL1, 44, 1)
FIELD(HFGWTR_EL2, ERXMISCN_EL1, 45, 1)
FIELD(HFGWTR_EL2, ERXPFGCTL_EL1, 47, 1)
FIELD(HFGWTR_EL2, ERXPFGCDN_EL1, 48, 1)
FIELD(HFGWTR_EL2, ERXADDR_EL1, 49, 1)
FIELD(HFGWTR_EL2, NACCDATA_EL1, 50, 1)
FIELD(HFGWTR_EL2, NSMPRI_EL1, 54, 1)
FIELD(HFGWTR_EL2, NTPIDR2_EL0, 55, 1)

FIELD(HFGITR_EL2, ICIALLUIS, 0, 1)
FIELD(HFGITR_EL2, ICIALLU, 1, 1)
FIELD(HFGITR_EL2, ICIVAU, 2, 1)
FIELD(HFGITR_EL2, DCIVAC, 3, 1)
FIELD(HFGITR_EL2, DCISW, 4, 1)
FIELD(HFGITR_EL2, DCCSW, 5, 1)
FIELD(HFGITR_EL2, DCCISW, 6, 1)
FIELD(HFGITR_EL2, DCCVAU, 7, 1)
FIELD(HFGITR_EL2, DCCVAP, 8, 1)
FIELD(HFGITR_EL2, DCCVADP, 9, 1)
FIELD(HFGITR_EL2, DCCIVAC, 10, 1)
FIELD(HFGITR_EL2, DCZVA, 11, 1)
FIELD(HFGITR_EL2, ATS1E1R, 12, 1)
FIELD(HFGITR_EL2, ATS1E1W, 13, 1)
FIELD(HFGITR_EL2, ATS1E0R, 14, 1)
FIELD(HFGITR_EL2, ATS1E0W, 15, 1)
FIELD(HFGITR_EL2, ATS1E1RP, 16, 1)
FIELD(HFGITR_EL2, ATS1E1WP, 17, 1)
FIELD(HFGITR_EL2, TLBIVMALLE1OS, 18, 1)
FIELD(HFGITR_EL2, TLBIVAE1OS, 19, 1)
FIELD(HFGITR_EL2, TLBIASIDE1OS, 20, 1)
FIELD(HFGITR_EL2, TLBIVAAE1OS, 21, 1)
FIELD(HFGITR_EL2, TLBIVALE1OS, 22, 1)
FIELD(HFGITR_EL2, TLBIVAALE1OS, 23, 1)
FIELD(HFGITR_EL2, TLBIRVAE1OS, 24, 1)
FIELD(HFGITR_EL2, TLBIRVAAE1OS, 25, 1)
FIELD(HFGITR_EL2, TLBIRVALE1OS, 26, 1)
FIELD(HFGITR_EL2, TLBIRVAALE1OS, 27, 1)
FIELD(HFGITR_EL2, TLBIVMALLE1IS, 28, 1)
FIELD(HFGITR_EL2, TLBIVAE1IS, 29, 1)
FIELD(HFGITR_EL2, TLBIASIDE1IS, 30, 1)
FIELD(HFGITR_EL2, TLBIVAAE1IS, 31, 1)
FIELD(HFGITR_EL2, TLBIVALE1IS, 32, 1)
FIELD(HFGITR_EL2, TLBIVAALE1IS, 33, 1)
FIELD(HFGITR_EL2, TLBIRVAE1IS, 34, 1)
FIELD(HFGITR_EL2, TLBIRVAAE1IS, 35, 1)
FIELD(HFGITR_EL2, TLBIRVALE1IS, 36, 1)
FIELD(HFGITR_EL2, TLBIRVAALE1IS, 37, 1)
FIELD(HFGITR_EL2, TLBIRVAE1, 38, 1)
FIELD(HFGITR_EL2, TLBIRVAAE1, 39, 1)
FIELD(HFGITR_EL2, TLBIRVALE1, 40, 1)
FIELD(HFGITR_EL2, TLBIRVAALE1, 41, 1)
FIELD(HFGITR_EL2, TLBIVMALLE1, 42, 1)
FIELD(HFGITR_EL2, TLBIVAE1, 43, 1)
FIELD(HFGITR_EL2, TLBIASIDE1, 44, 1)
FIELD(HFGITR_EL2, TLBIVAAE1, 45, 1)
FIELD(HFGITR_EL2, TLBIVALE1, 46, 1)
FIELD(HFGITR_EL2, TLBIVAALE1, 47, 1)
FIELD(HFGITR_EL2, CFPRCTX, 48, 1)
FIELD(HFGITR_EL2, DVPRCTX, 49, 1)
FIELD(HFGITR_EL2, CPPRCTX, 50, 1)
FIELD(HFGITR_EL2, ERET, 51, 1)
FIELD(HFGITR_EL2, SVC_EL0, 52, 1)
FIELD(HFGITR_EL2, SVC_EL1, 53, 1)
FIELD(HFGITR_EL2, DCCVAC, 54, 1)
FIELD(HFGITR_EL2, NBRBINJ, 55, 1)
FIELD(HFGITR_EL2, NBRBIALL, 56, 1)

FIELD(HDFGRTR_EL2, DBGBCRN_EL1, 0, 1)
FIELD(HDFGRTR_EL2, DBGBVRN_EL1, 1, 1)
FIELD(HDFGRTR_EL2, DBGWCRN_EL1, 2, 1)
FIELD(HDFGRTR_EL2, DBGWVRN_EL1, 3, 1)
FIELD(HDFGRTR_EL2, MDSCR_EL1, 4, 1)
FIELD(HDFGRTR_EL2, DBGCLAIM, 5, 1)
FIELD(HDFGRTR_EL2, DBGAUTHSTATUS_EL1, 6, 1)
FIELD(HDFGRTR_EL2, DBGPRCR_EL1, 7, 1)
/* 8: RES0: OSLAR_EL1 is WO */
FIELD(HDFGRTR_EL2, OSLSR_EL1, 9, 1)
FIELD(HDFGRTR_EL2, OSECCR_EL1, 10, 1)
FIELD(HDFGRTR_EL2, OSDLR_EL1, 11, 1)
FIELD(HDFGRTR_EL2, PMEVCNTRN_EL0, 12, 1)
FIELD(HDFGRTR_EL2, PMEVTYPERN_EL0, 13, 1)
FIELD(HDFGRTR_EL2, PMCCFILTR_EL0, 14, 1)
FIELD(HDFGRTR_EL2, PMCCNTR_EL0, 15, 1)
FIELD(HDFGRTR_EL2, PMCNTEN, 16, 1)
FIELD(HDFGRTR_EL2, PMINTEN, 17, 1)
FIELD(HDFGRTR_EL2, PMOVS, 18, 1)
FIELD(HDFGRTR_EL2, PMSELR_EL0, 19, 1)
/* 20: RES0: PMSWINC_EL0 is WO */
/* 21: RES0: PMCR_EL0 is WO */
FIELD(HDFGRTR_EL2, PMMIR_EL1, 22, 1)
FIELD(HDFGRTR_EL2, PMBLIMITR_EL1, 23, 1)
FIELD(HDFGRTR_EL2, PMBPTR_EL1, 24, 1)
FIELD(HDFGRTR_EL2, PMBSR_EL1, 25, 1)
FIELD(HDFGRTR_EL2, PMSCR_EL1, 26, 1)
FIELD(HDFGRTR_EL2, PMSEVFR_EL1, 27, 1)
FIELD(HDFGRTR_EL2, PMSFCR_EL1, 28, 1)
FIELD(HDFGRTR_EL2, PMSICR_EL1, 29, 1)
FIELD(HDFGRTR_EL2, PMSIDR_EL1, 30, 1)
FIELD(HDFGRTR_EL2, PMSIRR_EL1, 31, 1)
FIELD(HDFGRTR_EL2, PMSLATFR_EL1, 32, 1)
FIELD(HDFGRTR_EL2, TRC, 33, 1)
FIELD(HDFGRTR_EL2, TRCAUTHSTATUS, 34, 1)
FIELD(HDFGRTR_EL2, TRCAUXCTLR, 35, 1)
FIELD(HDFGRTR_EL2, TRCCLAIM, 36, 1)
FIELD(HDFGRTR_EL2, TRCCNTVRn, 37, 1)
/* 38, 39: RES0 */
FIELD(HDFGRTR_EL2, TRCID, 40, 1)
FIELD(HDFGRTR_EL2, TRCIMSPECN, 41, 1)
/* 42: RES0: TRCOSLAR is WO */
FIELD(HDFGRTR_EL2, TRCOSLSR, 43, 1)
FIELD(HDFGRTR_EL2, TRCPRGCTLR, 44, 1)
FIELD(HDFGRTR_EL2, TRCSEQSTR, 45, 1)
FIELD(HDFGRTR_EL2, TRCSSCSRN, 46, 1)
FIELD(HDFGRTR_EL2, TRCSTATR, 47, 1)
FIELD(HDFGRTR_EL2, TRCVICTLR, 48, 1)
/* 49: RES0: TRFCR_EL1 is WO */
FIELD(HDFGRTR_EL2, TRBBASER_EL1, 50, 1)
FIELD(HDFGRTR_EL2, TRBIDR_EL1, 51, 1)
FIELD(HDFGRTR_EL2, TRBLIMITR_EL1, 52, 1)
FIELD(HDFGRTR_EL2, TRBMAR_EL1, 53, 1)
FIELD(HDFGRTR_EL2, TRBPTR_EL1, 54, 1)
FIELD(HDFGRTR_EL2, TRBSR_EL1, 55, 1)
FIELD(HDFGRTR_EL2, TRBTRG_EL1, 56, 1)
FIELD(HDFGRTR_EL2, PMUSERENR_EL0, 57, 1)
FIELD(HDFGRTR_EL2, PMCEIDN_EL0, 58, 1)
FIELD(HDFGRTR_EL2, NBRBIDR, 59, 1)
FIELD(HDFGRTR_EL2, NBRBCTL, 60, 1)
FIELD(HDFGRTR_EL2, NBRBDATA, 61, 1)
FIELD(HDFGRTR_EL2, NPMSNEVFR_EL1, 62, 1)
FIELD(HDFGRTR_EL2, PMBIDR_EL1, 63, 1)

/*
 * These match HDFGRTR_EL2, but bits for RO registers are RES0.
 * A few bits are for WO registers, where the HDFGRTR_EL2 bit is RES0.
 */
FIELD(HDFGWTR_EL2, DBGBCRN_EL1, 0, 1)
FIELD(HDFGWTR_EL2, DBGBVRN_EL1, 1, 1)
FIELD(HDFGWTR_EL2, DBGWCRN_EL1, 2, 1)
FIELD(HDFGWTR_EL2, DBGWVRN_EL1, 3, 1)
FIELD(HDFGWTR_EL2, MDSCR_EL1, 4, 1)
FIELD(HDFGWTR_EL2, DBGCLAIM, 5, 1)
FIELD(HDFGWTR_EL2, DBGPRCR_EL1, 7, 1)
FIELD(HDFGWTR_EL2, OSLAR_EL1, 8, 1)
FIELD(HDFGWTR_EL2, OSLSR_EL1, 9, 1)
FIELD(HDFGWTR_EL2, OSECCR_EL1, 10, 1)
FIELD(HDFGWTR_EL2, OSDLR_EL1, 11, 1)
FIELD(HDFGWTR_EL2, PMEVCNTRN_EL0, 12, 1)
FIELD(HDFGWTR_EL2, PMEVTYPERN_EL0, 13, 1)
FIELD(HDFGWTR_EL2, PMCCFILTR_EL0, 14, 1)
FIELD(HDFGWTR_EL2, PMCCNTR_EL0, 15, 1)
FIELD(HDFGWTR_EL2, PMCNTEN, 16, 1)
FIELD(HDFGWTR_EL2, PMINTEN, 17, 1)
FIELD(HDFGWTR_EL2, PMOVS, 18, 1)
FIELD(HDFGWTR_EL2, PMSELR_EL0, 19, 1)
FIELD(HDFGWTR_EL2, PMSWINC_EL0, 20, 1)
FIELD(HDFGWTR_EL2, PMCR_EL0, 21, 1)
FIELD(HDFGWTR_EL2, PMBLIMITR_EL1, 23, 1)
FIELD(HDFGWTR_EL2, PMBPTR_EL1, 24, 1)
FIELD(HDFGWTR_EL2, PMBSR_EL1, 25, 1)
FIELD(HDFGWTR_EL2, PMSCR_EL1, 26, 1)
FIELD(HDFGWTR_EL2, PMSEVFR_EL1, 27, 1)
FIELD(HDFGWTR_EL2, PMSFCR_EL1, 28, 1)
FIELD(HDFGWTR_EL2, PMSICR_EL1, 29, 1)
FIELD(HDFGWTR_EL2, PMSIRR_EL1, 31, 1)
FIELD(HDFGWTR_EL2, PMSLATFR_EL1, 32, 1)
FIELD(HDFGWTR_EL2, TRC, 33, 1)
FIELD(HDFGWTR_EL2, TRCAUXCTLR, 35, 1)
FIELD(HDFGWTR_EL2, TRCCLAIM, 36, 1)
FIELD(HDFGWTR_EL2, TRCCNTVRn, 37, 1)
FIELD(HDFGWTR_EL2, TRCIMSPECN, 41, 1)
FIELD(HDFGWTR_EL2, TRCOSLAR, 42, 1)
FIELD(HDFGWTR_EL2, TRCPRGCTLR, 44, 1)
FIELD(HDFGWTR_EL2, TRCSEQSTR, 45, 1)
FIELD(HDFGWTR_EL2, TRCSSCSRN, 46, 1)
FIELD(HDFGWTR_EL2, TRCVICTLR, 48, 1)
FIELD(HDFGWTR_EL2, TRFCR_EL1, 49, 1)
FIELD(HDFGWTR_EL2, TRBBASER_EL1, 50, 1)
FIELD(HDFGWTR_EL2, TRBLIMITR_EL1, 52, 1)
FIELD(HDFGWTR_EL2, TRBMAR_EL1, 53, 1)
FIELD(HDFGWTR_EL2, TRBPTR_EL1, 54, 1)
FIELD(HDFGWTR_EL2, TRBSR_EL1, 55, 1)
FIELD(HDFGWTR_EL2, TRBTRG_EL1, 56, 1)
FIELD(HDFGWTR_EL2, PMUSERENR_EL0, 57, 1)
FIELD(HDFGWTR_EL2, NBRBCTL, 60, 1)
FIELD(HDFGWTR_EL2, NBRBDATA, 61, 1)
FIELD(HDFGWTR_EL2, NPMSNEVFR_EL1, 62, 1)

FIELD(FGT, NXS, 13, 1) /* Honour HCR_EL2.FGTnXS to suppress FGT */
/* Which fine-grained trap bit register to check, if any */
FIELD(FGT, TYPE, 10, 3)
FIELD(FGT, REV, 9, 1) /* Is bit sense reversed? */
FIELD(FGT, IDX, 6, 3) /* Index within a uint64_t[] array */
FIELD(FGT, BITPOS, 0, 6) /* Bit position within the uint64_t */

/*
 * Macros to define FGT_##bitname enum constants to use in ARMCPRegInfo::fgt
 * fields. We assume for brevity's sake that there are no duplicated
 * bit names across the various FGT registers.
 */
#define DO_BIT(REG, BITNAME)                                    \
    FGT_##BITNAME = FGT_##REG | R_##REG##_EL2_##BITNAME##_SHIFT

/* Some bits have reversed sense, so 0 means trap and 1 means not */
#define DO_REV_BIT(REG, BITNAME)                                        \
    FGT_##BITNAME = FGT_##REG | FGT_REV | R_##REG##_EL2_##BITNAME##_SHIFT

/*
 * The FGT bits for TLBI maintenance instructions accessible at EL1 always
 * affect the "normal" TLBI insns; they affect the corresponding TLBI insns
 * with the nXS qualifier only if HCRX_EL2.FGTnXS is 0. We define e.g.
 * FGT_TLBIVAE1 to use for the normal insn, and FGT_TLBIVAE1NXS to use
 * for the nXS qualified insn.
 */
#define DO_TLBINXS_BIT(REG, BITNAME)                             \
    FGT_##BITNAME = FGT_##REG | R_##REG##_EL2_##BITNAME##_SHIFT, \
    FGT_##BITNAME##NXS = FGT_##BITNAME | R_FGT_NXS_MASK

typedef enum FGTBit {
    /*
     * These bits tell us which register arrays to use:
     * if FGT_R is set then reads are checked against fgt_read[];
     * if FGT_W is set then writes are checked against fgt_write[];
     * if FGT_EXEC is set then all accesses are checked against fgt_exec[].
     *
     * For almost all bits in the R/W register pairs, the bit exists in
     * both registers for a RW register, in HFGRTR/HDFGRTR for a RO register
     * with the corresponding HFGWTR/HDFGTWTR bit being RES0, and vice-versa
     * for a WO register. There are unfortunately a couple of exceptions
     * (PMCR_EL0, TRFCR_EL1) where the register being trapped is RW but
     * the FGT system only allows trapping of writes, not reads.
     *
     * Note that we arrange these bits so that a 0 FGTBit means "no trap".
     */
    FGT_R = 1 << R_FGT_TYPE_SHIFT,
    FGT_W = 2 << R_FGT_TYPE_SHIFT,
    FGT_EXEC = 4 << R_FGT_TYPE_SHIFT,
    FGT_RW = FGT_R | FGT_W,
    /* Bit to identify whether trap bit is reversed sense */
    FGT_REV = R_FGT_REV_MASK,

    /*
     * If a bit exists in HFGRTR/HDFGRTR then either the register being
     * trapped is RO or the bit also exists in HFGWTR/HDFGWTR, so we either
     * want to trap for both reads and writes or else it's harmless to mark
     * it as trap-on-writes.
     * If a bit exists only in HFGWTR/HDFGWTR then either the register being
     * trapped is WO, or else it is one of the two oddball special cases
     * which are RW but have only a write trap. We mark these as only
     * FGT_W so we get the right behaviour for those special cases.
     * (If a bit was added in future that provided only a read trap for an
     * RW register we'd need to do something special to get the FGT_R bit
     * only. But this seems unlikely to happen.)
     *
     * So for the DO_BIT/DO_REV_BIT macros: use FGT_HFGRTR/FGT_HDFGRTR if
     * the bit exists in that register. Otherwise use FGT_HFGWTR/FGT_HDFGWTR.
     */
    FGT_HFGRTR = FGT_RW | (FGTREG_HFGRTR << R_FGT_IDX_SHIFT),
    FGT_HFGWTR = FGT_W | (FGTREG_HFGWTR << R_FGT_IDX_SHIFT),
    FGT_HDFGRTR = FGT_RW | (FGTREG_HDFGRTR << R_FGT_IDX_SHIFT),
    FGT_HDFGWTR = FGT_W | (FGTREG_HDFGWTR << R_FGT_IDX_SHIFT),
    FGT_HFGITR = FGT_EXEC | (FGTREG_HFGITR << R_FGT_IDX_SHIFT),

    /* Trap bits in HFGRTR_EL2 / HFGWTR_EL2, starting from bit 0. */
    DO_BIT(HFGRTR, AFSR0_EL1),
    DO_BIT(HFGRTR, AFSR1_EL1),
    DO_BIT(HFGRTR, AIDR_EL1),
    DO_BIT(HFGRTR, AMAIR_EL1),
    DO_BIT(HFGRTR, APDAKEY),
    DO_BIT(HFGRTR, APDBKEY),
    DO_BIT(HFGRTR, APGAKEY),
    DO_BIT(HFGRTR, APIAKEY),
    DO_BIT(HFGRTR, APIBKEY),
    DO_BIT(HFGRTR, CCSIDR_EL1),
    DO_BIT(HFGRTR, CLIDR_EL1),
    DO_BIT(HFGRTR, CONTEXTIDR_EL1),
    DO_BIT(HFGRTR, CPACR_EL1),
    DO_BIT(HFGRTR, CSSELR_EL1),
    DO_BIT(HFGRTR, CTR_EL0),
    DO_BIT(HFGRTR, DCZID_EL0),
    DO_BIT(HFGRTR, ESR_EL1),
    DO_BIT(HFGRTR, FAR_EL1),
    DO_BIT(HFGRTR, ISR_EL1),
    DO_BIT(HFGRTR, LORC_EL1),
    DO_BIT(HFGRTR, LOREA_EL1),
    DO_BIT(HFGRTR, LORID_EL1),
    DO_BIT(HFGRTR, LORN_EL1),
    DO_BIT(HFGRTR, LORSA_EL1),
    DO_BIT(HFGRTR, MAIR_EL1),
    DO_BIT(HFGRTR, MIDR_EL1),
    DO_BIT(HFGRTR, MPIDR_EL1),
    DO_BIT(HFGRTR, PAR_EL1),
    DO_BIT(HFGRTR, REVIDR_EL1),
    DO_BIT(HFGRTR, SCTLR_EL1),
    DO_BIT(HFGRTR, SCXTNUM_EL1),
    DO_BIT(HFGRTR, SCXTNUM_EL0),
    DO_BIT(HFGRTR, TCR_EL1),
    DO_BIT(HFGRTR, TPIDR_EL1),
    DO_BIT(HFGRTR, TPIDRRO_EL0),
    DO_BIT(HFGRTR, TPIDR_EL0),
    DO_BIT(HFGRTR, TTBR0_EL1),
    DO_BIT(HFGRTR, TTBR1_EL1),
    DO_BIT(HFGRTR, VBAR_EL1),
    DO_BIT(HFGRTR, ICC_IGRPENN_EL1),
    DO_BIT(HFGRTR, ERRIDR_EL1),
    DO_REV_BIT(HFGRTR, NSMPRI_EL1),
    DO_REV_BIT(HFGRTR, NTPIDR2_EL0),

    /* Trap bits in HDFGRTR_EL2 / HDFGWTR_EL2, starting from bit 0. */
    DO_BIT(HDFGRTR, DBGBCRN_EL1),
    DO_BIT(HDFGRTR, DBGBVRN_EL1),
    DO_BIT(HDFGRTR, DBGWCRN_EL1),
    DO_BIT(HDFGRTR, DBGWVRN_EL1),
    DO_BIT(HDFGRTR, MDSCR_EL1),
    DO_BIT(HDFGRTR, DBGCLAIM),
    DO_BIT(HDFGWTR, OSLAR_EL1),
    DO_BIT(HDFGRTR, OSLSR_EL1),
    DO_BIT(HDFGRTR, OSECCR_EL1),
    DO_BIT(HDFGRTR, OSDLR_EL1),
    DO_BIT(HDFGRTR, PMEVCNTRN_EL0),
    DO_BIT(HDFGRTR, PMEVTYPERN_EL0),
    DO_BIT(HDFGRTR, PMCCFILTR_EL0),
    DO_BIT(HDFGRTR, PMCCNTR_EL0),
    DO_BIT(HDFGRTR, PMCNTEN),
    DO_BIT(HDFGRTR, PMINTEN),
    DO_BIT(HDFGRTR, PMOVS),
    DO_BIT(HDFGRTR, PMSELR_EL0),
    DO_BIT(HDFGWTR, PMSWINC_EL0),
    DO_BIT(HDFGWTR, PMCR_EL0),
    DO_BIT(HDFGRTR, PMMIR_EL1),
    DO_BIT(HDFGRTR, PMCEIDN_EL0),

    /* Trap bits in HFGITR_EL2, starting from bit 0 */
    DO_BIT(HFGITR, ICIALLUIS),
    DO_BIT(HFGITR, ICIALLU),
    DO_BIT(HFGITR, ICIVAU),
    DO_BIT(HFGITR, DCIVAC),
    DO_BIT(HFGITR, DCISW),
    DO_BIT(HFGITR, DCCSW),
    DO_BIT(HFGITR, DCCISW),
    DO_BIT(HFGITR, DCCVAU),
    DO_BIT(HFGITR, DCCVAP),
    DO_BIT(HFGITR, DCCVADP),
    DO_BIT(HFGITR, DCCIVAC),
    DO_BIT(HFGITR, DCZVA),
    DO_BIT(HFGITR, ATS1E1R),
    DO_BIT(HFGITR, ATS1E1W),
    DO_BIT(HFGITR, ATS1E0R),
    DO_BIT(HFGITR, ATS1E0W),
    DO_BIT(HFGITR, ATS1E1RP),
    DO_BIT(HFGITR, ATS1E1WP),
    DO_TLBINXS_BIT(HFGITR, TLBIVMALLE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIVAE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIASIDE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIVAAE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIVALE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIVAALE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIRVAE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIRVAAE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIRVALE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIRVAALE1OS),
    DO_TLBINXS_BIT(HFGITR, TLBIVMALLE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIVAE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIASIDE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIVAAE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIVALE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIVAALE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIRVAE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIRVAAE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIRVALE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIRVAALE1IS),
    DO_TLBINXS_BIT(HFGITR, TLBIRVAE1),
    DO_TLBINXS_BIT(HFGITR, TLBIRVAAE1),
    DO_TLBINXS_BIT(HFGITR, TLBIRVALE1),
    DO_TLBINXS_BIT(HFGITR, TLBIRVAALE1),
    DO_TLBINXS_BIT(HFGITR, TLBIVMALLE1),
    DO_TLBINXS_BIT(HFGITR, TLBIVAE1),
    DO_TLBINXS_BIT(HFGITR, TLBIASIDE1),
    DO_TLBINXS_BIT(HFGITR, TLBIVAAE1),
    DO_TLBINXS_BIT(HFGITR, TLBIVALE1),
    DO_TLBINXS_BIT(HFGITR, TLBIVAALE1),
    DO_BIT(HFGITR, CFPRCTX),
    DO_BIT(HFGITR, DVPRCTX),
    DO_BIT(HFGITR, CPPRCTX),
    DO_BIT(HFGITR, DCCVAC),
} FGTBit;

#undef DO_BIT
#undef DO_REV_BIT

typedef struct ARMCPRegInfo ARMCPRegInfo;

/*
 * Access functions for coprocessor registers. These cannot fail and
 * may not raise exceptions.
 */
typedef uint64_t CPReadFn(CPUARMState *env, const ARMCPRegInfo *opaque);
typedef void CPWriteFn(CPUARMState *env, const ARMCPRegInfo *opaque,
                       uint64_t value);
/* Access permission check functions for coprocessor registers. */
typedef CPAccessResult CPAccessFn(CPUARMState *env,
                                  const ARMCPRegInfo *opaque,
                                  bool isread);
/* Hook function for register reset */
typedef void CPResetFn(CPUARMState *env, const ARMCPRegInfo *opaque);

#define CP_ANY 0xff

/* Flags in the high bits of nv2_redirect_offset */
#define NV2_REDIR_NV1 0x4000 /* Only redirect when HCR_EL2.NV1 == 1 */
#define NV2_REDIR_NO_NV1 0x8000 /* Only redirect when HCR_EL2.NV1 == 0 */
#define NV2_REDIR_FLAG_MASK 0xc000

/* Definition of an ARM coprocessor register */
struct ARMCPRegInfo {
    /* Name of register (useful mainly for debugging, need not be unique) */
    const char *name;
    /*
     * Location of register: coprocessor number and (crn,crm,opc1,opc2)
     * tuple. Any of crm, opc1 and opc2 may be CP_ANY to indicate a
     * 'wildcard' field -- any value of that field in the MRC/MCR insn
     * will be decoded to this register. The register read and write
     * callbacks will be passed an ARMCPRegInfo with the crn/crm/opc1/opc2
     * used by the program, so it is possible to register a wildcard and
     * then behave differently on read/write if necessary.
     * For 64 bit registers, only crm and opc1 are relevant; crn and opc2
     * must both be zero.
     * For AArch64-visible registers, opc0 is also used.
     * Since there are no "coprocessors" in AArch64, cp is purely used as a
     * way to distinguish (for KVM's benefit) guest-visible system registers
     * from demuxed ones provided to preserve the "no side effects on
     * KVM register read/write from QEMU" semantics. cp==0x13 is guest
     * visible (to match KVM's encoding); cp==0 will be converted to
     * cp==0x13 when the ARMCPRegInfo is registered, for convenience.
     */
    uint8_t cp;
    uint8_t crn;
    uint8_t crm;
    uint8_t opc0;
    uint8_t opc1;
    uint8_t opc2;
    /* Execution state in which this register is visible: ARM_CP_STATE_* */
    CPState state;
    /* Register type: ARM_CP_* bits/values */
    int type;
    /* Access rights: PL*_[RW] */
    CPAccessRights access;
    /* Security state: ARM_CP_SECSTATE_* bits/values */
    CPSecureState secure;
    /*
     * Which fine-grained trap register bit to check, if any. This
     * value encodes both the trap register and bit within it.
     */
    FGTBit fgt;

    /*
     * Offset from VNCR_EL2 when FEAT_NV2 redirects access to memory;
     * may include an NV2_REDIR_* flag.
     */
    uint32_t nv2_redirect_offset;

    /*
     * The opaque pointer passed to define_arm_cp_regs_with_opaque() when
     * this register was defined: can be used to hand data through to the
     * register read/write functions, since they are passed the ARMCPRegInfo*.
     */
    void *opaque;
    /*
     * Value of this register, if it is ARM_CP_CONST. Otherwise, if
     * fieldoffset is non-zero, the reset value of the register.
     */
    uint64_t resetvalue;
    /*
     * Offset of the field in CPUARMState for this register.
     * This is not needed if either:
     *  1. type is ARM_CP_CONST or one of the ARM_CP_SPECIALs
     *  2. both readfn and writefn are specified
     */
    ptrdiff_t fieldoffset; /* offsetof(CPUARMState, field) */

    /*
     * Offsets of the secure and non-secure fields in CPUARMState for the
     * register if it is banked.  These fields are only used during the static
     * registration of a register.  During hashing the bank associated
     * with a given security state is copied to fieldoffset which is used from
     * there on out.
     *
     * It is expected that register definitions use either fieldoffset or
     * bank_fieldoffsets in the definition but not both.  It is also expected
     * that both bank offsets are set when defining a banked register.  This
     * use indicates that a register is banked.
     */
    ptrdiff_t bank_fieldoffsets[2];

    /*
     * Function for making any access checks for this register in addition to
     * those specified by the 'access' permissions bits. If NULL, no extra
     * checks required. The access check is performed at runtime, not at
     * translate time.
     */
    CPAccessFn *accessfn;
    /*
     * Function for handling reads of this register. If NULL, then reads
     * will be done by loading from the offset into CPUARMState specified
     * by fieldoffset.
     */
    CPReadFn *readfn;
    /*
     * Function for handling writes of this register. If NULL, then writes
     * will be done by writing to the offset into CPUARMState specified
     * by fieldoffset.
     */
    CPWriteFn *writefn;
    /*
     * Function for doing a "raw" read; used when we need to copy
     * coprocessor state to the kernel for KVM or out for
     * migration. This only needs to be provided if there is also a
     * readfn and it has side effects (for instance clear-on-read bits).
     */
    CPReadFn *raw_readfn;
    /*
     * Function for doing a "raw" write; used when we need to copy KVM
     * kernel coprocessor state into userspace, or for inbound
     * migration. This only needs to be provided if there is also a
     * writefn and it masks out "unwritable" bits or has write-one-to-clear
     * or similar behaviour.
     */
    CPWriteFn *raw_writefn;
    /*
     * Function for resetting the register. If NULL, then reset will be done
     * by writing resetvalue to the field specified in fieldoffset. If
     * fieldoffset is 0 then no reset will be done.
     */
    CPResetFn *resetfn;

    /*
     * "Original" readfn, writefn, accessfn.
     * For ARMv8.1-VHE register aliases, we overwrite the read/write
     * accessor functions of various EL1/EL0 to perform the runtime
     * check for which sysreg should actually be modified, and then
     * forwards the operation.  Before overwriting the accessors,
     * the original function is copied here, so that accesses that
     * really do go to the EL1/EL0 version proceed normally.
     * (The corresponding EL2 register is linked via opaque.)
     */
    CPReadFn *orig_readfn;
    CPWriteFn *orig_writefn;
    CPAccessFn *orig_accessfn;
};

/*
 * Macros which are lvalues for the field in CPUARMState for the
 * ARMCPRegInfo *ri.
 */
#define CPREG_FIELD32(env, ri) \
    (*(uint32_t *)((char *)(env) + (ri)->fieldoffset))
#define CPREG_FIELD64(env, ri) \
    (*(uint64_t *)((char *)(env) + (ri)->fieldoffset))

void define_one_arm_cp_reg_with_opaque(ARMCPU *cpu, const ARMCPRegInfo *reg,
                                       void *opaque);

static inline void define_one_arm_cp_reg(ARMCPU *cpu, const ARMCPRegInfo *regs)
{
    define_one_arm_cp_reg_with_opaque(cpu, regs, NULL);
}

void define_arm_cp_regs_with_opaque_len(ARMCPU *cpu, const ARMCPRegInfo *regs,
                                        void *opaque, size_t len);

#define define_arm_cp_regs_with_opaque(CPU, REGS, OPAQUE)               \
    do {                                                                \
        QEMU_BUILD_BUG_ON(ARRAY_SIZE(REGS) == 0);                       \
        define_arm_cp_regs_with_opaque_len(CPU, REGS, OPAQUE,           \
                                           ARRAY_SIZE(REGS));           \
    } while (0)

#define define_arm_cp_regs(CPU, REGS) \
    define_arm_cp_regs_with_opaque(CPU, REGS, NULL)

const ARMCPRegInfo *get_arm_cp_reginfo(GHashTable *cpregs, uint32_t encoded_cp);

/*
 * Definition of an ARM co-processor register as viewed from
 * userspace. This is used for presenting sanitised versions of
 * registers to userspace when emulating the Linux AArch64 CPU
 * ID/feature ABI (advertised as HWCAP_CPUID).
 */
typedef struct ARMCPRegUserSpaceInfo {
    /* Name of register */
    const char *name;

    /* Is the name actually a glob pattern */
    bool is_glob;

    /* Only some bits are exported to user space */
    uint64_t exported_bits;

    /* Fixed bits are applied after the mask */
    uint64_t fixed_bits;
} ARMCPRegUserSpaceInfo;

void modify_arm_cp_regs_with_len(ARMCPRegInfo *regs, size_t regs_len,
                                 const ARMCPRegUserSpaceInfo *mods,
                                 size_t mods_len);

#define modify_arm_cp_regs(REGS, MODS)                                  \
    do {                                                                \
        QEMU_BUILD_BUG_ON(ARRAY_SIZE(REGS) == 0);                       \
        QEMU_BUILD_BUG_ON(ARRAY_SIZE(MODS) == 0);                       \
        modify_arm_cp_regs_with_len(REGS, ARRAY_SIZE(REGS),             \
                                    MODS, ARRAY_SIZE(MODS));            \
    } while (0)

/* CPWriteFn that can be used to implement writes-ignored behaviour */
void arm_cp_write_ignore(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value);
/* CPReadFn that can be used for read-as-zero behaviour */
uint64_t arm_cp_read_zero(CPUARMState *env, const ARMCPRegInfo *ri);

/* CPWriteFn that just writes the value to ri->fieldoffset */
void raw_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value);

/*
 * CPResetFn that does nothing, for use if no reset is required even
 * if fieldoffset is non zero.
 */
void arm_cp_reset_ignore(CPUARMState *env, const ARMCPRegInfo *opaque);

/*
 * Return true if this reginfo struct's field in the cpu state struct
 * is 64 bits wide.
 */
static inline bool cpreg_field_is_64bit(const ARMCPRegInfo *ri)
{
    return (ri->state == ARM_CP_STATE_AA64) || (ri->type & ARM_CP_64BIT);
}

static inline bool cp_access_ok(int current_el,
                                const ARMCPRegInfo *ri, int isread)
{
    return (ri->access >> ((current_el * 2) + isread)) & 1;
}

/* Raw read of a coprocessor register (as needed for migration, etc) */
uint64_t read_raw_cp_reg(CPUARMState *env, const ARMCPRegInfo *ri);

/*
 * Return true if the cp register encoding is in the "feature ID space" as
 * defined by FEAT_IDST (and thus should be reported with ER_ELx.EC
 * as EC_SYSTEMREGISTERTRAP rather than EC_UNCATEGORIZED).
 */
static inline bool arm_cpreg_encoding_in_idspace(uint8_t opc0, uint8_t opc1,
                                                 uint8_t opc2,
                                                 uint8_t crn, uint8_t crm)
{
    return opc0 == 3 && (opc1 == 0 || opc1 == 1 || opc1 == 3) &&
        crn == 0 && crm < 8;
}

/*
 * As arm_cpreg_encoding_in_idspace(), but take the encoding from an
 * ARMCPRegInfo.
 */
static inline bool arm_cpreg_in_idspace(const ARMCPRegInfo *ri)
{
    return ri->state == ARM_CP_STATE_AA64 &&
        arm_cpreg_encoding_in_idspace(ri->opc0, ri->opc1, ri->opc2,
                                      ri->crn, ri->crm);
}

#ifdef CONFIG_USER_ONLY
static inline void define_cortex_a72_a57_a53_cp_reginfo(ARMCPU *cpu) { }
#else
void define_cortex_a72_a57_a53_cp_reginfo(ARMCPU *cpu);
#endif

CPAccessResult access_tvm_trvm(CPUARMState *, const ARMCPRegInfo *, bool);

/**
 * arm_cpreg_trap_in_nv: Return true if cpreg traps in nested virtualization
 *
 * Return true if this cpreg is one which should be trapped to EL2 if
 * it is executed at EL1 when nested virtualization is enabled via HCR_EL2.NV.
 */
static inline bool arm_cpreg_traps_in_nv(const ARMCPRegInfo *ri)
{
    /*
     * The Arm ARM defines the registers to be trapped in terms of
     * their names (I_TZTZL). However the underlying principle is "if
     * it would UNDEF at EL1 but work at EL2 then it should trap", and
     * the way the encoding of sysregs and system instructions is done
     * means that the right set of registers is exactly those where
     * the opc1 field is 4 or 5. (You can see this also in the assert
     * we do that the opc1 field and the permissions mask line up in
     * define_one_arm_cp_reg_with_opaque().)
     * Checking the opc1 field is easier for us and avoids the problem
     * that we do not consistently use the right architectural names
     * for all sysregs, since we treat the name field as largely for debug.
     *
     * However we do this check, it is going to be at least potentially
     * fragile to future new sysregs, but this seems the least likely
     * to break.
     *
     * In particular, note that the released sysreg XML defines that
     * the FEAT_MEC sysregs and instructions do not follow this FEAT_NV
     * trapping rule, so we will need to add an ARM_CP_* flag to indicate
     * "register does not trap on NV" to handle those if/when we implement
     * FEAT_MEC.
     */
    return ri->opc1 == 4 || ri->opc1 == 5;
}

/* Macros for accessing a specified CP register bank */
#define A32_BANKED_REG_GET(_env, _regname, _secure)                     \
    ((_secure) ? (_env)->cp15._regname##_s : (_env)->cp15._regname##_ns)

#define A32_BANKED_REG_SET(_env, _regname, _secure, _val)       \
    do {                                                        \
        if (_secure) {                                          \
            (_env)->cp15._regname##_s = (_val);                 \
        } else {                                                \
            (_env)->cp15._regname##_ns = (_val);                \
        }                                                       \
    } while (0)

/*
 * Macros for automatically accessing a specific CP register bank depending on
 * the current secure state of the system.  These macros are not intended for
 * supporting instruction translation reads/writes as these are dependent
 * solely on the SCR.NS bit and not the mode.
 */
#define A32_BANKED_CURRENT_REG_GET(_env, _regname)                          \
    A32_BANKED_REG_GET((_env), _regname,                                    \
                       (arm_is_secure(_env) && !arm_el_is_aa64((_env), 3)))

#define A32_BANKED_CURRENT_REG_SET(_env, _regname, _val)                    \
    A32_BANKED_REG_SET((_env), _regname,                                    \
                       (arm_is_secure(_env) && !arm_el_is_aa64((_env), 3)), \
                       (_val))

#endif /* TARGET_ARM_CPREGS_H */
