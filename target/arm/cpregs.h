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
     * with gen_io_start() and also end the TB. In particular, registers which
     * implement clocks or timers require this.
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
};

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
     * 0xc or 0x18).
     */
    CP_ACCESS_TRAP = (1 << 2),
    CP_ACCESS_TRAP_EL2 = CP_ACCESS_TRAP | 2,
    CP_ACCESS_TRAP_EL3 = CP_ACCESS_TRAP | 3,

    /*
     * Access fails and results in an exception syndrome 0x0 ("uncategorized").
     * Note that this is not a catch-all case -- the set of cases which may
     * result in this failure is specifically defined by the architecture.
     */
    CP_ACCESS_TRAP_UNCATEGORIZED = (2 << 2),
    CP_ACCESS_TRAP_UNCATEGORIZED_EL2 = CP_ACCESS_TRAP_UNCATEGORIZED | 2,
    CP_ACCESS_TRAP_UNCATEGORIZED_EL3 = CP_ACCESS_TRAP_UNCATEGORIZED | 3,
} CPAccessResult;

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
     * "Original" writefn and readfn.
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

#endif /* TARGET_ARM_CPREGS_H */
