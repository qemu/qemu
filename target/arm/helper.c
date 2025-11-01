/*
 * ARM generic helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-features.h"
#include "exec/page-protection.h"
#include "exec/mmap-lock.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "qemu/qemu-print.h"
#include "exec/cputlb.h"
#include "exec/translation-block.h"
#include "hw/irq.h"
#include "system/cpu-timers.h"
#include "exec/icount.h"
#include "system/kvm.h"
#include "system/tcg.h"
#include "qapi/error.h"
#include "qemu/guest-random.h"
#ifdef CONFIG_TCG
#include "accel/tcg/probe.h"
#include "accel/tcg/getpc.h"
#include "semihosting/common-semi.h"
#endif
#include "cpregs.h"
#include "target/arm/gtimer.h"
#include "qemu/plugin.h"

#define HELPER_H "tcg/helper.h"
#include "exec/helper-proto.h.inc"

static void switch_mode(CPUARMState *env, int mode);

int compare_u64(const void *a, const void *b)
{
    if (*(uint64_t *)a > *(uint64_t *)b) {
        return 1;
    }
    if (*(uint64_t *)a < *(uint64_t *)b) {
        return -1;
    }
    return 0;
}

/*
 * Macros which are lvalues for the field in CPUARMState for the
 * ARMCPRegInfo *ri.
 */
#define CPREG_FIELD32(env, ri) \
    (*(uint32_t *)((char *)(env) + (ri)->fieldoffset))
#define CPREG_FIELD64(env, ri) \
    (*(uint64_t *)((char *)(env) + (ri)->fieldoffset))

uint64_t raw_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    assert(ri->fieldoffset);
    switch (cpreg_field_type(ri)) {
    case MO_64:
        return CPREG_FIELD64(env, ri);
    case MO_32:
        return CPREG_FIELD32(env, ri);
    default:
        g_assert_not_reached();
    }
}

void raw_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    assert(ri->fieldoffset);
    switch (cpreg_field_type(ri)) {
    case MO_64:
        CPREG_FIELD64(env, ri) = value;
        break;
    case MO_32:
        CPREG_FIELD32(env, ri) = value;
        break;
    default:
        g_assert_not_reached();
    }
}

#undef CPREG_FIELD32
#undef CPREG_FIELD64

static void *raw_ptr(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return (char *)env + ri->fieldoffset;
}

uint64_t read_raw_cp_reg(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Raw read of a coprocessor register (as needed for migration, etc). */
    if (ri->type & ARM_CP_CONST) {
        return ri->resetvalue;
    } else if (ri->raw_readfn) {
        return ri->raw_readfn(env, ri);
    } else if (ri->readfn) {
        return ri->readfn(env, ri);
    } else {
        return raw_read(env, ri);
    }
}

static void write_raw_cp_reg(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t v)
{
    /*
     * Raw write of a coprocessor register (as needed for migration, etc).
     * Note that constant registers are treated as write-ignored; the
     * caller should check for success by whether a readback gives the
     * value written.
     */
    if (ri->type & ARM_CP_CONST) {
        return;
    } else if (ri->raw_writefn) {
        ri->raw_writefn(env, ri, v);
    } else if (ri->writefn) {
        ri->writefn(env, ri, v);
    } else {
        raw_write(env, ri, v);
    }
}

static bool raw_accessors_invalid(const ARMCPRegInfo *ri)
{
   /*
    * Return true if the regdef would cause an assertion if you called
    * read_raw_cp_reg() or write_raw_cp_reg() on it (ie if it is a
    * program bug for it not to have the NO_RAW flag).
    * NB that returning false here doesn't necessarily mean that calling
    * read/write_raw_cp_reg() is safe, because we can't distinguish "has
    * read/write access functions which are safe for raw use" from "has
    * read/write access functions which have side effects but has forgotten
    * to provide raw access functions".
    * The tests here line up with the conditions in read/write_raw_cp_reg()
    * and assertions in raw_read()/raw_write().
    */
    if ((ri->type & ARM_CP_CONST) ||
        ri->fieldoffset ||
        ((ri->raw_writefn || ri->writefn) && (ri->raw_readfn || ri->readfn))) {
        return false;
    }
    return true;
}

bool write_cpustate_to_list(ARMCPU *cpu, bool kvm_sync)
{
    /* Write the coprocessor state from cpu->env to the (index,value) list. */
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        uint32_t regidx = kvm_to_cpreg_id(cpu->cpreg_indexes[i]);
        const ARMCPRegInfo *ri;
        uint64_t newval;

        ri = get_arm_cp_reginfo(cpu->cp_regs, regidx);
        if (!ri) {
            ok = false;
            continue;
        }
        if (ri->type & ARM_CP_NO_RAW) {
            continue;
        }

        newval = read_raw_cp_reg(&cpu->env, ri);
        if (kvm_sync) {
            /*
             * Only sync if the previous list->cpustate sync succeeded.
             * Rather than tracking the success/failure state for every
             * item in the list, we just recheck "does the raw write we must
             * have made in write_list_to_cpustate() read back OK" here.
             */
            uint64_t oldval = cpu->cpreg_values[i];

            if (oldval == newval) {
                continue;
            }

            write_raw_cp_reg(&cpu->env, ri, oldval);
            if (read_raw_cp_reg(&cpu->env, ri) != oldval) {
                continue;
            }

            write_raw_cp_reg(&cpu->env, ri, newval);
        }
        cpu->cpreg_values[i] = newval;
    }
    return ok;
}

bool write_list_to_cpustate(ARMCPU *cpu)
{
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        uint32_t regidx = kvm_to_cpreg_id(cpu->cpreg_indexes[i]);
        uint64_t v = cpu->cpreg_values[i];
        const ARMCPRegInfo *ri;

        ri = get_arm_cp_reginfo(cpu->cp_regs, regidx);
        if (!ri) {
            ok = false;
            continue;
        }
        if (ri->type & ARM_CP_NO_RAW) {
            continue;
        }
        /*
         * Write value and confirm it reads back as written
         * (to catch read-only registers and partially read-only
         * registers where the incoming migration value doesn't match)
         */
        write_raw_cp_reg(&cpu->env, ri, v);
        if (read_raw_cp_reg(&cpu->env, ri) != v) {
            ok = false;
        }
    }
    return ok;
}

static void add_cpreg_to_list(gpointer key, gpointer value, gpointer opaque)
{
    ARMCPU *cpu = opaque;
    uint32_t regidx = (uintptr_t)key;
    const ARMCPRegInfo *ri = value;

    if (!(ri->type & (ARM_CP_NO_RAW | ARM_CP_ALIAS))) {
        cpu->cpreg_indexes[cpu->cpreg_array_len] = cpreg_to_kvm_id(regidx);
        /* The value array need not be initialized at this point */
        cpu->cpreg_array_len++;
    }
}

static void count_cpreg(gpointer key, gpointer value, gpointer opaque)
{
    ARMCPU *cpu = opaque;
    const ARMCPRegInfo *ri = value;

    if (!(ri->type & (ARM_CP_NO_RAW | ARM_CP_ALIAS))) {
        cpu->cpreg_array_len++;
    }
}

void arm_init_cpreg_list(ARMCPU *cpu)
{
    /*
     * Initialise the cpreg_tuples[] array based on the cp_regs hash.
     * Note that we require cpreg_tuples[] to be sorted by key ID.
     */
    int arraylen;

    cpu->cpreg_array_len = 0;
    g_hash_table_foreach(cpu->cp_regs, count_cpreg, cpu);

    arraylen = cpu->cpreg_array_len;
    if (arraylen) {
        cpu->cpreg_indexes = g_new(uint64_t, arraylen);
        cpu->cpreg_values = g_new(uint64_t, arraylen);
        cpu->cpreg_vmstate_indexes = g_new(uint64_t, arraylen);
        cpu->cpreg_vmstate_values = g_new(uint64_t, arraylen);
    } else {
        cpu->cpreg_indexes = NULL;
        cpu->cpreg_values = NULL;
        cpu->cpreg_vmstate_indexes = NULL;
        cpu->cpreg_vmstate_values = NULL;
    }
    cpu->cpreg_vmstate_array_len = arraylen;
    cpu->cpreg_array_len = 0;

    g_hash_table_foreach(cpu->cp_regs, add_cpreg_to_list, cpu);

    assert(cpu->cpreg_array_len == arraylen);

    if (arraylen) {
        qsort(cpu->cpreg_indexes, arraylen, sizeof(uint64_t), compare_u64);
    }
}

bool arm_pan_enabled(CPUARMState *env)
{
    if (is_a64(env)) {
        if ((arm_hcr_el2_eff(env) & (HCR_NV | HCR_NV1)) == (HCR_NV | HCR_NV1)) {
            return false;
        }
        return env->pstate & PSTATE_PAN;
    } else {
        return env->uncached_cpsr & CPSR_PAN;
    }
}

/*
 * Some registers are not accessible from AArch32 EL3 if SCR.NS == 0.
 */
static CPAccessResult access_el3_aa32ns(CPUARMState *env,
                                        const ARMCPRegInfo *ri,
                                        bool isread)
{
    if (!is_a64(env) && arm_current_el(env) == 3 &&
        arm_is_secure_below_el3(env)) {
        return CP_ACCESS_UNDEFINED;
    }
    return CP_ACCESS_OK;
}

/*
 * Some secure-only AArch32 registers trap to EL3 if used from
 * Secure EL1 (but are just ordinary UNDEF in other non-EL3 contexts).
 * Note that an access from Secure EL1 can only happen if EL3 is AArch64.
 * We assume that the .access field is set to PL1_RW.
 */
static CPAccessResult access_trap_aa32s_el1(CPUARMState *env,
                                            const ARMCPRegInfo *ri,
                                            bool isread)
{
    if (arm_current_el(env) == 3) {
        return CP_ACCESS_OK;
    }
    if (arm_is_secure_below_el3(env)) {
        if (env->cp15.scr_el3 & SCR_EEL2) {
            return CP_ACCESS_TRAP_EL2;
        }
        return CP_ACCESS_TRAP_EL3;
    }
    /* This will be EL1 NS and EL2 NS, which just UNDEF */
    return CP_ACCESS_UNDEFINED;
}

/* Check for traps from EL1 due to HCR_EL2.TVM and HCR_EL2.TRVM.  */
CPAccessResult access_tvm_trvm(CPUARMState *env, const ARMCPRegInfo *ri,
                               bool isread)
{
    if (arm_current_el(env) == 1) {
        uint64_t trap = isread ? HCR_TRVM : HCR_TVM;
        if (arm_hcr_el2_eff(env) & trap) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    return CP_ACCESS_OK;
}

/* Check for traps from EL1 due to HCR_EL2.TSW.  */
static CPAccessResult access_tsw(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TSW)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

/* Check for traps from EL1 due to HCR_EL2.TACR.  */
static CPAccessResult access_tacr(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TACR)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

static void dacr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    raw_write(env, ri, value);
    tlb_flush(CPU(cpu)); /* Flush TLB as domain not tracked in TLB */
}

static void fcse_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    if (raw_read(env, ri) != value) {
        /*
         * Unlike real hardware the qemu TLB uses virtual addresses,
         * not modified virtual addresses, so this causes a TLB flush.
         */
        tlb_flush(CPU(cpu));
        raw_write(env, ri, value);
    }
}

static void contextidr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    if (raw_read(env, ri) != value && !arm_feature(env, ARM_FEATURE_PMSA)
        && !extended_addresses_enabled(env)) {
        /*
         * For VMSA (when not using the LPAE long descriptor page table
         * format) this register includes the ASID, so do a TLB flush.
         * For PMSA it is purely a process ID and no action is needed.
         */
        tlb_flush(CPU(cpu));
    }
    raw_write(env, ri, value);
}

int alle1_tlbmask(CPUARMState *env)
{
    /*
     * Note that the 'ALL' scope must invalidate both stage 1 and
     * stage 2 translations, whereas most other scopes only invalidate
     * stage 1 translations.
     *
     * For AArch32 this is only used for TLBIALLNSNH and VTTBR
     * writes, so only needs to apply to NS PL1&0, not S PL1&0.
     */
    return (ARMMMUIdxBit_E10_1 |
            ARMMMUIdxBit_E10_1_PAN |
            ARMMMUIdxBit_E10_1_GCS |
            ARMMMUIdxBit_E10_0 |
            ARMMMUIdxBit_E10_0_GCS |
            ARMMMUIdxBit_Stage2 |
            ARMMMUIdxBit_Stage2_S);
}

static const ARMCPRegInfo cp_reginfo[] = {
    /*
     * Define the secure and non-secure FCSE identifier CP registers
     * separately because there is no secure bank in V8 (no _EL3).  This allows
     * the secure register to be properly reset and migrated. There is also no
     * v8 EL1 version of the register so the non-secure instance stands alone.
     */
    { .name = "FCSEIDR",
      .cp = 15, .opc1 = 0, .crn = 13, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .secure = ARM_CP_SECSTATE_NS,
      .fieldoffset = offsetof(CPUARMState, cp15.fcseidr_ns),
      .resetvalue = 0, .writefn = fcse_write, .raw_writefn = raw_write, },
    { .name = "FCSEIDR_S",
      .cp = 15, .opc1 = 0, .crn = 13, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .secure = ARM_CP_SECSTATE_S,
      .fieldoffset = offsetof(CPUARMState, cp15.fcseidr_s),
      .resetvalue = 0, .writefn = fcse_write, .raw_writefn = raw_write, },
    /*
     * Define the secure and non-secure context identifier CP registers
     * separately because there is no secure bank in V8 (no _EL3).  This allows
     * the secure register to be properly reset and migrated.  In the
     * non-secure case, the 32-bit register will have reset and migration
     * disabled during registration as it is handled by the 64-bit instance.
     */
    { .name = "CONTEXTIDR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 13, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_CONTEXTIDR_EL1,
      .nv2_redirect_offset = 0x108 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 13, 0, 1),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 13, 0, 1),
      .secure = ARM_CP_SECSTATE_NS,
      .fieldoffset = offsetof(CPUARMState, cp15.contextidr_el[1]),
      .resetvalue = 0, .writefn = contextidr_write, .raw_writefn = raw_write, },
    { .name = "CONTEXTIDR_S", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 13, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .secure = ARM_CP_SECSTATE_S,
      .fieldoffset = offsetof(CPUARMState, cp15.contextidr_s),
      .resetvalue = 0, .writefn = contextidr_write, .raw_writefn = raw_write, },
};

static const ARMCPRegInfo not_v8_cp_reginfo[] = {
    /*
     * NB: Some of these registers exist in v8 but with more precise
     * definitions that don't use CP_ANY wildcards (mostly in v8_cp_reginfo[]).
     */
    /* MMU Domain access control / MPU write buffer control */
    { .name = "DACR",
      .cp = 15, .opc1 = CP_ANY, .crn = 3, .crm = CP_ANY, .opc2 = CP_ANY,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .resetvalue = 0,
      .writefn = dacr_write, .raw_writefn = raw_write,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.dacr_s),
                             offsetoflow32(CPUARMState, cp15.dacr_ns) } },
    /*
     * ARMv7 allocates a range of implementation defined TLB LOCKDOWN regs.
     * For v6 and v5, these mappings are overly broad.
     */
    { .name = "TLB_LOCKDOWN", .cp = 15, .crn = 10, .crm = 0,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "TLB_LOCKDOWN", .cp = 15, .crn = 10, .crm = 1,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "TLB_LOCKDOWN", .cp = 15, .crn = 10, .crm = 4,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "TLB_LOCKDOWN", .cp = 15, .crn = 10, .crm = 8,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_NOP },
    /* Cache maintenance ops; some of this space may be overridden later. */
    { .name = "CACHEMAINT", .cp = 15, .crn = 7, .crm = CP_ANY,
      .opc1 = 0, .opc2 = CP_ANY, .access = PL1_W,
      .type = ARM_CP_NOP | ARM_CP_OVERRIDE },
};

static const ARMCPRegInfo not_v6_cp_reginfo[] = {
    /*
     * Not all pre-v6 cores implemented this WFI, so this is slightly
     * over-broad.
     */
    { .name = "WFI_v5", .cp = 15, .crn = 7, .crm = 8, .opc1 = 0, .opc2 = 2,
      .access = PL1_W, .type = ARM_CP_WFI },
};

static const ARMCPRegInfo not_v7_cp_reginfo[] = {
    /*
     * Standard v6 WFI (also used in some pre-v6 cores); not in v7 (which
     * is UNPREDICTABLE; we choose to NOP as most implementations do).
     */
    { .name = "WFI_v6", .cp = 15, .crn = 7, .crm = 0, .opc1 = 0, .opc2 = 4,
      .access = PL1_W, .type = ARM_CP_WFI },
    /*
     * L1 cache lockdown. Not architectural in v6 and earlier but in practice
     * implemented in 926, 946, 1026, 1136, 1176 and 11MPCore. StrongARM and
     * OMAPCP will override this space.
     */
    { .name = "DLOCKDOWN", .cp = 15, .crn = 9, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .fieldoffset = offsetof(CPUARMState, cp15.c9_data),
      .resetvalue = 0 },
    { .name = "ILOCKDOWN", .cp = 15, .crn = 9, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .fieldoffset = offsetof(CPUARMState, cp15.c9_insn),
      .resetvalue = 0 },
    /* v6 doesn't have the cache ID registers but Linux reads them anyway */
    { .name = "DUMMY", .cp = 15, .crn = 0, .crm = 0, .opc1 = 1, .opc2 = CP_ANY,
      .access = PL1_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = 0 },
    /*
     * We don't implement pre-v7 debug but most CPUs had at least a DBGDIDR;
     * implementing it as RAZ means the "debug architecture version" bits
     * will read as a reserved value, which should cause Linux to not try
     * to use the debug hardware.
     */
    { .name = "DBGDIDR", .cp = 14, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL0_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "PRRR", .cp = 15, .crn = 10, .crm = 2,
      .opc1 = 0, .opc2 = 0, .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "NMRR", .cp = 15, .crn = 10, .crm = 2,
      .opc1 = 0, .opc2 = 1, .access = PL1_RW, .type = ARM_CP_NOP },
};

static void cpacr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    uint32_t mask = 0;

    /* In ARMv8 most bits of CPACR_EL1 are RES0. */
    if (!arm_feature(env, ARM_FEATURE_V8)) {
        /*
         * ARMv7 defines bits for unimplemented coprocessors as RAZ/WI.
         * ASEDIS [31] and D32DIS [30] are both UNK/SBZP without VFP.
         * TRCDIS [28] is RAZ/WI since we do not implement a trace macrocell.
         */
        if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))) {
            /* VFP coprocessor: cp10 & cp11 [23:20] */
            mask |= R_CPACR_ASEDIS_MASK |
                    R_CPACR_D32DIS_MASK |
                    R_CPACR_CP11_MASK |
                    R_CPACR_CP10_MASK;

            if (!arm_feature(env, ARM_FEATURE_NEON)) {
                /* ASEDIS [31] bit is RAO/WI */
                value |= R_CPACR_ASEDIS_MASK;
            }

            /*
             * VFPv3 and upwards with NEON implement 32 double precision
             * registers (D0-D31).
             */
            if (!cpu_isar_feature(aa32_simd_r32, env_archcpu(env))) {
                /* D32DIS [30] is RAO/WI if D16-31 are not implemented. */
                value |= R_CPACR_D32DIS_MASK;
            }
        }
        value &= mask;
    }

    /*
     * For A-profile AArch32 EL3 (but not M-profile secure mode), if NSACR.CP10
     * is 0 then CPACR.{CP11,CP10} ignore writes and read as 0b00.
     */
    if (arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
        !arm_is_secure(env) && !extract32(env->cp15.nsacr, 10, 1)) {
        mask = R_CPACR_CP11_MASK | R_CPACR_CP10_MASK;
        value = (value & ~mask) | (env->cp15.cpacr_el1 & mask);
    }

    env->cp15.cpacr_el1 = value;
}

static uint64_t cpacr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /*
     * For A-profile AArch32 EL3 (but not M-profile secure mode), if NSACR.CP10
     * is 0 then CPACR.{CP11,CP10} ignore writes and read as 0b00.
     */
    uint64_t value = env->cp15.cpacr_el1;

    if (arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
        !arm_is_secure(env) && !extract32(env->cp15.nsacr, 10, 1)) {
        value = ~(R_CPACR_CP11_MASK | R_CPACR_CP10_MASK);
    }
    return value;
}


static void cpacr_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /*
     * Call cpacr_write() so that we reset with the correct RAO bits set
     * for our CPU features.
     */
    cpacr_write(env, ri, 0);
}

static CPAccessResult cpacr_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    if (arm_feature(env, ARM_FEATURE_V8)) {
        /* Check if CPACR accesses are to be trapped to EL2 */
        if (arm_current_el(env) == 1 && arm_is_el2_enabled(env) &&
            FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, TCPAC)) {
            return CP_ACCESS_TRAP_EL2;
        /* Check if CPACR accesses are to be trapped to EL3 */
        } else if (arm_current_el(env) < 3 &&
                   FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, TCPAC)) {
            return CP_ACCESS_TRAP_EL3;
        }
    }

    return CP_ACCESS_OK;
}

static CPAccessResult cptr_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    /* Check if CPTR accesses are set to trap to EL3 */
    if (arm_current_el(env) == 2 &&
        FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, TCPAC)) {
        return CP_ACCESS_TRAP_EL3;
    }

    return CP_ACCESS_OK;
}

static const ARMCPRegInfo v6_cp_reginfo[] = {
    /* prefetch by MVA in v6, NOP in v7 */
    { .name = "MVA_prefetch",
      .cp = 15, .crn = 7, .crm = 13, .opc1 = 0, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NOP },
    /*
     * We need to break the TB after ISB to execute self-modifying code
     * correctly and also to take any pending interrupts immediately.
     * So use arm_cp_write_ignore() function instead of ARM_CP_NOP flag.
     */
    { .name = "ISB", .cp = 15, .crn = 7, .crm = 5, .opc1 = 0, .opc2 = 4,
      .access = PL0_W, .type = ARM_CP_NO_RAW, .writefn = arm_cp_write_ignore },
    { .name = "DSB", .cp = 15, .crn = 7, .crm = 10, .opc1 = 0, .opc2 = 4,
      .access = PL0_W, .type = ARM_CP_NOP },
    { .name = "DMB", .cp = 15, .crn = 7, .crm = 10, .opc1 = 0, .opc2 = 5,
      .access = PL0_W, .type = ARM_CP_NOP },
    { .name = "IFAR", .cp = 15, .crn = 6, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ifar_s),
                             offsetof(CPUARMState, cp15.ifar_ns) },
      .resetvalue = 0, },
    /*
     * Watchpoint Fault Address Register : should actually only be present
     * for 1136, 1176, 11MPCore.
     */
    { .name = "WFAR", .cp = 15, .crn = 6, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0, },
    { .name = "CPACR_EL1", .state = ARM_CP_STATE_BOTH, .opc0 = 3,
      .crn = 1, .crm = 0, .opc1 = 0, .opc2 = 2, .accessfn = cpacr_access,
      .fgt = FGT_CPACR_EL1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 1, 1, 2),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 1, 0, 2),
      .nv2_redirect_offset = 0x100 | NV2_REDIR_NV1,
      .access = PL1_RW, .fieldoffset = offsetof(CPUARMState, cp15.cpacr_el1),
      .resetfn = cpacr_reset, .writefn = cpacr_write, .readfn = cpacr_read },
};

/*
 * Bits in MDCR_EL2 and MDCR_EL3 which pmu_counter_enabled() looks at.
 * We use these to decide whether we need to wrap a write to MDCR_EL2
 * or MDCR_EL3 in pmu_op_start()/pmu_op_finish() calls.
 */
#define MDCR_EL2_PMU_ENABLE_BITS \
    (MDCR_HPME | MDCR_HPMD | MDCR_HPMN | MDCR_HCCD | MDCR_HLP)
#define MDCR_EL3_PMU_ENABLE_BITS (MDCR_SPME | MDCR_SCCD)

static void vbar_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    /*
     * Note that even though the AArch64 view of this register has bits
     * [10:0] all RES0 we can only mask the bottom 5, to comply with the
     * architectural requirements for bits which are RES0 only in some
     * contexts. (ARMv8 would permit us to do no masking at all, but ARMv7
     * requires the bottom five bits to be RAZ/WI because they're UNK/SBZP.)
     */
    raw_write(env, ri, value & ~0x1FULL);
}

static void scr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    /* Begin with base v8.0 state.  */
    uint64_t valid_mask = 0x3fff;
    ARMCPU *cpu = env_archcpu(env);
    uint64_t changed;

    /*
     * Because SCR_EL3 is the "real" cpreg and SCR is the alias, reset always
     * passes the reginfo for SCR_EL3, which has type ARM_CP_STATE_AA64.
     * Instead, choose the format based on the mode of EL3.
     */
    if (arm_el_is_aa64(env, 3)) {
        value |= SCR_FW | SCR_AW;      /* RES1 */
        valid_mask &= ~SCR_NET;        /* RES0 */

        if (!cpu_isar_feature(aa64_aa32_el1, cpu) &&
            !cpu_isar_feature(aa64_aa32_el2, cpu)) {
            value |= SCR_RW;           /* RAO/WI */
        }
        if (cpu_isar_feature(aa64_ras, cpu)) {
            valid_mask |= SCR_TERR;
        }
        if (cpu_isar_feature(aa64_lor, cpu)) {
            valid_mask |= SCR_TLOR;
        }
        if (cpu_isar_feature(aa64_pauth, cpu)) {
            valid_mask |= SCR_API | SCR_APK;
        }
        if (cpu_isar_feature(aa64_sel2, cpu)) {
            valid_mask |= SCR_EEL2;
        } else if (cpu_isar_feature(aa64_rme, cpu)) {
            /* With RME and without SEL2, NS is RES1 (R_GSWWH, I_DJJQJ). */
            value |= SCR_NS;
        }
        if (cpu_isar_feature(aa64_mte, cpu)) {
            valid_mask |= SCR_ATA;
        }
        if (cpu_isar_feature(aa64_scxtnum, cpu)) {
            valid_mask |= SCR_ENSCXT;
        }
        if (cpu_isar_feature(aa64_doublefault, cpu)) {
            valid_mask |= SCR_EASE | SCR_NMEA;
        }
        if (cpu_isar_feature(aa64_sme, cpu)) {
            valid_mask |= SCR_ENTP2;
        }
        if (cpu_isar_feature(aa64_hcx, cpu)) {
            valid_mask |= SCR_HXEN;
        }
        if (cpu_isar_feature(aa64_fgt, cpu)) {
            valid_mask |= SCR_FGTEN;
        }
        if (cpu_isar_feature(aa64_rme, cpu)) {
            valid_mask |= SCR_NSE | SCR_GPF;
        }
        if (cpu_isar_feature(aa64_ecv, cpu)) {
            valid_mask |= SCR_ECVEN;
        }
        if (cpu_isar_feature(aa64_gcs, cpu)) {
            valid_mask |= SCR_GCSEN;
        }
        if (cpu_isar_feature(aa64_tcr2, cpu)) {
            valid_mask |= SCR_TCR2EN;
        }
        if (cpu_isar_feature(aa64_sctlr2, cpu)) {
            valid_mask |= SCR_SCTLR2EN;
        }
        if (cpu_isar_feature(aa64_s1pie, cpu) ||
            cpu_isar_feature(aa64_s2pie, cpu)) {
            valid_mask |= SCR_PIEN;
        }
        if (cpu_isar_feature(aa64_aie, cpu)) {
            valid_mask |= SCR_AIEN;
        }
        if (cpu_isar_feature(aa64_mec, cpu)) {
            valid_mask |= SCR_MECEN;
        }
    } else {
        valid_mask &= ~(SCR_RW | SCR_ST);
        if (cpu_isar_feature(aa32_ras, cpu)) {
            valid_mask |= SCR_TERR;
        }
    }

    if (!arm_feature(env, ARM_FEATURE_EL2)) {
        valid_mask &= ~SCR_HCE;

        /*
         * On ARMv7, SMD (or SCD as it is called in v7) is only
         * supported if EL2 exists. The bit is UNK/SBZP when
         * EL2 is unavailable. In QEMU ARMv7, we force it to always zero
         * when EL2 is unavailable.
         * On ARMv8, this bit is always available.
         */
        if (arm_feature(env, ARM_FEATURE_V7) &&
            !arm_feature(env, ARM_FEATURE_V8)) {
            valid_mask &= ~SCR_SMD;
        }
    }

    /* Clear all-context RES0 bits.  */
    value &= valid_mask;
    changed = env->cp15.scr_el3 ^ value;
    env->cp15.scr_el3 = value;

    /*
     * If SCR_EL3.{NS,NSE} changes, i.e. change of security state,
     * we must invalidate all TLBs below EL3.
     */
    if (changed & (SCR_NS | SCR_NSE)) {
        tlb_flush_by_mmuidx(env_cpu(env), (ARMMMUIdxBit_E10_0 |
                                           ARMMMUIdxBit_E10_0_GCS |
                                           ARMMMUIdxBit_E20_0 |
                                           ARMMMUIdxBit_E20_0_GCS |
                                           ARMMMUIdxBit_E10_1 |
                                           ARMMMUIdxBit_E10_1_PAN |
                                           ARMMMUIdxBit_E10_1_GCS |
                                           ARMMMUIdxBit_E20_2 |
                                           ARMMMUIdxBit_E20_2_PAN |
                                           ARMMMUIdxBit_E20_2_GCS |
                                           ARMMMUIdxBit_E2 |
                                           ARMMMUIdxBit_E2_GCS));
    }
}

static void scr_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /*
     * scr_write will set the RES1 bits on an AArch64-only CPU.
     * The reset value will be 0x30 on an AArch64-only CPU and 0 otherwise.
     */
    scr_write(env, ri, 0);
}

static CPAccessResult access_tid4(CPUARMState *env,
                                  const ARMCPRegInfo *ri,
                                  bool isread)
{
    if (arm_current_el(env) == 1 &&
        (arm_hcr_el2_eff(env) & (HCR_TID2 | HCR_TID4))) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static uint64_t ccsidr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);

    /*
     * Acquire the CSSELR index from the bank corresponding to the CCSIDR
     * bank
     */
    uint32_t index = A32_BANKED_REG_GET(env, csselr,
                                        ri->secure & ARM_CP_SECSTATE_S);

    return cpu->ccsidr[index];
}

static void csselr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    raw_write(env, ri, value & 0xf);
}

static uint64_t isr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    CPUState *cs = env_cpu(env);
    bool el1 = arm_current_el(env) == 1;
    uint64_t hcr_el2 = el1 ? arm_hcr_el2_eff(env) : 0;
    uint64_t ret = 0;

    if (hcr_el2 & HCR_IMO) {
        if (cpu_test_interrupt(cs, CPU_INTERRUPT_VIRQ)) {
            ret |= CPSR_I;
        }
        if (cpu_test_interrupt(cs, CPU_INTERRUPT_VINMI)) {
            ret |= ISR_IS;
            ret |= CPSR_I;
        }
    } else {
        if (cpu_test_interrupt(cs, CPU_INTERRUPT_HARD)) {
            ret |= CPSR_I;
        }

        if (cpu_test_interrupt(cs, CPU_INTERRUPT_NMI)) {
            ret |= ISR_IS;
            ret |= CPSR_I;
        }
    }

    if (hcr_el2 & HCR_FMO) {
        if (cpu_test_interrupt(cs, CPU_INTERRUPT_VFIQ)) {
            ret |= CPSR_F;
        }
        if (cpu_test_interrupt(cs, CPU_INTERRUPT_VFNMI)) {
            ret |= ISR_FS;
            ret |= CPSR_F;
        }
    } else {
        if (cpu_test_interrupt(cs, CPU_INTERRUPT_FIQ)) {
            ret |= CPSR_F;
        }
    }

    if (hcr_el2 & HCR_AMO) {
        if (cpu_test_interrupt(cs, CPU_INTERRUPT_VSERR)) {
            ret |= CPSR_A;
        }
    }

    return ret;
}

static CPAccessResult access_aa64_tid1(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TID1)) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult access_aa32_tid1(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if (arm_feature(env, ARM_FEATURE_V8)) {
        return access_aa64_tid1(env, ri, isread);
    }

    return CP_ACCESS_OK;
}

static const ARMCPRegInfo v7_cp_reginfo[] = {
    /* the old v6 WFI, UNPREDICTABLE in v7 but we choose to NOP */
    { .name = "NOP", .cp = 15, .crn = 7, .crm = 0, .opc1 = 0, .opc2 = 4,
      .access = PL1_W, .type = ARM_CP_NOP },
    { .name = "CCSIDR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .crn = 0, .crm = 0, .opc1 = 1, .opc2 = 0,
      .access = PL1_R,
      .accessfn = access_tid4,
      .fgt = FGT_CCSIDR_EL1,
      .readfn = ccsidr_read, .type = ARM_CP_NO_RAW },
    { .name = "CSSELR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .crn = 0, .crm = 0, .opc1 = 2, .opc2 = 0,
      .access = PL1_RW,
      .accessfn = access_tid4,
      .fgt = FGT_CSSELR_EL1,
      .writefn = csselr_write, .resetvalue = 0,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.csselr_s),
                             offsetof(CPUARMState, cp15.csselr_ns) } },
    /*
     * Auxiliary ID register: this actually has an IMPDEF value but for now
     * just RAZ for all cores:
     */
    { .name = "AIDR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 1, .crn = 0, .crm = 0, .opc2 = 7,
      .access = PL1_R, .type = ARM_CP_CONST,
      .accessfn = access_aa64_tid1,
      .fgt = FGT_AIDR_EL1,
      .resetvalue = 0 },
    /*
     * Auxiliary fault status registers: these also are IMPDEF, and we
     * choose to RAZ/WI for all cores.
     */
    { .name = "AFSR0_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 5, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_AFSR0_EL1,
      .nv2_redirect_offset = 0x128 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 5, 1, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 5, 1, 0),
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "AFSR1_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 5, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_AFSR1_EL1,
      .nv2_redirect_offset = 0x130 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 5, 1, 1),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 5, 1, 1),
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /*
     * MAIR can just read-as-written because we don't implement caches
     * and so don't need to care about memory attributes.
     */
    { .name = "MAIR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_MAIR_EL1,
      .nv2_redirect_offset = 0x140 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 10, 2, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 10, 2, 0),
      .fieldoffset = offsetof(CPUARMState, cp15.mair_el[1]),
      .resetvalue = 0 },
    { .name = "MAIR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 10, .crm = 2, .opc2 = 0,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.mair_el[3]),
      .resetvalue = 0 },
    /*
     * For non-long-descriptor page tables these are PRRR and NMRR;
     * regardless they still act as reads-as-written for QEMU.
     */
     /*
      * MAIR0/1 are defined separately from their 64-bit counterpart which
      * allows them to assign the correct fieldoffset based on the endianness
      * handled in the field definitions.
      */
    { .name = "MAIR0", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 10, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.mair0_s),
                             offsetof(CPUARMState, cp15.mair0_ns) },
      .resetfn = arm_cp_reset_ignore },
    { .name = "MAIR1", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 10, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.mair1_s),
                             offsetof(CPUARMState, cp15.mair1_ns) },
      .resetfn = arm_cp_reset_ignore },
    { .name = "ISR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 0,
      .fgt = FGT_ISR_EL1,
      .type = ARM_CP_NO_RAW, .access = PL1_R, .readfn = isr_read },
};

static void teecr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    value &= 1;
    env->teecr = value;
}

static CPAccessResult teecr_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    /*
     * HSTR.TTEE only exists in v7A, not v8A, but v8A doesn't have T2EE
     * at all, so we don't need to check whether we're v8A.
     */
    if (arm_current_el(env) < 2 && !arm_is_secure_below_el3(env) &&
        (env->cp15.hstr_el2 & HSTR_TTEE)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult teehbr_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                    bool isread)
{
    if (arm_current_el(env) == 0 && (env->teecr & 1)) {
        return CP_ACCESS_TRAP_EL1;
    }
    return teecr_access(env, ri, isread);
}

static const ARMCPRegInfo t2ee_cp_reginfo[] = {
    { .name = "TEECR", .cp = 14, .crn = 0, .crm = 0, .opc1 = 6, .opc2 = 0,
      .access = PL1_RW, .fieldoffset = offsetof(CPUARMState, teecr),
      .resetvalue = 0,
      .writefn = teecr_write, .accessfn = teecr_access },
    { .name = "TEEHBR", .cp = 14, .crn = 1, .crm = 0, .opc1 = 6, .opc2 = 0,
      .access = PL0_RW, .fieldoffset = offsetof(CPUARMState, teehbr),
      .accessfn = teehbr_access, .resetvalue = 0 },
};

static const ARMCPRegInfo v6k_cp_reginfo[] = {
    { .name = "TPIDR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 2, .crn = 13, .crm = 0,
      .access = PL0_RW,
      .fgt = FGT_TPIDR_EL0,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidr_el[0]), .resetvalue = 0 },
    { .name = "TPIDRURW", .cp = 15, .crn = 13, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL0_RW,
      .fgt = FGT_TPIDR_EL0,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.tpidrurw_s),
                             offsetoflow32(CPUARMState, cp15.tpidrurw_ns) },
      .resetfn = arm_cp_reset_ignore },
    { .name = "TPIDRRO_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 3, .crn = 13, .crm = 0,
      .access = PL0_R | PL1_W,
      .fgt = FGT_TPIDRRO_EL0,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidrro_el[0]),
      .resetvalue = 0},
    { .name = "TPIDRURO", .cp = 15, .crn = 13, .crm = 0, .opc1 = 0, .opc2 = 3,
      .access = PL0_R | PL1_W,
      .fgt = FGT_TPIDRRO_EL0,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.tpidruro_s),
                             offsetoflow32(CPUARMState, cp15.tpidruro_ns) },
      .resetfn = arm_cp_reset_ignore },
    { .name = "TPIDR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .opc2 = 4, .crn = 13, .crm = 0,
      .access = PL1_RW,
      .fgt = FGT_TPIDR_EL1,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidr_el[1]), .resetvalue = 0 },
    { .name = "TPIDRPRW", .opc1 = 0, .cp = 15, .crn = 13, .crm = 0, .opc2 = 4,
      .access = PL1_RW,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.tpidrprw_s),
                             offsetoflow32(CPUARMState, cp15.tpidrprw_ns) },
      .resetvalue = 0 },
};

static void arm_gt_cntfrq_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);

    cpu->env.cp15.c14_cntfrq = cpu->gt_cntfrq_hz;
}

#ifndef CONFIG_USER_ONLY

static CPAccessResult gt_cntfrq_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    /*
     * CNTFRQ: not visible from PL0 if both PL0PCTEN and PL0VCTEN are zero.
     * Writable only at the highest implemented exception level.
     */
    int el = arm_current_el(env);
    uint64_t hcr;
    uint32_t cntkctl;

    switch (el) {
    case 0:
        hcr = arm_hcr_el2_eff(env);
        if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
            cntkctl = env->cp15.cnthctl_el2;
        } else {
            cntkctl = env->cp15.c14_cntkctl;
        }
        if (!extract32(cntkctl, 0, 2)) {
            return CP_ACCESS_TRAP_EL1;
        }
        break;
    case 1:
        if (!isread && ri->state == ARM_CP_STATE_AA32 &&
            arm_is_secure_below_el3(env)) {
            /* Accesses from 32-bit Secure EL1 UNDEF (*not* trap to EL3!) */
            return CP_ACCESS_UNDEFINED;
        }
        break;
    case 2:
    case 3:
        break;
    }

    if (!isread && el < arm_highest_el(env)) {
        return CP_ACCESS_UNDEFINED;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult gt_counter_access(CPUARMState *env, int timeridx,
                                        bool isread)
{
    unsigned int cur_el = arm_current_el(env);
    bool has_el2 = arm_is_el2_enabled(env);
    uint64_t hcr = arm_hcr_el2_eff(env);

    switch (cur_el) {
    case 0:
        /* If HCR_EL2.<E2H,TGE> == '11': check CNTHCTL_EL2.EL0[PV]CTEN. */
        if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
            return (extract32(env->cp15.cnthctl_el2, timeridx, 1)
                    ? CP_ACCESS_OK : CP_ACCESS_TRAP_EL2);
        }

        /* CNT[PV]CT: not visible from PL0 if EL0[PV]CTEN is zero */
        if (!extract32(env->cp15.c14_cntkctl, timeridx, 1)) {
            return CP_ACCESS_TRAP_EL1;
        }
        /* fall through */
    case 1:
        /* Check CNTHCTL_EL2.EL1PCTEN, which changes location based on E2H. */
        if (has_el2 && timeridx == GTIMER_PHYS &&
            (hcr & HCR_E2H
             ? !extract32(env->cp15.cnthctl_el2, 10, 1)
             : !extract32(env->cp15.cnthctl_el2, 0, 1))) {
            return CP_ACCESS_TRAP_EL2;
        }
        if (has_el2 && timeridx == GTIMER_VIRT) {
            if (FIELD_EX64(env->cp15.cnthctl_el2, CNTHCTL, EL1TVCT)) {
                return CP_ACCESS_TRAP_EL2;
            }
        }
        break;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult gt_timer_access(CPUARMState *env, int timeridx,
                                      bool isread)
{
    unsigned int cur_el = arm_current_el(env);
    bool has_el2 = arm_is_el2_enabled(env);
    uint64_t hcr = arm_hcr_el2_eff(env);

    switch (cur_el) {
    case 0:
        if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
            /* If HCR_EL2.<E2H,TGE> == '11': check CNTHCTL_EL2.EL0[PV]TEN. */
            return (extract32(env->cp15.cnthctl_el2, 9 - timeridx, 1)
                    ? CP_ACCESS_OK : CP_ACCESS_TRAP_EL2);
        }

        /*
         * CNT[PV]_CVAL, CNT[PV]_CTL, CNT[PV]_TVAL: not visible from
         * EL0 if EL0[PV]TEN is zero.
         */
        if (!extract32(env->cp15.c14_cntkctl, 9 - timeridx, 1)) {
            return CP_ACCESS_TRAP_EL1;
        }
        /* fall through */

    case 1:
        if (has_el2 && timeridx == GTIMER_PHYS) {
            if (hcr & HCR_E2H) {
                /* If HCR_EL2.<E2H,TGE> == '10': check CNTHCTL_EL2.EL1PTEN. */
                if (!extract32(env->cp15.cnthctl_el2, 11, 1)) {
                    return CP_ACCESS_TRAP_EL2;
                }
            } else {
                /* If HCR_EL2.<E2H> == 0: check CNTHCTL_EL2.EL1PCEN. */
                if (!extract32(env->cp15.cnthctl_el2, 1, 1)) {
                    return CP_ACCESS_TRAP_EL2;
                }
            }
        }
        if (has_el2 && timeridx == GTIMER_VIRT) {
            if (FIELD_EX64(env->cp15.cnthctl_el2, CNTHCTL, EL1TVT)) {
                return CP_ACCESS_TRAP_EL2;
            }
        }
        break;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult gt_pct_access(CPUARMState *env,
                                    const ARMCPRegInfo *ri,
                                    bool isread)
{
    return gt_counter_access(env, GTIMER_PHYS, isread);
}

static CPAccessResult gt_vct_access(CPUARMState *env,
                                    const ARMCPRegInfo *ri,
                                    bool isread)
{
    return gt_counter_access(env, GTIMER_VIRT, isread);
}

static CPAccessResult gt_ptimer_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    return gt_timer_access(env, GTIMER_PHYS, isread);
}

static CPAccessResult gt_vtimer_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    return gt_timer_access(env, GTIMER_VIRT, isread);
}

static CPAccessResult gt_stimer_access(CPUARMState *env,
                                       const ARMCPRegInfo *ri,
                                       bool isread)
{
    /*
     * The AArch64 register view of the secure physical timer is
     * always accessible from EL3, and configurably accessible from
     * Secure EL1.
     */
    switch (arm_current_el(env)) {
    case 1:
        if (!arm_is_secure(env)) {
            return CP_ACCESS_UNDEFINED;
        }
        if (arm_is_el2_enabled(env)) {
            return CP_ACCESS_UNDEFINED;
        }
        if (!(env->cp15.scr_el3 & SCR_ST)) {
            return CP_ACCESS_TRAP_EL3;
        }
        return CP_ACCESS_OK;
    case 0:
    case 2:
        return CP_ACCESS_UNDEFINED;
    case 3:
        return CP_ACCESS_OK;
    default:
        g_assert_not_reached();
    }
}

static CPAccessResult gt_sel2timer_access(CPUARMState *env,
                                          const ARMCPRegInfo *ri,
                                          bool isread)
{
    /*
     * The AArch64 register view of the secure EL2 timers are mostly
     * accessible from EL3 and EL2 although can also be trapped to EL2
     * from EL1 depending on nested virt config.
     */
    switch (arm_current_el(env)) {
    case 0: /* UNDEFINED */
        return CP_ACCESS_UNDEFINED;
    case 1:
        if (!arm_is_secure(env)) {
            /* UNDEFINED */
            return CP_ACCESS_UNDEFINED;
        } else if (arm_hcr_el2_eff(env) & HCR_NV) {
            /* Aarch64.SystemAccessTrap(EL2, 0x18) */
            return CP_ACCESS_TRAP_EL2;
        }
        /* UNDEFINED */
        return CP_ACCESS_UNDEFINED;
    case 2:
        if (!arm_is_secure(env)) {
            /* UNDEFINED */
            return CP_ACCESS_UNDEFINED;
        }
        return CP_ACCESS_OK;
    case 3:
        if (env->cp15.scr_el3 & SCR_EEL2) {
            return CP_ACCESS_OK;
        } else {
            return CP_ACCESS_UNDEFINED;
        }
    default:
        g_assert_not_reached();
    }
}

uint64_t gt_get_countervalue(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);

    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / gt_cntfrq_period_ns(cpu);
}

static void gt_update_irq(ARMCPU *cpu, int timeridx)
{
    CPUARMState *env = &cpu->env;
    uint64_t cnthctl = env->cp15.cnthctl_el2;
    ARMSecuritySpace ss = arm_security_space(env);
    /* ISTATUS && !IMASK */
    int irqstate = (env->cp15.c14_timer[timeridx].ctl & 6) == 4;

    /*
     * If bit CNTHCTL_EL2.CNT[VP]MASK is set, it overrides IMASK.
     * It is RES0 in Secure and NonSecure state.
     */
    if ((ss == ARMSS_Root || ss == ARMSS_Realm) &&
        ((timeridx == GTIMER_VIRT && (cnthctl & R_CNTHCTL_CNTVMASK_MASK)) ||
         (timeridx == GTIMER_PHYS && (cnthctl & R_CNTHCTL_CNTPMASK_MASK)))) {
        irqstate = 0;
    }

    qemu_set_irq(cpu->gt_timer_outputs[timeridx], irqstate);
    trace_arm_gt_update_irq(timeridx, irqstate);
}

void gt_rme_post_el_change(ARMCPU *cpu, void *ignored)
{
    /*
     * Changing security state between Root and Secure/NonSecure, which may
     * happen when switching EL, can change the effective value of CNTHCTL_EL2
     * mask bits. Update the IRQ state accordingly.
     */
    gt_update_irq(cpu, GTIMER_VIRT);
    gt_update_irq(cpu, GTIMER_PHYS);
}

static uint64_t gt_phys_raw_cnt_offset(CPUARMState *env)
{
    if ((env->cp15.scr_el3 & SCR_ECVEN) &&
        FIELD_EX64(env->cp15.cnthctl_el2, CNTHCTL, ECV) &&
        arm_is_el2_enabled(env) &&
        (arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        return env->cp15.cntpoff_el2;
    }
    return 0;
}

static uint64_t gt_indirect_access_timer_offset(CPUARMState *env, int timeridx)
{
    /*
     * Return the timer offset to use for indirect accesses to the timer.
     * This is the Offset value as defined in D12.2.4.1 "Operation of the
     * CompareValue views of the timers".
     *
     * The condition here is not always the same as the condition for
     * whether to apply an offset register when doing a direct read of
     * the counter sysreg; those conditions are described in the
     * access pseudocode for each counter register.
     */
    switch (timeridx) {
    case GTIMER_PHYS:
        return gt_phys_raw_cnt_offset(env);
    case GTIMER_VIRT:
        return env->cp15.cntvoff_el2;
    case GTIMER_HYP:
    case GTIMER_SEC:
    case GTIMER_HYPVIRT:
    case GTIMER_S_EL2_PHYS:
    case GTIMER_S_EL2_VIRT:
        return 0;
    default:
        g_assert_not_reached();
    }
}

uint64_t gt_direct_access_timer_offset(CPUARMState *env, int timeridx)
{
    /*
     * Return the timer offset to use for direct accesses to the
     * counter registers CNTPCT and CNTVCT, and for direct accesses
     * to the CNT*_TVAL registers.
     *
     * This isn't exactly the same as the indirect-access offset,
     * because here we also care about what EL the register access
     * is being made from.
     *
     * This corresponds to the access pseudocode for the registers.
     */
    uint64_t hcr;

    switch (timeridx) {
    case GTIMER_PHYS:
        if (arm_current_el(env) >= 2) {
            return 0;
        }
        return gt_phys_raw_cnt_offset(env);
    case GTIMER_VIRT:
        switch (arm_current_el(env)) {
        case 2:
            hcr = arm_hcr_el2_eff(env);
            if (hcr & HCR_E2H) {
                return 0;
            }
            break;
        case 0:
            hcr = arm_hcr_el2_eff(env);
            if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
                return 0;
            }
            break;
        }
        return env->cp15.cntvoff_el2;
    case GTIMER_HYP:
    case GTIMER_SEC:
    case GTIMER_HYPVIRT:
    case GTIMER_S_EL2_PHYS:
    case GTIMER_S_EL2_VIRT:
        return 0;
    default:
        g_assert_not_reached();
    }
}

static void gt_recalc_timer(ARMCPU *cpu, int timeridx)
{
    ARMGenericTimer *gt = &cpu->env.cp15.c14_timer[timeridx];

    if (gt->ctl & 1) {
        /*
         * Timer enabled: calculate and set current ISTATUS, irq, and
         * reset timer to when ISTATUS next has to change
         */
        uint64_t offset = gt_indirect_access_timer_offset(&cpu->env, timeridx);
        uint64_t count = gt_get_countervalue(&cpu->env);
        /* Note that this must be unsigned 64 bit arithmetic: */
        int istatus = count - offset >= gt->cval;
        uint64_t nexttick;

        gt->ctl = deposit32(gt->ctl, 2, 1, istatus);

        if (istatus) {
            /*
             * Next transition is when (count - offset) rolls back over to 0.
             * If offset > count then this is when count == offset;
             * if offset <= count then this is when count == offset + 2^64
             * For the latter case we set nexttick to an "as far in future
             * as possible" value and let the code below handle it.
             */
            if (offset > count) {
                nexttick = offset;
            } else {
                nexttick = UINT64_MAX;
            }
        } else {
            /*
             * Next transition is when (count - offset) == cval, i.e.
             * when count == (cval + offset).
             * If that would overflow, then again we set up the next interrupt
             * for "as far in the future as possible" for the code below.
             */
            if (uadd64_overflow(gt->cval, offset, &nexttick)) {
                nexttick = UINT64_MAX;
            }
        }
        /*
         * Note that the desired next expiry time might be beyond the
         * signed-64-bit range of a QEMUTimer -- in this case we just
         * set the timer for as far in the future as possible. When the
         * timer expires we will reset the timer for any remaining period.
         */
        if (nexttick > INT64_MAX / gt_cntfrq_period_ns(cpu)) {
            timer_mod_ns(cpu->gt_timer[timeridx], INT64_MAX);
        } else {
            timer_mod(cpu->gt_timer[timeridx], nexttick);
        }
        trace_arm_gt_recalc(timeridx, nexttick);
    } else {
        /* Timer disabled: ISTATUS and timer output always clear */
        gt->ctl &= ~4;
        timer_del(cpu->gt_timer[timeridx]);
        trace_arm_gt_recalc_disabled(timeridx);
    }
    gt_update_irq(cpu, timeridx);
}

static void gt_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri,
                           int timeridx)
{
    ARMCPU *cpu = env_archcpu(env);

    timer_del(cpu->gt_timer[timeridx]);
}

static uint64_t gt_cnt_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint64_t offset = gt_direct_access_timer_offset(env, GTIMER_PHYS);
    return gt_get_countervalue(env) - offset;
}

static uint64_t gt_virt_cnt_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint64_t offset = gt_direct_access_timer_offset(env, GTIMER_VIRT);
    return gt_get_countervalue(env) - offset;
}

static void gt_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          int timeridx,
                          uint64_t value)
{
    trace_arm_gt_cval_write(timeridx, value);
    env->cp15.c14_timer[timeridx].cval = value;
    gt_recalc_timer(env_archcpu(env), timeridx);
}

static uint64_t do_tval_read(CPUARMState *env, int timeridx, uint64_t offset)
{
    return (uint32_t)(env->cp15.c14_timer[timeridx].cval -
                      (gt_get_countervalue(env) - offset));
}

static uint64_t gt_tval_read(CPUARMState *env, const ARMCPRegInfo *ri,
                             int timeridx)
{
    uint64_t offset = gt_direct_access_timer_offset(env, timeridx);

    return do_tval_read(env, timeridx, offset);
}

static void do_tval_write(CPUARMState *env, int timeridx, uint64_t value,
                          uint64_t offset)
{
    trace_arm_gt_tval_write(timeridx, value);
    env->cp15.c14_timer[timeridx].cval = gt_get_countervalue(env) - offset +
                                         sextract64(value, 0, 32);
    gt_recalc_timer(env_archcpu(env), timeridx);
}

static void gt_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          int timeridx,
                          uint64_t value)
{
    uint64_t offset = gt_direct_access_timer_offset(env, timeridx);

    do_tval_write(env, timeridx, value, offset);
}

static void gt_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         int timeridx,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint32_t oldval = env->cp15.c14_timer[timeridx].ctl;

    trace_arm_gt_ctl_write(timeridx, value);
    env->cp15.c14_timer[timeridx].ctl = deposit64(oldval, 0, 2, value);
    if ((oldval ^ value) & 1) {
        /* Enable toggled */
        gt_recalc_timer(cpu, timeridx);
    } else if ((oldval ^ value) & 2) {
        /*
         * IMASK toggled: don't need to recalculate,
         * just set the interrupt line based on ISTATUS
         */
        trace_arm_gt_imask_toggle(timeridx);
        gt_update_irq(cpu, timeridx);
    }
}

static void gt_phys_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_PHYS);
}

static void gt_phys_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_PHYS, value);
}

static uint64_t gt_phys_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_PHYS);
}

static void gt_phys_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_PHYS, value);
}

static void gt_phys_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_PHYS, value);
}

static int gt_phys_redir_timeridx(CPUARMState *env)
{
    switch (arm_mmu_idx(env)) {
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
        return GTIMER_HYP;
    default:
        return GTIMER_PHYS;
    }
}

static int gt_virt_redir_timeridx(CPUARMState *env)
{
    switch (arm_mmu_idx(env)) {
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
        return GTIMER_HYPVIRT;
    default:
        return GTIMER_VIRT;
    }
}

static uint64_t gt_phys_redir_cval_read(CPUARMState *env,
                                        const ARMCPRegInfo *ri)
{
    int timeridx = gt_phys_redir_timeridx(env);
    return env->cp15.c14_timer[timeridx].cval;
}

static void gt_phys_redir_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    int timeridx = gt_phys_redir_timeridx(env);
    gt_cval_write(env, ri, timeridx, value);
}

static uint64_t gt_phys_redir_tval_read(CPUARMState *env,
                                        const ARMCPRegInfo *ri)
{
    int timeridx = gt_phys_redir_timeridx(env);
    return gt_tval_read(env, ri, timeridx);
}

static void gt_phys_redir_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    int timeridx = gt_phys_redir_timeridx(env);
    gt_tval_write(env, ri, timeridx, value);
}

static uint64_t gt_phys_redir_ctl_read(CPUARMState *env,
                                       const ARMCPRegInfo *ri)
{
    int timeridx = gt_phys_redir_timeridx(env);
    return env->cp15.c14_timer[timeridx].ctl;
}

static void gt_phys_redir_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    int timeridx = gt_phys_redir_timeridx(env);
    gt_ctl_write(env, ri, timeridx, value);
}

static void gt_virt_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_VIRT);
}

static void gt_virt_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_VIRT, value);
}

static uint64_t gt_virt_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /*
     * This is CNTV_TVAL_EL02; unlike the underlying CNTV_TVAL_EL0
     * we always apply CNTVOFF_EL2. Special case that here rather
     * than going into the generic gt_tval_read() and then having
     * to re-detect that it's this register.
     * Note that the accessfn/perms mean we know we're at EL2 or EL3 here.
     */
    return do_tval_read(env, GTIMER_VIRT, env->cp15.cntvoff_el2);
}

static void gt_virt_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    /* Similarly for writes to CNTV_TVAL_EL02 */
    do_tval_write(env, GTIMER_VIRT, value, env->cp15.cntvoff_el2);
}

static void gt_virt_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_VIRT, value);
}

static void gt_cnthctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint32_t oldval = env->cp15.cnthctl_el2;
    uint32_t valid_mask =
        R_CNTHCTL_EL0PCTEN_E2H1_MASK |
        R_CNTHCTL_EL0VCTEN_E2H1_MASK |
        R_CNTHCTL_EVNTEN_MASK |
        R_CNTHCTL_EVNTDIR_MASK |
        R_CNTHCTL_EVNTI_MASK |
        R_CNTHCTL_EL0VTEN_MASK |
        R_CNTHCTL_EL0PTEN_MASK |
        R_CNTHCTL_EL1PCTEN_E2H1_MASK |
        R_CNTHCTL_EL1PTEN_MASK;

    if (cpu_isar_feature(aa64_rme, cpu)) {
        valid_mask |= R_CNTHCTL_CNTVMASK_MASK | R_CNTHCTL_CNTPMASK_MASK;
    }
    if (cpu_isar_feature(aa64_ecv_traps, cpu)) {
        valid_mask |=
            R_CNTHCTL_EL1TVT_MASK |
            R_CNTHCTL_EL1TVCT_MASK |
            R_CNTHCTL_EL1NVPCT_MASK |
            R_CNTHCTL_EL1NVVCT_MASK |
            R_CNTHCTL_EVNTIS_MASK;
    }
    if (cpu_isar_feature(aa64_ecv, cpu)) {
        valid_mask |= R_CNTHCTL_ECV_MASK;
    }

    /* Clear RES0 bits */
    value &= valid_mask;

    raw_write(env, ri, value);

    if ((oldval ^ value) & R_CNTHCTL_CNTVMASK_MASK) {
        gt_update_irq(cpu, GTIMER_VIRT);
    } else if ((oldval ^ value) & R_CNTHCTL_CNTPMASK_MASK) {
        gt_update_irq(cpu, GTIMER_PHYS);
    }
}

static void gt_cntvoff_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    trace_arm_gt_cntvoff_write(value);
    raw_write(env, ri, value);
    gt_recalc_timer(cpu, GTIMER_VIRT);
}

static uint64_t gt_virt_redir_cval_read(CPUARMState *env,
                                        const ARMCPRegInfo *ri)
{
    int timeridx = gt_virt_redir_timeridx(env);
    return env->cp15.c14_timer[timeridx].cval;
}

static void gt_virt_redir_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    int timeridx = gt_virt_redir_timeridx(env);
    gt_cval_write(env, ri, timeridx, value);
}

static uint64_t gt_virt_redir_tval_read(CPUARMState *env,
                                        const ARMCPRegInfo *ri)
{
    int timeridx = gt_virt_redir_timeridx(env);
    return gt_tval_read(env, ri, timeridx);
}

static void gt_virt_redir_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    int timeridx = gt_virt_redir_timeridx(env);
    gt_tval_write(env, ri, timeridx, value);
}

static uint64_t gt_virt_redir_ctl_read(CPUARMState *env,
                                       const ARMCPRegInfo *ri)
{
    int timeridx = gt_virt_redir_timeridx(env);
    return env->cp15.c14_timer[timeridx].ctl;
}

static void gt_virt_redir_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    int timeridx = gt_virt_redir_timeridx(env);
    gt_ctl_write(env, ri, timeridx, value);
}

static void gt_hyp_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_HYP);
}

static void gt_hyp_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_HYP, value);
}

static uint64_t gt_hyp_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_HYP);
}

static void gt_hyp_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_HYP, value);
}

static void gt_hyp_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_HYP, value);
}

static void gt_sec_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_SEC);
}

static void gt_sec_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_SEC, value);
}

static uint64_t gt_sec_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_SEC);
}

static void gt_sec_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_SEC, value);
}

static void gt_sec_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_SEC, value);
}

static void gt_sec_pel2_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_S_EL2_PHYS);
}

static void gt_sec_pel2_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_S_EL2_PHYS, value);
}

static uint64_t gt_sec_pel2_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_S_EL2_PHYS);
}

static void gt_sec_pel2_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_S_EL2_PHYS, value);
}

static void gt_sec_pel2_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_S_EL2_PHYS, value);
}

static void gt_sec_vel2_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_S_EL2_VIRT);
}

static void gt_sec_vel2_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_S_EL2_VIRT, value);
}

static uint64_t gt_sec_vel2_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_S_EL2_VIRT);
}

static void gt_sec_vel2_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_S_EL2_VIRT, value);
}

static void gt_sec_vel2_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_S_EL2_VIRT, value);
}

static void gt_hv_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_HYPVIRT);
}

static void gt_hv_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_HYPVIRT, value);
}

static uint64_t gt_hv_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_HYPVIRT);
}

static void gt_hv_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_HYPVIRT, value);
}

static void gt_hv_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_HYPVIRT, value);
}

void arm_gt_ptimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_PHYS);
}

void arm_gt_vtimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_VIRT);
}

void arm_gt_htimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_HYP);
}

void arm_gt_stimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_SEC);
}

void arm_gt_sel2timer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_S_EL2_PHYS);
}

void arm_gt_sel2vtimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_S_EL2_VIRT);
}

void arm_gt_hvtimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_HYPVIRT);
}

static const ARMCPRegInfo generic_timer_cp_reginfo[] = {
    /*
     * Note that CNTFRQ is purely reads-as-written for the benefit
     * of software; writing it doesn't actually change the timer frequency.
     * Our reset value matches the fixed frequency we implement the timer at.
     */
    { .name = "CNTFRQ", .cp = 15, .crn = 14, .crm = 0, .opc1 = 0, .opc2 = 0,
      .type = ARM_CP_ALIAS,
      .access = PL1_RW | PL0_R, .accessfn = gt_cntfrq_access,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c14_cntfrq),
    },
    { .name = "CNTFRQ_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 0,
      .access = PL1_RW | PL0_R, .accessfn = gt_cntfrq_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_cntfrq),
      .resetfn = arm_gt_cntfrq_reset,
    },
    /* overall control: mostly access permissions */
    { .name = "CNTKCTL_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 14, .crm = 1, .opc2 = 0,
      .access = PL1_RW,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 14, 1, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 14, 1, 0),
      .fieldoffset = offsetof(CPUARMState, cp15.c14_cntkctl),
      .resetvalue = 0,
    },
    /* per-timer control */
    { .name = "CNTP_CTL", .cp = 15, .crn = 14, .crm = 2, .opc1 = 0, .opc2 = 1,
      .secure = ARM_CP_SECSTATE_NS,
      .type = ARM_CP_IO | ARM_CP_ALIAS, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .fieldoffset = offsetoflow32(CPUARMState,
                                   cp15.c14_timer[GTIMER_PHYS].ctl),
      .readfn = gt_phys_redir_ctl_read, .raw_readfn = raw_read,
      .writefn = gt_phys_redir_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTP_CTL_S",
      .cp = 15, .crn = 14, .crm = 2, .opc1 = 0, .opc2 = 1,
      .secure = ARM_CP_SECSTATE_S,
      .type = ARM_CP_IO | ARM_CP_ALIAS, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .fieldoffset = offsetoflow32(CPUARMState,
                                   cp15.c14_timer[GTIMER_SEC].ctl),
      .writefn = gt_sec_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTP_CTL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 2, .opc2 = 1,
      .type = ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .nv2_redirect_offset = 0x180 | NV2_REDIR_NV1,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].ctl),
      .resetvalue = 0,
      .readfn = gt_phys_redir_ctl_read, .raw_readfn = raw_read,
      .writefn = gt_phys_redir_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTV_CTL", .cp = 15, .crn = 14, .crm = 3, .opc1 = 0, .opc2 = 1,
      .type = ARM_CP_IO | ARM_CP_ALIAS, .access = PL0_RW,
      .accessfn = gt_vtimer_access,
      .fieldoffset = offsetoflow32(CPUARMState,
                                   cp15.c14_timer[GTIMER_VIRT].ctl),
      .readfn = gt_virt_redir_ctl_read, .raw_readfn = raw_read,
      .writefn = gt_virt_redir_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTV_CTL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 3, .opc2 = 1,
      .type = ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_vtimer_access,
      .nv2_redirect_offset = 0x170 | NV2_REDIR_NV1,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].ctl),
      .resetvalue = 0,
      .readfn = gt_virt_redir_ctl_read, .raw_readfn = raw_read,
      .writefn = gt_virt_redir_ctl_write, .raw_writefn = raw_write,
    },
    /* TimerValue views: a 32 bit downcounting view of the underlying state */
    { .name = "CNTP_TVAL", .cp = 15, .crn = 14, .crm = 2, .opc1 = 0, .opc2 = 0,
      .secure = ARM_CP_SECSTATE_NS,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .readfn = gt_phys_redir_tval_read, .writefn = gt_phys_redir_tval_write,
    },
    { .name = "CNTP_TVAL_S",
      .cp = 15, .crn = 14, .crm = 2, .opc1 = 0, .opc2 = 0,
      .secure = ARM_CP_SECSTATE_S,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .readfn = gt_sec_tval_read, .writefn = gt_sec_tval_write,
    },
    { .name = "CNTP_TVAL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_ptimer_access, .resetfn = gt_phys_timer_reset,
      .readfn = gt_phys_redir_tval_read, .writefn = gt_phys_redir_tval_write,
    },
    { .name = "CNTV_TVAL", .cp = 15, .crn = 14, .crm = 3, .opc1 = 0, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_vtimer_access,
      .readfn = gt_virt_redir_tval_read, .writefn = gt_virt_redir_tval_write,
    },
    { .name = "CNTV_TVAL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_vtimer_access, .resetfn = gt_virt_timer_reset,
      .readfn = gt_virt_redir_tval_read, .writefn = gt_virt_redir_tval_write,
    },
    /* The counter itself */
    { .name = "CNTPCT", .cp = 15, .crm = 14, .opc1 = 0,
      .access = PL0_R, .type = ARM_CP_64BIT | ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_pct_access,
      .readfn = gt_cnt_read, .resetfn = arm_cp_reset_ignore,
    },
    { .name = "CNTPCT_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 1,
      .access = PL0_R, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_pct_access, .readfn = gt_cnt_read,
    },
    { .name = "CNTVCT", .cp = 15, .crm = 14, .opc1 = 1,
      .access = PL0_R, .type = ARM_CP_64BIT | ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_vct_access,
      .readfn = gt_virt_cnt_read, .resetfn = arm_cp_reset_ignore,
    },
    { .name = "CNTVCT_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 2,
      .access = PL0_R, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_vct_access, .readfn = gt_virt_cnt_read,
    },
    /* Comparison value, indicating when the timer goes off */
    { .name = "CNTP_CVAL", .cp = 15, .crm = 14, .opc1 = 2,
      .secure = ARM_CP_SECSTATE_NS,
      .access = PL0_RW,
      .type = ARM_CP_64BIT | ARM_CP_IO | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].cval),
      .accessfn = gt_ptimer_access,
      .readfn = gt_phys_redir_cval_read, .raw_readfn = raw_read,
      .writefn = gt_phys_redir_cval_write, .raw_writefn = raw_write,
    },
    { .name = "CNTP_CVAL_S", .cp = 15, .crm = 14, .opc1 = 2,
      .secure = ARM_CP_SECSTATE_S,
      .access = PL0_RW,
      .type = ARM_CP_64BIT | ARM_CP_IO | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_SEC].cval),
      .accessfn = gt_ptimer_access,
      .writefn = gt_sec_cval_write, .raw_writefn = raw_write,
    },
    { .name = "CNTP_CVAL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 2, .opc2 = 2,
      .access = PL0_RW,
      .type = ARM_CP_IO,
      .nv2_redirect_offset = 0x178 | NV2_REDIR_NV1,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].cval),
      .resetvalue = 0, .accessfn = gt_ptimer_access,
      .readfn = gt_phys_redir_cval_read, .raw_readfn = raw_read,
      .writefn = gt_phys_redir_cval_write, .raw_writefn = raw_write,
    },
    { .name = "CNTV_CVAL", .cp = 15, .crm = 14, .opc1 = 3,
      .access = PL0_RW,
      .type = ARM_CP_64BIT | ARM_CP_IO | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].cval),
      .accessfn = gt_vtimer_access,
      .readfn = gt_virt_redir_cval_read, .raw_readfn = raw_read,
      .writefn = gt_virt_redir_cval_write, .raw_writefn = raw_write,
    },
    { .name = "CNTV_CVAL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 3, .opc2 = 2,
      .access = PL0_RW,
      .type = ARM_CP_IO,
      .nv2_redirect_offset = 0x168 | NV2_REDIR_NV1,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].cval),
      .resetvalue = 0, .accessfn = gt_vtimer_access,
      .readfn = gt_virt_redir_cval_read, .raw_readfn = raw_read,
      .writefn = gt_virt_redir_cval_write, .raw_writefn = raw_write,
    },
    /*
     * Secure timer -- this is actually restricted to only EL3
     * and configurably Secure-EL1 via the accessfn.
     */
    { .name = "CNTPS_TVAL_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 7, .crn = 14, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL1_RW,
      .accessfn = gt_stimer_access,
      .readfn = gt_sec_tval_read,
      .writefn = gt_sec_tval_write,
      .resetfn = gt_sec_timer_reset,
    },
    { .name = "CNTPS_CTL_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 7, .crn = 14, .crm = 2, .opc2 = 1,
      .type = ARM_CP_IO, .access = PL1_RW,
      .accessfn = gt_stimer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_SEC].ctl),
      .resetvalue = 0,
      .writefn = gt_sec_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTPS_CVAL_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 7, .crn = 14, .crm = 2, .opc2 = 2,
      .type = ARM_CP_IO, .access = PL1_RW,
      .accessfn = gt_stimer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_SEC].cval),
      .writefn = gt_sec_cval_write, .raw_writefn = raw_write,
    },
};

/*
 * FEAT_ECV adds extra views of CNTVCT_EL0 and CNTPCT_EL0 which
 * are "self-synchronizing". For QEMU all sysregs are self-synchronizing,
 * so our implementations here are identical to the normal registers.
 */
static const ARMCPRegInfo gen_timer_ecv_cp_reginfo[] = {
    { .name = "CNTVCTSS", .cp = 15, .crm = 14, .opc1 = 9,
      .access = PL0_R, .type = ARM_CP_64BIT | ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_vct_access,
      .readfn = gt_virt_cnt_read, .resetfn = arm_cp_reset_ignore,
    },
    { .name = "CNTVCTSS_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 6,
      .access = PL0_R, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_vct_access, .readfn = gt_virt_cnt_read,
    },
    { .name = "CNTPCTSS", .cp = 15, .crm = 14, .opc1 = 8,
      .access = PL0_R, .type = ARM_CP_64BIT | ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_pct_access,
      .readfn = gt_cnt_read, .resetfn = arm_cp_reset_ignore,
    },
    { .name = "CNTPCTSS_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 5,
      .access = PL0_R, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_pct_access, .readfn = gt_cnt_read,
    },
};

static CPAccessResult gt_cntpoff_access(CPUARMState *env,
                                        const ARMCPRegInfo *ri,
                                        bool isread)
{
    if (arm_current_el(env) == 2 && arm_feature(env, ARM_FEATURE_EL3) &&
        !(env->cp15.scr_el3 & SCR_ECVEN)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static void gt_cntpoff_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    trace_arm_gt_cntpoff_write(value);
    raw_write(env, ri, value);
    gt_recalc_timer(cpu, GTIMER_PHYS);
}

static const ARMCPRegInfo gen_timer_cntpoff_reginfo = {
    .name = "CNTPOFF_EL2", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 0, .opc2 = 6,
    .access = PL2_RW, .type = ARM_CP_IO, .resetvalue = 0,
    .accessfn = gt_cntpoff_access, .writefn = gt_cntpoff_write,
    .nv2_redirect_offset = 0x1a8,
    .fieldoffset = offsetof(CPUARMState, cp15.cntpoff_el2),
};
#else

/*
 * In user-mode most of the generic timer registers are inaccessible
 * however modern kernels (4.12+) allow access to cntvct_el0
 */

static uint64_t gt_virt_cnt_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);

    /*
     * Currently we have no support for QEMUTimer in linux-user so we
     * can't call gt_get_countervalue(env), instead we directly
     * call the lower level functions.
     */
    return cpu_get_clock() / gt_cntfrq_period_ns(cpu);
}

static const ARMCPRegInfo generic_timer_cp_reginfo[] = {
    { .name = "CNTFRQ_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 0,
      .access = PL0_R /* no PL1_RW in linux-user */,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_cntfrq),
      .resetfn = arm_gt_cntfrq_reset,
    },
    { .name = "CNTVCT_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 2,
      .access = PL0_R, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .readfn = gt_virt_cnt_read,
    },
};

/*
 * CNTVCTSS_EL0 has the same trap conditions as CNTVCT_EL0, so it also
 * is exposed to userspace by Linux.
 */
static const ARMCPRegInfo gen_timer_ecv_cp_reginfo[] = {
    { .name = "CNTVCTSS_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 6,
      .access = PL0_R, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .readfn = gt_virt_cnt_read,
    },
};

#endif

static void par_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    if (arm_feature(env, ARM_FEATURE_LPAE)) {
        raw_write(env, ri, value);
    } else if (arm_feature(env, ARM_FEATURE_V7)) {
        raw_write(env, ri, value & 0xfffff6ff);
    } else {
        raw_write(env, ri, value & 0xfffff1ff);
    }
}

/* Return basic MPU access permission bits.  */
static uint32_t simple_mpu_ap_bits(uint32_t val)
{
    uint32_t ret;
    uint32_t mask;
    int i;
    ret = 0;
    mask = 3;
    for (i = 0; i < 16; i += 2) {
        ret |= (val >> i) & mask;
        mask <<= 2;
    }
    return ret;
}

/* Pad basic MPU access permission bits to extended format.  */
static uint32_t extended_mpu_ap_bits(uint32_t val)
{
    uint32_t ret;
    uint32_t mask;
    int i;
    ret = 0;
    mask = 3;
    for (i = 0; i < 16; i += 2) {
        ret |= (val & mask) << i;
        mask <<= 2;
    }
    return ret;
}

static void pmsav5_data_ap_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    env->cp15.pmsav5_data_ap = extended_mpu_ap_bits(value);
}

static uint64_t pmsav5_data_ap_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return simple_mpu_ap_bits(env->cp15.pmsav5_data_ap);
}

static void pmsav5_insn_ap_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    env->cp15.pmsav5_insn_ap = extended_mpu_ap_bits(value);
}

static uint64_t pmsav5_insn_ap_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return simple_mpu_ap_bits(env->cp15.pmsav5_insn_ap);
}

static uint64_t pmsav7_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint32_t *u32p = *(uint32_t **)raw_ptr(env, ri);

    if (!u32p) {
        return 0;
    }

    u32p += env->pmsav7.rnr[M_REG_NS];
    return *u32p;
}

static void pmsav7_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint32_t *u32p = *(uint32_t **)raw_ptr(env, ri);

    if (!u32p) {
        return;
    }

    u32p += env->pmsav7.rnr[M_REG_NS];
    tlb_flush(CPU(cpu)); /* Mappings may have changed - purge! */
    *u32p = value;
}

static void pmsav7_rgnr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint32_t nrgs = cpu->pmsav7_dregion;

    if (value >= nrgs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PMSAv7 RGNR write >= # supported regions, %" PRIu32
                      " > %" PRIu32 "\n", (uint32_t)value, nrgs);
        return;
    }

    raw_write(env, ri, value);
}

static void prbar_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    tlb_flush(CPU(cpu)); /* Mappings may have changed - purge! */
    env->pmsav8.rbar[M_REG_NS][env->pmsav7.rnr[M_REG_NS]] = value;
}

static uint64_t prbar_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pmsav8.rbar[M_REG_NS][env->pmsav7.rnr[M_REG_NS]];
}

static void prlar_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    tlb_flush(CPU(cpu)); /* Mappings may have changed - purge! */
    env->pmsav8.rlar[M_REG_NS][env->pmsav7.rnr[M_REG_NS]] = value;
}

static uint64_t prlar_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pmsav8.rlar[M_REG_NS][env->pmsav7.rnr[M_REG_NS]];
}

static void prselr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    /*
     * Ignore writes that would select not implemented region.
     * This is architecturally UNPREDICTABLE.
     */
    if (value >= cpu->pmsav7_dregion) {
        return;
    }

    env->pmsav7.rnr[M_REG_NS] = value;
}

static void hprbar_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    tlb_flush(CPU(cpu)); /* Mappings may have changed - purge! */
    env->pmsav8.hprbar[env->pmsav8.hprselr] = value;
}

static uint64_t hprbar_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pmsav8.hprbar[env->pmsav8.hprselr];
}

static void hprlar_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    tlb_flush(CPU(cpu)); /* Mappings may have changed - purge! */
    env->pmsav8.hprlar[env->pmsav8.hprselr] = value;
}

static uint64_t hprlar_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pmsav8.hprlar[env->pmsav8.hprselr];
}

static void hprenr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    uint32_t n;
    uint32_t bit;
    ARMCPU *cpu = env_archcpu(env);

    /* Ignore writes to unimplemented regions */
    int rmax = MIN(cpu->pmsav8r_hdregion, 32);
    value &= MAKE_64BIT_MASK(0, rmax);

    tlb_flush(CPU(cpu)); /* Mappings may have changed - purge! */

    /* Register alias is only valid for first 32 indexes */
    for (n = 0; n < rmax; ++n) {
        bit = extract32(value, n, 1);
        env->pmsav8.hprlar[n] = deposit32(
                    env->pmsav8.hprlar[n], 0, 1, bit);
    }
}

static uint64_t hprenr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint32_t n;
    uint32_t result = 0x0;
    ARMCPU *cpu = env_archcpu(env);

    /* Register alias is only valid for first 32 indexes */
    for (n = 0; n < MIN(cpu->pmsav8r_hdregion, 32); ++n) {
        if (env->pmsav8.hprlar[n] & 0x1) {
            result |= (0x1 << n);
        }
    }
    return result;
}

static void hprselr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    /*
     * Ignore writes that would select not implemented region.
     * This is architecturally UNPREDICTABLE.
     */
    if (value >= cpu->pmsav8r_hdregion) {
        return;
    }

    env->pmsav8.hprselr = value;
}

static void pmsav8r_regn_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint8_t index = (extract32(ri->opc0, 0, 1) << 4) |
                    (extract32(ri->crm, 0, 3) << 1) | extract32(ri->opc2, 2, 1);

    tlb_flush(CPU(cpu)); /* Mappings may have changed - purge! */

    if (ri->opc1 & 4) {
        if (index >= cpu->pmsav8r_hdregion) {
            return;
        }
        if (ri->opc2 & 0x1) {
            env->pmsav8.hprlar[index] = value;
        } else {
            env->pmsav8.hprbar[index] = value;
        }
    } else {
        if (index >= cpu->pmsav7_dregion) {
            return;
        }
        if (ri->opc2 & 0x1) {
            env->pmsav8.rlar[M_REG_NS][index] = value;
        } else {
            env->pmsav8.rbar[M_REG_NS][index] = value;
        }
    }
}

static uint64_t pmsav8r_regn_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);
    uint8_t index = (extract32(ri->opc0, 0, 1) << 4) |
                    (extract32(ri->crm, 0, 3) << 1) | extract32(ri->opc2, 2, 1);

    if (ri->opc1 & 4) {
        if (index >= cpu->pmsav8r_hdregion) {
            return 0x0;
        }
        if (ri->opc2 & 0x1) {
            return env->pmsav8.hprlar[index];
        } else {
            return env->pmsav8.hprbar[index];
        }
    } else {
        if (index >= cpu->pmsav7_dregion) {
            return 0x0;
        }
        if (ri->opc2 & 0x1) {
            return env->pmsav8.rlar[M_REG_NS][index];
        } else {
            return env->pmsav8.rbar[M_REG_NS][index];
        }
    }
}

static const ARMCPRegInfo pmsav8r_cp_reginfo[] = {
    { .name = "PRBAR",
      .cp = 15, .opc1 = 0, .crn = 6, .crm = 3, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_NO_RAW,
      .accessfn = access_tvm_trvm,
      .readfn = prbar_read, .writefn = prbar_write },
    { .name = "PRLAR",
      .cp = 15, .opc1 = 0, .crn = 6, .crm = 3, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_NO_RAW,
      .accessfn = access_tvm_trvm,
      .readfn = prlar_read, .writefn = prlar_write },
    { .name = "PRSELR", .resetvalue = 0,
      .cp = 15, .opc1 = 0, .crn = 6, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .writefn = prselr_write,
      .fieldoffset = offsetof(CPUARMState, pmsav7.rnr[M_REG_NS]) },
    { .name = "HPRBAR", .resetvalue = 0,
      .cp = 15, .opc1 = 4, .crn = 6, .crm = 3, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_NO_RAW,
      .readfn = hprbar_read, .writefn = hprbar_write },
    { .name = "HPRLAR",
      .cp = 15, .opc1 = 4, .crn = 6, .crm = 3, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_NO_RAW,
      .readfn = hprlar_read, .writefn = hprlar_write },
    { .name = "HPRSELR", .resetvalue = 0,
      .cp = 15, .opc1 = 4, .crn = 6, .crm = 2, .opc2 = 1,
      .access = PL2_RW,
      .writefn = hprselr_write,
      .fieldoffset = offsetof(CPUARMState, pmsav8.hprselr) },
    { .name = "HPRENR",
      .cp = 15, .opc1 = 4, .crn = 6, .crm = 1, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_NO_RAW,
      .readfn = hprenr_read, .writefn = hprenr_write },
};

static const ARMCPRegInfo pmsav7_cp_reginfo[] = {
    /*
     * Reset for all these registers is handled in arm_cpu_reset(),
     * because the PMSAv7 is also used by M-profile CPUs, which do
     * not register cpregs but still need the state to be reset.
     */
    { .name = "DRBAR", .cp = 15, .crn = 6, .opc1 = 0, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_NO_RAW,
      .fieldoffset = offsetof(CPUARMState, pmsav7.drbar),
      .readfn = pmsav7_read, .writefn = pmsav7_write,
      .resetfn = arm_cp_reset_ignore },
    { .name = "DRSR", .cp = 15, .crn = 6, .opc1 = 0, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_NO_RAW,
      .fieldoffset = offsetof(CPUARMState, pmsav7.drsr),
      .readfn = pmsav7_read, .writefn = pmsav7_write,
      .resetfn = arm_cp_reset_ignore },
    { .name = "DRACR", .cp = 15, .crn = 6, .opc1 = 0, .crm = 1, .opc2 = 4,
      .access = PL1_RW, .type = ARM_CP_NO_RAW,
      .fieldoffset = offsetof(CPUARMState, pmsav7.dracr),
      .readfn = pmsav7_read, .writefn = pmsav7_write,
      .resetfn = arm_cp_reset_ignore },
    { .name = "RGNR", .cp = 15, .crn = 6, .opc1 = 0, .crm = 2, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, pmsav7.rnr[M_REG_NS]),
      .writefn = pmsav7_rgnr_write,
      .resetfn = arm_cp_reset_ignore },
};

static const ARMCPRegInfo pmsav5_cp_reginfo[] = {
    { .name = "DATA_AP", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.pmsav5_data_ap),
      .readfn = pmsav5_data_ap_read, .writefn = pmsav5_data_ap_write, },
    { .name = "INSN_AP", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.pmsav5_insn_ap),
      .readfn = pmsav5_insn_ap_read, .writefn = pmsav5_insn_ap_write, },
    { .name = "DATA_EXT_AP", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.pmsav5_data_ap),
      .resetvalue = 0, },
    { .name = "INSN_EXT_AP", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 3,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.pmsav5_insn_ap),
      .resetvalue = 0, },
    { .name = "DCACHE_CFG", .cp = 15, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c2_data), .resetvalue = 0, },
    { .name = "ICACHE_CFG", .cp = 15, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c2_insn), .resetvalue = 0, },
    /* Protection region base and size registers */
    { .name = "946_PRBS0", .cp = 15, .crn = 6, .crm = 0, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[0]) },
    { .name = "946_PRBS1", .cp = 15, .crn = 6, .crm = 1, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[1]) },
    { .name = "946_PRBS2", .cp = 15, .crn = 6, .crm = 2, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[2]) },
    { .name = "946_PRBS3", .cp = 15, .crn = 6, .crm = 3, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[3]) },
    { .name = "946_PRBS4", .cp = 15, .crn = 6, .crm = 4, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[4]) },
    { .name = "946_PRBS5", .cp = 15, .crn = 6, .crm = 5, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[5]) },
    { .name = "946_PRBS6", .cp = 15, .crn = 6, .crm = 6, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[6]) },
    { .name = "946_PRBS7", .cp = 15, .crn = 6, .crm = 7, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[7]) },
};

static void vmsa_ttbcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    if (!arm_feature(env, ARM_FEATURE_V8)) {
        if (arm_feature(env, ARM_FEATURE_LPAE) && (value & TTBCR_EAE)) {
            /*
             * Pre ARMv8 bits [21:19], [15:14] and [6:3] are UNK/SBZP when
             * using Long-descriptor translation table format
             */
            value &= ~((7 << 19) | (3 << 14) | (0xf << 3));
        } else if (arm_feature(env, ARM_FEATURE_EL3)) {
            /*
             * In an implementation that includes the Security Extensions
             * TTBCR has additional fields PD0 [4] and PD1 [5] for
             * Short-descriptor translation table format.
             */
            value &= TTBCR_PD1 | TTBCR_PD0 | TTBCR_N;
        } else {
            value &= TTBCR_N;
        }
    }

    if (arm_feature(env, ARM_FEATURE_LPAE)) {
        /*
         * With LPAE the TTBCR could result in a change of ASID
         * via the TTBCR.A1 bit, so do a TLB flush.
         */
        tlb_flush(CPU(cpu));
    }
    raw_write(env, ri, value);
}

static void vmsa_tcr_el12_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    /* For AArch64 the A1 bit could result in a change of ASID, so TLB flush. */
    tlb_flush(CPU(cpu));
    raw_write(env, ri, value);
}

static void vmsa_ttbr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    /* If the ASID changes (with a 64-bit write), we must flush the TLB.  */
    if (cpreg_field_type(ri) == MO_64 &&
        extract64(raw_read(env, ri) ^ value, 48, 16) != 0) {
        ARMCPU *cpu = env_archcpu(env);
        tlb_flush(CPU(cpu));
    }
    raw_write(env, ri, value);
}

static void vmsa_tcr_ttbr_el2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    /*
     * If we are running with E2&0 regime, then an ASID is active.
     * Flush if that might be changing.  Note we're not checking
     * TCR_EL2.A1 to know if this is really the TTBRx_EL2 that
     * holds the active ASID, only checking the field that might.
     */
    if (extract64(raw_read(env, ri) ^ value, 48, 16) &&
        (arm_hcr_el2_eff(env) & HCR_E2H)) {
        uint16_t mask = ARMMMUIdxBit_E20_2 |
                        ARMMMUIdxBit_E20_2_PAN |
                        ARMMMUIdxBit_E20_2_GCS |
                        ARMMMUIdxBit_E20_0 |
                        ARMMMUIdxBit_E20_0_GCS;
        tlb_flush_by_mmuidx(env_cpu(env), mask);
    }
    raw_write(env, ri, value);
}

static void vttbr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = CPU(cpu);

    /*
     * A change in VMID to the stage2 page table (Stage2) invalidates
     * the stage2 and combined stage 1&2 tlbs (EL10_1 and EL10_0).
     */
    if (extract64(raw_read(env, ri) ^ value, 48, 16) != 0) {
        tlb_flush_by_mmuidx(cs, alle1_tlbmask(env));
    }
    raw_write(env, ri, value);
}

static const ARMCPRegInfo vmsa_pmsa_cp_reginfo[] = {
    { .name = "DFSR", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .type = ARM_CP_ALIAS,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.dfsr_s),
                             offsetoflow32(CPUARMState, cp15.dfsr_ns) }, },
    { .name = "IFSR", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .resetvalue = 0,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.ifsr_s),
                             offsetoflow32(CPUARMState, cp15.ifsr_ns) } },
    { .name = "DFAR", .cp = 15, .opc1 = 0, .crn = 6, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .resetvalue = 0,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.dfar_s),
                             offsetof(CPUARMState, cp15.dfar_ns) } },
    { .name = "FAR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .crn = 6, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_FAR_EL1,
      .nv2_redirect_offset = 0x220 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 6, 0, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 6, 0, 0),
      .fieldoffset = offsetof(CPUARMState, cp15.far_el[1]),
      .resetvalue = 0, },
};

static const ARMCPRegInfo vmsa_cp_reginfo[] = {
    { .name = "ESR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .crn = 5, .crm = 2, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_ESR_EL1,
      .nv2_redirect_offset = 0x138 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 5, 2, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 5, 2, 0),
      .fieldoffset = offsetof(CPUARMState, cp15.esr_el[1]), .resetvalue = 0, },
    { .name = "TTBR0_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_TTBR0_EL1,
      .nv2_redirect_offset = 0x200 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 2, 0, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 2, 0, 0),
      .writefn = vmsa_ttbr_write, .resetvalue = 0, .raw_writefn = raw_write,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ttbr0_s),
                             offsetof(CPUARMState, cp15.ttbr0_ns) } },
    { .name = "TTBR1_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_TTBR1_EL1,
      .nv2_redirect_offset = 0x210 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 2, 0, 1),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 2, 0, 1),
      .writefn = vmsa_ttbr_write, .resetvalue = 0, .raw_writefn = raw_write,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ttbr1_s),
                             offsetof(CPUARMState, cp15.ttbr1_ns) } },
    { .name = "TCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_TCR_EL1,
      .nv2_redirect_offset = 0x120 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 2, 0, 2),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 2, 0, 2),
      .writefn = vmsa_tcr_el12_write,
      .raw_writefn = raw_write,
      .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.tcr_el[1]) },
    { .name = "TTBCR", .cp = 15, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_ALIAS, .writefn = vmsa_ttbcr_write,
      .raw_writefn = raw_write,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.tcr_el[3]),
                             offsetoflow32(CPUARMState, cp15.tcr_el[1])} },
};

/*
 * Note that unlike TTBCR, writing to TTBCR2 does not require flushing
 * qemu tlbs nor adjusting cached masks.
 */
static const ARMCPRegInfo ttbcr2_reginfo = {
    .name = "TTBCR2", .cp = 15, .opc1 = 0, .crn = 2, .crm = 0, .opc2 = 3,
    .access = PL1_RW, .accessfn = access_tvm_trvm,
    .type = ARM_CP_ALIAS,
    .bank_fieldoffsets = {
        offsetofhigh32(CPUARMState, cp15.tcr_el[3]),
        offsetofhigh32(CPUARMState, cp15.tcr_el[1]),
    },
};

static void omap_ticonfig_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    env->cp15.c15_ticonfig = value & 0xe7;
    /* The OS_TYPE bit in this register changes the reported CPUID! */
    env->cp15.c0_cpuid = (value & (1 << 5)) ?
        ARM_CPUID_TI915T : ARM_CPUID_TI925T;
}

static void omap_threadid_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    env->cp15.c15_threadid = value & 0xffff;
}

static void omap_wfi_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
#ifdef CONFIG_USER_ONLY
    g_assert_not_reached();
#else
    /* Wait-for-interrupt (deprecated) */
    cpu_interrupt(env_cpu(env), CPU_INTERRUPT_HALT);
#endif
}

static void omap_cachemaint_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    /*
     * On OMAP there are registers indicating the max/min index of dcache lines
     * containing a dirty line; cache flush operations have to reset these.
     */
    env->cp15.c15_i_max = 0x000;
    env->cp15.c15_i_min = 0xff0;
}

static const ARMCPRegInfo omap_cp_reginfo[] = {
    { .name = "DFSR", .cp = 15, .crn = 5, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_OVERRIDE,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.esr_el[1]),
      .resetvalue = 0, },
    { .name = "", .cp = 15, .crn = 15, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "TICONFIG", .cp = 15, .crn = 15, .crm = 1, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_ticonfig), .resetvalue = 0,
      .writefn = omap_ticonfig_write },
    { .name = "IMAX", .cp = 15, .crn = 15, .crm = 2, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_i_max), .resetvalue = 0, },
    { .name = "IMIN", .cp = 15, .crn = 15, .crm = 3, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .resetvalue = 0xff0,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_i_min) },
    { .name = "THREADID", .cp = 15, .crn = 15, .crm = 4, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_threadid), .resetvalue = 0,
      .writefn = omap_threadid_write },
    { .name = "TI925T_STATUS", .cp = 15, .crn = 15,
      .crm = 8, .opc1 = 0, .opc2 = 0, .access = PL1_RW,
      .type = ARM_CP_NO_RAW,
      .readfn = arm_cp_read_zero, .writefn = omap_wfi_write, },
    /*
     * TODO: Peripheral port remap register:
     * On OMAP2 mcr p15, 0, rn, c15, c2, 4 sets up the interrupt controller
     * base address at $rn & ~0xfff and map size of 0x200 << ($rn & 0xfff),
     * when MMU is off.
     */
    { .name = "OMAP_CACHEMAINT", .cp = 15, .crn = 7, .crm = CP_ANY,
      .opc1 = 0, .opc2 = CP_ANY, .access = PL1_W,
      .type = ARM_CP_OVERRIDE | ARM_CP_NO_RAW,
      .writefn = omap_cachemaint_write },
    { .name = "C9", .cp = 15, .crn = 9,
      .crm = CP_ANY, .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW,
      .type = ARM_CP_CONST | ARM_CP_OVERRIDE, .resetvalue = 0 },
};

static const ARMCPRegInfo dummy_c15_cp_reginfo[] = {
    /*
     * RAZ/WI the whole crn=15 space, when we don't have a more specific
     * implementation of this implementation-defined space.
     * Ideally this should eventually disappear in favour of actually
     * implementing the correct behaviour for all cores.
     */
    { .name = "C15_IMPDEF", .cp = 15, .crn = 15,
      .crm = CP_ANY, .opc1 = CP_ANY, .opc2 = CP_ANY,
      .access = PL1_RW,
      .type = ARM_CP_CONST | ARM_CP_NO_RAW | ARM_CP_OVERRIDE,
      .resetvalue = 0 },
};

static const ARMCPRegInfo cache_dirty_status_cp_reginfo[] = {
    /* Cache status: RAZ because we have no cache so it's always clean */
    { .name = "CDSR", .cp = 15, .crn = 7, .crm = 10, .opc1 = 0, .opc2 = 6,
      .access = PL1_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = 0 },
};

static const ARMCPRegInfo cache_block_ops_cp_reginfo[] = {
    /* We never have a block transfer operation in progress */
    { .name = "BXSR", .cp = 15, .crn = 7, .crm = 12, .opc1 = 0, .opc2 = 4,
      .access = PL0_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = 0 },
    /* The cache ops themselves: these all NOP for QEMU */
    { .name = "IICR", .cp = 15, .crm = 5, .opc1 = 0,
      .access = PL1_W, .type = ARM_CP_NOP | ARM_CP_64BIT },
    { .name = "IDCR", .cp = 15, .crm = 6, .opc1 = 0,
      .access = PL1_W, .type = ARM_CP_NOP | ARM_CP_64BIT },
    { .name = "CDCR", .cp = 15, .crm = 12, .opc1 = 0,
      .access = PL0_W, .type = ARM_CP_NOP | ARM_CP_64BIT },
    { .name = "PIR", .cp = 15, .crm = 12, .opc1 = 1,
      .access = PL0_W, .type = ARM_CP_NOP | ARM_CP_64BIT },
    { .name = "PDR", .cp = 15, .crm = 12, .opc1 = 2,
      .access = PL0_W, .type = ARM_CP_NOP | ARM_CP_64BIT },
    { .name = "CIDCR", .cp = 15, .crm = 14, .opc1 = 0,
      .access = PL1_W, .type = ARM_CP_NOP | ARM_CP_64BIT },
};

static const ARMCPRegInfo cache_test_clean_cp_reginfo[] = {
    /*
     * The cache test-and-clean instructions always return (1 << 30)
     * to indicate that there are no dirty cache lines.
     */
    { .name = "TC_DCACHE", .cp = 15, .crn = 7, .crm = 10, .opc1 = 0, .opc2 = 3,
      .access = PL0_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = (1 << 30) },
    { .name = "TCI_DCACHE", .cp = 15, .crn = 7, .crm = 14, .opc1 = 0, .opc2 = 3,
      .access = PL0_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = (1 << 30) },
};

static const ARMCPRegInfo strongarm_cp_reginfo[] = {
    /* Ignore ReadBuffer accesses */
    { .name = "C9_READBUFFER", .cp = 15, .crn = 9,
      .crm = CP_ANY, .opc1 = CP_ANY, .opc2 = CP_ANY,
      .access = PL1_RW, .resetvalue = 0,
      .type = ARM_CP_CONST | ARM_CP_OVERRIDE | ARM_CP_NO_RAW },
};

static uint64_t midr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    unsigned int cur_el = arm_current_el(env);

    if (arm_is_el2_enabled(env) && cur_el == 1) {
        return env->cp15.vpidr_el2;
    }
    return raw_read(env, ri);
}

static uint64_t mpidr_read_val(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t mpidr = cpu->mp_affinity;

    if (arm_feature(env, ARM_FEATURE_V7MP)) {
        mpidr |= (1U << 31);
        /*
         * Cores which are uniprocessor (non-coherent)
         * but still implement the MP extensions set
         * bit 30. (For instance, Cortex-R5).
         */
        if (cpu->mp_is_up) {
            mpidr |= (1u << 30);
        }
    }
    return mpidr;
}

static uint64_t mpidr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    unsigned int cur_el = arm_current_el(env);

    if (arm_is_el2_enabled(env) && cur_el == 1) {
        return env->cp15.vmpidr_el2;
    }
    return mpidr_read_val(env);
}

static const ARMCPRegInfo lpae_cp_reginfo[] = {
    /* AMAIR0 is mapped to AMAIR_EL1[31:0] */
    { .name = "AMAIR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .crn = 10, .crm = 3, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fgt = FGT_AMAIR_EL1,
      .nv2_redirect_offset = 0x148 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 10, 3, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 10, 3, 0),
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /* AMAIR1 is mapped to AMAIR_EL1[63:32] */
    { .name = "AMAIR1", .cp = 15, .crn = 10, .crm = 3, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "PAR", .cp = 15, .crm = 7, .opc1 = 0,
      .access = PL1_RW, .type = ARM_CP_64BIT, .resetvalue = 0,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.par_s),
                             offsetof(CPUARMState, cp15.par_ns)} },
    { .name = "TTBR0", .cp = 15, .crm = 2, .opc1 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_64BIT | ARM_CP_ALIAS,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ttbr0_s),
                             offsetof(CPUARMState, cp15.ttbr0_ns) },
      .writefn = vmsa_ttbr_write, .raw_writefn = raw_write },
    { .name = "TTBR1", .cp = 15, .crm = 2, .opc1 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_64BIT | ARM_CP_ALIAS,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ttbr1_s),
                             offsetof(CPUARMState, cp15.ttbr1_ns) },
      .writefn = vmsa_ttbr_write, .raw_writefn = raw_write },
};

static uint64_t aa64_fpcr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return vfp_get_fpcr(env);
}

static void aa64_fpcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    vfp_set_fpcr(env, value);
}

static uint64_t aa64_fpsr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return vfp_get_fpsr(env);
}

static void aa64_fpsr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    vfp_set_fpsr(env, value);
}

static CPAccessResult aa64_daif_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if (arm_current_el(env) == 0 && !(arm_sctlr(env, 0) & SCTLR_UMA)) {
        return CP_ACCESS_TRAP_EL1;
    }
    return CP_ACCESS_OK;
}

static void aa64_daif_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    env->daif = value & PSTATE_DAIF;
}

static uint64_t aa64_pan_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_PAN;
}

static void aa64_pan_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    env->pstate = (env->pstate & ~PSTATE_PAN) | (value & PSTATE_PAN);
}

static const ARMCPRegInfo pan_reginfo = {
    .name = "PAN", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 2, .opc2 = 3,
    .type = ARM_CP_NO_RAW, .access = PL1_RW,
    .readfn = aa64_pan_read, .writefn = aa64_pan_write
};

static uint64_t aa64_uao_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_UAO;
}

static void aa64_uao_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    env->pstate = (env->pstate & ~PSTATE_UAO) | (value & PSTATE_UAO);
}

static const ARMCPRegInfo uao_reginfo = {
    .name = "UAO", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 2, .opc2 = 4,
    .type = ARM_CP_NO_RAW, .access = PL1_RW,
    .readfn = aa64_uao_read, .writefn = aa64_uao_write
};

static uint64_t aa64_dit_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_DIT;
}

static void aa64_dit_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    env->pstate = (env->pstate & ~PSTATE_DIT) | (value & PSTATE_DIT);
}

static const ARMCPRegInfo dit_reginfo = {
    .name = "DIT", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 3, .crn = 4, .crm = 2, .opc2 = 5,
    .type = ARM_CP_NO_RAW, .access = PL0_RW,
    .readfn = aa64_dit_read, .writefn = aa64_dit_write
};

static uint64_t aa64_ssbs_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_SSBS;
}

static void aa64_ssbs_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    env->pstate = (env->pstate & ~PSTATE_SSBS) | (value & PSTATE_SSBS);
}

static const ARMCPRegInfo ssbs_reginfo = {
    .name = "SSBS", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 3, .crn = 4, .crm = 2, .opc2 = 6,
    .type = ARM_CP_NO_RAW, .access = PL0_RW,
    .readfn = aa64_ssbs_read, .writefn = aa64_ssbs_write
};

static CPAccessResult aa64_cacheop_poc_access(CPUARMState *env,
                                              const ARMCPRegInfo *ri,
                                              bool isread)
{
    /* Cache invalidate/clean to Point of Coherency or Persistence...  */
    switch (arm_current_el(env)) {
    case 0:
        /* ... EL0 must trap to EL1 unless SCTLR_EL1.UCI is set.  */
        if (!(arm_sctlr(env, 0) & SCTLR_UCI)) {
            return CP_ACCESS_TRAP_EL1;
        }
        /* fall through */
    case 1:
        /* ... EL1 must trap to EL2 if HCR_EL2.TPCP is set.  */
        if (arm_hcr_el2_eff(env) & HCR_TPCP) {
            return CP_ACCESS_TRAP_EL2;
        }
        break;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult do_cacheop_pou_access(CPUARMState *env, uint64_t hcrflags)
{
    /* Cache invalidate/clean to Point of Unification... */
    switch (arm_current_el(env)) {
    case 0:
        /* ... EL0 must trap to EL1 unless SCTLR_EL1.UCI is set.  */
        if (!(arm_sctlr(env, 0) & SCTLR_UCI)) {
            return CP_ACCESS_TRAP_EL1;
        }
        /* fall through */
    case 1:
        /* ... EL1 must trap to EL2 if relevant HCR_EL2 flags are set.  */
        if (arm_hcr_el2_eff(env) & hcrflags) {
            return CP_ACCESS_TRAP_EL2;
        }
        break;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_ticab(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    return do_cacheop_pou_access(env, HCR_TICAB | HCR_TPU);
}

static CPAccessResult access_tocu(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    return do_cacheop_pou_access(env, HCR_TOCU | HCR_TPU);
}

static CPAccessResult aa64_zva_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    int cur_el = arm_current_el(env);

    if (cur_el < 2) {
        uint64_t hcr = arm_hcr_el2_eff(env);

        if (cur_el == 0) {
            if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
                if (!(env->cp15.sctlr_el[2] & SCTLR_DZE)) {
                    return CP_ACCESS_TRAP_EL2;
                }
            } else {
                if (!(env->cp15.sctlr_el[1] & SCTLR_DZE)) {
                    return CP_ACCESS_TRAP_EL1;
                }
                if (hcr & HCR_TDZ) {
                    return CP_ACCESS_TRAP_EL2;
                }
            }
        } else if (hcr & HCR_TDZ) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    return CP_ACCESS_OK;
}

static uint64_t aa64_dczid_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);
    int dzp_bit = 1 << 4;

    /* DZP indicates whether DC ZVA access is allowed */
    if (aa64_zva_access(env, NULL, false) == CP_ACCESS_OK) {
        dzp_bit = 0;
    }
    return cpu->dcz_blocksize | dzp_bit;
}

static CPAccessResult sp_el0_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                    bool isread)
{
    if (!(env->pstate & PSTATE_SP)) {
        /*
         * Access to SP_EL0 is undefined if it's being used as
         * the stack pointer.
         */
        return CP_ACCESS_UNDEFINED;
    }
    return CP_ACCESS_OK;
}

static uint64_t spsel_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_SP;
}

static void spsel_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t val)
{
    update_spsel(env, val);
}

static void sctlr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    if (arm_feature(env, ARM_FEATURE_PMSA) && !cpu->has_mpu) {
        /* M bit is RAZ/WI for PMSA with no MPU implemented */
        value &= ~SCTLR_M;
    }

    /* ??? Lots of these bits are not implemented.  */

    if (ri->state == ARM_CP_STATE_AA64 && !cpu_isar_feature(aa64_mte, cpu)) {
        if (ri->opc1 == 6) { /* SCTLR_EL3 */
            value &= ~(SCTLR_ITFSB | SCTLR_TCF | SCTLR_ATA);
        } else {
            value &= ~(SCTLR_ITFSB | SCTLR_TCF0 | SCTLR_TCF |
                       SCTLR_ATA0 | SCTLR_ATA);
        }
    }

    if (raw_read(env, ri) == value) {
        /*
         * Skip the TLB flush if nothing actually changed; Linux likes
         * to do a lot of pointless SCTLR writes.
         */
        return;
    }

    raw_write(env, ri, value);

    /* This may enable/disable the MMU, so do a TLB flush.  */
    tlb_flush(CPU(cpu));
}

static void mdcr_el3_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /*
     * Some MDCR_EL3 bits affect whether PMU counters are running:
     * if we are trying to change any of those then we must
     * bracket this update with PMU start/finish calls.
     */
    bool pmu_op = (env->cp15.mdcr_el3 ^ value) & MDCR_EL3_PMU_ENABLE_BITS;

    if (pmu_op) {
        pmu_op_start(env);
    }
    env->cp15.mdcr_el3 = value;
    if (pmu_op) {
        pmu_op_finish(env);
    }
}

static void sdcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    /* Not all bits defined for MDCR_EL3 exist in the AArch32 SDCR */
    mdcr_el3_write(env, ri, value & SDCR_VALID_MASK);
}

static void mdcr_el2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /*
     * Some MDCR_EL2 bits affect whether PMU counters are running:
     * if we are trying to change any of those then we must
     * bracket this update with PMU start/finish calls.
     */
    bool pmu_op = (env->cp15.mdcr_el2 ^ value) & MDCR_EL2_PMU_ENABLE_BITS;

    if (pmu_op) {
        pmu_op_start(env);
    }
    env->cp15.mdcr_el2 = value;
    if (pmu_op) {
        pmu_op_finish(env);
    }
}

static CPAccessResult access_nv1_with_nvx(uint64_t hcr_nv)
{
    return hcr_nv == (HCR_NV | HCR_NV1) ? CP_ACCESS_TRAP_EL2 : CP_ACCESS_OK;
}

static CPAccessResult access_nv1(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (arm_current_el(env) == 1) {
        return access_nv1_with_nvx(arm_hcr_el2_nvx_eff(env));
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_nv1_or_exlock_el1(CPUARMState *env,
                                               const ARMCPRegInfo *ri,
                                               bool isread)
{
    if (arm_current_el(env) == 1) {
        uint64_t nvx = arm_hcr_el2_nvx_eff(env);

        if (!isread &&
            (env->pstate & PSTATE_EXLOCK) &&
            (env->cp15.gcscr_el[1] & GCSCR_EXLOCKEN) &&
            !(nvx & HCR_NV1)) {
            return CP_ACCESS_EXLOCK;
        }
        return access_nv1_with_nvx(nvx);
    }

    /*
     * At EL2, since VHE redirection is done at translation time,
     * el_is_in_host is always false here, so EXLOCK does not apply.
     */
    return CP_ACCESS_OK;
}

static CPAccessResult access_exlock_el2(CPUARMState *env,
                                        const ARMCPRegInfo *ri, bool isread)
{
    int el = arm_current_el(env);

    if (el == 3) {
        return CP_ACCESS_OK;
    }

    /*
     * Access to the EL2 register from EL1 means NV is set, and
     * EXLOCK has priority over an NV1 trap to EL2.
     */
    if (!isread &&
        (env->pstate & PSTATE_EXLOCK) &&
        (env->cp15.gcscr_el[el] & GCSCR_EXLOCKEN)) {
        return CP_ACCESS_EXLOCK;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_exlock_el3(CPUARMState *env,
                                        const ARMCPRegInfo *ri, bool isread)
{
    if (!isread &&
        (env->pstate & PSTATE_EXLOCK) &&
        (env->cp15.gcscr_el[3] & GCSCR_EXLOCKEN)) {
        return CP_ACCESS_EXLOCK;
    }
    return CP_ACCESS_OK;
}

#ifdef CONFIG_USER_ONLY
/*
 * `IC IVAU` is handled to improve compatibility with JITs that dual-map their
 * code to get around W^X restrictions, where one region is writable and the
 * other is executable.
 *
 * Since the executable region is never written to we cannot detect code
 * changes when running in user mode, and rely on the emulated JIT telling us
 * that the code has changed by executing this instruction.
 */
static void ic_ivau_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    uint64_t icache_line_mask, start_address, end_address;
    const ARMCPU *cpu;

    cpu = env_archcpu(env);

    icache_line_mask = (4 << extract32(cpu->ctr, 0, 4)) - 1;
    start_address = value & ~icache_line_mask;
    end_address = value | icache_line_mask;

    mmap_lock();

    tb_invalidate_phys_range(env_cpu(env), start_address, end_address);

    mmap_unlock();
}
#endif

static const ARMCPRegInfo v8_cp_reginfo[] = {
    /*
     * Minimal set of EL0-visible registers. This will need to be expanded
     * significantly for system emulation of AArch64 CPUs.
     */
    { .name = "NZCV", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 0, .crn = 4, .crm = 2,
      .access = PL0_RW, .type = ARM_CP_NZCV },
    { .name = "DAIF", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 1, .crn = 4, .crm = 2,
      .type = ARM_CP_NO_RAW,
      .access = PL0_RW, .accessfn = aa64_daif_access,
      .fieldoffset = offsetof(CPUARMState, daif),
      .writefn = aa64_daif_write, .resetfn = arm_cp_reset_ignore },
    { .name = "FPCR", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 0, .crn = 4, .crm = 4,
      .access = PL0_RW, .type = ARM_CP_FPU,
      .readfn = aa64_fpcr_read, .writefn = aa64_fpcr_write },
    { .name = "FPSR", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 1, .crn = 4, .crm = 4,
      .access = PL0_RW, .type = ARM_CP_FPU | ARM_CP_SUPPRESS_TB_END,
      .readfn = aa64_fpsr_read, .writefn = aa64_fpsr_write },
    { .name = "DCZID_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 7, .crn = 0, .crm = 0,
      .access = PL0_R, .type = ARM_CP_NO_RAW,
      .fgt = FGT_DCZID_EL0,
      .readfn = aa64_dczid_read },
    { .name = "DC_ZVA", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 4, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_DC_ZVA,
#ifndef CONFIG_USER_ONLY
      /* Avoid overhead of an access check that always passes in user-mode */
      .accessfn = aa64_zva_access,
      .fgt = FGT_DCZVA,
#endif
    },
    { .name = "CURRENTEL", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .opc2 = 2, .crn = 4, .crm = 2,
      .access = PL1_R, .type = ARM_CP_CURRENTEL },
    /*
     * Instruction cache ops. All of these except `IC IVAU` NOP because we
     * don't emulate caches.
     */
    { .name = "IC_IALLUIS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 1, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NOP,
      .fgt = FGT_ICIALLUIS,
      .accessfn = access_ticab },
    { .name = "IC_IALLU", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NOP,
      .fgt = FGT_ICIALLU,
      .accessfn = access_tocu },
    { .name = "IC_IVAU", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 5, .opc2 = 1,
      .access = PL0_W,
      .fgt = FGT_ICIVAU,
      .accessfn = access_tocu,
#ifdef CONFIG_USER_ONLY
      .type = ARM_CP_NO_RAW,
      .writefn = ic_ivau_write
#else
      .type = ARM_CP_NOP
#endif
    },
    /* Cache ops: all NOPs since we don't emulate caches */
    { .name = "DC_IVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 1,
      .access = PL1_W, .accessfn = aa64_cacheop_poc_access,
      .fgt = FGT_DCIVAC,
      .type = ARM_CP_NOP },
    { .name = "DC_ISW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 2,
      .fgt = FGT_DCISW,
      .access = PL1_W, .accessfn = access_tsw, .type = ARM_CP_NOP },
    { .name = "DC_CVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 10, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NOP,
      .fgt = FGT_DCCVAC,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 2,
      .fgt = FGT_DCCSW,
      .access = PL1_W, .accessfn = access_tsw, .type = ARM_CP_NOP },
    { .name = "DC_CVAU", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 11, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NOP,
      .fgt = FGT_DCCVAU,
      .accessfn = access_tocu },
    { .name = "DC_CIVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 14, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NOP,
      .fgt = FGT_DCCIVAC,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CISW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 2,
      .fgt = FGT_DCCISW,
      .access = PL1_W, .accessfn = access_tsw, .type = ARM_CP_NOP },
    { .name = "PAR_EL1", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 0, .crn = 7, .crm = 4, .opc2 = 0,
      .access = PL1_RW, .resetvalue = 0,
      .fgt = FGT_PAR_EL1,
      .fieldoffset = offsetof(CPUARMState, cp15.par_el[1]),
      .writefn = par_write },
    /* 32 bit cache operations */
    { .name = "ICIALLUIS", .cp = 15, .opc1 = 0, .crn = 7, .crm = 1, .opc2 = 0,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_ticab },
    { .name = "BPIALLUIS", .cp = 15, .opc1 = 0, .crn = 7, .crm = 1, .opc2 = 6,
      .type = ARM_CP_NOP, .access = PL1_W },
    { .name = "ICIALLU", .cp = 15, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 0,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tocu },
    { .name = "ICIMVAU", .cp = 15, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tocu },
    { .name = "BPIALL", .cp = 15, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 6,
      .type = ARM_CP_NOP, .access = PL1_W },
    { .name = "BPIMVA", .cp = 15, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 7,
      .type = ARM_CP_NOP, .access = PL1_W },
    { .name = "DCIMVAC", .cp = 15, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_poc_access },
    { .name = "DCISW", .cp = 15, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 2,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DCCMVAC", .cp = 15, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_poc_access },
    { .name = "DCCSW", .cp = 15, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 2,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DCCMVAU", .cp = 15, .opc1 = 0, .crn = 7, .crm = 11, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tocu },
    { .name = "DCCIMVAC", .cp = 15, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_poc_access },
    { .name = "DCCISW", .cp = 15, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 2,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    /* MMU Domain access control / MPU write buffer control */
    { .name = "DACR", .cp = 15, .opc1 = 0, .crn = 3, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .resetvalue = 0,
      .writefn = dacr_write, .raw_writefn = raw_write,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.dacr_s),
                             offsetoflow32(CPUARMState, cp15.dacr_ns) } },
    { .name = "ELR_EL1", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_nv1_or_exlock_el1,
      .nv2_redirect_offset = 0x230 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 4, 0, 1),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 4, 0, 1),
      .fieldoffset = offsetof(CPUARMState, elr_el[1]) },
    { .name = "SPSR_EL1", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_nv1_or_exlock_el1,
      .nv2_redirect_offset = 0x160 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 4, 0, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 4, 0, 0),
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_SVC]) },
    /*
     * We rely on the access checks not allowing the guest to write to the
     * state field when SPSel indicates that it's being used as the stack
     * pointer.
     */
    { .name = "SP_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .accessfn = sp_el0_access,
      .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, sp_el[0]) },
    { .name = "SP_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 1, .opc2 = 0,
      .nv2_redirect_offset = 0x240,
      .access = PL2_RW, .type = ARM_CP_ALIAS | ARM_CP_EL3_NO_EL2_KEEP,
      .fieldoffset = offsetof(CPUARMState, sp_el[1]) },
    { .name = "SPSel", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW,
      .access = PL1_RW, .readfn = spsel_read, .writefn = spsel_write },
    { .name = "SPSR_IRQ", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 3, .opc2 = 0,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_IRQ]) },
    { .name = "SPSR_ABT", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 3, .opc2 = 1,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_ABT]) },
    { .name = "SPSR_UND", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 3, .opc2 = 2,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_UND]) },
    { .name = "SPSR_FIQ", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 3, .opc2 = 3,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_FIQ]) },
    { .name = "MDCR_EL3", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_IO,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 3, .opc2 = 1,
      .resetvalue = 0,
      .access = PL3_RW,
      .writefn = mdcr_el3_write,
      .fieldoffset = offsetof(CPUARMState, cp15.mdcr_el3) },
    { .name = "SDCR", .type = ARM_CP_ALIAS | ARM_CP_IO,
      .cp = 15, .opc1 = 0, .crn = 1, .crm = 3, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_trap_aa32s_el1,
      .writefn = sdcr_write,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.mdcr_el3) },
};

/* These are present only when EL1 supports AArch32 */
static const ARMCPRegInfo v8_aa32_el1_reginfo[] = {
    { .name = "FPEXC32_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 3, .opc2 = 0,
      .access = PL2_RW,
      .type = ARM_CP_ALIAS | ARM_CP_FPU | ARM_CP_EL3_NO_EL2_KEEP,
      .fieldoffset = offsetof(CPUARMState, vfp.xregs[ARM_VFP_FPEXC]) },
    { .name = "DACR32_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 3, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .resetvalue = 0, .type = ARM_CP_EL3_NO_EL2_KEEP,
      .writefn = dacr_write, .raw_writefn = raw_write,
      .fieldoffset = offsetof(CPUARMState, cp15.dacr32_el2) },
    { .name = "IFSR32_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 0, .opc2 = 1,
      .access = PL2_RW, .resetvalue = 0, .type = ARM_CP_EL3_NO_EL2_KEEP,
      .fieldoffset = offsetof(CPUARMState, cp15.ifsr32_el2) },
};

static void do_hcr_write(CPUARMState *env, uint64_t value, uint64_t valid_mask)
{
    ARMCPU *cpu = env_archcpu(env);

    if (arm_feature(env, ARM_FEATURE_V8)) {
        valid_mask |= MAKE_64BIT_MASK(0, 34);  /* ARMv8.0 */
    } else {
        valid_mask |= MAKE_64BIT_MASK(0, 28);  /* ARMv7VE */
    }

    if (arm_feature(env, ARM_FEATURE_EL3)) {
        valid_mask &= ~HCR_HCD;
    } else if (cpu->psci_conduit != QEMU_PSCI_CONDUIT_SMC) {
        /*
         * Architecturally HCR.TSC is RES0 if EL3 is not implemented.
         * However, if we're using the SMC PSCI conduit then QEMU is
         * effectively acting like EL3 firmware and so the guest at
         * EL2 should retain the ability to prevent EL1 from being
         * able to make SMC calls into the ersatz firmware, so in
         * that case HCR.TSC should be read/write.
         */
        valid_mask &= ~HCR_TSC;
    }

    if (arm_feature(env, ARM_FEATURE_AARCH64)) {
        if (cpu_isar_feature(aa64_vh, cpu)) {
            valid_mask |= HCR_E2H;
        }
        if (cpu_isar_feature(aa64_ras, cpu)) {
            valid_mask |= HCR_TERR | HCR_TEA;
        }
        if (cpu_isar_feature(aa64_lor, cpu)) {
            valid_mask |= HCR_TLOR;
        }
        if (cpu_isar_feature(aa64_pauth, cpu)) {
            valid_mask |= HCR_API | HCR_APK;
        }
        if (cpu_isar_feature(aa64_mte, cpu)) {
            valid_mask |= HCR_ATA | HCR_DCT | HCR_TID5;
        }
        if (cpu_isar_feature(aa64_scxtnum, cpu)) {
            valid_mask |= HCR_ENSCXT;
        }
        if (cpu_isar_feature(aa64_fwb, cpu)) {
            valid_mask |= HCR_FWB;
        }
        if (cpu_isar_feature(aa64_rme, cpu)) {
            valid_mask |= HCR_GPF;
        }
        if (cpu_isar_feature(aa64_nv, cpu)) {
            valid_mask |= HCR_NV | HCR_NV1 | HCR_AT;
        }
        if (cpu_isar_feature(aa64_nv2, cpu)) {
            valid_mask |= HCR_NV2;
        }
    }

    if (cpu_isar_feature(any_evt, cpu)) {
        valid_mask |= HCR_TTLBIS | HCR_TTLBOS | HCR_TICAB | HCR_TOCU | HCR_TID4;
    } else if (cpu_isar_feature(any_half_evt, cpu)) {
        valid_mask |= HCR_TICAB | HCR_TOCU | HCR_TID4;
    }

    /* Clear RES0 bits.  */
    value &= valid_mask;

    /* RW is RAO/WI if EL1 is AArch64 only */
    if (arm_feature(env, ARM_FEATURE_AARCH64) &&
        !cpu_isar_feature(aa64_aa32_el1, cpu)) {
        value |= HCR_RW;
    }

    /*
     * These bits change the MMU setup:
     * HCR_VM enables stage 2 translation
     * HCR_PTW forbids certain page-table setups
     * HCR_DC disables stage1 and enables stage2 translation
     * HCR_DCT enables tagging on (disabled) stage1 translation
     * HCR_FWB changes the interpretation of stage2 descriptor bits
     * HCR_NV and HCR_NV1 affect interpretation of descriptor bits
     */
    if ((env->cp15.hcr_el2 ^ value) &
        (HCR_VM | HCR_PTW | HCR_DC | HCR_DCT | HCR_FWB | HCR_NV | HCR_NV1)) {
        tlb_flush(CPU(cpu));
    }
    env->cp15.hcr_el2 = value;

    /*
     * Updates to VI and VF require us to update the status of
     * virtual interrupts, which are the logical OR of these bits
     * and the state of the input lines from the GIC. (This requires
     * that we have the BQL, which is done by marking the
     * reginfo structs as ARM_CP_IO.)
     * Note that if a write to HCR pends a VIRQ or VFIQ or VINMI or
     * VFNMI, it is never possible for it to be taken immediately
     * because VIRQ, VFIQ, VINMI and VFNMI are masked unless running
     * at EL0 or EL1, and HCR can only be written at EL2.
     */
    g_assert(bql_locked());
    arm_cpu_update_virq(cpu);
    arm_cpu_update_vfiq(cpu);
    arm_cpu_update_vserr(cpu);
    if (cpu_isar_feature(aa64_nmi, cpu)) {
        arm_cpu_update_vinmi(cpu);
        arm_cpu_update_vfnmi(cpu);
    }
}

static void hcr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    do_hcr_write(env, value, 0);
}

static void hcr_writehigh(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    /* Handle HCR2 write, i.e. write to high half of HCR_EL2 */
    value = deposit64(env->cp15.hcr_el2, 32, 32, value);
    do_hcr_write(env, value, MAKE_64BIT_MASK(0, 32));
}

static void hcr_writelow(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    /* Handle HCR write, i.e. write to low half of HCR_EL2 */
    value = deposit64(env->cp15.hcr_el2, 0, 32, value);
    do_hcr_write(env, value, MAKE_64BIT_MASK(32, 32));
}

static void hcr_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* hcr_write will set the RES1 bits on an AArch64-only CPU */
    hcr_write(env, ri, 0);
}

/*
 * Return the effective value of HCR_EL2, at the given security state.
 * Bits that are not included here:
 * RW       (read from SCR_EL3.RW as needed)
 */
uint64_t arm_hcr_el2_eff_secstate(CPUARMState *env, ARMSecuritySpace space)
{
    uint64_t ret = env->cp15.hcr_el2;

    assert(space != ARMSS_Root);

    if (!arm_is_el2_enabled_secstate(env, space)) {
        /*
         * "This register has no effect if EL2 is not enabled in the
         * current Security state".  This is ARMv8.4-SecEL2 speak for
         * !(SCR_EL3.NS==1 || SCR_EL3.EEL2==1).
         *
         * Prior to that, the language was "In an implementation that
         * includes EL3, when the value of SCR_EL3.NS is 0 the PE behaves
         * as if this field is 0 for all purposes other than a direct
         * read or write access of HCR_EL2".  With lots of enumeration
         * on a per-field basis.  In current QEMU, this is condition
         * is arm_is_secure_below_el3.
         *
         * Since the v8.4 language applies to the entire register, and
         * appears to be backward compatible, use that.
         */
        return 0;
    }

    /*
     * For a cpu that supports both aarch64 and aarch32, we can set bits
     * in HCR_EL2 (e.g. via EL3) that are RES0 when we enter EL2 as aa32.
     * Ignore all of the bits in HCR+HCR2 that are not valid for aarch32.
     */
    if (!arm_el_is_aa64(env, 2)) {
        uint64_t aa32_valid;

        /*
         * These bits are up-to-date as of ARMv8.6.
         * For HCR, it's easiest to list just the 2 bits that are invalid.
         * For HCR2, list those that are valid.
         */
        aa32_valid = MAKE_64BIT_MASK(0, 32) & ~(HCR_RW | HCR_TDZ);
        aa32_valid |= (HCR_CD | HCR_ID | HCR_TERR | HCR_TEA | HCR_MIOCNCE |
                       HCR_TID4 | HCR_TICAB | HCR_TOCU | HCR_TTLBIS);
        ret &= aa32_valid;
    }

    if (ret & HCR_TGE) {
        /* These bits are up-to-date as of ARMv8.6.  */
        if (ret & HCR_E2H) {
            ret &= ~(HCR_VM | HCR_FMO | HCR_IMO | HCR_AMO |
                     HCR_BSU_MASK | HCR_DC | HCR_TWI | HCR_TWE |
                     HCR_TID0 | HCR_TID2 | HCR_TPCP | HCR_TPU |
                     HCR_TDZ | HCR_CD | HCR_ID | HCR_MIOCNCE |
                     HCR_TID4 | HCR_TICAB | HCR_TOCU | HCR_ENSCXT |
                     HCR_TTLBIS | HCR_TTLBOS | HCR_TID5);
        } else {
            ret |= HCR_FMO | HCR_IMO | HCR_AMO;
        }
        ret &= ~(HCR_SWIO | HCR_PTW | HCR_VF | HCR_VI | HCR_VSE |
                 HCR_FB | HCR_TID1 | HCR_TID3 | HCR_TSC | HCR_TACR |
                 HCR_TSW | HCR_TTLB | HCR_TVM | HCR_HCD | HCR_TRVM |
                 HCR_TLOR);
    }

    return ret;
}

uint64_t arm_hcr_el2_eff(CPUARMState *env)
{
    if (arm_feature(env, ARM_FEATURE_M)) {
        return 0;
    }
    return arm_hcr_el2_eff_secstate(env, arm_security_space_below_el3(env));
}

uint64_t arm_hcr_el2_nvx_eff(CPUARMState *env)
{
    uint64_t hcr = arm_hcr_el2_eff(env);

    if (!(hcr & HCR_NV)) {
        return 0; /* CONSTRAINED UNPREDICTABLE wrt NV1 */
    }
    return hcr & (HCR_NV2 | HCR_NV1 | HCR_NV);
}

/*
 * Corresponds to ARM pseudocode function ELIsInHost().
 */
bool el_is_in_host(CPUARMState *env, int el)
{
    uint64_t mask;

    /*
     * Since we only care about E2H and TGE, we can skip arm_hcr_el2_eff().
     * Perform the simplest bit tests first, and validate EL2 afterward.
     */
    if (el & 1) {
        return false; /* EL1 or EL3 */
    }

    /*
     * Note that hcr_write() checks isar_feature_aa64_vh(),
     * aka HaveVirtHostExt(), in allowing HCR_E2H to be set.
     */
    mask = el ? HCR_E2H : HCR_E2H | HCR_TGE;
    if ((env->cp15.hcr_el2 & mask) != mask) {
        return false;
    }

    /* TGE and/or E2H set: double check those bits are currently legal. */
    return arm_is_el2_enabled(env) && arm_el_is_aa64(env, 2);
}

static void hcrx_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t valid_mask = 0;

    if (cpu_isar_feature(aa64_mops, cpu)) {
        valid_mask |= HCRX_MSCEN | HCRX_MCE2;
    }
    if (cpu_isar_feature(aa64_nmi, cpu)) {
        valid_mask |= HCRX_TALLINT | HCRX_VINMI | HCRX_VFNMI;
    }
    if (cpu_isar_feature(aa64_cmow, cpu)) {
        valid_mask |= HCRX_CMOW;
    }
    if (cpu_isar_feature(aa64_xs, cpu)) {
        valid_mask |= HCRX_FGTNXS | HCRX_FNXS;
    }
    if (cpu_isar_feature(aa64_tcr2, cpu)) {
        valid_mask |= HCRX_TCR2EN;
    }
    if (cpu_isar_feature(aa64_sctlr2, cpu)) {
        valid_mask |= HCRX_SCTLR2EN;
    }
    if (cpu_isar_feature(aa64_gcs, cpu)) {
        valid_mask |= HCRX_GCSEN;
    }

    /* Clear RES0 bits.  */
    env->cp15.hcrx_el2 = value & valid_mask;

    /*
     * Updates to VINMI and VFNMI require us to update the status of
     * virtual NMI, which are the logical OR of these bits
     * and the state of the input lines from the GIC. (This requires
     * that we have the BQL, which is done by marking the
     * reginfo structs as ARM_CP_IO.)
     * Note that if a write to HCRX pends a VINMI or VFNMI it is never
     * possible for it to be taken immediately, because VINMI and
     * VFNMI are masked unless running at EL0 or EL1, and HCRX
     * can only be written at EL2.
     */
    if (cpu_isar_feature(aa64_nmi, cpu)) {
        g_assert(bql_locked());
        arm_cpu_update_vinmi(cpu);
        arm_cpu_update_vfnmi(cpu);
    }
}

static CPAccessResult access_hxen(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    if (arm_current_el(env) == 2
        && arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.scr_el3 & SCR_HXEN)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo hcrx_el2_reginfo = {
    .name = "HCRX_EL2", .state = ARM_CP_STATE_AA64,
    .type = ARM_CP_IO,
    .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 2, .opc2 = 2,
    .access = PL2_RW, .writefn = hcrx_write, .accessfn = access_hxen,
    .nv2_redirect_offset = 0xa0,
    .fieldoffset = offsetof(CPUARMState, cp15.hcrx_el2),
};

/* Return the effective value of HCRX_EL2.  */
uint64_t arm_hcrx_el2_eff(CPUARMState *env)
{
    /*
     * The bits in this register behave as 0 for all purposes other than
     * direct reads of the register if SCR_EL3.HXEn is 0.
     * If EL2 is not enabled in the current security state, then the
     * bit may behave as if 0, or as if 1, depending on the bit.
     * For the moment, we treat the EL2-disabled case as taking
     * priority over the HXEn-disabled case. This is true for the only
     * bit for a feature which we implement where the answer is different
     * for the two cases (MSCEn for FEAT_MOPS).
     * This may need to be revisited for future bits.
     */
    if (!arm_is_el2_enabled(env)) {
        ARMCPU *cpu = env_archcpu(env);
        uint64_t hcrx = 0;

        /* Bits which whose effective value is 1 if el2 not enabled. */
        if (cpu_isar_feature(aa64_mops, cpu)) {
            hcrx |= HCRX_MSCEN;
        }
        if (cpu_isar_feature(aa64_tcr2, cpu)) {
            hcrx |= HCRX_TCR2EN;
        }
        if (cpu_isar_feature(aa64_sctlr2, cpu)) {
            hcrx |= HCRX_SCTLR2EN;
        }
        if (cpu_isar_feature(aa64_gcs, cpu)) {
            hcrx |= HCRX_GCSEN;
        }
        return hcrx;
    }
    if (arm_feature(env, ARM_FEATURE_EL3) && !(env->cp15.scr_el3 & SCR_HXEN)) {
        return 0;
    }
    return env->cp15.hcrx_el2;
}

static void cptr_el2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /*
     * For A-profile AArch32 EL3, if NSACR.CP10
     * is 0 then HCPTR.{TCP11,TCP10} ignore writes and read as 1.
     */
    if (arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
        !arm_is_secure(env) && !extract32(env->cp15.nsacr, 10, 1)) {
        uint64_t mask = R_HCPTR_TCP11_MASK | R_HCPTR_TCP10_MASK;
        value = (value & ~mask) | (env->cp15.cptr_el[2] & mask);
    }
    env->cp15.cptr_el[2] = value;
}

static uint64_t cptr_el2_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /*
     * For A-profile AArch32 EL3, if NSACR.CP10
     * is 0 then HCPTR.{TCP11,TCP10} ignore writes and read as 1.
     */
    uint64_t value = env->cp15.cptr_el[2];

    if (arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
        !arm_is_secure(env) && !extract32(env->cp15.nsacr, 10, 1)) {
        value |= R_HCPTR_TCP11_MASK | R_HCPTR_TCP10_MASK;
    }
    return value;
}

static const ARMCPRegInfo el2_cp_reginfo[] = {
    { .name = "HCR_EL2", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_IO,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.hcr_el2),
      .nv2_redirect_offset = 0x78,
      .resetfn = hcr_reset,
      .writefn = hcr_write, .raw_writefn = raw_write },
    { .name = "HCR", .state = ARM_CP_STATE_AA32,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .cp = 15, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.hcr_el2),
      .writefn = hcr_writelow },
    { .name = "HACR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 7,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "ELR_EL2", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS | ARM_CP_NV2_REDIRECT,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 0, .opc2 = 1,
      .access = PL2_RW, .accessfn = access_exlock_el2,
      .fieldoffset = offsetof(CPUARMState, elr_el[2]) },
    { .name = "ESR_EL2", .state = ARM_CP_STATE_BOTH,
      .type = ARM_CP_NV2_REDIRECT,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 2, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.esr_el[2]) },
    { .name = "FAR_EL2", .state = ARM_CP_STATE_BOTH,
      .type = ARM_CP_NV2_REDIRECT,
      .opc0 = 3, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.far_el[2]) },
    { .name = "HIFAR", .state = ARM_CP_STATE_AA32,
      .type = ARM_CP_ALIAS,
      .cp = 15, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 2,
      .access = PL2_RW,
      .fieldoffset = offsetofhigh32(CPUARMState, cp15.far_el[2]) },
    { .name = "SPSR_EL2", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS | ARM_CP_NV2_REDIRECT,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .accessfn = access_exlock_el2,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_HYP]) },
    { .name = "VBAR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 12, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .writefn = vbar_write,
      .fieldoffset = offsetof(CPUARMState, cp15.vbar_el[2]),
      .resetvalue = 0 },
    { .name = "SP_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 4, .crm = 1, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, sp_el[2]) },
    { .name = "CPTR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 2,
      .access = PL2_RW, .accessfn = cptr_access, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.cptr_el[2]),
      .readfn = cptr_el2_read, .writefn = cptr_el2_write },
    { .name = "MAIR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 10, .crm = 2, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.mair_el[2]),
      .resetvalue = 0 },
    { .name = "HMAIR1", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 10, .crm = 2, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetofhigh32(CPUARMState, cp15.mair_el[2]) },
    { .name = "AMAIR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 10, .crm = 3, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    /* HAMAIR1 is mapped to AMAIR_EL2[63:32] */
    { .name = "HAMAIR1", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 10, .crm = 3, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR0_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR1_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 1, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "TCR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 0, .opc2 = 2,
      .access = PL2_RW, .writefn = vmsa_tcr_el12_write,
      .raw_writefn = raw_write,
      .fieldoffset = offsetof(CPUARMState, cp15.tcr_el[2]) },
    { .name = "VTCR", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 2, .crm = 1, .opc2 = 2,
      .type = ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = access_el3_aa32ns,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.vtcr_el2) },
    { .name = "VTCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 1, .opc2 = 2,
      .access = PL2_RW,
      .nv2_redirect_offset = 0x40,
      /* no .writefn needed as this can't cause an ASID change */
      .fieldoffset = offsetof(CPUARMState, cp15.vtcr_el2) },
    { .name = "VTTBR", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 6, .crm = 2,
      .type = ARM_CP_64BIT | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = access_el3_aa32ns,
      .fieldoffset = offsetof(CPUARMState, cp15.vttbr_el2),
      .writefn = vttbr_write, .raw_writefn = raw_write },
    { .name = "VTTBR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .writefn = vttbr_write, .raw_writefn = raw_write,
      .nv2_redirect_offset = 0x20,
      .fieldoffset = offsetof(CPUARMState, cp15.vttbr_el2) },
    { .name = "SCTLR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .raw_writefn = raw_write, .writefn = sctlr_write,
      .fieldoffset = offsetof(CPUARMState, cp15.sctlr_el[2]) },
    { .name = "TPIDR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 13, .crm = 0, .opc2 = 2,
      .access = PL2_RW, .resetvalue = 0,
      .nv2_redirect_offset = 0x90,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidr_el[2]) },
    { .name = "TTBR0_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .resetvalue = 0,
      .writefn = vmsa_tcr_ttbr_el2_write, .raw_writefn = raw_write,
      .fieldoffset = offsetof(CPUARMState, cp15.ttbr0_el[2]) },
    { .name = "HTTBR", .cp = 15, .opc1 = 4, .crm = 2,
      .access = PL2_RW, .type = ARM_CP_64BIT | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.ttbr0_el[2]) },
#ifndef CONFIG_USER_ONLY
    { .name = "CNTHCTL_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 1, .opc2 = 0,
      /*
       * ARMv7 requires bit 0 and 1 to reset to 1. ARMv8 defines the
       * reset values as IMPDEF. We choose to reset to 3 to comply with
       * both ARMv7 and ARMv8.
       */
      .access = PL2_RW, .type = ARM_CP_IO, .resetvalue = 3,
      .writefn = gt_cnthctl_write, .raw_writefn = raw_write,
      .fieldoffset = offsetof(CPUARMState, cp15.cnthctl_el2) },
    { .name = "CNTVOFF_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 0, .opc2 = 3,
      .access = PL2_RW, .type = ARM_CP_IO, .resetvalue = 0,
      .writefn = gt_cntvoff_write,
      .nv2_redirect_offset = 0x60,
      .fieldoffset = offsetof(CPUARMState, cp15.cntvoff_el2) },
    { .name = "CNTVOFF", .cp = 15, .opc1 = 4, .crm = 14,
      .access = PL2_RW, .type = ARM_CP_64BIT | ARM_CP_ALIAS | ARM_CP_IO,
      .writefn = gt_cntvoff_write,
      .fieldoffset = offsetof(CPUARMState, cp15.cntvoff_el2) },
    { .name = "CNTHP_CVAL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 2, .opc2 = 2,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYP].cval),
      .type = ARM_CP_IO, .access = PL2_RW,
      .writefn = gt_hyp_cval_write, .raw_writefn = raw_write },
    { .name = "CNTHP_CVAL", .cp = 15, .opc1 = 6, .crm = 14,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYP].cval),
      .access = PL2_RW, .type = ARM_CP_64BIT | ARM_CP_IO,
      .writefn = gt_hyp_cval_write, .raw_writefn = raw_write },
    { .name = "CNTHP_TVAL_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL2_RW,
      .resetfn = gt_hyp_timer_reset,
      .readfn = gt_hyp_tval_read, .writefn = gt_hyp_tval_write },
    { .name = "CNTHP_CTL_EL2", .state = ARM_CP_STATE_BOTH,
      .type = ARM_CP_IO,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 2, .opc2 = 1,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYP].ctl),
      .resetvalue = 0,
      .writefn = gt_hyp_ctl_write, .raw_writefn = raw_write },
#endif
    { .name = "HPFAR", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 4,
      .access = PL2_RW, .accessfn = access_el3_aa32ns,
      .fieldoffset = offsetof(CPUARMState, cp15.hpfar_el2) },
    { .name = "HPFAR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 4,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.hpfar_el2) },
    { .name = "HSTR_EL2", .state = ARM_CP_STATE_BOTH,
      .cp = 15, .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 3,
      .access = PL2_RW,
      .nv2_redirect_offset = 0x80,
      .fieldoffset = offsetof(CPUARMState, cp15.hstr_el2) },
};

static const ARMCPRegInfo el2_v8_cp_reginfo[] = {
    { .name = "HCR2", .state = ARM_CP_STATE_AA32,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .cp = 15, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 4,
      .access = PL2_RW,
      .fieldoffset = offsetofhigh32(CPUARMState, cp15.hcr_el2),
      .writefn = hcr_writehigh },
};

static CPAccessResult sel2_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    if (arm_current_el(env) == 3 || arm_is_secure_below_el3(env)) {
        return CP_ACCESS_OK;
    }
    return CP_ACCESS_UNDEFINED;
}

static const ARMCPRegInfo el2_sec_cp_reginfo[] = {
    { .name = "VSTTBR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 6, .opc2 = 0,
      .access = PL2_RW, .accessfn = sel2_access,
      .nv2_redirect_offset = 0x30,
      .fieldoffset = offsetof(CPUARMState, cp15.vsttbr_el2) },
    { .name = "VSTCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 6, .opc2 = 2,
      .access = PL2_RW, .accessfn = sel2_access,
      .nv2_redirect_offset = 0x48,
      .fieldoffset = offsetof(CPUARMState, cp15.vstcr_el2) },
#ifndef CONFIG_USER_ONLY
    /* Secure EL2 Physical Timer */
    { .name = "CNTHPS_TVAL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 5, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL2_RW,
      .accessfn = gt_sel2timer_access,
      .readfn = gt_sec_pel2_tval_read,
      .writefn = gt_sec_pel2_tval_write,
      .resetfn = gt_sec_pel2_timer_reset,
    },
    { .name = "CNTHPS_CTL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 5, .opc2 = 1,
      .type = ARM_CP_IO, .access = PL2_RW,
      .accessfn = gt_sel2timer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_S_EL2_PHYS].ctl),
      .resetvalue = 0,
      .writefn = gt_sec_pel2_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTHPS_CVAL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 5, .opc2 = 2,
      .type = ARM_CP_IO, .access = PL2_RW,
      .accessfn = gt_sel2timer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_S_EL2_PHYS].cval),
      .writefn = gt_sec_pel2_cval_write, .raw_writefn = raw_write,
    },
    /* Secure EL2 Virtual Timer */
    { .name = "CNTHVS_TVAL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 4, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL2_RW,
      .accessfn = gt_sel2timer_access,
      .readfn = gt_sec_vel2_tval_read,
      .writefn = gt_sec_vel2_tval_write,
      .resetfn = gt_sec_vel2_timer_reset,
    },
    { .name = "CNTHVS_CTL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 4, .opc2 = 1,
      .type = ARM_CP_IO, .access = PL2_RW,
      .accessfn = gt_sel2timer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_S_EL2_VIRT].ctl),
      .resetvalue = 0,
      .writefn = gt_sec_vel2_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTHVS_CVAL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 4, .opc2 = 2,
      .type = ARM_CP_IO, .access = PL2_RW,
      .accessfn = gt_sel2timer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_S_EL2_VIRT].cval),
      .writefn = gt_sec_vel2_cval_write, .raw_writefn = raw_write,
    },
#endif
};

static CPAccessResult nsacr_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    /*
     * The NSACR is RW at EL3, and RO for NS EL1 and NS EL2.
     * At Secure EL1 it traps to EL3 or EL2.
     */
    if (arm_current_el(env) == 3) {
        return CP_ACCESS_OK;
    }
    if (arm_is_secure_below_el3(env)) {
        if (env->cp15.scr_el3 & SCR_EEL2) {
            return CP_ACCESS_TRAP_EL2;
        }
        return CP_ACCESS_TRAP_EL3;
    }
    /* Accesses from EL1 NS and EL2 NS are UNDEF for write but allow reads. */
    if (isread) {
        return CP_ACCESS_OK;
    }
    return CP_ACCESS_UNDEFINED;
}

static const ARMCPRegInfo el3_cp_reginfo[] = {
    { .name = "SCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 1, .opc2 = 0,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.scr_el3),
      .resetfn = scr_reset, .writefn = scr_write, .raw_writefn = raw_write },
    { .name = "SCR",  .type = ARM_CP_ALIAS | ARM_CP_NEWEL,
      .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_trap_aa32s_el1,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.scr_el3),
      .writefn = scr_write, .raw_writefn = raw_write },
    { .name = "SDER32_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 1, .opc2 = 1,
      .access = PL3_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.sder) },
    { .name = "SDER",
      .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 1,
      .access = PL3_RW, .resetvalue = 0,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.sder) },
    { .name = "MVBAR", .cp = 15, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_trap_aa32s_el1,
      .writefn = vbar_write, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.mvbar) },
    { .name = "TTBR0_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 2, .crm = 0, .opc2 = 0,
      .access = PL3_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.ttbr0_el[3]) },
    { .name = "TCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 2, .crm = 0, .opc2 = 2,
      .access = PL3_RW,
      /* no .writefn needed as this can't cause an ASID change */
      .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.tcr_el[3]) },
    { .name = "ELR_EL3", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 6, .crn = 4, .crm = 0, .opc2 = 1,
      .access = PL3_RW, .accessfn = access_exlock_el3,
      .fieldoffset = offsetof(CPUARMState, elr_el[3]) },
    { .name = "ESR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 5, .crm = 2, .opc2 = 0,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.esr_el[3]) },
    { .name = "FAR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 6, .crm = 0, .opc2 = 0,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.far_el[3]) },
    { .name = "SPSR_EL3", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 6, .crn = 4, .crm = 0, .opc2 = 0,
      .access = PL3_RW, .accessfn = access_exlock_el3,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_MON]) },
    { .name = "VBAR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 12, .crm = 0, .opc2 = 0,
      .access = PL3_RW, .writefn = vbar_write,
      .fieldoffset = offsetof(CPUARMState, cp15.vbar_el[3]),
      .resetvalue = 0 },
    { .name = "CPTR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 1, .opc2 = 2,
      .access = PL3_RW, .accessfn = cptr_access, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.cptr_el[3]) },
    { .name = "TPIDR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 13, .crm = 0, .opc2 = 2,
      .access = PL3_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidr_el[3]) },
    { .name = "AMAIR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 10, .crm = 3, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR0_EL3", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 6, .crn = 5, .crm = 1, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR1_EL3", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 6, .crn = 5, .crm = 1, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
};

#ifndef CONFIG_USER_ONLY

static CPAccessResult e2h_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (arm_current_el(env) == 1) {
        /* This must be a FEAT_NV access */
        return CP_ACCESS_OK;
    }
    if (!(arm_hcr_el2_eff(env) & HCR_E2H)) {
        return CP_ACCESS_UNDEFINED;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_el1nvpct(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    if (arm_current_el(env) == 1) {
        /* This must be a FEAT_NV access with NVx == 101 */
        if (FIELD_EX64(env->cp15.cnthctl_el2, CNTHCTL, EL1NVPCT)) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    return e2h_access(env, ri, isread);
}

static CPAccessResult access_el1nvvct(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    if (arm_current_el(env) == 1) {
        /* This must be a FEAT_NV access with NVx == 101 */
        if (FIELD_EX64(env->cp15.cnthctl_el2, CNTHCTL, EL1NVVCT)) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    return e2h_access(env, ri, isread);
}

#endif

static CPAccessResult ctr_el0_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    int cur_el = arm_current_el(env);

    if (cur_el < 2) {
        uint64_t hcr = arm_hcr_el2_eff(env);

        if (cur_el == 0) {
            if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
                if (!(env->cp15.sctlr_el[2] & SCTLR_UCT)) {
                    return CP_ACCESS_TRAP_EL2;
                }
            } else {
                if (!(env->cp15.sctlr_el[1] & SCTLR_UCT)) {
                    return CP_ACCESS_TRAP_EL1;
                }
                if (hcr & HCR_TID2) {
                    return CP_ACCESS_TRAP_EL2;
                }
            }
        } else if (hcr & HCR_TID2) {
            return CP_ACCESS_TRAP_EL2;
        }
    }

    if (arm_current_el(env) < 2 && arm_hcr_el2_eff(env) & HCR_TID2) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

/*
 * Check for traps to RAS registers, which are controlled
 * by HCR_EL2.TERR and SCR_EL3.TERR.
 */
static CPAccessResult access_terr(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    int el = arm_current_el(env);

    if (el < 2 && (arm_hcr_el2_eff(env) & HCR_TERR)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (!arm_is_el3_or_mon(env) && (env->cp15.scr_el3 & SCR_TERR)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static uint64_t disr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    int el = arm_current_el(env);

    if (el < 2 && (arm_hcr_el2_eff(env) & HCR_AMO)) {
        return env->cp15.vdisr_el2;
    }
    if (el < 3 && (env->cp15.scr_el3 & SCR_EA)) {
        return 0; /* RAZ/WI */
    }
    return env->cp15.disr_el1;
}

static void disr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t val)
{
    int el = arm_current_el(env);

    if (el < 2 && (arm_hcr_el2_eff(env) & HCR_AMO)) {
        env->cp15.vdisr_el2 = val;
        return;
    }
    if (el < 3 && (env->cp15.scr_el3 & SCR_EA)) {
        return; /* RAZ/WI */
    }
    env->cp15.disr_el1 = val;
}

/*
 * Minimal RAS implementation with no Error Records.
 * Which means that all of the Error Record registers:
 *   ERXADDR_EL1
 *   ERXCTLR_EL1
 *   ERXFR_EL1
 *   ERXMISC0_EL1
 *   ERXMISC1_EL1
 *   ERXMISC2_EL1
 *   ERXMISC3_EL1
 *   ERXPFGCDN_EL1  (RASv1p1)
 *   ERXPFGCTL_EL1  (RASv1p1)
 *   ERXPFGF_EL1    (RASv1p1)
 *   ERXSTATUS_EL1
 * and
 *   ERRSELR_EL1
 * may generate UNDEFINED, which is the effect we get by not
 * listing them at all.
 *
 * These registers have fine-grained trap bits, but UNDEF-to-EL1
 * is higher priority than FGT-to-EL2 so we do not need to list them
 * in order to check for an FGT.
 */
static const ARMCPRegInfo minimal_ras_reginfo[] = {
    { .name = "DISR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .fieldoffset = offsetof(CPUARMState, cp15.disr_el1),
      .readfn = disr_read, .writefn = disr_write, .raw_writefn = raw_write },
    { .name = "ERRIDR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 5, .crm = 3, .opc2 = 0,
      .access = PL1_R, .accessfn = access_terr,
      .fgt = FGT_ERRIDR_EL1,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "VDISR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 12, .crm = 1, .opc2 = 1,
      .nv2_redirect_offset = 0x500,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.vdisr_el2) },
    { .name = "VSESR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 2, .opc2 = 3,
      .nv2_redirect_offset = 0x508,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.vsesr_el2) },
};

/*
 * Return the exception level to which exceptions should be taken
 * via SVEAccessTrap.  This excludes the check for whether the exception
 * should be routed through AArch64.AdvSIMDFPAccessTrap.  That can easily
 * be found by testing 0 < fp_exception_el < sve_exception_el.
 *
 * C.f. the ARM pseudocode function CheckSVEEnabled.  Note that the
 * pseudocode does *not* separate out the FP trap checks, but has them
 * all in one function.
 */
int sve_exception_el(CPUARMState *env, int el)
{
#ifndef CONFIG_USER_ONLY
    if (el <= 1 && !el_is_in_host(env, el)) {
        switch (FIELD_EX64(env->cp15.cpacr_el1, CPACR_EL1, ZEN)) {
        case 1:
            if (el != 0) {
                break;
            }
            /* fall through */
        case 0:
        case 2:
            return 1;
        }
    }

    if (el <= 2 && arm_is_el2_enabled(env)) {
        /* CPTR_EL2 changes format with HCR_EL2.E2H (regardless of TGE). */
        if (env->cp15.hcr_el2 & HCR_E2H) {
            switch (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, ZEN)) {
            case 1:
                if (el != 0 || !(env->cp15.hcr_el2 & HCR_TGE)) {
                    break;
                }
                /* fall through */
            case 0:
            case 2:
                return 2;
            }
        } else {
            if (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, TZ)) {
                return 2;
            }
        }
    }

    /* CPTR_EL3.  Since EZ is negative we must check for EL3.  */
    if (arm_feature(env, ARM_FEATURE_EL3)
        && !FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, EZ)) {
        return 3;
    }
#endif
    return 0;
}

/*
 * Return the exception level to which exceptions should be taken for SME.
 * C.f. the ARM pseudocode function CheckSMEAccess.
 */
int sme_exception_el(CPUARMState *env, int el)
{
#ifndef CONFIG_USER_ONLY
    if (el <= 1 && !el_is_in_host(env, el)) {
        switch (FIELD_EX64(env->cp15.cpacr_el1, CPACR_EL1, SMEN)) {
        case 1:
            if (el != 0) {
                break;
            }
            /* fall through */
        case 0:
        case 2:
            return 1;
        }
    }

    if (el <= 2 && arm_is_el2_enabled(env)) {
        /* CPTR_EL2 changes format with HCR_EL2.E2H (regardless of TGE). */
        if (env->cp15.hcr_el2 & HCR_E2H) {
            switch (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, SMEN)) {
            case 1:
                if (el != 0 || !(env->cp15.hcr_el2 & HCR_TGE)) {
                    break;
                }
                /* fall through */
            case 0:
            case 2:
                return 2;
            }
        } else {
            if (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, TSM)) {
                return 2;
            }
        }
    }

    /* CPTR_EL3.  Since ESM is negative we must check for EL3.  */
    if (arm_feature(env, ARM_FEATURE_EL3)
        && !FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, ESM)) {
        return 3;
    }
#endif
    return 0;
}

/*
 * Given that SVE is enabled, return the vector length for EL.
 */
uint32_t sve_vqm1_for_el_sm(CPUARMState *env, int el, bool sm)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t *cr = env->vfp.zcr_el;
    uint32_t map = cpu->sve_vq.map;
    uint32_t len = ARM_MAX_VQ - 1;

    if (sm) {
        cr = env->vfp.smcr_el;
        map = cpu->sme_vq.map;
    }

    if (el <= 1 && !el_is_in_host(env, el)) {
        len = MIN(len, 0xf & (uint32_t)cr[1]);
    }
    if (el <= 2 && arm_is_el2_enabled(env)) {
        len = MIN(len, 0xf & (uint32_t)cr[2]);
    }
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        len = MIN(len, 0xf & (uint32_t)cr[3]);
    }

    map &= MAKE_64BIT_MASK(0, len + 1);
    if (map != 0) {
        return 31 - clz32(map);
    }

    /* Bit 0 is always set for Normal SVE -- not so for Streaming SVE. */
    assert(sm);
    return ctz32(cpu->sme_vq.map);
}

uint32_t sve_vqm1_for_el(CPUARMState *env, int el)
{
    return sve_vqm1_for_el_sm(env, el, FIELD_EX64(env->svcr, SVCR, SM));
}

static void zcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                      uint64_t value)
{
    int cur_el = arm_current_el(env);
    int old_len = sve_vqm1_for_el(env, cur_el);
    int new_len;

    /* Bits other than [3:0] are RAZ/WI.  */
    QEMU_BUILD_BUG_ON(ARM_MAX_VQ > 16);
    raw_write(env, ri, value & 0xf);

    /*
     * Because we arrived here, we know both FP and SVE are enabled;
     * otherwise we would have trapped access to the ZCR_ELn register.
     */
    new_len = sve_vqm1_for_el(env, cur_el);
    if (new_len < old_len) {
        aarch64_sve_narrow_vq(env, new_len + 1);
    }
}

static const ARMCPRegInfo zcr_reginfo[] = {
    { .name = "ZCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 2, .opc2 = 0,
      .nv2_redirect_offset = 0x1e0 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 1, 2, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 1, 2, 0),
      .access = PL1_RW, .type = ARM_CP_SVE,
      .fieldoffset = offsetof(CPUARMState, vfp.zcr_el[1]),
      .writefn = zcr_write, .raw_writefn = raw_write },
    { .name = "ZCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 2, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_SVE,
      .fieldoffset = offsetof(CPUARMState, vfp.zcr_el[2]),
      .writefn = zcr_write, .raw_writefn = raw_write },
    { .name = "ZCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 2, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_SVE,
      .fieldoffset = offsetof(CPUARMState, vfp.zcr_el[3]),
      .writefn = zcr_write, .raw_writefn = raw_write },
};

static CPAccessResult access_tpidr2(CPUARMState *env, const ARMCPRegInfo *ri,
                                    bool isread)
{
    int el = arm_current_el(env);

    if (el == 0) {
        uint64_t sctlr = arm_sctlr(env, el);
        if (!(sctlr & SCTLR_EnTP2)) {
            return CP_ACCESS_TRAP_EL1;
        }
    }
    /* TODO: FEAT_FGT */
    if (el < 3
        && arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.scr_el3 & SCR_ENTP2)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_smprimap(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    /* If EL1 this is a FEAT_NV access and CPTR_EL3.ESM doesn't apply */
    if (arm_current_el(env) == 2
        && arm_feature(env, ARM_FEATURE_EL3)
        && !FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, ESM)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_smpri(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    if (arm_current_el(env) < 3
        && arm_feature(env, ARM_FEATURE_EL3)
        && !FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, ESM)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

/* ResetSVEState */
static void arm_reset_sve_state(CPUARMState *env)
{
    memset(env->vfp.zregs, 0, sizeof(env->vfp.zregs));
    /* Recall that FFR is stored as pregs[16]. */
    memset(env->vfp.pregs, 0, sizeof(env->vfp.pregs));
    vfp_set_fpsr(env, 0x0800009f);
}

void aarch64_set_svcr(CPUARMState *env, uint64_t new, uint64_t mask)
{
    uint64_t change = (env->svcr ^ new) & mask;

    if (change == 0) {
        return;
    }
    env->svcr ^= change;

    if (change & R_SVCR_SM_MASK) {
        arm_reset_sve_state(env);
    }

    /*
     * ResetSMEState.
     *
     * SetPSTATE_ZA zeros on enable and disable.  We can zero this only
     * on enable: while disabled, the storage is inaccessible and the
     * value does not matter.  We're not saving the storage in vmstate
     * when disabled either.
     */
    if (change & new & R_SVCR_ZA_MASK) {
        memset(&env->za_state, 0, sizeof(env->za_state));
    }

    if (tcg_enabled()) {
        arm_rebuild_hflags(env);
    }
}

static void svcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    aarch64_set_svcr(env, value, -1);
}

static void smcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    int cur_el = arm_current_el(env);
    int old_len = sve_vqm1_for_el(env, cur_el);
    uint64_t valid_mask = R_SMCR_LEN_MASK | R_SMCR_FA64_MASK;
    int new_len;

    QEMU_BUILD_BUG_ON(ARM_MAX_VQ > R_SMCR_LEN_MASK + 1);
    if (cpu_isar_feature(aa64_sme2, env_archcpu(env))) {
        valid_mask |= R_SMCR_EZT0_MASK;
    }
    value &= valid_mask;
    raw_write(env, ri, value);

    /*
     * Note that it is CONSTRAINED UNPREDICTABLE what happens to ZA storage
     * when SVL is widened (old values kept, or zeros).  Choose to keep the
     * current values for simplicity.  But for QEMU internals, we must still
     * apply the narrower SVL to the Zregs and Pregs -- see the comment
     * above aarch64_sve_narrow_vq.
     */
    new_len = sve_vqm1_for_el(env, cur_el);
    if (new_len < old_len) {
        aarch64_sve_narrow_vq(env, new_len + 1);
    }
}

static const ARMCPRegInfo sme_reginfo[] = {
    { .name = "TPIDR2_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 13, .crm = 0, .opc2 = 5,
      .access = PL0_RW, .accessfn = access_tpidr2,
      .fgt = FGT_NTPIDR2_EL0,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidr2_el0) },
    { .name = "SVCR", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 4, .crm = 2, .opc2 = 2,
      .access = PL0_RW, .type = ARM_CP_SME,
      .fieldoffset = offsetof(CPUARMState, svcr),
      .writefn = svcr_write, .raw_writefn = raw_write },
    { .name = "SMCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 2, .opc2 = 6,
      .nv2_redirect_offset = 0x1f0 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 1, 2, 6),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 1, 2, 6),
      .access = PL1_RW, .type = ARM_CP_SME,
      .fieldoffset = offsetof(CPUARMState, vfp.smcr_el[1]),
      .writefn = smcr_write, .raw_writefn = raw_write },
    { .name = "SMCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 2, .opc2 = 6,
      .access = PL2_RW, .type = ARM_CP_SME,
      .fieldoffset = offsetof(CPUARMState, vfp.smcr_el[2]),
      .writefn = smcr_write, .raw_writefn = raw_write },
    { .name = "SMCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 2, .opc2 = 6,
      .access = PL3_RW, .type = ARM_CP_SME,
      .fieldoffset = offsetof(CPUARMState, vfp.smcr_el[3]),
      .writefn = smcr_write, .raw_writefn = raw_write },
    { .name = "SMIDR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 0, .crm = 0, .opc2 = 6,
      .access = PL1_R, .accessfn = access_aa64_tid1,
      /*
       * IMPLEMENTOR = 0 (software)
       * REVISION    = 0 (implementation defined)
       * SMPS        = 0 (no streaming execution priority in QEMU)
       * AFFINITY    = 0 (streaming sve mode not shared with other PEs)
       */
      .type = ARM_CP_CONST, .resetvalue = 0, },
    /*
     * Because SMIDR_EL1.SMPS is 0, SMPRI_EL1 and SMPRIMAP_EL2 are RES 0.
     */
    { .name = "SMPRI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 2, .opc2 = 4,
      .access = PL1_RW, .accessfn = access_smpri,
      .fgt = FGT_NSMPRI_EL1,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "SMPRIMAP_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 2, .opc2 = 5,
      .nv2_redirect_offset = 0x1f8,
      .access = PL2_RW, .accessfn = access_smprimap,
      .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void gpccr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    /* L0GPTSZ is RO; other bits not mentioned are RES0. */
    uint64_t rw_mask = R_GPCCR_PPS_MASK | R_GPCCR_IRGN_MASK |
        R_GPCCR_ORGN_MASK | R_GPCCR_SH_MASK | R_GPCCR_PGS_MASK |
        R_GPCCR_GPC_MASK | R_GPCCR_GPCP_MASK;

    if (cpu_isar_feature(aa64_rme_gpc2, env_archcpu(env))) {
        rw_mask |= R_GPCCR_APPSAA_MASK | R_GPCCR_NSO_MASK |
                   R_GPCCR_SPAD_MASK | R_GPCCR_NSPAD_MASK | R_GPCCR_RLPAD_MASK;
    }

    env->cp15.gpccr_el3 = (value & rw_mask) | (env->cp15.gpccr_el3 & ~rw_mask);
}

static void gpccr_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    env->cp15.gpccr_el3 = FIELD_DP64(0, GPCCR, L0GPTSZ,
                                     env_archcpu(env)->reset_l0gptsz);
}

static const ARMCPRegInfo rme_reginfo[] = {
    { .name = "GPCCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 2, .crm = 1, .opc2 = 6,
      .access = PL3_RW, .writefn = gpccr_write, .resetfn = gpccr_reset,
      .fieldoffset = offsetof(CPUARMState, cp15.gpccr_el3) },
    { .name = "GPTBR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 2, .crm = 1, .opc2 = 4,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.gptbr_el3) },
    { .name = "MFAR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 6, .crm = 0, .opc2 = 5,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.mfar_el3) },
    { .name = "DC_CIPAPA", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 14, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NOP },
};

static const ARMCPRegInfo rme_mte_reginfo[] = {
    { .name = "DC_CIGDPAPA", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 14, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NOP },
};

static void aa64_allint_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    env->pstate = (env->pstate & ~PSTATE_ALLINT) | (value & PSTATE_ALLINT);
}

static uint64_t aa64_allint_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_ALLINT;
}

static CPAccessResult aa64_allint_access(CPUARMState *env,
                                         const ARMCPRegInfo *ri, bool isread)
{
    if (!isread && arm_current_el(env) == 1 &&
        (arm_hcrx_el2_eff(env) & HCRX_TALLINT)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo nmi_reginfo[] = {
    { .name = "ALLINT", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .opc2 = 0, .crn = 4, .crm = 3,
      .type = ARM_CP_NO_RAW,
      .access = PL1_RW, .accessfn = aa64_allint_access,
      .fieldoffset = offsetof(CPUARMState, pstate),
      .writefn = aa64_allint_write, .readfn = aa64_allint_read,
      .resetfn = arm_cp_reset_ignore },
};

static CPAccessResult mecid_access(CPUARMState *env,
                                   const ARMCPRegInfo *ri, bool isread)
{
    int el = arm_current_el(env);

    if (el == 2) {
        if (arm_security_space(env) != ARMSS_Realm) {
            return CP_ACCESS_UNDEFINED;
        }

        if (!(env->cp15.scr_el3 & SCR_MECEN)) {
            return CP_ACCESS_TRAP_EL3;
        }
    }

    return CP_ACCESS_OK;
}

static void mecid_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    value = extract64(value, 0, MECID_WIDTH);
    raw_write(env, ri, value);
}

static CPAccessResult cipae_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    switch (arm_security_space(env)) {
    case ARMSS_Root:  /* EL3 */
    case ARMSS_Realm: /* Realm EL2 */
        return CP_ACCESS_OK;
    default:
        return CP_ACCESS_UNDEFINED;
    }
}

static const ARMCPRegInfo mec_reginfo[] = {
    { .name = "MECIDR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 7, .crn = 10, .crm = 8,
      .access = PL2_R, .type = ARM_CP_CONST | ARM_CP_NV_NO_TRAP,
      .resetvalue = MECID_WIDTH - 1 },
    { .name = "MECID_P0_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 0, .crn = 10, .crm = 8,
      .access = PL2_RW, .type = ARM_CP_NV_NO_TRAP,
      .accessfn = mecid_access, .writefn = mecid_write,
      .fieldoffset = offsetof(CPUARMState, cp15.mecid_p0_el2) },
    { .name = "MECID_A0_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 1, .crn = 10, .crm = 8,
      .access = PL2_RW, .type = ARM_CP_NV_NO_TRAP,
      .accessfn = mecid_access, .writefn = mecid_write,
      .fieldoffset = offsetof(CPUARMState, cp15.mecid_a0_el2) },
    { .name = "MECID_P1_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 2, .crn = 10, .crm = 8,
      .access = PL2_RW, .type = ARM_CP_NV_NO_TRAP,
      .accessfn = mecid_access, .writefn = mecid_write,
      .fieldoffset = offsetof(CPUARMState, cp15.mecid_p1_el2) },
    { .name = "MECID_A1_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 3, .crn = 10, .crm = 8,
      .access = PL2_RW, .type = ARM_CP_NV_NO_TRAP,
      .accessfn = mecid_access, .writefn = mecid_write,
      .fieldoffset = offsetof(CPUARMState, cp15.mecid_a1_el2) },
    { .name = "MECID_RL_A_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .opc2 = 1, .crn = 10, .crm = 10,
      .access = PL3_RW, .accessfn = mecid_access,
      .writefn = mecid_write,
      .fieldoffset = offsetof(CPUARMState, cp15.mecid_rl_a_el3) },
    { .name = "VMECID_P_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 0, .crn = 10, .crm = 9,
      .access = PL2_RW, .type = ARM_CP_NV_NO_TRAP,
      .accessfn = mecid_access, .writefn = mecid_write,
      .fieldoffset = offsetof(CPUARMState, cp15.vmecid_p_el2) },
    { .name = "VMECID_A_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 1, .crn = 10, .crm = 9,
      .access = PL2_RW, .type = ARM_CP_NV_NO_TRAP,
      .accessfn = mecid_access, .writefn = mecid_write,
      .fieldoffset = offsetof(CPUARMState, cp15.vmecid_a_el2) },
    { .name = "DC_CIPAE", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 14, .opc2 = 0,
      .access = PL2_W, .type = ARM_CP_NOP | ARM_CP_NV_NO_TRAP,
      .accessfn = cipae_access },
};

static const ARMCPRegInfo mec_mte_reginfo[] = {
    { .name = "DC_CIGDPAE", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 14, .opc2 = 7,
      .access = PL2_W, .type = ARM_CP_NOP | ARM_CP_NV_NO_TRAP,
      .accessfn = cipae_access },
};

#ifndef CONFIG_USER_ONLY
/*
 * We don't know until after realize whether there's a GICv3
 * attached, and that is what registers the gicv3 sysregs.
 * So we have to fill in the GIC fields in ID_PFR/ID_PFR1_EL1/ID_AA64PFR0_EL1
 * at runtime.
 */
static uint64_t id_pfr1_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t pfr1 = GET_IDREG(&cpu->isar, ID_PFR1);

    if (env->gicv3state) {
        pfr1 = FIELD_DP64(pfr1, ID_PFR1, GIC, 1);
    }
    return pfr1;
}

static uint64_t id_aa64pfr0_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t pfr0 = GET_IDREG(&cpu->isar, ID_AA64PFR0);

    if (env->gicv3state) {
        pfr0 = FIELD_DP64(pfr0, ID_AA64PFR0, GIC, 1);
    }
    return pfr0;
}
#endif

/*
 * Shared logic between LORID and the rest of the LOR* registers.
 * Secure state exclusion has already been dealt with.
 */
static CPAccessResult access_lor_ns(CPUARMState *env,
                                    const ARMCPRegInfo *ri, bool isread)
{
    int el = arm_current_el(env);

    if (el < 2 && (arm_hcr_el2_eff(env) & HCR_TLOR)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.scr_el3 & SCR_TLOR)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_lor_other(CPUARMState *env,
                                       const ARMCPRegInfo *ri, bool isread)
{
    if (arm_is_secure_below_el3(env)) {
        /* UNDEF if SCR_EL3.NS == 0 */
        return CP_ACCESS_UNDEFINED;
    }
    return access_lor_ns(env, ri, isread);
}

/*
 * A trivial implementation of ARMv8.1-LOR leaves all of these
 * registers fixed at 0, which indicates that there are zero
 * supported Limited Ordering regions.
 */
static const ARMCPRegInfo lor_reginfo[] = {
    { .name = "LORSA_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_lor_other,
      .fgt = FGT_LORSA_EL1,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "LOREA_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_lor_other,
      .fgt = FGT_LOREA_EL1,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "LORN_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_lor_other,
      .fgt = FGT_LORN_EL1,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "LORC_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 3,
      .access = PL1_RW, .accessfn = access_lor_other,
      .fgt = FGT_LORC_EL1,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "LORID_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 7,
      .access = PL1_R, .accessfn = access_lor_ns,
      .fgt = FGT_LORID_EL1,
      .type = ARM_CP_CONST, .resetvalue = 0 },
};

static CPAccessResult access_pauth(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    int el = arm_current_el(env);

    if (el < 2 &&
        arm_is_el2_enabled(env) &&
        !(arm_hcr_el2_eff(env) & HCR_APK)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 &&
        arm_feature(env, ARM_FEATURE_EL3) &&
        !(env->cp15.scr_el3 & SCR_APK)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo pauth_reginfo[] = {
    { .name = "APDAKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APDAKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apda.lo) },
    { .name = "APDAKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APDAKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apda.hi) },
    { .name = "APDBKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APDBKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apdb.lo) },
    { .name = "APDBKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 2, .opc2 = 3,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APDBKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apdb.hi) },
    { .name = "APGAKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 3, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APGAKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apga.lo) },
    { .name = "APGAKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 3, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APGAKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apga.hi) },
    { .name = "APIAKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APIAKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apia.lo) },
    { .name = "APIAKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APIAKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apia.hi) },
    { .name = "APIBKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APIBKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apib.lo) },
    { .name = "APIBKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 1, .opc2 = 3,
      .access = PL1_RW, .accessfn = access_pauth,
      .fgt = FGT_APIBKEY,
      .fieldoffset = offsetof(CPUARMState, keys.apib.hi) },
};

static uint64_t rndr_readfn(CPUARMState *env, const ARMCPRegInfo *ri)
{
    Error *err = NULL;
    uint64_t ret;

    /* Success sets NZCV = 0000.  */
    env->NF = env->CF = env->VF = 0, env->ZF = 1;

    if (qemu_guest_getrandom(&ret, sizeof(ret), &err) < 0) {
        /*
         * ??? Failed, for unknown reasons in the crypto subsystem.
         * The best we can do is log the reason and return the
         * timed-out indication to the guest.  There is no reason
         * we know to expect this failure to be transitory, so the
         * guest may well hang retrying the operation.
         */
        qemu_log_mask(LOG_UNIMP, "%s: Crypto failure: %s",
                      ri->name, error_get_pretty(err));
        error_free(err);

        env->ZF = 0; /* NZCF = 0100 */
        return 0;
    }
    return ret;
}

/* We do not support re-seeding, so the two registers operate the same.  */
static const ARMCPRegInfo rndr_reginfo[] = {
    { .name = "RNDR", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_NO_RAW | ARM_CP_SUPPRESS_TB_END | ARM_CP_IO,
      .opc0 = 3, .opc1 = 3, .crn = 2, .crm = 4, .opc2 = 0,
      .access = PL0_R, .readfn = rndr_readfn },
    { .name = "RNDRRS", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_NO_RAW | ARM_CP_SUPPRESS_TB_END | ARM_CP_IO,
      .opc0 = 3, .opc1 = 3, .crn = 2, .crm = 4, .opc2 = 1,
      .access = PL0_R, .readfn = rndr_readfn },
};

static void dccvap_writefn(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
#ifdef CONFIG_TCG
    ARMCPU *cpu = env_archcpu(env);
    /* CTR_EL0 System register -> DminLine, bits [19:16] */
    uint64_t dline_size = 4 << ((cpu->ctr >> 16) & 0xF);
    uint64_t vaddr_in = (uint64_t) value;
    uint64_t vaddr = vaddr_in & ~(dline_size - 1);
    void *haddr;
    int mem_idx = arm_env_mmu_index(env);

    /* This won't be crossing page boundaries */
    haddr = probe_read(env, vaddr, dline_size, mem_idx, GETPC());
    if (haddr) {
#ifndef CONFIG_USER_ONLY

        ram_addr_t offset;
        MemoryRegion *mr;

        /* RCU lock is already being held */
        mr = memory_region_from_host(haddr, &offset);

        if (mr) {
            memory_region_writeback(mr, offset, dline_size);
        }
#endif /*CONFIG_USER_ONLY*/
    }
#else
    /* Handled by hardware accelerator. */
    g_assert_not_reached();
#endif /* CONFIG_TCG */
}

static const ARMCPRegInfo dcpop_reg[] = {
    { .name = "DC_CVAP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 12, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NO_RAW | ARM_CP_SUPPRESS_TB_END,
      .fgt = FGT_DCCVAP,
      .accessfn = aa64_cacheop_poc_access, .writefn = dccvap_writefn },
};

static const ARMCPRegInfo dcpodp_reg[] = {
    { .name = "DC_CVADP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 13, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NO_RAW | ARM_CP_SUPPRESS_TB_END,
      .fgt = FGT_DCCVADP,
      .accessfn = aa64_cacheop_poc_access, .writefn = dccvap_writefn },
};

static CPAccessResult access_aa64_tid5(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if ((arm_current_el(env) < 2) && (arm_hcr_el2_eff(env) & HCR_TID5)) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult access_mte(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    int el = arm_current_el(env);
    if (el < 2 && arm_is_el2_enabled(env)) {
        uint64_t hcr = arm_hcr_el2_eff(env);
        if (!(hcr & HCR_ATA) && (!(hcr & HCR_E2H) || !(hcr & HCR_TGE))) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    if (el < 3 &&
        arm_feature(env, ARM_FEATURE_EL3) &&
        !(env->cp15.scr_el3 & SCR_ATA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_tfsr_el1(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    CPAccessResult nv1 = access_nv1(env, ri, isread);

    if (nv1 != CP_ACCESS_OK) {
        return nv1;
    }
    return access_mte(env, ri, isread);
}

static CPAccessResult access_tfsr_el2(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    /*
     * TFSR_EL2: similar to generic access_mte(), but we need to
     * account for FEAT_NV. At EL1 this must be a FEAT_NV access;
     * if NV2 is enabled then we will redirect this to TFSR_EL1
     * after doing the HCR and SCR ATA traps; otherwise this will
     * be a trap to EL2 and the HCR/SCR traps do not apply.
     */
    int el = arm_current_el(env);

    if (el == 1 && (arm_hcr_el2_eff(env) & HCR_NV2)) {
        return CP_ACCESS_OK;
    }
    if (el < 2 && arm_is_el2_enabled(env)) {
        uint64_t hcr = arm_hcr_el2_eff(env);
        if (!(hcr & HCR_ATA) && (!(hcr & HCR_E2H) || !(hcr & HCR_TGE))) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    if (el < 3 &&
        arm_feature(env, ARM_FEATURE_EL3) &&
        !(env->cp15.scr_el3 & SCR_ATA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static uint64_t tco_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_TCO;
}

static void tco_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t val)
{
    env->pstate = (env->pstate & ~PSTATE_TCO) | (val & PSTATE_TCO);
}

static const ARMCPRegInfo mte_reginfo[] = {
    { .name = "TFSRE0_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 5, .crm = 6, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_mte,
      .fieldoffset = offsetof(CPUARMState, cp15.tfsr_el[0]) },
    { .name = "TFSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 5, .crm = 6, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tfsr_el1,
      .nv2_redirect_offset = 0x190 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 5, 6, 0),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 5, 6, 0),
      .fieldoffset = offsetof(CPUARMState, cp15.tfsr_el[1]) },
    { .name = "TFSR_EL2", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_NV2_REDIRECT,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 6, .opc2 = 0,
      .access = PL2_RW, .accessfn = access_tfsr_el2,
      .fieldoffset = offsetof(CPUARMState, cp15.tfsr_el[2]) },
    { .name = "TFSR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 5, .crm = 6, .opc2 = 0,
      .access = PL3_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.tfsr_el[3]) },
    { .name = "RGSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 5,
      .access = PL1_RW, .accessfn = access_mte,
      .fieldoffset = offsetof(CPUARMState, cp15.rgsr_el1) },
    { .name = "GCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 6,
      .access = PL1_RW, .accessfn = access_mte,
      .fieldoffset = offsetof(CPUARMState, cp15.gcr_el1) },
    { .name = "TCO", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 4, .crm = 2, .opc2 = 7,
      .type = ARM_CP_NO_RAW,
      .access = PL0_RW, .readfn = tco_read, .writefn = tco_write },
    { .name = "DC_IGVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL1_W,
      .fgt = FGT_DCIVAC,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_IGSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 4,
      .fgt = FGT_DCISW,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_IGDVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL1_W,
      .fgt = FGT_DCIVAC,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_IGDSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 6,
      .fgt = FGT_DCISW,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_CGSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 4,
      .fgt = FGT_DCCSW,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_CGDSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 6,
      .fgt = FGT_DCCSW,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_CIGSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 4,
      .fgt = FGT_DCCISW,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_CIGDSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 6,
      .fgt = FGT_DCCISW,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
};

static const ARMCPRegInfo mte_tco_ro_reginfo[] = {
    { .name = "TCO", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 4, .crm = 2, .opc2 = 7,
      .type = ARM_CP_CONST, .access = PL0_RW, },
};

static const ARMCPRegInfo mte_el0_cacheop_reginfo[] = {
    { .name = "DC_CGVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 10, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL0_W,
      .fgt = FGT_DCCVAC,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGDVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 10, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W,
      .fgt = FGT_DCCVAC,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGVAP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 12, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL0_W,
      .fgt = FGT_DCCVAP,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGDVAP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 12, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W,
      .fgt = FGT_DCCVAP,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGVADP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 13, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL0_W,
      .fgt = FGT_DCCVADP,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGDVADP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 13, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W,
      .fgt = FGT_DCCVADP,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CIGVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 14, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL0_W,
      .fgt = FGT_DCCIVAC,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CIGDVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 14, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W,
      .fgt = FGT_DCCIVAC,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_GVA", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 4, .opc2 = 3,
      .access = PL0_W, .type = ARM_CP_DC_GVA,
#ifndef CONFIG_USER_ONLY
      /* Avoid overhead of an access check that always passes in user-mode */
      .accessfn = aa64_zva_access,
      .fgt = FGT_DCZVA,
#endif
    },
    { .name = "DC_GZVA", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 4, .opc2 = 4,
      .access = PL0_W, .type = ARM_CP_DC_GZVA,
#ifndef CONFIG_USER_ONLY
      /* Avoid overhead of an access check that always passes in user-mode */
      .accessfn = aa64_zva_access,
      .fgt = FGT_DCZVA,
#endif
    },
};

static CPAccessResult access_scxtnum(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    uint64_t hcr = arm_hcr_el2_eff(env);
    int el = arm_current_el(env);

    if (el == 0 && !((hcr & HCR_E2H) && (hcr & HCR_TGE))) {
        if (env->cp15.sctlr_el[1] & SCTLR_TSCXT) {
            if (hcr & HCR_TGE) {
                return CP_ACCESS_TRAP_EL2;
            }
            return CP_ACCESS_TRAP_EL1;
        }
    } else if (el < 2 && (env->cp15.sctlr_el[2] & SCTLR_TSCXT)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 2 && arm_is_el2_enabled(env) && !(hcr & HCR_ENSCXT)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3
        && arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.scr_el3 & SCR_ENSCXT)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_scxtnum_el1(CPUARMState *env,
                                         const ARMCPRegInfo *ri,
                                         bool isread)
{
    CPAccessResult nv1 = access_nv1(env, ri, isread);

    if (nv1 != CP_ACCESS_OK) {
        return nv1;
    }
    return access_scxtnum(env, ri, isread);
}

static const ARMCPRegInfo scxtnum_reginfo[] = {
    { .name = "SCXTNUM_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 13, .crm = 0, .opc2 = 7,
      .access = PL0_RW, .accessfn = access_scxtnum,
      .fgt = FGT_SCXTNUM_EL0,
      .fieldoffset = offsetof(CPUARMState, scxtnum_el[0]) },
    { .name = "SCXTNUM_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 13, .crm = 0, .opc2 = 7,
      .access = PL1_RW, .accessfn = access_scxtnum_el1,
      .fgt = FGT_SCXTNUM_EL1,
      .nv2_redirect_offset = 0x188 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 13, 0, 7),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 13, 0, 7),
      .fieldoffset = offsetof(CPUARMState, scxtnum_el[1]) },
    { .name = "SCXTNUM_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 13, .crm = 0, .opc2 = 7,
      .access = PL2_RW, .accessfn = access_scxtnum,
      .fieldoffset = offsetof(CPUARMState, scxtnum_el[2]) },
    { .name = "SCXTNUM_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 13, .crm = 0, .opc2 = 7,
      .access = PL3_RW,
      .fieldoffset = offsetof(CPUARMState, scxtnum_el[3]) },
};

static CPAccessResult access_fgt(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (arm_current_el(env) == 2 &&
        arm_feature(env, ARM_FEATURE_EL3) && !(env->cp15.scr_el3 & SCR_FGTEN)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo fgt_reginfo[] = {
    { .name = "HFGRTR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 4,
      .nv2_redirect_offset = 0x1b8,
      .access = PL2_RW, .accessfn = access_fgt,
      .fieldoffset = offsetof(CPUARMState, cp15.fgt_read[FGTREG_HFGRTR]) },
    { .name = "HFGWTR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 5,
      .nv2_redirect_offset = 0x1c0,
      .access = PL2_RW, .accessfn = access_fgt,
      .fieldoffset = offsetof(CPUARMState, cp15.fgt_write[FGTREG_HFGWTR]) },
    { .name = "HDFGRTR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 3, .crm = 1, .opc2 = 4,
      .nv2_redirect_offset = 0x1d0,
      .access = PL2_RW, .accessfn = access_fgt,
      .fieldoffset = offsetof(CPUARMState, cp15.fgt_read[FGTREG_HDFGRTR]) },
    { .name = "HDFGWTR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 3, .crm = 1, .opc2 = 5,
      .nv2_redirect_offset = 0x1d8,
      .access = PL2_RW, .accessfn = access_fgt,
      .fieldoffset = offsetof(CPUARMState, cp15.fgt_write[FGTREG_HDFGWTR]) },
    { .name = "HFGITR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 6,
      .nv2_redirect_offset = 0x1c8,
      .access = PL2_RW, .accessfn = access_fgt,
      .fieldoffset = offsetof(CPUARMState, cp15.fgt_exec[FGTREG_HFGITR]) },
};

static void vncr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    /*
     * Clear the RES0 bottom 12 bits; this means at runtime we can guarantee
     * that VNCR_EL2 + offset is 64-bit aligned. We don't need to do anything
     * about the RESS bits at the top -- we choose the "generate an EL2
     * translation abort on use" CONSTRAINED UNPREDICTABLE option (i.e. let
     * the ptw.c code detect the resulting invalid address).
     */
    env->cp15.vncr_el2 = value & ~0xfffULL;
}

static const ARMCPRegInfo nv2_reginfo[] = {
    { .name = "VNCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 2, .opc2 = 0,
      .access = PL2_RW,
      .writefn = vncr_write,
      .nv2_redirect_offset = 0xb0,
      .fieldoffset = offsetof(CPUARMState, cp15.vncr_el2) },
};

static CPAccessResult access_predinv(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    int el = arm_current_el(env);

    if (el == 0) {
        uint64_t sctlr = arm_sctlr(env, el);
        if (!(sctlr & SCTLR_EnRCTX)) {
            return CP_ACCESS_TRAP_EL1;
        }
    } else if (el == 1) {
        uint64_t hcr = arm_hcr_el2_eff(env);
        if (hcr & HCR_NV) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo predinv_reginfo[] = {
    { .name = "CFP_RCTX", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 3, .opc2 = 4,
      .fgt = FGT_CFPRCTX,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    { .name = "DVP_RCTX", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 3, .opc2 = 5,
      .fgt = FGT_DVPRCTX,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    { .name = "CPP_RCTX", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 3, .opc2 = 7,
      .fgt = FGT_CPPRCTX,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    /*
     * Note the AArch32 opcodes have a different OPC1.
     */
    { .name = "CFPRCTX", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 3, .opc2 = 4,
      .fgt = FGT_CFPRCTX,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    { .name = "DVPRCTX", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 3, .opc2 = 5,
      .fgt = FGT_DVPRCTX,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    { .name = "CPPRCTX", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 3, .opc2 = 7,
      .fgt = FGT_CPPRCTX,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
};

static uint64_t ccsidr2_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Read the high 32 bits of the current CCSIDR */
    return extract64(ccsidr_read(env, ri), 32, 32);
}

static const ARMCPRegInfo ccsidr2_reginfo[] = {
    { .name = "CCSIDR2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 1, .crn = 0, .crm = 0, .opc2 = 2,
      .access = PL1_R,
      .accessfn = access_tid4,
      .readfn = ccsidr2_read, .type = ARM_CP_NO_RAW },
};

static CPAccessResult access_aa64_tid3(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if ((arm_current_el(env) < 2) && (arm_hcr_el2_eff(env) & HCR_TID3)) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult access_aa32_tid3(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if (arm_feature(env, ARM_FEATURE_V8)) {
        return access_aa64_tid3(env, ri, isread);
    }

    return CP_ACCESS_OK;
}

static CPAccessResult access_jazelle(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TID0)) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult access_joscr_jmcr(CPUARMState *env,
                                        const ARMCPRegInfo *ri, bool isread)
{
    /*
     * HSTR.TJDBX traps JOSCR and JMCR accesses, but it exists only
     * in v7A, not in v8A.
     */
    if (!arm_feature(env, ARM_FEATURE_V8) &&
        arm_current_el(env) < 2 && !arm_is_secure_below_el3(env) &&
        (env->cp15.hstr_el2 & HSTR_TJDBX)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo jazelle_regs[] = {
    { .name = "JIDR",
      .cp = 14, .crn = 0, .crm = 0, .opc1 = 7, .opc2 = 0,
      .access = PL1_R, .accessfn = access_jazelle,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "JOSCR",
      .cp = 14, .crn = 1, .crm = 0, .opc1 = 7, .opc2 = 0,
      .accessfn = access_joscr_jmcr,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "JMCR",
      .cp = 14, .crn = 2, .crm = 0, .opc1 = 7, .opc2 = 0,
      .accessfn = access_joscr_jmcr,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static const ARMCPRegInfo contextidr_el2 = {
    .name = "CONTEXTIDR_EL2", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 4, .crn = 13, .crm = 0, .opc2 = 1,
    .access = PL2_RW,
    .fieldoffset = offsetof(CPUARMState, cp15.contextidr_el[2])
};

static const ARMCPRegInfo vhe_reginfo[] = {
    { .name = "TTBR1_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 0, .opc2 = 1,
      .access = PL2_RW, .writefn = vmsa_tcr_ttbr_el2_write,
      .raw_writefn = raw_write,
      .fieldoffset = offsetof(CPUARMState, cp15.ttbr1_el[2]) },
#ifndef CONFIG_USER_ONLY
    { .name = "CNTHV_CVAL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 3, .opc2 = 2,
      .fieldoffset =
        offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYPVIRT].cval),
      .type = ARM_CP_IO, .access = PL2_RW,
      .writefn = gt_hv_cval_write, .raw_writefn = raw_write },
    { .name = "CNTHV_TVAL_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL2_RW,
      .resetfn = gt_hv_timer_reset,
      .readfn = gt_hv_tval_read, .writefn = gt_hv_tval_write },
    { .name = "CNTHV_CTL_EL2", .state = ARM_CP_STATE_BOTH,
      .type = ARM_CP_IO,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 3, .opc2 = 1,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYPVIRT].ctl),
      .writefn = gt_hv_ctl_write, .raw_writefn = raw_write },
    { .name = "CNTP_CTL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 2, .opc2 = 1,
      .type = ARM_CP_IO | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = access_el1nvpct,
      .nv2_redirect_offset = 0x180 | NV2_REDIR_NO_NV1,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].ctl),
      .writefn = gt_phys_ctl_write, .raw_writefn = raw_write },
    { .name = "CNTV_CTL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 3, .opc2 = 1,
      .type = ARM_CP_IO | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = access_el1nvvct,
      .nv2_redirect_offset = 0x170 | NV2_REDIR_NO_NV1,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].ctl),
      .writefn = gt_virt_ctl_write, .raw_writefn = raw_write },
    { .name = "CNTP_TVAL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = e2h_access,
      .readfn = gt_phys_tval_read, .writefn = gt_phys_tval_write },
    { .name = "CNTV_TVAL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = e2h_access,
      .readfn = gt_virt_tval_read, .writefn = gt_virt_tval_write },
    { .name = "CNTP_CVAL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 2, .opc2 = 2,
      .type = ARM_CP_IO | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].cval),
      .nv2_redirect_offset = 0x178 | NV2_REDIR_NO_NV1,
      .access = PL2_RW, .accessfn = access_el1nvpct,
      .writefn = gt_phys_cval_write, .raw_writefn = raw_write },
    { .name = "CNTV_CVAL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 3, .opc2 = 2,
      .type = ARM_CP_IO | ARM_CP_ALIAS,
      .nv2_redirect_offset = 0x168 | NV2_REDIR_NO_NV1,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].cval),
      .access = PL2_RW, .accessfn = access_el1nvvct,
      .writefn = gt_virt_cval_write, .raw_writefn = raw_write },
#endif
};

/*
 * ACTLR2 and HACTLR2 map to ACTLR_EL1[63:32] and
 * ACTLR_EL2[63:32]. They exist only if the ID_MMFR4.AC2 field
 * is non-zero, which is never for ARMv7, optionally in ARMv8
 * and mandatorily for ARMv8.2 and up.
 * ACTLR2 is banked for S and NS if EL3 is AArch32. Since QEMU's
 * implementation is RAZ/WI we can ignore this detail, as we
 * do for ACTLR.
 */
static const ARMCPRegInfo actlr2_hactlr2_reginfo[] = {
    { .name = "ACTLR2", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .accessfn = access_tacr,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "HACTLR2", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 1, .crm = 0, .opc2 = 3,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
};

static CPAccessResult sctlr2_el2_access(CPUARMState *env,
                                        const ARMCPRegInfo *ri,
                                        bool isread)
{
    if (arm_current_el(env) < 3
        && arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.scr_el3 & SCR_SCTLR2EN)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult sctlr2_el1_access(CPUARMState *env,
                                        const ARMCPRegInfo *ri,
                                        bool isread)
{
    CPAccessResult ret = access_tvm_trvm(env, ri, isread);
    if (ret != CP_ACCESS_OK) {
        return ret;
    }
    if (arm_current_el(env) < 2 && !(arm_hcrx_el2_eff(env) & HCRX_SCTLR2EN)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return sctlr2_el2_access(env, ri, isread);
}

static void sctlr2_el1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    uint64_t valid_mask = 0;

    value &= valid_mask;
    raw_write(env, ri, value);
}

static void sctlr2_el2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    uint64_t valid_mask = 0;

    if (cpu_isar_feature(aa64_mec, env_archcpu(env))) {
        valid_mask |= SCTLR2_EMEC;
    }
    value &= valid_mask;
    raw_write(env, ri, value);
}

static void sctlr2_el3_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    uint64_t valid_mask = 0;

    if (cpu_isar_feature(aa64_mec, env_archcpu(env))) {
        valid_mask |= SCTLR2_EMEC;
    }
    value &= valid_mask;
    raw_write(env, ri, value);
}

static const ARMCPRegInfo sctlr2_reginfo[] = {
    { .name = "SCTLR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .opc2 = 3, .crn = 1, .crm = 0,
      .access = PL1_RW, .accessfn = sctlr2_el1_access,
      .writefn = sctlr2_el1_write, .fgt = FGT_SCTLR_EL1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 1, 0, 3),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 1, 0, 3),
      .nv2_redirect_offset = 0x278 | NV2_REDIR_NV1,
      .fieldoffset = offsetof(CPUARMState, cp15.sctlr2_el[1]) },
    { .name = "SCTLR2_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 3, .crn = 1, .crm = 0,
      .access = PL2_RW, .accessfn = sctlr2_el2_access,
      .writefn = sctlr2_el2_write,
      .fieldoffset = offsetof(CPUARMState, cp15.sctlr2_el[2]) },
    { .name = "SCTLR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .opc2 = 3, .crn = 1, .crm = 0,
      .access = PL3_RW, .writefn = sctlr2_el3_write,
      .fieldoffset = offsetof(CPUARMState, cp15.sctlr2_el[3]) },
};

static CPAccessResult tcr2_el2_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    if (arm_current_el(env) < 3
        && arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.scr_el3 & SCR_TCR2EN)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult tcr2_el1_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    CPAccessResult ret = access_tvm_trvm(env, ri, isread);
    if (ret != CP_ACCESS_OK) {
        return ret;
    }
    if (arm_current_el(env) < 2 && !(arm_hcrx_el2_eff(env) & HCRX_TCR2EN)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return tcr2_el2_access(env, ri, isread);
}

static void tcr2_el1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t valid_mask = 0;

    if (cpu_isar_feature(aa64_s1pie, cpu)) {
        valid_mask |= TCR2_PIE;
    }
    if (cpu_isar_feature(aa64_aie, cpu)) {
        valid_mask |= TCR2_AIE;
    }
    value &= valid_mask;
    raw_write(env, ri, value);
}

static void tcr2_el2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t valid_mask = 0;

    if (cpu_isar_feature(aa64_s1pie, cpu)) {
        valid_mask |= TCR2_PIE;
    }
    if (cpu_isar_feature(aa64_aie, cpu)) {
        valid_mask |= TCR2_AIE;
    }
    if (cpu_isar_feature(aa64_mec, cpu)) {
        valid_mask |= TCR2_AMEC0 | TCR2_AMEC1;
    }
    value &= valid_mask;
    raw_write(env, ri, value);
}

static const ARMCPRegInfo tcr2_reginfo[] = {
    { .name = "TCR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .opc2 = 3, .crn = 2, .crm = 0,
      .access = PL1_RW, .accessfn = tcr2_el1_access,
      .writefn = tcr2_el1_write, .fgt = FGT_TCR_EL1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 2, 0, 3),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 2, 0, 3),
      .nv2_redirect_offset = 0x270 | NV2_REDIR_NV1,
      .fieldoffset = offsetof(CPUARMState, cp15.tcr2_el[1]) },
    { .name = "TCR2_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 3, .crn = 2, .crm = 0,
      .access = PL2_RW, .accessfn = tcr2_el2_access,
      .writefn = tcr2_el2_write,
      .fieldoffset = offsetof(CPUARMState, cp15.tcr2_el[2]) },
};

static CPAccessResult pien_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    if (arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.scr_el3 & SCR_PIEN)
        && arm_current_el(env) < 3) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult pien_el1_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    CPAccessResult ret = access_tvm_trvm(env, ri, isread);
    if (ret == CP_ACCESS_OK) {
        ret = pien_access(env, ri, isread);
    }
    return ret;
}

static const ARMCPRegInfo s1pie_reginfo[] = {
    { .name = "PIR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .opc2 = 3, .crn = 10, .crm = 2,
      .access = PL1_RW, .accessfn = pien_el1_access,
      .fgt = FGT_NPIR_EL1, .nv2_redirect_offset = 0x2a0 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 10, 2, 3),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 10, 2, 3),
      .fieldoffset = offsetof(CPUARMState, cp15.pir_el[1]) },
    { .name = "PIR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 3, .crn = 10, .crm = 2,
      .access = PL2_RW, .accessfn = pien_access,
      .fieldoffset = offsetof(CPUARMState, cp15.pir_el[2]) },
    { .name = "PIR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .opc2 = 3, .crn = 10, .crm = 2,
      .access = PL3_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.pir_el[3]) },
    { .name = "PIRE0_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .opc2 = 2, .crn = 10, .crm = 2,
      .access = PL1_RW, .accessfn = pien_el1_access,
      .fgt = FGT_NPIRE0_EL1, .nv2_redirect_offset = 0x290 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 10, 2, 2),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 10, 2, 2),
      .fieldoffset = offsetof(CPUARMState, cp15.pir_el[0]) },
    { .name = "PIRE0_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 2, .crn = 10, .crm = 2,
      .access = PL2_RW, .accessfn = pien_access,
      .fieldoffset = offsetof(CPUARMState, cp15.pire0_el2) },
};

static const ARMCPRegInfo s2pie_reginfo[] = {
    { .name = "S2PIR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .opc2 = 5, .crn = 10, .crm = 2,
      .access = PL2_RW, .accessfn = pien_access,
      .nv2_redirect_offset = 0x2b0,
      .fieldoffset = offsetof(CPUARMState, cp15.s2pir_el2) },
};

static CPAccessResult aien_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    if (arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.scr_el3 & SCR_AIEN)
        && arm_current_el(env) < 3) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult aien_el1_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    CPAccessResult ret = access_tvm_trvm(env, ri, isread);
    if (ret == CP_ACCESS_OK) {
        ret = aien_access(env, ri, isread);
    }
    return ret;
}

static const ARMCPRegInfo aie_reginfo[] = {
    { .name = "MAIR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .accessfn = aien_el1_access,
      .fgt = FGT_NMAIR2_EL1, .nv2_redirect_offset = 0x280 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 10, 1, 1),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 10, 2, 1),
      .fieldoffset = offsetof(CPUARMState, cp15.mair2_el[1]) },
    { .name = "MAIR2_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 10, .crm = 1, .opc2 = 1,
      .access = PL2_RW, .accessfn = aien_access,
      .fieldoffset = offsetof(CPUARMState, cp15.mair2_el[2]) },
    { .name = "MAIR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 10, .crm = 1, .opc2 = 1,
      .access = PL3_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.mair2_el[3]) },

    { .name = "AMAIR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 3, .opc2 = 1,
      .access = PL1_RW, .accessfn = aien_el1_access,
      .fgt = FGT_NAMAIR2_EL1, .nv2_redirect_offset = 0x288 | NV2_REDIR_NV1,
      .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 10, 3, 1),
      .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 10, 3, 1),
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "AMAIR2_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 10, .crm = 3, .opc2 = 1,
      .access = PL2_RW, .accessfn = aien_access,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "AMAIR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 10, .crm = 3, .opc2 = 1,
      .access = PL3_RW,
      .type = ARM_CP_CONST, .resetvalue = 0 },
};

void register_cp_regs_for_features(ARMCPU *cpu)
{
    /* Register all the coprocessor registers based on feature bits */
    CPUARMState *env = &cpu->env;
    ARMISARegisters *isar = &cpu->isar;

    if (arm_feature(env, ARM_FEATURE_M)) {
        /* M profile has no coprocessor registers */
        return;
    }

    define_arm_cp_regs(cpu, cp_reginfo);
    if (!arm_feature(env, ARM_FEATURE_V8)) {
        /*
         * Must go early as it is full of wildcards that may be
         * overridden by later definitions.
         */
        define_arm_cp_regs(cpu, not_v8_cp_reginfo);
    }

#ifndef CONFIG_USER_ONLY
    if (tcg_enabled()) {
        define_tlb_insn_regs(cpu);
        define_at_insn_regs(cpu);
    }
#endif

    if (arm_feature(env, ARM_FEATURE_V6)) {
        /* The ID registers all have impdef reset values */
        ARMCPRegInfo v6_idregs[] = {
            { .name = "ID_PFR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_PFR0)},
            /*
             * ID_PFR1 is not a plain ARM_CP_CONST because we don't know
             * the value of the GIC field until after we define these regs.
             */
            { .name = "ID_PFR1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_NO_RAW,
              .accessfn = access_aa32_tid3,
#ifdef CONFIG_USER_ONLY
              .type = ARM_CP_CONST,
              .resetvalue = GET_IDREG(isar, ID_PFR1),
#else
              .type = ARM_CP_NO_RAW,
              .accessfn = access_aa32_tid3,
              .readfn = id_pfr1_read,
              .writefn = arm_cp_write_ignore
#endif
            },
            { .name = "ID_DFR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_DFR0)},
            { .name = "ID_AFR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_AFR0)},
            { .name = "ID_MMFR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_MMFR0)},
            { .name = "ID_MMFR1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_MMFR1)},
            { .name = "ID_MMFR2", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_MMFR2)},
            { .name = "ID_MMFR3", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_MMFR3)},
            { .name = "ID_ISAR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_ISAR0)},
            { .name = "ID_ISAR1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_ISAR1)},
            { .name = "ID_ISAR2", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_ISAR2)},
            { .name = "ID_ISAR3", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_ISAR3) },
            { .name = "ID_ISAR4", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_ISAR4) },
            { .name = "ID_ISAR5", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_ISAR5) },
            { .name = "ID_MMFR4", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_MMFR4)},
            { .name = "ID_ISAR6", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = GET_IDREG(isar, ID_ISAR6) },
        };
        define_arm_cp_regs(cpu, v6_idregs);
        define_arm_cp_regs(cpu, v6_cp_reginfo);
    } else {
        define_arm_cp_regs(cpu, not_v6_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V6K)) {
        define_arm_cp_regs(cpu, v6k_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V7)) {
        ARMCPRegInfo clidr = {
            .name = "CLIDR", .state = ARM_CP_STATE_BOTH,
            .opc0 = 3, .crn = 0, .crm = 0, .opc1 = 1, .opc2 = 1,
            .access = PL1_R, .type = ARM_CP_CONST,
            .accessfn = access_tid4,
            .fgt = FGT_CLIDR_EL1,
            .resetvalue = GET_IDREG(isar, CLIDR)
        };
        define_one_arm_cp_reg(cpu, &clidr);
        define_arm_cp_regs(cpu, v7_cp_reginfo);
        define_debug_regs(cpu);
    } else {
        define_arm_cp_regs(cpu, not_v7_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V8)) {
        /*
         * v8 ID registers, which all have impdef reset values.
         * Note that within the ID register ranges the unused slots
         * must all RAZ, not UNDEF; future architecture versions may
         * define new registers here.
         * ID registers which are AArch64 views of the AArch32 ID registers
         * which already existed in v6 and v7 are handled elsewhere,
         * in v6_idregs[].
         */
        int i;
        ARMCPRegInfo v8_idregs[] = {
            /*
             * ID_AA64PFR0_EL1 is not a plain ARM_CP_CONST in system
             * emulation because we don't know the right value for the
             * GIC field until after we define these regs.
             */
            { .name = "ID_AA64PFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 0,
              .access = PL1_R,
#ifdef CONFIG_USER_ONLY
              .type = ARM_CP_CONST,
              .resetvalue = GET_IDREG(isar, ID_AA64PFR0)
#else
              .type = ARM_CP_NO_RAW,
              .accessfn = access_aa64_tid3,
              .readfn = id_aa64pfr0_read,
              .writefn = arm_cp_write_ignore
#endif
            },
            { .name = "ID_AA64PFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64PFR1)},
            { .name = "ID_AA64PFR2_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64PFR2)},
            { .name = "ID_AA64PFR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ZFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64ZFR0)},
            { .name = "ID_AA64SMFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64SMFR0)},
            { .name = "ID_AA64PFR6_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64PFR7_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64DFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64DFR0) },
            { .name = "ID_AA64DFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64DFR1) },
            { .name = "ID_AA64DFR2_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64DFR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64AFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64AFR0) },
            { .name = "ID_AA64AFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64AFR1) },
            { .name = "ID_AA64AFR2_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64AFR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64ISAR0)},
            { .name = "ID_AA64ISAR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64ISAR1)},
            { .name = "ID_AA64ISAR2_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64ISAR2)},
            { .name = "ID_AA64ISAR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR4_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR5_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR6_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR7_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64MMFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64MMFR0)},
            { .name = "ID_AA64MMFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64MMFR1) },
            { .name = "ID_AA64MMFR2_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64MMFR2) },
            { .name = "ID_AA64MMFR3_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_AA64MMFR3) },
            { .name = "ID_AA64MMFR4_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64MMFR5_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64MMFR6_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64MMFR7_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "MVFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.mvfr0 },
            { .name = "MVFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.mvfr1 },
            { .name = "MVFR2_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.mvfr2 },
            /*
             * "0, c0, c3, {0,1,2}" are the encodings corresponding to
             * AArch64 MVFR[012]_EL1. Define the STATE_AA32 encoding
             * as RAZ, since it is in the "reserved for future ID
             * registers, RAZ" part of the AArch32 encoding space.
             */
            { .name = "RES_0_C0_C3_0", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "RES_0_C0_C3_1", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "RES_0_C0_C3_2", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            /*
             * Other encodings in "0, c0, c3, ..." are STATE_BOTH because
             * they're also RAZ for AArch64, and in v8 are gradually
             * being filled with AArch64-view-of-AArch32-ID-register
             * for new ID registers.
             */
            { .name = "RES_0_C0_C3_3", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_PFR2", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_PFR2)},
            { .name = "ID_DFR1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_DFR1)},
            { .name = "ID_MMFR5", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = GET_IDREG(isar, ID_MMFR5)},
            { .name = "RES_0_C0_C3_7", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
        };
#ifdef CONFIG_USER_ONLY
        static const ARMCPRegUserSpaceInfo v8_user_idregs[] = {
            { .name = "ID_AA64PFR0_EL1",
              .exported_bits = R_ID_AA64PFR0_FP_MASK |
                               R_ID_AA64PFR0_ADVSIMD_MASK |
                               R_ID_AA64PFR0_SVE_MASK |
                               R_ID_AA64PFR0_DIT_MASK,
              .fixed_bits = (0x1u << R_ID_AA64PFR0_EL0_SHIFT) |
                            (0x1u << R_ID_AA64PFR0_EL1_SHIFT) },
            { .name = "ID_AA64PFR1_EL1",
              .exported_bits = R_ID_AA64PFR1_BT_MASK |
                               R_ID_AA64PFR1_SSBS_MASK |
                               R_ID_AA64PFR1_MTE_MASK |
                               R_ID_AA64PFR1_SME_MASK },
            { .name = "ID_AA64PFR2_EL1",
              .exported_bits = 0 },
            { .name = "ID_AA64PFR*_EL1_RESERVED",
              .is_glob = true },
            { .name = "ID_AA64ZFR0_EL1",
              .exported_bits = R_ID_AA64ZFR0_SVEVER_MASK |
                               R_ID_AA64ZFR0_AES_MASK |
                               R_ID_AA64ZFR0_BITPERM_MASK |
                               R_ID_AA64ZFR0_BFLOAT16_MASK |
                               R_ID_AA64ZFR0_B16B16_MASK |
                               R_ID_AA64ZFR0_SHA3_MASK |
                               R_ID_AA64ZFR0_SM4_MASK |
                               R_ID_AA64ZFR0_I8MM_MASK |
                               R_ID_AA64ZFR0_F32MM_MASK |
                               R_ID_AA64ZFR0_F64MM_MASK },
            { .name = "ID_AA64SMFR0_EL1",
              .exported_bits = R_ID_AA64SMFR0_F32F32_MASK |
                               R_ID_AA64SMFR0_BI32I32_MASK |
                               R_ID_AA64SMFR0_B16F32_MASK |
                               R_ID_AA64SMFR0_F16F32_MASK |
                               R_ID_AA64SMFR0_I8I32_MASK |
                               R_ID_AA64SMFR0_F16F16_MASK |
                               R_ID_AA64SMFR0_B16B16_MASK |
                               R_ID_AA64SMFR0_I16I32_MASK |
                               R_ID_AA64SMFR0_F64F64_MASK |
                               R_ID_AA64SMFR0_I16I64_MASK |
                               R_ID_AA64SMFR0_SMEVER_MASK |
                               R_ID_AA64SMFR0_FA64_MASK },
            { .name = "ID_AA64MMFR0_EL1",
              .exported_bits = R_ID_AA64MMFR0_ECV_MASK,
              .fixed_bits = (0xfu << R_ID_AA64MMFR0_TGRAN64_SHIFT) |
                            (0xfu << R_ID_AA64MMFR0_TGRAN4_SHIFT) },
            { .name = "ID_AA64MMFR1_EL1",
              .exported_bits = R_ID_AA64MMFR1_AFP_MASK },
            { .name = "ID_AA64MMFR2_EL1",
              .exported_bits = R_ID_AA64MMFR2_AT_MASK },
            { .name = "ID_AA64MMFR3_EL1",
              .exported_bits = 0 },
            { .name = "ID_AA64MMFR*_EL1_RESERVED",
              .is_glob = true },
            { .name = "ID_AA64DFR0_EL1",
              .fixed_bits = (0x6u << R_ID_AA64DFR0_DEBUGVER_SHIFT) },
            { .name = "ID_AA64DFR1_EL1" },
            { .name = "ID_AA64DFR*_EL1_RESERVED",
              .is_glob = true },
            { .name = "ID_AA64AFR*",
              .is_glob = true },
            { .name = "ID_AA64ISAR0_EL1",
              .exported_bits = R_ID_AA64ISAR0_AES_MASK |
                               R_ID_AA64ISAR0_SHA1_MASK |
                               R_ID_AA64ISAR0_SHA2_MASK |
                               R_ID_AA64ISAR0_CRC32_MASK |
                               R_ID_AA64ISAR0_ATOMIC_MASK |
                               R_ID_AA64ISAR0_RDM_MASK |
                               R_ID_AA64ISAR0_SHA3_MASK |
                               R_ID_AA64ISAR0_SM3_MASK |
                               R_ID_AA64ISAR0_SM4_MASK |
                               R_ID_AA64ISAR0_DP_MASK |
                               R_ID_AA64ISAR0_FHM_MASK |
                               R_ID_AA64ISAR0_TS_MASK |
                               R_ID_AA64ISAR0_RNDR_MASK },
            { .name = "ID_AA64ISAR1_EL1",
              .exported_bits = R_ID_AA64ISAR1_DPB_MASK |
                               R_ID_AA64ISAR1_APA_MASK |
                               R_ID_AA64ISAR1_API_MASK |
                               R_ID_AA64ISAR1_JSCVT_MASK |
                               R_ID_AA64ISAR1_FCMA_MASK |
                               R_ID_AA64ISAR1_LRCPC_MASK |
                               R_ID_AA64ISAR1_GPA_MASK |
                               R_ID_AA64ISAR1_GPI_MASK |
                               R_ID_AA64ISAR1_FRINTTS_MASK |
                               R_ID_AA64ISAR1_SB_MASK |
                               R_ID_AA64ISAR1_BF16_MASK |
                               R_ID_AA64ISAR1_DGH_MASK |
                               R_ID_AA64ISAR1_I8MM_MASK },
            { .name = "ID_AA64ISAR2_EL1",
              .exported_bits = R_ID_AA64ISAR2_WFXT_MASK |
                               R_ID_AA64ISAR2_RPRES_MASK |
                               R_ID_AA64ISAR2_GPA3_MASK |
                               R_ID_AA64ISAR2_APA3_MASK |
                               R_ID_AA64ISAR2_MOPS_MASK |
                               R_ID_AA64ISAR2_BC_MASK |
                               R_ID_AA64ISAR2_RPRFM_MASK |
                               R_ID_AA64ISAR2_CSSC_MASK },
            { .name = "ID_AA64ISAR*_EL1_RESERVED",
              .is_glob = true },
        };
        modify_arm_cp_regs(v8_idregs, v8_user_idregs);
#endif
        /*
         * RVBAR_EL1 and RMR_EL1 only implemented if EL1 is the highest EL.
         * TODO: For RMR, a write with bit 1 set should do something with
         * cpu_reset(). In the meantime, "the bit is strictly a request",
         * so we are in spec just ignoring writes.
         */
        if (!arm_feature(env, ARM_FEATURE_EL3) &&
            !arm_feature(env, ARM_FEATURE_EL2)) {
            ARMCPRegInfo el1_reset_regs[] = {
                { .name = "RVBAR_EL1", .state = ARM_CP_STATE_BOTH,
                  .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 1,
                  .access = PL1_R,
                  .fieldoffset = offsetof(CPUARMState, cp15.rvbar) },
                { .name = "RMR_EL1", .state = ARM_CP_STATE_BOTH,
                  .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 2,
                  .access = PL1_RW, .type = ARM_CP_CONST,
                  .resetvalue = arm_feature(env, ARM_FEATURE_AARCH64) }
            };
            define_arm_cp_regs(cpu, el1_reset_regs);
        }
        define_arm_cp_regs(cpu, v8_idregs);
        define_arm_cp_regs(cpu, v8_cp_reginfo);
        if (cpu_isar_feature(aa64_aa32_el1, cpu)) {
            define_arm_cp_regs(cpu, v8_aa32_el1_reginfo);
        }

        for (i = 4; i < 16; i++) {
            /*
             * Encodings in "0, c0, {c4-c7}, {0-7}" are RAZ for AArch32.
             * For pre-v8 cores there are RAZ patterns for these in
             * id_pre_v8_midr_cp_reginfo[]; for v8 we do that here.
             * v8 extends the "must RAZ" part of the ID register space
             * to also cover c0, 0, c{8-15}, {0-7}.
             * These are STATE_AA32 because in the AArch64 sysreg space
             * c4-c7 is where the AArch64 ID registers live (and we've
             * already defined those in v8_idregs[]), and c8-c15 are not
             * "must RAZ" for AArch64.
             */
            g_autofree char *name = g_strdup_printf("RES_0_C0_C%d_X", i);
            ARMCPRegInfo v8_aa32_raz_idregs = {
                .name = name,
                .state = ARM_CP_STATE_AA32,
                .cp = 15, .opc1 = 0, .crn = 0, .crm = i, .opc2 = CP_ANY,
                .access = PL1_R, .type = ARM_CP_CONST,
                .accessfn = access_aa64_tid3,
                .resetvalue = 0 };
            define_one_arm_cp_reg(cpu, &v8_aa32_raz_idregs);
        }
    }

    /*
     * Register the base EL2 cpregs.
     * Pre v8, these registers are implemented only as part of the
     * Virtualization Extensions (EL2 present).  Beginning with v8,
     * if EL2 is missing but EL3 is enabled, mostly these become
     * RES0 from EL3, with some specific exceptions.
     */
    if (arm_feature(env, ARM_FEATURE_EL2)
        || (arm_feature(env, ARM_FEATURE_EL3)
            && arm_feature(env, ARM_FEATURE_V8))) {
        uint64_t vmpidr_def = mpidr_read_val(env);
        ARMCPRegInfo vpidr_regs[] = {
            { .name = "VPIDR", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 0,
              .access = PL2_RW, .accessfn = access_el3_aa32ns,
              .resetvalue = cpu->midr,
              .type = ARM_CP_ALIAS | ARM_CP_EL3_NO_EL2_C_NZ,
              .fieldoffset = offsetoflow32(CPUARMState, cp15.vpidr_el2) },
            { .name = "VPIDR_EL2", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 0,
              .access = PL2_RW, .resetvalue = cpu->midr,
              .type = ARM_CP_EL3_NO_EL2_C_NZ,
              .nv2_redirect_offset = 0x88,
              .fieldoffset = offsetof(CPUARMState, cp15.vpidr_el2) },
            { .name = "VMPIDR", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 5,
              .access = PL2_RW, .accessfn = access_el3_aa32ns,
              .resetvalue = vmpidr_def,
              .type = ARM_CP_ALIAS | ARM_CP_EL3_NO_EL2_C_NZ,
              .fieldoffset = offsetoflow32(CPUARMState, cp15.vmpidr_el2) },
            { .name = "VMPIDR_EL2", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 5,
              .access = PL2_RW, .resetvalue = vmpidr_def,
              .type = ARM_CP_EL3_NO_EL2_C_NZ,
              .nv2_redirect_offset = 0x50,
              .fieldoffset = offsetof(CPUARMState, cp15.vmpidr_el2) },
        };
        /*
         * The only field of MDCR_EL2 that has a defined architectural reset
         * value is MDCR_EL2.HPMN which should reset to the value of PMCR_EL0.N.
         */
        ARMCPRegInfo mdcr_el2 = {
            .name = "MDCR_EL2", .state = ARM_CP_STATE_BOTH, .type = ARM_CP_IO,
            .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 1,
            .writefn = mdcr_el2_write,
            .access = PL2_RW, .resetvalue = pmu_num_counters(env),
            .fieldoffset = offsetof(CPUARMState, cp15.mdcr_el2),
        };
        define_one_arm_cp_reg(cpu, &mdcr_el2);
        define_arm_cp_regs(cpu, vpidr_regs);
        define_arm_cp_regs(cpu, el2_cp_reginfo);
        if (arm_feature(env, ARM_FEATURE_V8)) {
            define_arm_cp_regs(cpu, el2_v8_cp_reginfo);
        }
        if (cpu_isar_feature(aa64_sel2, cpu)) {
            define_arm_cp_regs(cpu, el2_sec_cp_reginfo);
        }
        /*
         * RVBAR_EL2 and RMR_EL2 only implemented if EL2 is the highest EL.
         * See commentary near RMR_EL1.
         */
        if (!arm_feature(env, ARM_FEATURE_EL3)) {
            static const ARMCPRegInfo el2_reset_regs[] = {
                { .name = "RVBAR_EL2", .state = ARM_CP_STATE_AA64,
                  .opc0 = 3, .opc1 = 4, .crn = 12, .crm = 0, .opc2 = 1,
                  .access = PL2_R,
                  .fieldoffset = offsetof(CPUARMState, cp15.rvbar) },
                { .name = "RVBAR", .type = ARM_CP_ALIAS,
                  .cp = 15, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 1,
                  .access = PL2_R,
                  .fieldoffset = offsetof(CPUARMState, cp15.rvbar) },
                { .name = "RMR_EL2", .state = ARM_CP_STATE_AA64,
                  .opc0 = 3, .opc1 = 4, .crn = 12, .crm = 0, .opc2 = 2,
                  .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 1 },
            };
            define_arm_cp_regs(cpu, el2_reset_regs);
        }
    }

    /* Register the base EL3 cpregs. */
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        define_arm_cp_regs(cpu, el3_cp_reginfo);
        ARMCPRegInfo el3_regs[] = {
            { .name = "RVBAR_EL3", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 6, .crn = 12, .crm = 0, .opc2 = 1,
              .access = PL3_R,
              .fieldoffset = offsetof(CPUARMState, cp15.rvbar), },
            { .name = "RMR_EL3", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 6, .crn = 12, .crm = 0, .opc2 = 2,
              .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 1 },
            { .name = "RMR", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 2,
              .access = PL3_RW, .type = ARM_CP_CONST,
              .resetvalue = arm_feature(env, ARM_FEATURE_AARCH64) },
            { .name = "SCTLR_EL3", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 0, .opc2 = 0,
              .access = PL3_RW,
              .raw_writefn = raw_write, .writefn = sctlr_write,
              .fieldoffset = offsetof(CPUARMState, cp15.sctlr_el[3]),
              .resetvalue = cpu->reset_sctlr },
        };

        define_arm_cp_regs(cpu, el3_regs);
    }
    /*
     * The behaviour of NSACR is sufficiently various that we don't
     * try to describe it in a single reginfo:
     *  if EL3 is 64 bit, then trap to EL3 from S EL1,
     *     reads as constant 0xc00 from NS EL1 and NS EL2
     *  if EL3 is 32 bit, then RW at EL3, RO at NS EL1 and NS EL2
     *  if v7 without EL3, register doesn't exist
     *  if v8 without EL3, reads as constant 0xc00 from NS EL1 and NS EL2
     */
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            static const ARMCPRegInfo nsacr = {
                .name = "NSACR", .type = ARM_CP_CONST,
                .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 2,
                .access = PL1_RW, .accessfn = nsacr_access,
                .resetvalue = 0xc00
            };
            define_one_arm_cp_reg(cpu, &nsacr);
        } else {
            static const ARMCPRegInfo nsacr = {
                .name = "NSACR",
                .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 2,
                .access = PL3_RW | PL1_R,
                .resetvalue = 0,
                .fieldoffset = offsetof(CPUARMState, cp15.nsacr)
            };
            define_one_arm_cp_reg(cpu, &nsacr);
        }
    } else {
        if (arm_feature(env, ARM_FEATURE_V8)) {
            static const ARMCPRegInfo nsacr = {
                .name = "NSACR", .type = ARM_CP_CONST,
                .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 2,
                .access = PL1_R,
                .resetvalue = 0xc00
            };
            define_one_arm_cp_reg(cpu, &nsacr);
        }
    }

    if (arm_feature(env, ARM_FEATURE_PMSA)) {
        if (arm_feature(env, ARM_FEATURE_V6)) {
            /* PMSAv6 not implemented */
            assert(arm_feature(env, ARM_FEATURE_V7));
            define_arm_cp_regs(cpu, vmsa_pmsa_cp_reginfo);
            define_arm_cp_regs(cpu, pmsav7_cp_reginfo);
        } else {
            define_arm_cp_regs(cpu, pmsav5_cp_reginfo);
        }
    } else {
        define_arm_cp_regs(cpu, vmsa_pmsa_cp_reginfo);
        define_arm_cp_regs(cpu, vmsa_cp_reginfo);
        /* TTCBR2 is introduced with ARMv8.2-AA32HPD.  */
        if (cpu_isar_feature(aa32_hpd, cpu)) {
            define_one_arm_cp_reg(cpu, &ttbcr2_reginfo);
        }
    }
    if (arm_feature(env, ARM_FEATURE_THUMB2EE)) {
        define_arm_cp_regs(cpu, t2ee_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_GENERIC_TIMER)) {
        define_arm_cp_regs(cpu, generic_timer_cp_reginfo);
    }
    if (cpu_isar_feature(aa64_ecv_traps, cpu)) {
        define_arm_cp_regs(cpu, gen_timer_ecv_cp_reginfo);
    }
#ifndef CONFIG_USER_ONLY
    if (cpu_isar_feature(aa64_ecv, cpu)) {
        define_one_arm_cp_reg(cpu, &gen_timer_cntpoff_reginfo);
    }
#endif
    if (arm_feature(env, ARM_FEATURE_VAPA)) {
        ARMCPRegInfo vapa_cp_reginfo[] = {
            { .name = "PAR", .cp = 15, .crn = 7, .crm = 4, .opc1 = 0, .opc2 = 0,
              .access = PL1_RW, .resetvalue = 0,
              .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.par_s),
                                     offsetoflow32(CPUARMState, cp15.par_ns) },
              .writefn = par_write},
        };

        /*
         * When LPAE exists this 32-bit PAR register is an alias of the
         * 64-bit AArch32 PAR register defined in lpae_cp_reginfo[]
         */
        if (arm_feature(env, ARM_FEATURE_LPAE)) {
            vapa_cp_reginfo[0].type = ARM_CP_ALIAS | ARM_CP_NO_GDB;
        }
        define_arm_cp_regs(cpu, vapa_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_CACHE_TEST_CLEAN)) {
        define_arm_cp_regs(cpu, cache_test_clean_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_CACHE_DIRTY_REG)) {
        define_arm_cp_regs(cpu, cache_dirty_status_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_CACHE_BLOCK_OPS)) {
        define_arm_cp_regs(cpu, cache_block_ops_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_OMAPCP)) {
        define_arm_cp_regs(cpu, omap_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_STRONGARM)) {
        define_arm_cp_regs(cpu, strongarm_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_DUMMY_C15_REGS)) {
        define_arm_cp_regs(cpu, dummy_c15_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_LPAE)) {
        define_arm_cp_regs(cpu, lpae_cp_reginfo);
    }
    if (cpu_isar_feature(aa32_jazelle, cpu)) {
        define_arm_cp_regs(cpu, jazelle_regs);
    }
    /*
     * Slightly awkwardly, the OMAP and StrongARM cores need all of
     * cp15 crn=0 to be writes-ignored, whereas for other cores they should
     * be read-only (ie write causes UNDEF exception).
     */
    {
        ARMCPRegInfo id_pre_v8_midr_cp_reginfo[] = {
            /*
             * Pre-v8 MIDR space.
             * Note that the MIDR isn't a simple constant register because
             * of the TI925 behaviour where writes to another register can
             * cause the MIDR value to change.
             *
             * Unimplemented registers in the c15 0 0 0 space default to
             * MIDR. Define MIDR first as this entire space, then CTR, TCMTR
             * and friends override accordingly.
             */
            { .name = "MIDR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .resetvalue = cpu->midr,
              .writefn = arm_cp_write_ignore, .raw_writefn = raw_write,
              .readfn = midr_read,
              .fieldoffset = offsetof(CPUARMState, cp15.c0_cpuid),
              .type = ARM_CP_OVERRIDE },
            /* crn = 0 op1 = 0 crm = 3..7 : currently unassigned; we RAZ. */
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 3, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 4, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 5, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 6, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 7, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
        };
        ARMCPRegInfo id_v8_midr_cp_reginfo[] = {
            { .name = "MIDR_EL1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 0, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_NO_RAW, .resetvalue = cpu->midr,
              .fgt = FGT_MIDR_EL1,
              .fieldoffset = offsetof(CPUARMState, cp15.c0_cpuid),
              .readfn = midr_read },
            /* crn = 0 op1 = 0 crm = 0 op2 = 7 : AArch32 aliases of MIDR */
            { .name = "MIDR", .type = ARM_CP_ALIAS | ARM_CP_CONST,
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 7,
              .access = PL1_R, .resetvalue = cpu->midr },
            { .name = "REVIDR_EL1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 0, .opc2 = 6,
              .access = PL1_R,
              .accessfn = access_aa64_tid1,
              .fgt = FGT_REVIDR_EL1,
              .type = ARM_CP_CONST, .resetvalue = cpu->revidr },
        };
        ARMCPRegInfo id_v8_midr_alias_cp_reginfo = {
            .name = "MIDR", .type = ARM_CP_ALIAS | ARM_CP_CONST | ARM_CP_NO_GDB,
            .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 4,
            .access = PL1_R, .resetvalue = cpu->midr
        };
        ARMCPRegInfo id_cp_reginfo[] = {
            /* These are common to v8 and pre-v8 */
            { .name = "CTR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 1,
              .access = PL1_R, .accessfn = ctr_el0_access,
              .type = ARM_CP_CONST, .resetvalue = cpu->ctr },
            { .name = "CTR_EL0", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 3, .opc2 = 1, .crn = 0, .crm = 0,
              .access = PL0_R, .accessfn = ctr_el0_access,
              .fgt = FGT_CTR_EL0,
              .type = ARM_CP_CONST, .resetvalue = cpu->ctr },
            /* TCMTR and TLBTR exist in v8 but have no 64-bit versions */
            { .name = "TCMTR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 2,
              .access = PL1_R,
              .accessfn = access_aa32_tid1,
              .type = ARM_CP_CONST, .resetvalue = 0 },
        };
        /* TLBTR is specific to VMSA */
        ARMCPRegInfo id_tlbtr_reginfo = {
              .name = "TLBTR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 3,
              .access = PL1_R,
              .accessfn = access_aa32_tid1,
              .type = ARM_CP_CONST, .resetvalue = 0,
        };
        /* MPUIR is specific to PMSA V6+ */
        ARMCPRegInfo id_mpuir_reginfo = {
              .name = "MPUIR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .resetvalue = cpu->pmsav7_dregion << 8
        };
        /* HMPUIR is specific to PMSA V8 */
        ARMCPRegInfo id_hmpuir_reginfo = {
            .name = "HMPUIR",
            .cp = 15, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 4,
            .access = PL2_R, .type = ARM_CP_CONST,
            .resetvalue = cpu->pmsav8r_hdregion
        };
        static const ARMCPRegInfo crn0_wi_reginfo = {
            .name = "CRN0_WI", .cp = 15, .crn = 0, .crm = CP_ANY,
            .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_W,
            .type = ARM_CP_NOP | ARM_CP_OVERRIDE
        };
#ifdef CONFIG_USER_ONLY
        static const ARMCPRegUserSpaceInfo id_v8_user_midr_cp_reginfo[] = {
            { .name = "MIDR_EL1",
              .exported_bits = R_MIDR_EL1_REVISION_MASK |
                               R_MIDR_EL1_PARTNUM_MASK |
                               R_MIDR_EL1_ARCHITECTURE_MASK |
                               R_MIDR_EL1_VARIANT_MASK |
                               R_MIDR_EL1_IMPLEMENTER_MASK },
            { .name = "REVIDR_EL1" },
        };
        modify_arm_cp_regs(id_v8_midr_cp_reginfo, id_v8_user_midr_cp_reginfo);
#endif
        if (arm_feature(env, ARM_FEATURE_OMAPCP) ||
            arm_feature(env, ARM_FEATURE_STRONGARM)) {
            size_t i;
            /*
             * Register the blanket "writes ignored" value first to cover the
             * whole space. Then update the specific ID registers to allow write
             * access, so that they ignore writes rather than causing them to
             * UNDEF.
             */
            define_one_arm_cp_reg(cpu, &crn0_wi_reginfo);
            for (i = 0; i < ARRAY_SIZE(id_pre_v8_midr_cp_reginfo); ++i) {
                id_pre_v8_midr_cp_reginfo[i].access = PL1_RW;
            }
            for (i = 0; i < ARRAY_SIZE(id_cp_reginfo); ++i) {
                id_cp_reginfo[i].access = PL1_RW;
            }
            id_mpuir_reginfo.access = PL1_RW;
            id_tlbtr_reginfo.access = PL1_RW;
        }
        if (arm_feature(env, ARM_FEATURE_V8)) {
            define_arm_cp_regs(cpu, id_v8_midr_cp_reginfo);
            if (!arm_feature(env, ARM_FEATURE_PMSA)) {
                define_one_arm_cp_reg(cpu, &id_v8_midr_alias_cp_reginfo);
            }
        } else {
            define_arm_cp_regs(cpu, id_pre_v8_midr_cp_reginfo);
        }
        define_arm_cp_regs(cpu, id_cp_reginfo);
        if (!arm_feature(env, ARM_FEATURE_PMSA)) {
            define_one_arm_cp_reg(cpu, &id_tlbtr_reginfo);
        } else if (arm_feature(env, ARM_FEATURE_PMSA) &&
                   arm_feature(env, ARM_FEATURE_V8)) {
            uint32_t i = 0;
            char *tmp_string;

            define_one_arm_cp_reg(cpu, &id_mpuir_reginfo);
            define_one_arm_cp_reg(cpu, &id_hmpuir_reginfo);
            define_arm_cp_regs(cpu, pmsav8r_cp_reginfo);

            /* Register alias is only valid for first 32 indexes */
            for (i = 0; i < MIN(cpu->pmsav7_dregion, 32); ++i) {
                uint8_t crm = 0b1000 | extract32(i, 1, 3);
                uint8_t opc1 = extract32(i, 4, 1);
                uint8_t opc2 = extract32(i, 0, 1) << 2;

                tmp_string = g_strdup_printf("PRBAR%u", i);
                ARMCPRegInfo tmp_prbarn_reginfo = {
                    .name = tmp_string, .type = ARM_CP_ALIAS | ARM_CP_NO_RAW,
                    .cp = 15, .opc1 = opc1, .crn = 6, .crm = crm, .opc2 = opc2,
                    .access = PL1_RW, .resetvalue = 0,
                    .accessfn = access_tvm_trvm,
                    .writefn = pmsav8r_regn_write, .readfn = pmsav8r_regn_read
                };
                define_one_arm_cp_reg(cpu, &tmp_prbarn_reginfo);
                g_free(tmp_string);

                opc2 = extract32(i, 0, 1) << 2 | 0x1;
                tmp_string = g_strdup_printf("PRLAR%u", i);
                ARMCPRegInfo tmp_prlarn_reginfo = {
                    .name = tmp_string, .type = ARM_CP_ALIAS | ARM_CP_NO_RAW,
                    .cp = 15, .opc1 = opc1, .crn = 6, .crm = crm, .opc2 = opc2,
                    .access = PL1_RW, .resetvalue = 0,
                    .accessfn = access_tvm_trvm,
                    .writefn = pmsav8r_regn_write, .readfn = pmsav8r_regn_read
                };
                define_one_arm_cp_reg(cpu, &tmp_prlarn_reginfo);
                g_free(tmp_string);
            }

            /* Register alias is only valid for first 32 indexes */
            for (i = 0; i < MIN(cpu->pmsav8r_hdregion, 32); ++i) {
                uint8_t crm = 0b1000 | extract32(i, 1, 3);
                uint8_t opc1 = 0b100 | extract32(i, 4, 1);
                uint8_t opc2 = extract32(i, 0, 1) << 2;

                tmp_string = g_strdup_printf("HPRBAR%u", i);
                ARMCPRegInfo tmp_hprbarn_reginfo = {
                    .name = tmp_string,
                    .type = ARM_CP_NO_RAW,
                    .cp = 15, .opc1 = opc1, .crn = 6, .crm = crm, .opc2 = opc2,
                    .access = PL2_RW, .resetvalue = 0,
                    .writefn = pmsav8r_regn_write, .readfn = pmsav8r_regn_read
                };
                define_one_arm_cp_reg(cpu, &tmp_hprbarn_reginfo);
                g_free(tmp_string);

                opc2 = extract32(i, 0, 1) << 2 | 0x1;
                tmp_string = g_strdup_printf("HPRLAR%u", i);
                ARMCPRegInfo tmp_hprlarn_reginfo = {
                    .name = tmp_string,
                    .type = ARM_CP_NO_RAW,
                    .cp = 15, .opc1 = opc1, .crn = 6, .crm = crm, .opc2 = opc2,
                    .access = PL2_RW, .resetvalue = 0,
                    .writefn = pmsav8r_regn_write, .readfn = pmsav8r_regn_read
                };
                define_one_arm_cp_reg(cpu, &tmp_hprlarn_reginfo);
                g_free(tmp_string);
            }
        } else if (arm_feature(env, ARM_FEATURE_V7)) {
            define_one_arm_cp_reg(cpu, &id_mpuir_reginfo);
        }
    }

    if (arm_feature(env, ARM_FEATURE_MPIDR)) {
        ARMCPRegInfo mpidr_cp_reginfo[] = {
            { .name = "MPIDR_EL1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 5,
              .fgt = FGT_MPIDR_EL1,
              .access = PL1_R, .readfn = mpidr_read, .type = ARM_CP_NO_RAW },
        };
#ifdef CONFIG_USER_ONLY
        static const ARMCPRegUserSpaceInfo mpidr_user_cp_reginfo[] = {
            { .name = "MPIDR_EL1",
              .fixed_bits = 0x0000000080000000 },
        };
        modify_arm_cp_regs(mpidr_cp_reginfo, mpidr_user_cp_reginfo);
#endif
        define_arm_cp_regs(cpu, mpidr_cp_reginfo);
    }

    if (arm_feature(env, ARM_FEATURE_AUXCR)) {
        ARMCPRegInfo auxcr_reginfo[] = {
            { .name = "ACTLR_EL1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 1,
              .access = PL1_RW, .accessfn = access_tacr,
              .nv2_redirect_offset = 0x118,
              .type = ARM_CP_CONST, .resetvalue = cpu->reset_auxcr },
            { .name = "ACTLR_EL2", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 0, .opc2 = 1,
              .access = PL2_RW, .type = ARM_CP_CONST,
              .resetvalue = 0 },
            { .name = "ACTLR_EL3", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 0, .opc2 = 1,
              .access = PL3_RW, .type = ARM_CP_CONST,
              .resetvalue = 0 },
        };
        define_arm_cp_regs(cpu, auxcr_reginfo);
        if (cpu_isar_feature(aa32_ac2, cpu)) {
            define_arm_cp_regs(cpu, actlr2_hactlr2_reginfo);
        }
    }

    if (arm_feature(env, ARM_FEATURE_CBAR)) {
        /*
         * CBAR is IMPDEF, but common on Arm Cortex-A implementations.
         * There are two flavours:
         *  (1) older 32-bit only cores have a simple 32-bit CBAR
         *  (2) 64-bit cores have a 64-bit CBAR visible to AArch64, plus a
         *      32-bit register visible to AArch32 at a different encoding
         *      to the "flavour 1" register and with the bits rearranged to
         *      be able to squash a 64-bit address into the 32-bit view.
         * We distinguish the two via the ARM_FEATURE_AARCH64 flag, but
         * in future if we support AArch32-only configs of some of the
         * AArch64 cores we might need to add a specific feature flag
         * to indicate cores with "flavour 2" CBAR.
         */
        if (arm_feature(env, ARM_FEATURE_V8)) {
            /* 32 bit view is [31:18] 0...0 [43:32]. */
            uint32_t cbar32 = (extract64(cpu->reset_cbar, 18, 14) << 18)
                | extract64(cpu->reset_cbar, 32, 12);
            ARMCPRegInfo cbar_reginfo[] = {
                { .name = "CBAR",
                  .type = ARM_CP_CONST,
                  .cp = 15, .crn = 15, .crm = 3, .opc1 = 1, .opc2 = 0,
                  .access = PL1_R, .resetvalue = cbar32 },
                { .name = "CBAR_EL1", .state = ARM_CP_STATE_AA64,
                  .type = ARM_CP_CONST,
                  .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 3, .opc2 = 0,
                  .access = PL1_R, .resetvalue = cpu->reset_cbar },
            };
            /* We don't implement a r/w 64 bit CBAR currently */
            assert(arm_feature(env, ARM_FEATURE_CBAR_RO));
            define_arm_cp_regs(cpu, cbar_reginfo);
        } else {
            ARMCPRegInfo cbar = {
                .name = "CBAR",
                .cp = 15, .crn = 15, .crm = 0, .opc1 = 4, .opc2 = 0,
                .access = PL1_R | PL3_W, .resetvalue = cpu->reset_cbar,
                .fieldoffset = offsetof(CPUARMState,
                                        cp15.c15_config_base_address)
            };
            if (arm_feature(env, ARM_FEATURE_CBAR_RO)) {
                cbar.access = PL1_R;
                cbar.fieldoffset = 0;
                cbar.type = ARM_CP_CONST;
            }
            define_one_arm_cp_reg(cpu, &cbar);
        }
    }

    if (arm_feature(env, ARM_FEATURE_VBAR)) {
        static const ARMCPRegInfo vbar_cp_reginfo[] = {
            { .name = "VBAR_EL1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .crn = 12, .crm = 0, .opc1 = 0, .opc2 = 0,
              .access = PL1_RW, .writefn = vbar_write,
              .accessfn = access_nv1,
              .fgt = FGT_VBAR_EL1,
              .nv2_redirect_offset = 0x250 | NV2_REDIR_NV1,
              .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 12, 0, 0),
              .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 12, 0, 0),
              .bank_fieldoffsets = { offsetof(CPUARMState, cp15.vbar_s),
                                     offsetof(CPUARMState, cp15.vbar_ns) },
              .resetvalue = 0 },
        };
        define_arm_cp_regs(cpu, vbar_cp_reginfo);
    }

    /* Generic registers whose values depend on the implementation */
    {
        ARMCPRegInfo sctlr = {
            .name = "SCTLR_EL1", .state = ARM_CP_STATE_BOTH,
            .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 0,
            .access = PL1_RW, .accessfn = access_tvm_trvm,
            .fgt = FGT_SCTLR_EL1,
            .vhe_redir_to_el2 = ENCODE_AA64_CP_REG(3, 4, 1, 0, 0),
            .vhe_redir_to_el01 = ENCODE_AA64_CP_REG(3, 5, 1, 0, 0),
            .nv2_redirect_offset = 0x110 | NV2_REDIR_NV1,
            .bank_fieldoffsets = { offsetof(CPUARMState, cp15.sctlr_s),
                                   offsetof(CPUARMState, cp15.sctlr_ns) },
            .writefn = sctlr_write, .resetvalue = cpu->reset_sctlr,
            .raw_writefn = raw_write,
        };
        define_one_arm_cp_reg(cpu, &sctlr);

        if (arm_feature(env, ARM_FEATURE_PMSA) &&
            arm_feature(env, ARM_FEATURE_V8)) {
            ARMCPRegInfo vsctlr = {
                .name = "VSCTLR", .state = ARM_CP_STATE_AA32,
                .cp = 15, .opc1 = 4, .crn = 2, .crm = 0, .opc2 = 0,
                .access = PL2_RW, .resetvalue = 0x0,
                .fieldoffset = offsetoflow32(CPUARMState, cp15.vsctlr),
            };
            define_one_arm_cp_reg(cpu, &vsctlr);
        }
    }

    if (cpu_isar_feature(aa64_lor, cpu)) {
        define_arm_cp_regs(cpu, lor_reginfo);
    }
    if (cpu_isar_feature(aa64_pan, cpu)) {
        define_one_arm_cp_reg(cpu, &pan_reginfo);
    }
    if (cpu_isar_feature(aa64_uao, cpu)) {
        define_one_arm_cp_reg(cpu, &uao_reginfo);
    }

    if (cpu_isar_feature(aa64_dit, cpu)) {
        define_one_arm_cp_reg(cpu, &dit_reginfo);
    }
    if (cpu_isar_feature(aa64_ssbs, cpu)) {
        define_one_arm_cp_reg(cpu, &ssbs_reginfo);
    }
    if (cpu_isar_feature(any_ras, cpu)) {
        define_arm_cp_regs(cpu, minimal_ras_reginfo);
    }

    if (cpu_isar_feature(aa64_vh, cpu) ||
        cpu_isar_feature(aa64_debugv8p2, cpu)) {
        define_one_arm_cp_reg(cpu, &contextidr_el2);
    }
    if (arm_feature(env, ARM_FEATURE_EL2) && cpu_isar_feature(aa64_vh, cpu)) {
        define_arm_cp_regs(cpu, vhe_reginfo);
    }

    if (cpu_isar_feature(aa64_sve, cpu)) {
        define_arm_cp_regs(cpu, zcr_reginfo);
    }

    if (cpu_isar_feature(aa64_hcx, cpu)) {
        define_one_arm_cp_reg(cpu, &hcrx_el2_reginfo);
    }

    if (cpu_isar_feature(aa64_sme, cpu)) {
        define_arm_cp_regs(cpu, sme_reginfo);
    }
    if (cpu_isar_feature(aa64_pauth, cpu)) {
        define_arm_cp_regs(cpu, pauth_reginfo);
    }
    if (cpu_isar_feature(aa64_rndr, cpu)) {
        define_arm_cp_regs(cpu, rndr_reginfo);
    }
    /* Data Cache clean instructions up to PoP */
    if (cpu_isar_feature(aa64_dcpop, cpu)) {
        define_one_arm_cp_reg(cpu, dcpop_reg);

        if (cpu_isar_feature(aa64_dcpodp, cpu)) {
            define_one_arm_cp_reg(cpu, dcpodp_reg);
        }
    }

    /*
     * If full MTE is enabled, add all of the system registers.
     * If only "instructions available at EL0" are enabled,
     * then define only a RAZ/WI version of PSTATE.TCO.
     */
    if (cpu_isar_feature(aa64_mte, cpu)) {
        ARMCPRegInfo gmid_reginfo = {
            .name = "GMID_EL1", .state = ARM_CP_STATE_AA64,
            .opc0 = 3, .opc1 = 1, .crn = 0, .crm = 0, .opc2 = 4,
            .access = PL1_R, .accessfn = access_aa64_tid5,
            .type = ARM_CP_CONST, .resetvalue = cpu->gm_blocksize,
        };
        define_one_arm_cp_reg(cpu, &gmid_reginfo);
        define_arm_cp_regs(cpu, mte_reginfo);
        define_arm_cp_regs(cpu, mte_el0_cacheop_reginfo);
    } else if (cpu_isar_feature(aa64_mte_insn_reg, cpu)) {
        define_arm_cp_regs(cpu, mte_tco_ro_reginfo);
        define_arm_cp_regs(cpu, mte_el0_cacheop_reginfo);
    }

    if (cpu_isar_feature(aa64_scxtnum, cpu)) {
        define_arm_cp_regs(cpu, scxtnum_reginfo);
    }

    if (cpu_isar_feature(aa64_fgt, cpu)) {
        define_arm_cp_regs(cpu, fgt_reginfo);
    }

    if (cpu_isar_feature(aa64_rme, cpu)) {
        define_arm_cp_regs(cpu, rme_reginfo);
        if (cpu_isar_feature(aa64_mte, cpu)) {
            define_arm_cp_regs(cpu, rme_mte_reginfo);
        }
    }

    if (cpu_isar_feature(aa64_nv2, cpu)) {
        define_arm_cp_regs(cpu, nv2_reginfo);
    }

    if (cpu_isar_feature(aa64_nmi, cpu)) {
        define_arm_cp_regs(cpu, nmi_reginfo);
    }

    if (cpu_isar_feature(aa64_sctlr2, cpu)) {
        define_arm_cp_regs(cpu, sctlr2_reginfo);
    }

    if (cpu_isar_feature(aa64_tcr2, cpu)) {
        define_arm_cp_regs(cpu, tcr2_reginfo);
    }

    if (cpu_isar_feature(aa64_s1pie, cpu)) {
        define_arm_cp_regs(cpu, s1pie_reginfo);
    }
    if (cpu_isar_feature(aa64_s2pie, cpu)) {
        define_arm_cp_regs(cpu, s2pie_reginfo);
    }
    if (cpu_isar_feature(aa64_mec, cpu)) {
        define_arm_cp_regs(cpu, mec_reginfo);
        if (cpu_isar_feature(aa64_mte, cpu)) {
            define_arm_cp_regs(cpu, mec_mte_reginfo);
        }
    }

    if (cpu_isar_feature(aa64_aie, cpu)) {
        define_arm_cp_regs(cpu, aie_reginfo);
    }

    if (cpu_isar_feature(any_predinv, cpu)) {
        define_arm_cp_regs(cpu, predinv_reginfo);
    }

    if (cpu_isar_feature(any_ccidx, cpu)) {
        define_arm_cp_regs(cpu, ccsidr2_reginfo);
    }

    define_pm_cpregs(cpu);
    define_gcs_cpregs(cpu);
}

/*
 * Copy a ARMCPRegInfo structure, allocating it along with the name
 * and an optional suffix to the name.
 */
static ARMCPRegInfo *alloc_cpreg(const ARMCPRegInfo *in, const char *suffix)
{
    const char *name = in->name;
    size_t name_len = strlen(name);
    size_t suff_len = suffix ? strlen(suffix) : 0;
    ARMCPRegInfo *out = g_malloc(sizeof(*in) + name_len + suff_len + 1);
    char *p = (char *)(out + 1);

    *out = *in;
    out->name = p;

    memcpy(p, name, name_len + 1);
    if (suffix) {
        memcpy(p + name_len, suffix, suff_len + 1);
    }
    return out;
}

/*
 * Private utility function for define_one_arm_cp_reg():
 * add a single reginfo struct to the hash table.
 */
static void add_cpreg_to_hashtable(ARMCPU *cpu, ARMCPRegInfo *r,
                                   CPState state, CPSecureState secstate,
                                   uint32_t key)
{
    CPUARMState *env = &cpu->env;
    bool ns = secstate & ARM_CP_SECSTATE_NS;

    /* Overriding of an existing definition must be explicitly requested. */
    if (!(r->type & ARM_CP_OVERRIDE)) {
        const ARMCPRegInfo *oldreg = get_arm_cp_reginfo(cpu->cp_regs, key);
        if (oldreg) {
            assert(oldreg->type & ARM_CP_OVERRIDE);
        }
    }

    {
        bool isbanked = r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1];

        if (isbanked) {
            /*
             * Register is banked (using both entries in array).
             * Overwriting fieldoffset as the array is only used to define
             * banked registers but later only fieldoffset is used.
             */
            r->fieldoffset = r->bank_fieldoffsets[ns];
        }
        if (state == ARM_CP_STATE_AA32) {
            if (isbanked) {
                /*
                 * If the register is banked then we don't need to migrate or
                 * reset the 32-bit instance in certain cases:
                 *
                 * 1) If the register has both 32-bit and 64-bit instances
                 *    then we can count on the 64-bit instance taking care
                 *    of the non-secure bank.
                 * 2) If ARMv8 is enabled then we can count on a 64-bit
                 *    version taking care of the secure bank.  This requires
                 *    that separate 32 and 64-bit definitions are provided.
                 */
                if ((r->state == ARM_CP_STATE_BOTH && ns) ||
                    (arm_feature(env, ARM_FEATURE_V8) && !ns)) {
                    r->type |= ARM_CP_ALIAS;
                }
            } else if ((secstate != r->secure) && !ns) {
                /*
                 * The register is not banked so we only want to allow
                 * migration of the non-secure instance.
                 */
                r->type |= ARM_CP_ALIAS;
            }
        }
    }

    /*
     * For 32-bit AArch32 regs shared with 64-bit AArch64 regs,
     * adjust the field offset for endianness.  This had to be
     * delayed until banked registers were resolved.
     */
    if (HOST_BIG_ENDIAN &&
        state == ARM_CP_STATE_AA32 &&
        r->state == ARM_CP_STATE_BOTH &&
        r->fieldoffset) {
        r->fieldoffset += sizeof(uint32_t);
    }

    /*
     * Special registers (ie NOP/WFI) are never migratable and
     * are not even raw-accessible.
     */
    if (r->type & ARM_CP_SPECIAL_MASK) {
        r->type |= ARM_CP_NO_RAW;
    }

    /*
     * Update fields to match the instantiation, overwiting wildcards
     * such as ARM_CP_STATE_BOTH or ARM_CP_SECSTATE_BOTH.
     */
    r->state = state;
    r->secure = secstate;

    /*
     * Check that raw accesses are either forbidden or handled. Note that
     * we can't assert this earlier because the setup of fieldoffset for
     * banked registers has to be done first.
     */
    if (!(r->type & ARM_CP_NO_RAW)) {
        assert(!raw_accessors_invalid(r));
    }

    g_hash_table_insert(cpu->cp_regs, (gpointer)(uintptr_t)key, r);
}

static void add_cpreg_to_hashtable_aa32(ARMCPU *cpu, ARMCPRegInfo *r)
{
    /*
     * Under AArch32 CP registers can be common
     * (same for secure and non-secure world) or banked.
     */
    ARMCPRegInfo *r_s;
    bool is64 = r->type & ARM_CP_64BIT;
    uint32_t key = ENCODE_CP_REG(r->cp, is64, 0, r->crn,
                                 r->crm, r->opc1, r->opc2);

    assert(!(r->type & ARM_CP_ADD_TLBI_NXS)); /* aa64 only */
    r->vhe_redir_to_el2 = 0;
    r->vhe_redir_to_el01 = 0;

    switch (r->secure) {
    case ARM_CP_SECSTATE_NS:
        key |= CP_REG_AA32_NS_MASK;
        /* fall through */
    case ARM_CP_SECSTATE_S:
        add_cpreg_to_hashtable(cpu, r, ARM_CP_STATE_AA32, r->secure, key);
        break;
    case ARM_CP_SECSTATE_BOTH:
        r_s = alloc_cpreg(r, "_S");
        add_cpreg_to_hashtable(cpu, r_s, ARM_CP_STATE_AA32,
                               ARM_CP_SECSTATE_S, key);

        key |= CP_REG_AA32_NS_MASK;
        add_cpreg_to_hashtable(cpu, r, ARM_CP_STATE_AA32,
                               ARM_CP_SECSTATE_NS, key);
        break;
    default:
        g_assert_not_reached();
    }
}

static void add_cpreg_to_hashtable_aa64(ARMCPU *cpu, ARMCPRegInfo *r)
{
    uint32_t key = ENCODE_AA64_CP_REG(r->opc0, r->opc1,
                                      r->crn, r->crm, r->opc2);

    if ((r->type & ARM_CP_ADD_TLBI_NXS) &&
        cpu_isar_feature(aa64_xs, cpu)) {
        /*
         * This is a TLBI insn which has an NXS variant. The
         * NXS variant is at the same encoding except that
         * crn is +1, and has the same behaviour except for
         * fine-grained trapping. Add the NXS insn here and
         * then fall through to add the normal register.
         * add_cpreg_to_hashtable() copies the cpreg struct
         * and name that it is passed, so it's OK to use
         * a local struct here.
         */
        ARMCPRegInfo *nxs_ri = alloc_cpreg(r, "NXS");
        uint32_t nxs_key;

        assert(nxs_ri->crn < 0xf);
        nxs_ri->crn++;
        /* Also increment the CRN field inside the key value */
        nxs_key = key + (1 << CP_REG_ARM64_SYSREG_CRN_SHIFT);
        if (nxs_ri->fgt) {
            nxs_ri->fgt |= R_FGT_NXS_MASK;
        }

        add_cpreg_to_hashtable(cpu, nxs_ri, ARM_CP_STATE_AA64,
                               ARM_CP_SECSTATE_NS, nxs_key);
    }

    if (!r->vhe_redir_to_el01) {
        assert(!r->vhe_redir_to_el2);
    } else if (!arm_feature(&cpu->env, ARM_FEATURE_EL2) ||
               !cpu_isar_feature(aa64_vh, cpu)) {
        r->vhe_redir_to_el2 = 0;
        r->vhe_redir_to_el01 = 0;
    } else {
        /* Create the FOO_EL12 alias. */
        ARMCPRegInfo *r2 = alloc_cpreg(r, "2");
        uint32_t key2 = r->vhe_redir_to_el01;

        /*
         * Clear EL1 redirection on the FOO_EL1 reg;
         * Clear EL2 redirection on the FOO_EL12 reg;
         * Install redirection from FOO_EL12 back to FOO_EL1.
         */
        r->vhe_redir_to_el01 = 0;
        r2->vhe_redir_to_el2 = 0;
        r2->vhe_redir_to_el01 = key;

        r2->type |= ARM_CP_ALIAS | ARM_CP_NO_RAW;
        /* Remove PL1/PL0 access, leaving PL2/PL3 R/W in place.  */
        r2->access &= PL2_RW | PL3_RW;
        /* The new_reg op fields are as per new_key, not the target reg */
        r2->crn = (key2 & CP_REG_ARM64_SYSREG_CRN_MASK)
            >> CP_REG_ARM64_SYSREG_CRN_SHIFT;
        r2->crm = (key2 & CP_REG_ARM64_SYSREG_CRM_MASK)
            >> CP_REG_ARM64_SYSREG_CRM_SHIFT;
        r2->opc0 = (key2 & CP_REG_ARM64_SYSREG_OP0_MASK)
            >> CP_REG_ARM64_SYSREG_OP0_SHIFT;
        r2->opc1 = (key2 & CP_REG_ARM64_SYSREG_OP1_MASK)
            >> CP_REG_ARM64_SYSREG_OP1_SHIFT;
        r2->opc2 = (key2 & CP_REG_ARM64_SYSREG_OP2_MASK)
            >> CP_REG_ARM64_SYSREG_OP2_SHIFT;

        /* Non-redirected access to this register will abort. */
        r2->readfn = NULL;
        r2->writefn = NULL;
        r2->raw_readfn = NULL;
        r2->raw_writefn = NULL;
        r2->accessfn = NULL;
        r2->fieldoffset = 0;

        /*
         * If the _EL1 register is redirected to memory by FEAT_NV2,
         * then it shares the offset with the _EL12 register,
         * and which one is redirected depends on HCR_EL2.NV1.
         */
        if (r2->nv2_redirect_offset) {
            assert(r2->nv2_redirect_offset & NV2_REDIR_NV1);
            r2->nv2_redirect_offset &= ~NV2_REDIR_NV1;
            r2->nv2_redirect_offset |= NV2_REDIR_NO_NV1;
        }
        add_cpreg_to_hashtable(cpu, r2, ARM_CP_STATE_AA64,
                               ARM_CP_SECSTATE_NS, key2);
    }

    add_cpreg_to_hashtable(cpu, r, ARM_CP_STATE_AA64,
                           ARM_CP_SECSTATE_NS, key);
}

void define_one_arm_cp_reg(ARMCPU *cpu, const ARMCPRegInfo *r)
{
    /*
     * Define implementations of coprocessor registers.
     * We store these in a hashtable because typically
     * there are less than 150 registers in a space which
     * is 16*16*16*8*8 = 262144 in size.
     * Wildcarding is supported for the crm, opc1 and opc2 fields.
     * If a register is defined twice then the second definition is
     * used, so this can be used to define some generic registers and
     * then override them with implementation specific variations.
     * At least one of the original and the second definition should
     * include ARM_CP_OVERRIDE in its type bits -- this is just a guard
     * against accidental use.
     *
     * The state field defines whether the register is to be
     * visible in the AArch32 or AArch64 execution state. If the
     * state is set to ARM_CP_STATE_BOTH then we synthesise a
     * reginfo structure for the AArch32 view, which sees the lower
     * 32 bits of the 64 bit register.
     *
     * Only registers visible in AArch64 may set r->opc0; opc0 cannot
     * be wildcarded. AArch64 registers are always considered to be 64
     * bits; the ARM_CP_64BIT* flag applies only to the AArch32 view of
     * the register, if any.
     */
    int crmmin = (r->crm == CP_ANY) ? 0 : r->crm;
    int crmmax = (r->crm == CP_ANY) ? 15 : r->crm;
    int opc1min = (r->opc1 == CP_ANY) ? 0 : r->opc1;
    int opc1max = (r->opc1 == CP_ANY) ? 7 : r->opc1;
    int opc2min = (r->opc2 == CP_ANY) ? 0 : r->opc2;
    int opc2max = (r->opc2 == CP_ANY) ? 7 : r->opc2;
    int cp = r->cp;
    ARMCPRegInfo r_const;
    CPUARMState *env = &cpu->env;

    /*
     * AArch64 regs are all 64 bit so ARM_CP_64BIT is meaningless.
     * Moreover, the encoding test just following in general prevents
     * shared encoding so ARM_CP_STATE_BOTH won't work either.
     */
    assert(r->state == ARM_CP_STATE_AA32 || !(r->type & ARM_CP_64BIT));
    /* AArch32 64-bit registers have only CRm and Opc1 fields. */
    assert(!(r->type & ARM_CP_64BIT) || !(r->opc2 || r->crn));
    /* op0 only exists in the AArch64 encodings */
    assert(r->state != ARM_CP_STATE_AA32 || r->opc0 == 0);

    /*
     * This API is only for Arm's system coprocessors (14 and 15) or
     * (M-profile or v7A-and-earlier only) for implementation defined
     * coprocessors in the range 0..7.  Our decode assumes this, since
     * 8..13 can be used for other insns including VFP and Neon. See
     * valid_cp() in translate.c.  Assert here that we haven't tried
     * to use an invalid coprocessor number.
     */
    switch (r->state) {
    case ARM_CP_STATE_BOTH:
        /*
         * If the cp field is left unset, assume cp15.
         * Otherwise apply the same rules as AA32.
         */
        if (cp == 0) {
            cp = 15;
            break;
        }
        /* fall through */
    case ARM_CP_STATE_AA32:
        if (arm_feature(&cpu->env, ARM_FEATURE_V8) &&
            !arm_feature(&cpu->env, ARM_FEATURE_M)) {
            assert(cp >= 14 && cp <= 15);
        } else {
            assert(cp < 8 || (cp >= 14 && cp <= 15));
        }
        break;
    case ARM_CP_STATE_AA64:
        assert(cp == 0);
        break;
    default:
        g_assert_not_reached();
    }
    /*
     * The AArch64 pseudocode CheckSystemAccess() specifies that op1
     * encodes a minimum access level for the register. We roll this
     * runtime check into our general permission check code, so check
     * here that the reginfo's specified permissions are strict enough
     * to encompass the generic architectural permission check.
     */
    if (r->state != ARM_CP_STATE_AA32) {
        CPAccessRights mask;
        switch (r->opc1) {
        case 0:
            /* min_EL EL1, but some accessible to EL0 via kernel ABI */
            mask = PL0U_R | PL1_RW;
            break;
        case 1: case 2:
            /* min_EL EL1 */
            mask = PL1_RW;
            break;
        case 3:
            /* min_EL EL0 */
            mask = PL0_RW;
            break;
        case 4:
        case 5:
            /* min_EL EL2 */
            mask = PL2_RW;
            break;
        case 6:
            /* min_EL EL3 */
            mask = PL3_RW;
            break;
        case 7:
            /* min_EL EL1, secure mode only (we don't check the latter) */
            mask = PL1_RW;
            break;
        default:
            /* broken reginfo with out-of-range opc1 */
            g_assert_not_reached();
        }
        /* assert our permissions are not too lax (stricter is fine) */
        assert((r->access & ~mask) == 0);
    }

    /*
     * Check that the register definition has enough info to handle
     * reads and writes if they are permitted.
     */
    if (!(r->type & (ARM_CP_SPECIAL_MASK | ARM_CP_CONST))) {
        if (r->access & PL3_R) {
            assert((r->fieldoffset ||
                   (r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1])) ||
                   r->readfn);
        }
        if (r->access & PL3_W) {
            assert((r->fieldoffset ||
                   (r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1])) ||
                   r->writefn);
        }
    }

    /*
     * Eliminate registers that are not present because the EL is missing.
     * Doing this here makes it easier to put all registers for a given
     * feature into the same ARMCPRegInfo array and define them all at once.
     */
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        /*
         * An EL2 register without EL2 but with EL3 is (usually) RES0.
         * See rule RJFFP in section D1.1.3 of DDI0487H.a.
         */
        int min_el = ctz32(r->access) / 2;
        if (min_el == 2 && !arm_feature(env, ARM_FEATURE_EL2)) {
            if (r->type & ARM_CP_EL3_NO_EL2_UNDEF) {
                return;
            }
            if (!(r->type & ARM_CP_EL3_NO_EL2_KEEP)) {
                /* This should not have been a very special register. */
                int old_special = r->type & ARM_CP_SPECIAL_MASK;
                assert(old_special == 0 || old_special == ARM_CP_NOP);

                r_const = *r;

                /*
                 * Set the special function to CONST, retaining the other flags.
                 * This is important for e.g. ARM_CP_SVE so that we still
                 * take the SVE trap if CPTR_EL3.EZ == 0.
                 */
                r_const.type = (r->type & ~ARM_CP_SPECIAL_MASK) | ARM_CP_CONST;
                /*
                 * Usually, these registers become RES0, but there are a few
                 * special cases like VPIDR_EL2 which have a constant non-zero
                 * value with writes ignored.
                 */
                if (!(r->type & ARM_CP_EL3_NO_EL2_C_NZ)) {
                    r_const.resetvalue = 0;
                }
                /*
                 * ARM_CP_CONST has precedence, so removing the callbacks and
                 * offsets are not strictly necessary, but it is potentially
                 * less confusing to debug later.
                 */
                r_const.readfn = NULL;
                r_const.writefn = NULL;
                r_const.raw_readfn = NULL;
                r_const.raw_writefn = NULL;
                r_const.resetfn = NULL;
                r_const.fieldoffset = 0;
                r_const.bank_fieldoffsets[0] = 0;
                r_const.bank_fieldoffsets[1] = 0;

                r = &r_const;
            }
        }
    } else {
        CPAccessRights max_el = (arm_feature(env, ARM_FEATURE_EL2)
                                 ? PL2_RW : PL1_RW);
        if ((r->access & max_el) == 0) {
            return;
        }
    }

    for (int crm = crmmin; crm <= crmmax; crm++) {
        for (int opc1 = opc1min; opc1 <= opc1max; opc1++) {
            for (int opc2 = opc2min; opc2 <= opc2max; opc2++) {
                ARMCPRegInfo *r2 = alloc_cpreg(r, NULL);
                ARMCPRegInfo *r3;

                /*
                 * By convention, for wildcarded registers only the first
                 * entry is used for migration; the others are marked as
                 * ALIAS so we don't try to transfer the register
                 * multiple times.
                 */
                if (crm != crmmin || opc1 != opc1min || opc2 != opc2min) {
                    r2->type |= ARM_CP_ALIAS | ARM_CP_NO_GDB;
                }

                /* Overwrite CP_ANY with the instantiation. */
                r2->crm = crm;
                r2->opc1 = opc1;
                r2->opc2 = opc2;

                switch (r->state) {
                case ARM_CP_STATE_AA32:
                    add_cpreg_to_hashtable_aa32(cpu, r2);
                    break;
                case ARM_CP_STATE_AA64:
                    add_cpreg_to_hashtable_aa64(cpu, r2);
                    break;
                case ARM_CP_STATE_BOTH:
                    r3 = alloc_cpreg(r2, NULL);
                    r2->cp = cp;
                    add_cpreg_to_hashtable_aa32(cpu, r2);
                    r3->cp = 0;
                    add_cpreg_to_hashtable_aa64(cpu, r3);
                    break;
                default:
                    g_assert_not_reached();
                }
            }
        }
    }
}

/* Define a whole list of registers */
void define_arm_cp_regs_len(ARMCPU *cpu, const ARMCPRegInfo *regs, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        define_one_arm_cp_reg(cpu, regs + i);
    }
}

/*
 * Modify ARMCPRegInfo for access from userspace.
 *
 * This is a data driven modification directed by
 * ARMCPRegUserSpaceInfo. All registers become ARM_CP_CONST as
 * user-space cannot alter any values and dynamic values pertaining to
 * execution state are hidden from user space view anyway.
 */
void modify_arm_cp_regs_with_len(ARMCPRegInfo *regs, size_t regs_len,
                                 const ARMCPRegUserSpaceInfo *mods,
                                 size_t mods_len)
{
    for (size_t mi = 0; mi < mods_len; ++mi) {
        const ARMCPRegUserSpaceInfo *m = mods + mi;
        GPatternSpec *pat = NULL;

        if (m->is_glob) {
            pat = g_pattern_spec_new(m->name);
        }
        for (size_t ri = 0; ri < regs_len; ++ri) {
            ARMCPRegInfo *r = regs + ri;

            if (pat && g_pattern_match_string(pat, r->name)) {
                r->type = ARM_CP_CONST;
                r->access = PL0U_R;
                r->resetvalue = 0;
                /* continue */
            } else if (strcmp(r->name, m->name) == 0) {
                r->type = ARM_CP_CONST;
                r->access = PL0U_R;
                r->resetvalue &= m->exported_bits;
                r->resetvalue |= m->fixed_bits;
                break;
            }
        }
        if (pat) {
            g_pattern_spec_free(pat);
        }
    }
}

const ARMCPRegInfo *get_arm_cp_reginfo(GHashTable *cpregs, uint32_t encoded_cp)
{
    return g_hash_table_lookup(cpregs, (gpointer)(uintptr_t)encoded_cp);
}

void arm_cp_write_ignore(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    /* Helper coprocessor write function for write-ignore registers */
}

uint64_t arm_cp_read_zero(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Helper coprocessor write function for read-as-zero registers */
    return 0;
}

void arm_cp_reset_ignore(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Helper coprocessor reset function for do-nothing-on-reset registers */
}

static int bad_mode_switch(CPUARMState *env, int mode, CPSRWriteType write_type)
{
    /*
     * Return true if it is not valid for us to switch to
     * this CPU mode (ie all the UNPREDICTABLE cases in
     * the ARM ARM CPSRWriteByInstr pseudocode).
     */

    /* Changes to or from Hyp via MSR and CPS are illegal. */
    if (write_type == CPSRWriteByInstr &&
        ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_HYP ||
         mode == ARM_CPU_MODE_HYP)) {
        return 1;
    }

    switch (mode) {
    case ARM_CPU_MODE_USR:
        return 0;
    case ARM_CPU_MODE_SYS:
    case ARM_CPU_MODE_SVC:
    case ARM_CPU_MODE_ABT:
    case ARM_CPU_MODE_UND:
    case ARM_CPU_MODE_IRQ:
    case ARM_CPU_MODE_FIQ:
        /*
         * Note that we don't implement the IMPDEF NSACR.RFR which in v7
         * allows FIQ mode to be Secure-only. (In v8 this doesn't exist.)
         */
        /*
         * If HCR.TGE is set then changes from Monitor to NS PL1 via MSR
         * and CPS are treated as illegal mode changes.
         */
        if (write_type == CPSRWriteByInstr &&
            (env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_MON &&
            (arm_hcr_el2_eff(env) & HCR_TGE)) {
            return 1;
        }
        return 0;
    case ARM_CPU_MODE_HYP:
        return !arm_is_el2_enabled(env) || arm_current_el(env) < 2;
    case ARM_CPU_MODE_MON:
        return arm_current_el(env) < 3;
    default:
        return 1;
    }
}

uint32_t cpsr_read(CPUARMState *env)
{
    int ZF;
    ZF = (env->ZF == 0);
    return env->uncached_cpsr | (env->NF & 0x80000000) | (ZF << 30) |
        (env->CF << 29) | ((env->VF & 0x80000000) >> 3) | (env->QF << 27)
        | (env->thumb << 5) | ((env->condexec_bits & 3) << 25)
        | ((env->condexec_bits & 0xfc) << 8)
        | (env->GE << 16) | (env->daif & CPSR_AIF);
}

void cpsr_write(CPUARMState *env, uint32_t val, uint32_t mask,
                CPSRWriteType write_type)
{
    uint32_t changed_daif;
    bool rebuild_hflags = (write_type != CPSRWriteRaw) &&
        (mask & (CPSR_M | CPSR_E | CPSR_IL));

    if (mask & CPSR_NZCV) {
        env->ZF = (~val) & CPSR_Z;
        env->NF = val;
        env->CF = (val >> 29) & 1;
        env->VF = (val << 3) & 0x80000000;
    }
    if (mask & CPSR_Q) {
        env->QF = ((val & CPSR_Q) != 0);
    }
    if (mask & CPSR_T) {
        env->thumb = ((val & CPSR_T) != 0);
    }
    if (mask & CPSR_IT_0_1) {
        env->condexec_bits &= ~3;
        env->condexec_bits |= (val >> 25) & 3;
    }
    if (mask & CPSR_IT_2_7) {
        env->condexec_bits &= 3;
        env->condexec_bits |= (val >> 8) & 0xfc;
    }
    if (mask & CPSR_GE) {
        env->GE = (val >> 16) & 0xf;
    }

    /*
     * In a V7 implementation that includes the security extensions but does
     * not include Virtualization Extensions the SCR.FW and SCR.AW bits control
     * whether non-secure software is allowed to change the CPSR_F and CPSR_A
     * bits respectively.
     *
     * In a V8 implementation, it is permitted for privileged software to
     * change the CPSR A/F bits regardless of the SCR.AW/FW bits.
     */
    if (write_type != CPSRWriteRaw && !arm_feature(env, ARM_FEATURE_V8) &&
        arm_feature(env, ARM_FEATURE_EL3) &&
        !arm_feature(env, ARM_FEATURE_EL2) &&
        !arm_is_secure(env)) {

        changed_daif = (env->daif ^ val) & mask;

        if (changed_daif & CPSR_A) {
            /*
             * Check to see if we are allowed to change the masking of async
             * abort exceptions from a non-secure state.
             */
            if (!(env->cp15.scr_el3 & SCR_AW)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Ignoring attempt to switch CPSR_A flag from "
                              "non-secure world with SCR.AW bit clear\n");
                mask &= ~CPSR_A;
            }
        }

        if (changed_daif & CPSR_F) {
            /*
             * Check to see if we are allowed to change the masking of FIQ
             * exceptions from a non-secure state.
             */
            if (!(env->cp15.scr_el3 & SCR_FW)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Ignoring attempt to switch CPSR_F flag from "
                              "non-secure world with SCR.FW bit clear\n");
                mask &= ~CPSR_F;
            }

            /*
             * Check whether non-maskable FIQ (NMFI) support is enabled.
             * If this bit is set software is not allowed to mask
             * FIQs, but is allowed to set CPSR_F to 0.
             */
            if ((A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_NMFI) &&
                (val & CPSR_F)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Ignoring attempt to enable CPSR_F flag "
                              "(non-maskable FIQ [NMFI] support enabled)\n");
                mask &= ~CPSR_F;
            }
        }
    }

    env->daif &= ~(CPSR_AIF & mask);
    env->daif |= val & CPSR_AIF & mask;

    if (write_type != CPSRWriteRaw &&
        ((env->uncached_cpsr ^ val) & mask & CPSR_M)) {
        if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_USR) {
            /*
             * Note that we can only get here in USR mode if this is a
             * gdb stub write; for this case we follow the architectural
             * behaviour for guest writes in USR mode of ignoring an attempt
             * to switch mode. (Those are caught by translate.c for writes
             * triggered by guest instructions.)
             */
            mask &= ~CPSR_M;
        } else if (bad_mode_switch(env, val & CPSR_M, write_type)) {
            /*
             * Attempt to switch to an invalid mode: this is UNPREDICTABLE in
             * v7, and has defined behaviour in v8:
             *  + leave CPSR.M untouched
             *  + allow changes to the other CPSR fields
             *  + set PSTATE.IL
             * For user changes via the GDB stub, we don't set PSTATE.IL,
             * as this would be unnecessarily harsh for a user error.
             */
            mask &= ~CPSR_M;
            if (write_type != CPSRWriteByGDBStub &&
                arm_feature(env, ARM_FEATURE_V8)) {
                mask |= CPSR_IL;
                val |= CPSR_IL;
            }
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Illegal AArch32 mode switch attempt from %s to %s\n",
                          aarch32_mode_name(env->uncached_cpsr),
                          aarch32_mode_name(val));
        } else {
            qemu_log_mask(CPU_LOG_INT, "%s %s to %s PC 0x%" PRIx32 "\n",
                          write_type == CPSRWriteExceptionReturn ?
                          "Exception return from AArch32" :
                          "AArch32 mode switch from",
                          aarch32_mode_name(env->uncached_cpsr),
                          aarch32_mode_name(val), env->regs[15]);
            switch_mode(env, val & CPSR_M);
        }
    }
    mask &= ~CACHED_CPSR_BITS;
    env->uncached_cpsr = (env->uncached_cpsr & ~mask) | (val & mask);
    if (tcg_enabled() && rebuild_hflags) {
        arm_rebuild_hflags(env);
    }
}

#ifdef CONFIG_USER_ONLY

static void switch_mode(CPUARMState *env, int mode)
{
    ARMCPU *cpu = env_archcpu(env);

    if (mode != ARM_CPU_MODE_USR) {
        cpu_abort(CPU(cpu), "Tried to switch out of user mode\n");
    }
}

uint32_t arm_phys_excp_target_el(CPUState *cs, uint32_t excp_idx,
                                 uint32_t cur_el, bool secure)
{
    return 1;
}

void aarch64_sync_64_to_32(CPUARMState *env)
{
    g_assert_not_reached();
}

#else

static void switch_mode(CPUARMState *env, int mode)
{
    int old_mode;
    int i;

    old_mode = env->uncached_cpsr & CPSR_M;
    if (mode == old_mode) {
        return;
    }

    if (old_mode == ARM_CPU_MODE_FIQ) {
        memcpy(env->fiq_regs, env->regs + 8, 5 * sizeof(uint32_t));
        memcpy(env->regs + 8, env->usr_regs, 5 * sizeof(uint32_t));
    } else if (mode == ARM_CPU_MODE_FIQ) {
        memcpy(env->usr_regs, env->regs + 8, 5 * sizeof(uint32_t));
        memcpy(env->regs + 8, env->fiq_regs, 5 * sizeof(uint32_t));
    }

    i = bank_number(old_mode);
    env->banked_r13[i] = env->regs[13];
    env->banked_spsr[i] = env->spsr;

    i = bank_number(mode);
    env->regs[13] = env->banked_r13[i];
    env->spsr = env->banked_spsr[i];

    env->banked_r14[r14_bank_number(old_mode)] = env->regs[14];
    env->regs[14] = env->banked_r14[r14_bank_number(mode)];
}

/*
 * Physical Interrupt Target EL Lookup Table
 *
 * [ From ARM ARM section G1.13.4 (Table G1-15) ]
 *
 * The below multi-dimensional table is used for looking up the target
 * exception level given numerous condition criteria.  Specifically, the
 * target EL is based on SCR and HCR routing controls as well as the
 * currently executing EL and secure state.
 *
 *    Dimensions:
 *    target_el_table[2][2][2][2][2][4]
 *                    |  |  |  |  |  +--- Current EL
 *                    |  |  |  |  +------ Non-secure(0)/Secure(1)
 *                    |  |  |  +--------- HCR mask override
 *                    |  |  +------------ SCR exec state control
 *                    |  +--------------- SCR mask override
 *                    +------------------ 32-bit(0)/64-bit(1) EL3
 *
 *    The table values are as such:
 *    0-3 = EL0-EL3
 *     -1 = Cannot occur
 *
 * The ARM ARM target EL table includes entries indicating that an "exception
 * is not taken".  The two cases where this is applicable are:
 *    1) An exception is taken from EL3 but the SCR does not have the exception
 *    routed to EL3.
 *    2) An exception is taken from EL2 but the HCR does not have the exception
 *    routed to EL2.
 * In these two cases, the below table contain a target of EL1.  This value is
 * returned as it is expected that the consumer of the table data will check
 * for "target EL >= current EL" to ensure the exception is not taken.
 *
 *            SCR     HCR
 *         64  EA     AMO                 From
 *        BIT IRQ     IMO      Non-secure         Secure
 *        EL3 FIQ  RW FMO   EL0 EL1 EL2 EL3   EL0 EL1 EL2 EL3
 */
static const int8_t target_el_table[2][2][2][2][2][4] = {
    {{{{/* 0   0   0   0 */{ 1,  1,  2, -1 },{ 3, -1, -1,  3 },},
       {/* 0   0   0   1 */{ 2,  2,  2, -1 },{ 3, -1, -1,  3 },},},
      {{/* 0   0   1   0 */{ 1,  1,  2, -1 },{ 3, -1, -1,  3 },},
       {/* 0   0   1   1 */{ 2,  2,  2, -1 },{ 3, -1, -1,  3 },},},},
     {{{/* 0   1   0   0 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},
       {/* 0   1   0   1 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},},
      {{/* 0   1   1   0 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},
       {/* 0   1   1   1 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},},},},
    {{{{/* 1   0   0   0 */{ 1,  1,  2, -1 },{ 1,  1, -1,  1 },},
       {/* 1   0   0   1 */{ 2,  2,  2, -1 },{ 2,  2, -1,  1 },},},
      {{/* 1   0   1   0 */{ 1,  1,  1, -1 },{ 1,  1,  1,  1 },},
       {/* 1   0   1   1 */{ 2,  2,  2, -1 },{ 2,  2,  2,  1 },},},},
     {{{/* 1   1   0   0 */{ 3,  3,  3, -1 },{ 3,  3, -1,  3 },},
       {/* 1   1   0   1 */{ 3,  3,  3, -1 },{ 3,  3, -1,  3 },},},
      {{/* 1   1   1   0 */{ 3,  3,  3, -1 },{ 3,  3,  3,  3 },},
       {/* 1   1   1   1 */{ 3,  3,  3, -1 },{ 3,  3,  3,  3 },},},},},
};

/*
 * Determine the target EL for physical exceptions
 */
uint32_t arm_phys_excp_target_el(CPUState *cs, uint32_t excp_idx,
                                 uint32_t cur_el, bool secure)
{
    CPUARMState *env = cpu_env(cs);
    bool rw;
    bool scr;
    bool hcr;
    int target_el;
    /* Is the highest EL AArch64? */
    bool is64 = arm_feature(env, ARM_FEATURE_AARCH64);
    uint64_t hcr_el2;

    if (arm_feature(env, ARM_FEATURE_EL3)) {
        rw = arm_scr_rw_eff(env);
    } else {
        /*
         * Either EL2 is the highest EL (and so the EL2 register width
         * is given by is64); or there is no EL2 or EL3, in which case
         * the value of 'rw' does not affect the table lookup anyway.
         */
        rw = is64;
    }

    hcr_el2 = arm_hcr_el2_eff(env);
    switch (excp_idx) {
    case EXCP_IRQ:
    case EXCP_NMI:
        scr = ((env->cp15.scr_el3 & SCR_IRQ) == SCR_IRQ);
        hcr = hcr_el2 & HCR_IMO;
        break;
    case EXCP_FIQ:
        scr = ((env->cp15.scr_el3 & SCR_FIQ) == SCR_FIQ);
        hcr = hcr_el2 & HCR_FMO;
        break;
    default:
        scr = ((env->cp15.scr_el3 & SCR_EA) == SCR_EA);
        hcr = hcr_el2 & HCR_AMO;
        break;
    };

    /*
     * For these purposes, TGE and AMO/IMO/FMO both force the
     * interrupt to EL2.  Fold TGE into the bit extracted above.
     */
    hcr |= (hcr_el2 & HCR_TGE) != 0;

    /* Perform a table-lookup for the target EL given the current state */
    target_el = target_el_table[is64][scr][rw][hcr][secure][cur_el];

    assert(target_el > 0);

    return target_el;
}

void arm_log_exception(CPUState *cs)
{
    int idx = cs->exception_index;

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        const char *exc = NULL;
        static const char * const excnames[] = {
            [EXCP_UDEF] = "Undefined Instruction",
            [EXCP_SWI] = "SVC",
            [EXCP_PREFETCH_ABORT] = "Prefetch Abort",
            [EXCP_DATA_ABORT] = "Data Abort",
            [EXCP_IRQ] = "IRQ",
            [EXCP_FIQ] = "FIQ",
            [EXCP_BKPT] = "Breakpoint",
            [EXCP_EXCEPTION_EXIT] = "QEMU v7M exception exit",
            [EXCP_KERNEL_TRAP] = "QEMU intercept of kernel commpage",
            [EXCP_HVC] = "Hypervisor Call",
            [EXCP_HYP_TRAP] = "Hypervisor Trap",
            [EXCP_SMC] = "Secure Monitor Call",
            [EXCP_VIRQ] = "Virtual IRQ",
            [EXCP_VFIQ] = "Virtual FIQ",
            [EXCP_SEMIHOST] = "Semihosting call",
            [EXCP_NOCP] = "v7M NOCP UsageFault",
            [EXCP_INVSTATE] = "v7M INVSTATE UsageFault",
            [EXCP_STKOF] = "v8M STKOF UsageFault",
            [EXCP_LAZYFP] = "v7M exception during lazy FP stacking",
            [EXCP_LSERR] = "v8M LSERR UsageFault",
            [EXCP_UNALIGNED] = "v7M UNALIGNED UsageFault",
            [EXCP_DIVBYZERO] = "v7M DIVBYZERO UsageFault",
            [EXCP_VSERR] = "Virtual SERR",
            [EXCP_GPC] = "Granule Protection Check",
            [EXCP_NMI] = "NMI",
            [EXCP_VINMI] = "Virtual IRQ NMI",
            [EXCP_VFNMI] = "Virtual FIQ NMI",
            [EXCP_MON_TRAP] = "Monitor Trap",
        };

        if (idx >= 0 && idx < ARRAY_SIZE(excnames)) {
            exc = excnames[idx];
        }
        if (!exc) {
            exc = "unknown";
        }
        qemu_log_mask(CPU_LOG_INT, "Taking exception %d [%s] on CPU %d\n",
                      idx, exc, cs->cpu_index);
    }
}

/*
 * Function used to synchronize QEMU's AArch64 register set with AArch32
 * register set.  This is necessary when switching between AArch32 and AArch64
 * execution state.
 */
void aarch64_sync_32_to_64(CPUARMState *env)
{
    int i;
    uint32_t mode = env->uncached_cpsr & CPSR_M;

    /* We can blanket copy R[0:7] to X[0:7] */
    for (i = 0; i < 8; i++) {
        env->xregs[i] = env->regs[i];
    }

    /*
     * Unless we are in FIQ mode, x8-x12 come from the user registers r8-r12.
     * Otherwise, they come from the banked user regs.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 8; i < 13; i++) {
            env->xregs[i] = env->usr_regs[i - 8];
        }
    } else {
        for (i = 8; i < 13; i++) {
            env->xregs[i] = env->regs[i];
        }
    }

    /*
     * Registers x13-x23 are the various mode SP and FP registers. Registers
     * r13 and r14 are only copied if we are in that mode, otherwise we copy
     * from the mode banked register.
     */
    if (mode == ARM_CPU_MODE_USR || mode == ARM_CPU_MODE_SYS) {
        env->xregs[13] = env->regs[13];
        env->xregs[14] = env->regs[14];
    } else {
        env->xregs[13] = env->banked_r13[bank_number(ARM_CPU_MODE_USR)];
        /* HYP is an exception in that it is copied from r14 */
        if (mode == ARM_CPU_MODE_HYP) {
            env->xregs[14] = env->regs[14];
        } else {
            env->xregs[14] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_USR)];
        }
    }

    if (mode == ARM_CPU_MODE_HYP) {
        env->xregs[15] = env->regs[13];
    } else {
        env->xregs[15] = env->banked_r13[bank_number(ARM_CPU_MODE_HYP)];
    }

    if (mode == ARM_CPU_MODE_IRQ) {
        env->xregs[16] = env->regs[14];
        env->xregs[17] = env->regs[13];
    } else {
        env->xregs[16] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_IRQ)];
        env->xregs[17] = env->banked_r13[bank_number(ARM_CPU_MODE_IRQ)];
    }

    if (mode == ARM_CPU_MODE_SVC) {
        env->xregs[18] = env->regs[14];
        env->xregs[19] = env->regs[13];
    } else {
        env->xregs[18] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_SVC)];
        env->xregs[19] = env->banked_r13[bank_number(ARM_CPU_MODE_SVC)];
    }

    if (mode == ARM_CPU_MODE_ABT) {
        env->xregs[20] = env->regs[14];
        env->xregs[21] = env->regs[13];
    } else {
        env->xregs[20] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_ABT)];
        env->xregs[21] = env->banked_r13[bank_number(ARM_CPU_MODE_ABT)];
    }

    if (mode == ARM_CPU_MODE_UND) {
        env->xregs[22] = env->regs[14];
        env->xregs[23] = env->regs[13];
    } else {
        env->xregs[22] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_UND)];
        env->xregs[23] = env->banked_r13[bank_number(ARM_CPU_MODE_UND)];
    }

    /*
     * Registers x24-x30 are mapped to r8-r14 in FIQ mode.  If we are in FIQ
     * mode, then we can copy from r8-r14.  Otherwise, we copy from the
     * FIQ bank for r8-r14.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 24; i < 31; i++) {
            env->xregs[i] = env->regs[i - 16];   /* X[24:30] <- R[8:14] */
        }
    } else {
        for (i = 24; i < 29; i++) {
            env->xregs[i] = env->fiq_regs[i - 24];
        }
        env->xregs[29] = env->banked_r13[bank_number(ARM_CPU_MODE_FIQ)];
        env->xregs[30] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_FIQ)];
    }

    env->pc = env->regs[15];
}

/*
 * Function used to synchronize QEMU's AArch32 register set with AArch64
 * register set.  This is necessary when switching between AArch32 and AArch64
 * execution state.
 */
void aarch64_sync_64_to_32(CPUARMState *env)
{
    int i;
    uint32_t mode = env->uncached_cpsr & CPSR_M;

    /* We can blanket copy X[0:7] to R[0:7] */
    for (i = 0; i < 8; i++) {
        env->regs[i] = env->xregs[i];
    }

    /*
     * Unless we are in FIQ mode, r8-r12 come from the user registers x8-x12.
     * Otherwise, we copy x8-x12 into the banked user regs.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 8; i < 13; i++) {
            env->usr_regs[i - 8] = env->xregs[i];
        }
    } else {
        for (i = 8; i < 13; i++) {
            env->regs[i] = env->xregs[i];
        }
    }

    /*
     * Registers r13 & r14 depend on the current mode.
     * If we are in a given mode, we copy the corresponding x registers to r13
     * and r14.  Otherwise, we copy the x register to the banked r13 and r14
     * for the mode.
     */
    if (mode == ARM_CPU_MODE_USR || mode == ARM_CPU_MODE_SYS) {
        env->regs[13] = env->xregs[13];
        env->regs[14] = env->xregs[14];
    } else {
        env->banked_r13[bank_number(ARM_CPU_MODE_USR)] = env->xregs[13];

        /*
         * HYP is an exception in that it does not have its own banked r14 but
         * shares the USR r14
         */
        if (mode == ARM_CPU_MODE_HYP) {
            env->regs[14] = env->xregs[14];
        } else {
            env->banked_r14[r14_bank_number(ARM_CPU_MODE_USR)] = env->xregs[14];
        }
    }

    if (mode == ARM_CPU_MODE_HYP) {
        env->regs[13] = env->xregs[15];
    } else {
        env->banked_r13[bank_number(ARM_CPU_MODE_HYP)] = env->xregs[15];
    }

    if (mode == ARM_CPU_MODE_IRQ) {
        env->regs[14] = env->xregs[16];
        env->regs[13] = env->xregs[17];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_IRQ)] = env->xregs[16];
        env->banked_r13[bank_number(ARM_CPU_MODE_IRQ)] = env->xregs[17];
    }

    if (mode == ARM_CPU_MODE_SVC) {
        env->regs[14] = env->xregs[18];
        env->regs[13] = env->xregs[19];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_SVC)] = env->xregs[18];
        env->banked_r13[bank_number(ARM_CPU_MODE_SVC)] = env->xregs[19];
    }

    if (mode == ARM_CPU_MODE_ABT) {
        env->regs[14] = env->xregs[20];
        env->regs[13] = env->xregs[21];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_ABT)] = env->xregs[20];
        env->banked_r13[bank_number(ARM_CPU_MODE_ABT)] = env->xregs[21];
    }

    if (mode == ARM_CPU_MODE_UND) {
        env->regs[14] = env->xregs[22];
        env->regs[13] = env->xregs[23];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_UND)] = env->xregs[22];
        env->banked_r13[bank_number(ARM_CPU_MODE_UND)] = env->xregs[23];
    }

    /*
     * Registers x24-x30 are mapped to r8-r14 in FIQ mode.  If we are in FIQ
     * mode, then we can copy to r8-r14.  Otherwise, we copy to the
     * FIQ bank for r8-r14.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 24; i < 31; i++) {
            env->regs[i - 16] = env->xregs[i];   /* X[24:30] -> R[8:14] */
        }
    } else {
        for (i = 24; i < 29; i++) {
            env->fiq_regs[i - 24] = env->xregs[i];
        }
        env->banked_r13[bank_number(ARM_CPU_MODE_FIQ)] = env->xregs[29];
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_FIQ)] = env->xregs[30];
    }

    env->regs[15] = env->pc;
}

static void take_aarch32_exception(CPUARMState *env, int new_mode,
                                   uint32_t mask, uint32_t offset,
                                   uint32_t newpc)
{
    int new_el;

    /* Change the CPU state so as to actually take the exception. */
    switch_mode(env, new_mode);

    /*
     * For exceptions taken to AArch32 we must clear the SS bit in both
     * PSTATE and in the old-state value we save to SPSR_<mode>, so zero it now.
     */
    env->pstate &= ~PSTATE_SS;
    env->spsr = cpsr_read(env);
    /* Clear IT bits.  */
    env->condexec_bits = 0;
    /* Switch to the new mode, and to the correct instruction set.  */
    env->uncached_cpsr = (env->uncached_cpsr & ~CPSR_M) | new_mode;

    /* This must be after mode switching. */
    new_el = arm_current_el(env);

    /* Set new mode endianness */
    env->uncached_cpsr &= ~CPSR_E;
    if (env->cp15.sctlr_el[new_el] & SCTLR_EE) {
        env->uncached_cpsr |= CPSR_E;
    }
    /* J and IL must always be cleared for exception entry */
    env->uncached_cpsr &= ~(CPSR_IL | CPSR_J);
    env->daif |= mask;

    if (cpu_isar_feature(aa32_ssbs, env_archcpu(env))) {
        if (env->cp15.sctlr_el[new_el] & SCTLR_DSSBS_32) {
            env->uncached_cpsr |= CPSR_SSBS;
        } else {
            env->uncached_cpsr &= ~CPSR_SSBS;
        }
    }

    if (new_mode == ARM_CPU_MODE_HYP) {
        env->thumb = (env->cp15.sctlr_el[2] & SCTLR_TE) != 0;
        env->elr_el[2] = env->regs[15];
    } else {
        /* CPSR.PAN is normally preserved preserved unless...  */
        if (cpu_isar_feature(aa32_pan, env_archcpu(env))) {
            switch (new_el) {
            case 3:
                if (!arm_is_secure_below_el3(env)) {
                    /* ... the target is EL3, from non-secure state.  */
                    env->uncached_cpsr &= ~CPSR_PAN;
                    break;
                }
                /* ... the target is EL3, from secure state ... */
                /* fall through */
            case 1:
                /* ... the target is EL1 and SCTLR.SPAN is 0.  */
                if (!(env->cp15.sctlr_el[new_el] & SCTLR_SPAN)) {
                    env->uncached_cpsr |= CPSR_PAN;
                }
                break;
            }
        }
        /*
         * this is a lie, as there was no c1_sys on V4T/V5, but who cares
         * and we should just guard the thumb mode on V4
         */
        if (arm_feature(env, ARM_FEATURE_V4T)) {
            env->thumb =
                (A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_TE) != 0;
        }
        env->regs[14] = env->regs[15] + offset;
    }
    env->regs[15] = newpc;

    if (tcg_enabled()) {
        arm_rebuild_hflags(env);
    }
}

void arm_do_plugin_vcpu_discon_cb(CPUState *cs, uint64_t from)
{
    switch (cs->exception_index) {
    case EXCP_IRQ:
    case EXCP_VIRQ:
    case EXCP_NMI:
    case EXCP_VINMI:
    case EXCP_FIQ:
    case EXCP_VFIQ:
    case EXCP_VFNMI:
    case EXCP_VSERR:
        qemu_plugin_vcpu_interrupt_cb(cs, from);
        break;
    default:
        qemu_plugin_vcpu_exception_cb(cs, from);
    }
}

static void arm_cpu_do_interrupt_aarch32_hyp(CPUState *cs)
{
    /*
     * Handle exception entry to Hyp mode; this is sufficiently
     * different to entry to other AArch32 modes that we handle it
     * separately here.
     *
     * The vector table entry used is always the 0x14 Hyp mode entry point,
     * unless this is an UNDEF/SVC/HVC/abort taken from Hyp to Hyp.
     * The offset applied to the preferred return address is always zero
     * (see DDI0487C.a section G1.12.3).
     * PSTATE A/I/F masks are set based only on the SCR.EA/IRQ/FIQ values.
     */
    uint32_t addr, mask;
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (cs->exception_index) {
    case EXCP_UDEF:
        addr = 0x04;
        break;
    case EXCP_SWI:
        addr = 0x08;
        break;
    case EXCP_BKPT:
        /* Fall through to prefetch abort.  */
    case EXCP_PREFETCH_ABORT:
        env->cp15.ifar_s = env->exception.vaddress;
        qemu_log_mask(CPU_LOG_INT, "...with HIFAR 0x%x\n",
                      (uint32_t)env->exception.vaddress);
        addr = 0x0c;
        break;
    case EXCP_DATA_ABORT:
        env->cp15.dfar_s = env->exception.vaddress;
        qemu_log_mask(CPU_LOG_INT, "...with HDFAR 0x%x\n",
                      (uint32_t)env->exception.vaddress);
        addr = 0x10;
        break;
    case EXCP_IRQ:
        addr = 0x18;
        break;
    case EXCP_FIQ:
        addr = 0x1c;
        break;
    case EXCP_HVC:
        addr = 0x08;
        break;
    case EXCP_HYP_TRAP:
        addr = 0x14;
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
    }

    if (cs->exception_index != EXCP_IRQ && cs->exception_index != EXCP_FIQ) {
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            /*
             * QEMU syndrome values are v8-style. v7 has the IL bit
             * UNK/SBZP for "field not valid" cases, where v8 uses RES1.
             * If this is a v7 CPU, squash the IL bit in those cases.
             */
            if (cs->exception_index == EXCP_PREFETCH_ABORT ||
                (cs->exception_index == EXCP_DATA_ABORT &&
                 !(env->exception.syndrome & ARM_EL_ISV)) ||
                syn_get_ec(env->exception.syndrome) == EC_UNCATEGORIZED) {
                env->exception.syndrome &= ~ARM_EL_IL;
            }
        }
        env->cp15.esr_el[2] = env->exception.syndrome;
    }

    if (arm_current_el(env) != 2 && addr < 0x14) {
        addr = 0x14;
    }

    mask = 0;
    if (!(env->cp15.scr_el3 & SCR_EA)) {
        mask |= CPSR_A;
    }
    if (!(env->cp15.scr_el3 & SCR_IRQ)) {
        mask |= CPSR_I;
    }
    if (!(env->cp15.scr_el3 & SCR_FIQ)) {
        mask |= CPSR_F;
    }

    addr += env->cp15.hvbar;

    take_aarch32_exception(env, ARM_CPU_MODE_HYP, mask, 0, addr);
}

static void arm_cpu_do_interrupt_aarch32(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t addr;
    uint32_t mask;
    int new_mode;
    uint32_t offset;
    uint32_t moe;

    /* If this is a debug exception we must update the DBGDSCR.MOE bits */
    switch (syn_get_ec(env->exception.syndrome)) {
    case EC_BREAKPOINT:
    case EC_BREAKPOINT_SAME_EL:
        moe = 1;
        break;
    case EC_WATCHPOINT:
    case EC_WATCHPOINT_SAME_EL:
        moe = 10;
        break;
    case EC_AA32_BKPT:
        moe = 3;
        break;
    case EC_VECTORCATCH:
        moe = 5;
        break;
    default:
        moe = 0;
        break;
    }

    if (moe) {
        env->cp15.mdscr_el1 = deposit64(env->cp15.mdscr_el1, 2, 4, moe);
    }

    if (env->exception.target_el == 2) {
        /* Debug exceptions are reported differently on AArch32 */
        switch (syn_get_ec(env->exception.syndrome)) {
        case EC_BREAKPOINT:
        case EC_BREAKPOINT_SAME_EL:
        case EC_AA32_BKPT:
        case EC_VECTORCATCH:
            env->exception.syndrome = syn_insn_abort(arm_current_el(env) == 2,
                                                     0, 0, 0x22);
            break;
        case EC_WATCHPOINT:
            env->exception.syndrome = syn_set_ec(env->exception.syndrome,
                                                 EC_DATAABORT);
            break;
        case EC_WATCHPOINT_SAME_EL:
            env->exception.syndrome = syn_set_ec(env->exception.syndrome,
                                                 EC_DATAABORT_SAME_EL);
            break;
        }
        arm_cpu_do_interrupt_aarch32_hyp(cs);
        return;
    }

    switch (cs->exception_index) {
    case EXCP_UDEF:
        new_mode = ARM_CPU_MODE_UND;
        addr = 0x04;
        mask = CPSR_I;
        if (env->thumb) {
            offset = 2;
        } else {
            offset = 4;
        }
        break;
    case EXCP_SWI:
        new_mode = ARM_CPU_MODE_SVC;
        addr = 0x08;
        mask = CPSR_I;
        /* The PC already points to the next instruction.  */
        offset = 0;
        break;
    case EXCP_BKPT:
        /* Fall through to prefetch abort.  */
    case EXCP_PREFETCH_ABORT:
        A32_BANKED_CURRENT_REG_SET(env, ifsr, env->exception.fsr);
        A32_BANKED_CURRENT_REG_SET(env, ifar, env->exception.vaddress);
        qemu_log_mask(CPU_LOG_INT, "...with IFSR 0x%x IFAR 0x%x\n",
                      env->exception.fsr, (uint32_t)env->exception.vaddress);
        new_mode = ARM_CPU_MODE_ABT;
        addr = 0x0c;
        mask = CPSR_A | CPSR_I;
        offset = 4;
        break;
    case EXCP_DATA_ABORT:
        A32_BANKED_CURRENT_REG_SET(env, dfsr, env->exception.fsr);
        A32_BANKED_CURRENT_REG_SET(env, dfar, env->exception.vaddress);
        qemu_log_mask(CPU_LOG_INT, "...with DFSR 0x%x DFAR 0x%x\n",
                      env->exception.fsr,
                      (uint32_t)env->exception.vaddress);
        new_mode = ARM_CPU_MODE_ABT;
        addr = 0x10;
        mask = CPSR_A | CPSR_I;
        offset = 8;
        break;
    case EXCP_IRQ:
        new_mode = ARM_CPU_MODE_IRQ;
        addr = 0x18;
        /* Disable IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I;
        offset = 4;
        if (env->cp15.scr_el3 & SCR_IRQ) {
            /* IRQ routed to monitor mode */
            new_mode = ARM_CPU_MODE_MON;
            mask |= CPSR_F;
        }
        break;
    case EXCP_FIQ:
        new_mode = ARM_CPU_MODE_FIQ;
        addr = 0x1c;
        /* Disable FIQ, IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I | CPSR_F;
        if (env->cp15.scr_el3 & SCR_FIQ) {
            /* FIQ routed to monitor mode */
            new_mode = ARM_CPU_MODE_MON;
        }
        offset = 4;
        break;
    case EXCP_VIRQ:
        new_mode = ARM_CPU_MODE_IRQ;
        addr = 0x18;
        /* Disable IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I;
        offset = 4;
        break;
    case EXCP_VFIQ:
        new_mode = ARM_CPU_MODE_FIQ;
        addr = 0x1c;
        /* Disable FIQ, IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I | CPSR_F;
        offset = 4;
        break;
    case EXCP_VSERR:
        {
            /*
             * Note that this is reported as a data abort, but the DFAR
             * has an UNKNOWN value.  Construct the SError syndrome from
             * AET and ExT fields.
             */
            ARMMMUFaultInfo fi = { .type = ARMFault_AsyncExternal, };

            if (extended_addresses_enabled(env)) {
                env->exception.fsr = arm_fi_to_lfsc(&fi);
            } else {
                env->exception.fsr = arm_fi_to_sfsc(&fi);
            }
            env->exception.fsr |= env->cp15.vsesr_el2 & 0xd000;
            A32_BANKED_CURRENT_REG_SET(env, dfsr, env->exception.fsr);
            qemu_log_mask(CPU_LOG_INT, "...with IFSR 0x%x\n",
                          env->exception.fsr);

            new_mode = ARM_CPU_MODE_ABT;
            addr = 0x10;
            mask = CPSR_A | CPSR_I;
            offset = 8;
        }
        break;
    case EXCP_SMC:
        new_mode = ARM_CPU_MODE_MON;
        addr = 0x08;
        mask = CPSR_A | CPSR_I | CPSR_F;
        offset = 0;
        break;
    case EXCP_MON_TRAP:
        new_mode = ARM_CPU_MODE_MON;
        addr = 0x04;
        mask = CPSR_A | CPSR_I | CPSR_F;
        if (env->thumb) {
            offset = 2;
        } else {
            offset = 4;
        }
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
        return; /* Never happens.  Keep compiler happy.  */
    }

    if (new_mode == ARM_CPU_MODE_MON) {
        addr += env->cp15.mvbar;
    } else if (A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_V) {
        /* High vectors. When enabled, base address cannot be remapped. */
        addr += 0xffff0000;
    } else {
        /*
         * ARM v7 architectures provide a vector base address register to remap
         * the interrupt vector table.
         * This register is only followed in non-monitor mode, and is banked.
         * Note: only bits 31:5 are valid.
         */
        addr += A32_BANKED_CURRENT_REG_GET(env, vbar);
    }

    if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_MON) {
        env->cp15.scr_el3 &= ~SCR_NS;
    }

    take_aarch32_exception(env, new_mode, mask, offset, addr);
}

static int aarch64_regnum(CPUARMState *env, int aarch32_reg)
{
    /*
     * Return the register number of the AArch64 view of the AArch32
     * register @aarch32_reg. The CPUARMState CPSR is assumed to still
     * be that of the AArch32 mode the exception came from.
     */
    int mode = env->uncached_cpsr & CPSR_M;

    switch (aarch32_reg) {
    case 0 ... 7:
        return aarch32_reg;
    case 8 ... 12:
        return mode == ARM_CPU_MODE_FIQ ? aarch32_reg + 16 : aarch32_reg;
    case 13:
        switch (mode) {
        case ARM_CPU_MODE_USR:
        case ARM_CPU_MODE_SYS:
            return 13;
        case ARM_CPU_MODE_HYP:
            return 15;
        case ARM_CPU_MODE_IRQ:
            return 17;
        case ARM_CPU_MODE_SVC:
            return 19;
        case ARM_CPU_MODE_ABT:
            return 21;
        case ARM_CPU_MODE_UND:
            return 23;
        case ARM_CPU_MODE_FIQ:
            return 29;
        default:
            g_assert_not_reached();
        }
    case 14:
        switch (mode) {
        case ARM_CPU_MODE_USR:
        case ARM_CPU_MODE_SYS:
        case ARM_CPU_MODE_HYP:
            return 14;
        case ARM_CPU_MODE_IRQ:
            return 16;
        case ARM_CPU_MODE_SVC:
            return 18;
        case ARM_CPU_MODE_ABT:
            return 20;
        case ARM_CPU_MODE_UND:
            return 22;
        case ARM_CPU_MODE_FIQ:
            return 30;
        default:
            g_assert_not_reached();
        }
    case 15:
        return 31;
    default:
        g_assert_not_reached();
    }
}

uint32_t cpsr_read_for_spsr_elx(CPUARMState *env)
{
    uint32_t ret = cpsr_read(env);

    /* Move DIT to the correct location for SPSR_ELx */
    if (ret & CPSR_DIT) {
        ret &= ~CPSR_DIT;
        ret |= PSTATE_DIT;
    }
    /* Merge PSTATE.SS into SPSR_ELx */
    ret |= env->pstate & PSTATE_SS;

    return ret;
}

void cpsr_write_from_spsr_elx(CPUARMState *env, uint32_t val)
{
    uint32_t mask;

    /* Save SPSR_ELx.SS into PSTATE. */
    env->pstate = (env->pstate & ~PSTATE_SS) | (val & PSTATE_SS);
    val &= ~PSTATE_SS;

    /* Move DIT to the correct location for CPSR */
    if (val & PSTATE_DIT) {
        val &= ~PSTATE_DIT;
        val |= CPSR_DIT;
    }

    mask = aarch32_cpsr_valid_mask(env->features, &env_archcpu(env)->isar);
    cpsr_write(env, val, mask, CPSRWriteRaw);
}

static bool syndrome_is_sync_extabt(uint32_t syndrome)
{
    /* Return true if this syndrome value is a synchronous external abort */
    switch (syn_get_ec(syndrome)) {
    case EC_INSNABORT:
    case EC_INSNABORT_SAME_EL:
    case EC_DATAABORT:
    case EC_DATAABORT_SAME_EL:
        /* Look at fault status code for all the synchronous ext abort cases */
        switch (syndrome & 0x3f) {
        case 0x10:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

/* Handle exception entry to a target EL which is using AArch64 */
static void arm_cpu_do_interrupt_aarch64(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    unsigned int new_el = env->exception.target_el;
    vaddr addr = env->cp15.vbar_el[new_el];
    uint64_t new_mode = aarch64_pstate_mode(new_el, true);
    uint64_t old_mode;
    unsigned int cur_el = arm_current_el(env);
    int rt;

    if (tcg_enabled()) {
        /*
         * Note that new_el can never be 0.  If cur_el is 0, then
         * el0_a64 is is_a64(), else el0_a64 is ignored.
         */
        aarch64_sve_change_el(env, cur_el, new_el, is_a64(env));
    }

    if (cur_el < new_el) {
        /*
         * Entry vector offset depends on whether the implemented EL
         * immediately lower than the target level is using AArch32 or AArch64
         */
        bool is_aa64;
        uint64_t hcr;

        switch (new_el) {
        case 3:
            is_aa64 = arm_scr_rw_eff(env);
            break;
        case 2:
            hcr = arm_hcr_el2_eff(env);
            if ((hcr & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
                is_aa64 = (hcr & HCR_RW) != 0;
                break;
            }
            /* fall through */
        case 1:
            is_aa64 = is_a64(env);
            break;
        default:
            g_assert_not_reached();
        }

        if (is_aa64) {
            addr += 0x400;
        } else {
            addr += 0x600;
        }
    } else {
        if (pstate_read(env) & PSTATE_SP) {
            addr += 0x200;
        }
        if (is_a64(env) && (env->cp15.gcscr_el[new_el] & GCSCR_EXLOCKEN)) {
            new_mode |= PSTATE_EXLOCK;
        }
    }

    switch (cs->exception_index) {
    case EXCP_GPC:
        qemu_log_mask(CPU_LOG_INT, "...with MFAR 0x%" PRIx64 "\n",
                      env->cp15.mfar_el3);
        /* fall through */
    case EXCP_PREFETCH_ABORT:
    case EXCP_DATA_ABORT:
        /*
         * FEAT_DoubleFault allows synchronous external aborts taken to EL3
         * to be taken to the SError vector entrypoint.
         */
        if (new_el == 3 && (env->cp15.scr_el3 & SCR_EASE) &&
            syndrome_is_sync_extabt(env->exception.syndrome)) {
            addr += 0x180;
        }
        env->cp15.far_el[new_el] = env->exception.vaddress;
        qemu_log_mask(CPU_LOG_INT, "...with FAR 0x%" PRIx64 "\n",
                      env->cp15.far_el[new_el]);
        /* fall through */
    case EXCP_BKPT:
    case EXCP_UDEF:
    case EXCP_SWI:
    case EXCP_HVC:
    case EXCP_HYP_TRAP:
    case EXCP_SMC:
        switch (syn_get_ec(env->exception.syndrome)) {
        case EC_ADVSIMDFPACCESSTRAP:
            /*
             * QEMU internal FP/SIMD syndromes from AArch32 include the
             * TA and coproc fields which are only exposed if the exception
             * is taken to AArch32 Hyp mode. Mask them out to get a valid
             * AArch64 format syndrome.
             */
            env->exception.syndrome &= ~MAKE_64BIT_MASK(0, 20);
            break;
        case EC_CP14RTTRAP:
        case EC_CP15RTTRAP:
        case EC_CP14DTTRAP:
            /*
             * For a trap on AArch32 MRC/MCR/LDC/STC the Rt field is currently
             * the raw register field from the insn; when taking this to
             * AArch64 we must convert it to the AArch64 view of the register
             * number. Notice that we read a 4-bit AArch32 register number and
             * write back a 5-bit AArch64 one.
             */
            rt = extract32(env->exception.syndrome, 5, 4);
            rt = aarch64_regnum(env, rt);
            env->exception.syndrome = deposit32(env->exception.syndrome,
                                                5, 5, rt);
            break;
        case EC_CP15RRTTRAP:
        case EC_CP14RRTTRAP:
            /* Similarly for MRRC/MCRR traps for Rt and Rt2 fields */
            rt = extract32(env->exception.syndrome, 5, 4);
            rt = aarch64_regnum(env, rt);
            env->exception.syndrome = deposit32(env->exception.syndrome,
                                                5, 5, rt);
            rt = extract32(env->exception.syndrome, 10, 4);
            rt = aarch64_regnum(env, rt);
            env->exception.syndrome = deposit32(env->exception.syndrome,
                                                10, 5, rt);
            break;
        }
        env->cp15.esr_el[new_el] = env->exception.syndrome;
        break;
    case EXCP_IRQ:
    case EXCP_VIRQ:
    case EXCP_NMI:
    case EXCP_VINMI:
        addr += 0x80;
        break;
    case EXCP_FIQ:
    case EXCP_VFIQ:
    case EXCP_VFNMI:
        addr += 0x100;
        break;
    case EXCP_VSERR:
        addr += 0x180;
        /* Construct the SError syndrome from IDS and ISS fields. */
        env->exception.syndrome = syn_serror(env->cp15.vsesr_el2 & 0x1ffffff);
        env->cp15.esr_el[new_el] = env->exception.syndrome;
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
    }

    if (is_a64(env)) {
        old_mode = pstate_read(env);
        aarch64_save_sp(env, arm_current_el(env));
        env->elr_el[new_el] = env->pc;

        if (cur_el == 1 && new_el == 1) {
            uint64_t hcr = arm_hcr_el2_eff(env);
            if ((hcr & (HCR_NV | HCR_NV1 | HCR_NV2)) == HCR_NV ||
                (hcr & (HCR_NV | HCR_NV2)) == (HCR_NV | HCR_NV2)) {
                /*
                 * FEAT_NV, FEAT_NV2 may need to report EL2 in the SPSR
                 * by setting M[3:2] to 0b10.
                 * If NV2 is disabled, change SPSR when NV,NV1 == 1,0 (I_ZJRNN)
                 * If NV2 is enabled, change SPSR when NV is 1 (I_DBTLM)
                 */
                old_mode = deposit64(old_mode, 2, 2, 2);
            }
        }
    } else {
        old_mode = cpsr_read_for_spsr_elx(env);
        env->elr_el[new_el] = env->regs[15];

        aarch64_sync_32_to_64(env);

        env->condexec_bits = 0;
    }
    env->banked_spsr[aarch64_banked_spsr_index(new_el)] = old_mode;

    qemu_log_mask(CPU_LOG_INT, "...with SPSR 0x%" PRIx64 "\n", old_mode);
    qemu_log_mask(CPU_LOG_INT, "...with ELR 0x%" PRIx64 "\n",
                  env->elr_el[new_el]);

    if (cpu_isar_feature(aa64_pan, cpu)) {
        /* The value of PSTATE.PAN is normally preserved, except when ... */
        new_mode |= old_mode & PSTATE_PAN;
        switch (new_el) {
        case 2:
            /* ... the target is EL2 with HCR_EL2.{E2H,TGE} == '11' ...  */
            if ((arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE))
                != (HCR_E2H | HCR_TGE)) {
                break;
            }
            /* fall through */
        case 1:
            /* ... the target is EL1 ... */
            /* ... and SCTLR_ELx.SPAN == 0, then set to 1.  */
            if ((env->cp15.sctlr_el[new_el] & SCTLR_SPAN) == 0) {
                new_mode |= PSTATE_PAN;
            }
            break;
        }
    }
    if (cpu_isar_feature(aa64_mte, cpu)) {
        new_mode |= PSTATE_TCO;
    }

    if (cpu_isar_feature(aa64_ssbs, cpu)) {
        if (env->cp15.sctlr_el[new_el] & SCTLR_DSSBS_64) {
            new_mode |= PSTATE_SSBS;
        } else {
            new_mode &= ~PSTATE_SSBS;
        }
    }

    if (cpu_isar_feature(aa64_nmi, cpu)) {
        if (!(env->cp15.sctlr_el[new_el] & SCTLR_SPINTMASK)) {
            new_mode |= PSTATE_ALLINT;
        } else {
            new_mode &= ~PSTATE_ALLINT;
        }
    }

    pstate_write(env, PSTATE_DAIF | new_mode);
    env->aarch64 = true;
    aarch64_restore_sp(env, new_el);

    if (tcg_enabled()) {
        helper_rebuild_hflags_a64(env, new_el);
    }

    env->pc = addr;

    qemu_log_mask(CPU_LOG_INT, "...to EL%d PC 0x%" PRIx64
                  " PSTATE 0x%" PRIx64 "\n",
                  new_el, env->pc, pstate_read(env));
}

/*
 * Do semihosting call and set the appropriate return value. All the
 * permission and validity checks have been done at translate time.
 *
 * We only see semihosting exceptions in TCG only as they are not
 * trapped to the hypervisor in KVM.
 */
#ifdef CONFIG_TCG
static void tcg_handle_semihosting(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (is_a64(env)) {
        qemu_log_mask(CPU_LOG_INT,
                      "...handling as semihosting call 0x%" PRIx64 "\n",
                      env->xregs[0]);
        do_common_semihosting(cs);
        env->pc += 4;
    } else {
        qemu_log_mask(CPU_LOG_INT,
                      "...handling as semihosting call 0x%x\n",
                      env->regs[0]);
        do_common_semihosting(cs);
        env->regs[15] += env->thumb ? 2 : 4;
    }
}
#endif

/*
 * Handle a CPU exception for A and R profile CPUs.
 * Do any appropriate logging, handle PSCI calls, and then hand off
 * to the AArch64-entry or AArch32-entry function depending on the
 * target exception level's register width.
 *
 * Note: this is used for both TCG (as the do_interrupt tcg op),
 *       and KVM to re-inject guest debug exceptions, and to
 *       inject a Synchronous-External-Abort.
 */
void arm_cpu_do_interrupt(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    unsigned int new_el = env->exception.target_el;
    uint64_t last_pc = cs->cc->get_pc(cs);

    assert(!arm_feature(env, ARM_FEATURE_M));

    arm_log_exception(cs);
    qemu_log_mask(CPU_LOG_INT, "...from EL%d to EL%d\n", arm_current_el(env),
                  new_el);
    if (qemu_loglevel_mask(CPU_LOG_INT)
        && !excp_is_internal(cs->exception_index)) {
        qemu_log_mask(CPU_LOG_INT, "...with ESR 0x%x/0x%" PRIx64 "\n",
                      syn_get_ec(env->exception.syndrome),
                      env->exception.syndrome);
    }

    if (tcg_enabled() && arm_is_psci_call(cpu, cs->exception_index)) {
        arm_handle_psci_call(cpu);
        qemu_log_mask(CPU_LOG_INT, "...handled as PSCI call\n");
        qemu_plugin_vcpu_hostcall_cb(cs, last_pc);
        return;
    }

    /*
     * Semihosting semantics depend on the register width of the code
     * that caused the exception, not the target exception level, so
     * must be handled here.
     */
#ifdef CONFIG_TCG
    if (cs->exception_index == EXCP_SEMIHOST) {
        tcg_handle_semihosting(cs);
        qemu_plugin_vcpu_hostcall_cb(cs, last_pc);
        return;
    }
#endif

    /*
     * Hooks may change global state so BQL should be held, also the
     * BQL needs to be held for any modification of
     * cs->interrupt_request.
     */
    g_assert(bql_locked());

    arm_call_pre_el_change_hook(cpu);

    assert(!excp_is_internal(cs->exception_index));
    if (arm_el_is_aa64(env, new_el)) {
        arm_cpu_do_interrupt_aarch64(cs);
    } else {
        arm_cpu_do_interrupt_aarch32(cs);
    }

    arm_call_el_change_hook(cpu);

    if (!kvm_enabled()) {
        cpu_set_interrupt(cs, CPU_INTERRUPT_EXITTB);
    }

    arm_do_plugin_vcpu_discon_cb(cs, last_pc);
}
#endif /* !CONFIG_USER_ONLY */

uint64_t arm_sctlr(CPUARMState *env, int el)
{
    /* Only EL0 needs to be adjusted for EL1&0 or EL2&0 or EL3&0 */
    if (el == 0) {
        ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, 0);
        switch (mmu_idx) {
        case ARMMMUIdx_E20_0:
            el = 2;
            break;
        case ARMMMUIdx_E30_0:
            el = 3;
            break;
        default:
            el = 1;
            break;
        }
    }
    return env->cp15.sctlr_el[el];
}

int aa64_va_parameter_tbi(uint64_t tcr, ARMMMUIdx mmu_idx)
{
    if (regime_has_2_ranges(mmu_idx)) {
        return extract64(tcr, 37, 2);
    } else if (regime_is_stage2(mmu_idx)) {
        return 0; /* VTCR_EL2 */
    } else {
        /* Replicate the single TBI bit so we always have 2 bits.  */
        return extract32(tcr, 20, 1) * 3;
    }
}

int aa64_va_parameter_tbid(uint64_t tcr, ARMMMUIdx mmu_idx)
{
    if (regime_has_2_ranges(mmu_idx)) {
        return extract64(tcr, 51, 2);
    } else if (regime_is_stage2(mmu_idx)) {
        return 0; /* VTCR_EL2 */
    } else {
        /* Replicate the single TBID bit so we always have 2 bits.  */
        return extract32(tcr, 29, 1) * 3;
    }
}

int aa64_va_parameter_tcma(uint64_t tcr, ARMMMUIdx mmu_idx)
{
    if (regime_has_2_ranges(mmu_idx)) {
        return extract64(tcr, 57, 2);
    } else {
        /* Replicate the single TCMA bit so we always have 2 bits.  */
        return extract32(tcr, 30, 1) * 3;
    }
}

static ARMGranuleSize tg0_to_gran_size(int tg)
{
    switch (tg) {
    case 0:
        return Gran4K;
    case 1:
        return Gran64K;
    case 2:
        return Gran16K;
    default:
        return GranInvalid;
    }
}

static ARMGranuleSize tg1_to_gran_size(int tg)
{
    switch (tg) {
    case 1:
        return Gran16K;
    case 2:
        return Gran4K;
    case 3:
        return Gran64K;
    default:
        return GranInvalid;
    }
}

static inline bool have4k(ARMCPU *cpu, bool stage2)
{
    return stage2 ? cpu_isar_feature(aa64_tgran4_2, cpu)
        : cpu_isar_feature(aa64_tgran4, cpu);
}

static inline bool have16k(ARMCPU *cpu, bool stage2)
{
    return stage2 ? cpu_isar_feature(aa64_tgran16_2, cpu)
        : cpu_isar_feature(aa64_tgran16, cpu);
}

static inline bool have64k(ARMCPU *cpu, bool stage2)
{
    return stage2 ? cpu_isar_feature(aa64_tgran64_2, cpu)
        : cpu_isar_feature(aa64_tgran64, cpu);
}

static ARMGranuleSize sanitize_gran_size(ARMCPU *cpu, ARMGranuleSize gran,
                                         bool stage2)
{
    switch (gran) {
    case Gran4K:
        if (have4k(cpu, stage2)) {
            return gran;
        }
        break;
    case Gran16K:
        if (have16k(cpu, stage2)) {
            return gran;
        }
        break;
    case Gran64K:
        if (have64k(cpu, stage2)) {
            return gran;
        }
        break;
    case GranInvalid:
        break;
    }
    /*
     * If the guest selects a granule size that isn't implemented,
     * the architecture requires that we behave as if it selected one
     * that is (with an IMPDEF choice of which one to pick). We choose
     * to implement the smallest supported granule size.
     */
    if (have4k(cpu, stage2)) {
        return Gran4K;
    }
    if (have16k(cpu, stage2)) {
        return Gran16K;
    }
    assert(have64k(cpu, stage2));
    return Gran64K;
}

ARMVAParameters aa64_va_parameters(CPUARMState *env, uint64_t va,
                                   ARMMMUIdx mmu_idx, bool data,
                                   bool el1_is_aa32)
{
    uint64_t tcr = regime_tcr(env, mmu_idx);
    bool epd, hpd, tsz_oob, ds, ha, hd, pie = false;
    bool aie = false;
    int select, tsz, tbi, max_tsz, min_tsz, ps, sh;
    ARMGranuleSize gran;
    ARMCPU *cpu = env_archcpu(env);
    bool stage2 = regime_is_stage2(mmu_idx);
    int r_el = regime_el(mmu_idx);

    if (!regime_has_2_ranges(mmu_idx)) {
        select = 0;
        tsz = extract32(tcr, 0, 6);
        gran = tg0_to_gran_size(extract32(tcr, 14, 2));
        if (stage2) {
            /*
             * Stage2 does not have hierarchical permissions.
             * Thus disabling them makes things easier during ptw.
             */
            hpd = true;
            pie = extract64(tcr, 36, 1) && cpu_isar_feature(aa64_s2pie, cpu);
        } else {
            hpd = extract32(tcr, 24, 1);
            if (r_el == 3) {
                pie = (extract64(tcr, 35, 1)
                       && cpu_isar_feature(aa64_s1pie, cpu));
                aie = (extract64(tcr, 37, 1)
                       && cpu_isar_feature(aa64_aie, cpu));
            } else if (!arm_feature(env, ARM_FEATURE_EL3)
                       || (env->cp15.scr_el3 & SCR_TCR2EN)) {
                pie = env->cp15.tcr2_el[2] & TCR2_PIE;
                aie = env->cp15.tcr2_el[2] & TCR2_AIE;
            }
        }
        epd = false;
        sh = extract32(tcr, 12, 2);
        ps = extract32(tcr, 16, 3);
        ha = extract32(tcr, 21, 1) && cpu_isar_feature(aa64_hafs, cpu);
        hd = extract32(tcr, 22, 1) && cpu_isar_feature(aa64_hdbs, cpu);
        ds = extract64(tcr, 32, 1);
    } else {
        bool e0pd;

        /*
         * Bit 55 is always between the two regions, and is canonical for
         * determining if address tagging is enabled.
         */
        select = extract64(va, 55, 1);
        if (!select) {
            tsz = extract32(tcr, 0, 6);
            gran = tg0_to_gran_size(extract32(tcr, 14, 2));
            epd = extract32(tcr, 7, 1);
            sh = extract32(tcr, 12, 2);
            hpd = extract64(tcr, 41, 1);
            e0pd = extract64(tcr, 55, 1);
        } else {
            tsz = extract32(tcr, 16, 6);
            gran = tg1_to_gran_size(extract32(tcr, 30, 2));
            epd = extract32(tcr, 23, 1);
            sh = extract32(tcr, 28, 2);
            hpd = extract64(tcr, 42, 1);
            e0pd = extract64(tcr, 56, 1);
        }
        ps = extract64(tcr, 32, 3);
        ha = extract64(tcr, 39, 1) && cpu_isar_feature(aa64_hafs, cpu);
        hd = extract64(tcr, 40, 1) && cpu_isar_feature(aa64_hdbs, cpu);
        ds = extract64(tcr, 59, 1);

        if (e0pd && cpu_isar_feature(aa64_e0pd, cpu) &&
            regime_is_user(mmu_idx)) {
            epd = true;
        }

        if ((!arm_feature(env, ARM_FEATURE_EL3)
             || (env->cp15.scr_el3 & SCR_TCR2EN))
            && (r_el == 2 || (arm_hcrx_el2_eff(env) & HCRX_TCR2EN))) {
            pie = env->cp15.tcr2_el[r_el] & TCR2_PIE;
            aie = env->cp15.tcr2_el[r_el] & TCR2_AIE;
        }
    }
    hpd |= pie;

    gran = sanitize_gran_size(cpu, gran, stage2);

    if (cpu_isar_feature(aa64_st, cpu)) {
        max_tsz = 48 - (gran == Gran64K);
    } else {
        max_tsz = 39;
    }

    /*
     * DS is RES0 unless FEAT_LPA2 is supported for the given page size;
     * adjust the effective value of DS, as documented.
     */
    min_tsz = 16;
    if (gran == Gran64K) {
        if (cpu_isar_feature(aa64_lva, cpu)) {
            min_tsz = 12;
        }
        ds = false;
    } else if (ds) {
        if (regime_is_stage2(mmu_idx)) {
            if (gran == Gran16K) {
                ds = cpu_isar_feature(aa64_tgran16_2_lpa2, cpu);
            } else {
                ds = cpu_isar_feature(aa64_tgran4_2_lpa2, cpu);
            }
        } else {
            if (gran == Gran16K) {
                ds = cpu_isar_feature(aa64_tgran16_lpa2, cpu);
            } else {
                ds = cpu_isar_feature(aa64_tgran4_lpa2, cpu);
            }
        }
        if (ds) {
            min_tsz = 12;
        }
    }

    if (stage2 && el1_is_aa32) {
        /*
         * For AArch32 EL1 the min txsz (and thus max IPA size) requirements
         * are loosened: a configured IPA of 40 bits is permitted even if
         * the implemented PA is less than that (and so a 40 bit IPA would
         * fault for an AArch64 EL1). See R_DTLMN.
         */
        min_tsz = MIN(min_tsz, 24);
    }

    if (tsz > max_tsz) {
        tsz = max_tsz;
        tsz_oob = true;
    } else if (tsz < min_tsz) {
        tsz = min_tsz;
        tsz_oob = true;
    } else {
        tsz_oob = false;
    }

    /* Present TBI as a composite with TBID.  */
    tbi = aa64_va_parameter_tbi(tcr, mmu_idx);
    if (!data) {
        tbi &= ~aa64_va_parameter_tbid(tcr, mmu_idx);
    }
    tbi = (tbi >> select) & 1;

    return (ARMVAParameters) {
        .tsz = tsz,
        .ps = ps,
        .sh = sh,
        .select = select,
        .tbi = tbi,
        .epd = epd,
        .hpd = hpd,
        .tsz_oob = tsz_oob,
        .ds = ds,
        .ha = ha,
        .hd = ha && hd,
        .gran = gran,
        .pie = pie,
        .aie = aie,
    };
}


/*
 * Return the exception level to which FP-disabled exceptions should
 * be taken, or 0 if FP is enabled.
 */
int fp_exception_el(CPUARMState *env, int cur_el)
{
#ifndef CONFIG_USER_ONLY
    uint64_t hcr_el2;

    /*
     * CPACR and the CPTR registers don't exist before v6, so FP is
     * always accessible
     */
    if (!arm_feature(env, ARM_FEATURE_V6)) {
        return 0;
    }

    if (arm_feature(env, ARM_FEATURE_M)) {
        /* CPACR can cause a NOCP UsageFault taken to current security state */
        if (!v7m_cpacr_pass(env, env->v7m.secure, cur_el != 0)) {
            return 1;
        }

        if (arm_feature(env, ARM_FEATURE_M_SECURITY) && !env->v7m.secure) {
            if (!extract32(env->v7m.nsacr, 10, 1)) {
                /* FP insns cause a NOCP UsageFault taken to Secure */
                return 3;
            }
        }

        return 0;
    }

    hcr_el2 = arm_hcr_el2_eff(env);

    /*
     * The CPACR controls traps to EL1, or PL1 if we're 32 bit:
     * 0, 2 : trap EL0 and EL1/PL1 accesses
     * 1    : trap only EL0 accesses
     * 3    : trap no accesses
     * This register is ignored if E2H+TGE are both set.
     */
    if ((hcr_el2 & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        int fpen = FIELD_EX64(env->cp15.cpacr_el1, CPACR_EL1, FPEN);

        switch (fpen) {
        case 1:
            if (cur_el != 0) {
                break;
            }
            /* fall through */
        case 0:
        case 2:
            /* Trap from Secure PL0 or PL1 to Secure PL1. */
            if (!arm_el_is_aa64(env, 3)
                && (cur_el == 3 || arm_is_secure_below_el3(env))) {
                return 3;
            }
            if (cur_el <= 1) {
                return 1;
            }
            break;
        }
    }

    /*
     * The NSACR allows A-profile AArch32 EL3 and M-profile secure mode
     * to control non-secure access to the FPU. It doesn't have any
     * effect if EL3 is AArch64 or if EL3 doesn't exist at all.
     */
    if ((arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
         cur_el <= 2 && !arm_is_secure_below_el3(env))) {
        if (!extract32(env->cp15.nsacr, 10, 1)) {
            /* FP insns act as UNDEF */
            return cur_el == 2 ? 2 : 1;
        }
    }

    /*
     * CPTR_EL2 is present in v7VE or v8, and changes format
     * with HCR_EL2.E2H (regardless of TGE).
     */
    if (cur_el <= 2) {
        if (hcr_el2 & HCR_E2H) {
            switch (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, FPEN)) {
            case 1:
                if (cur_el != 0 || !(hcr_el2 & HCR_TGE)) {
                    break;
                }
                /* fall through */
            case 0:
            case 2:
                return 2;
            }
        } else if (arm_is_el2_enabled(env)) {
            if (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, TFP)) {
                return 2;
            }
        }
    }

    /* CPTR_EL3 : present in v8 */
    if (FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, TFP)) {
        /* Trap all FP ops to EL3 */
        return 3;
    }
#endif
    return 0;
}

#ifndef CONFIG_TCG
ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate)
{
    g_assert_not_reached();
}
#endif

ARMMMUIdx arm_mmu_idx_el(CPUARMState *env, int el)
{
    ARMMMUIdx idx;
    uint64_t hcr;

    if (arm_feature(env, ARM_FEATURE_M)) {
        return arm_v7m_mmu_idx_for_secstate(env, env->v7m.secure);
    }

    /* See ARM pseudo-function ELIsInHost.  */
    switch (el) {
    case 0:
        hcr = arm_hcr_el2_eff(env);
        if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
            idx = ARMMMUIdx_E20_0;
        } else if (arm_is_secure_below_el3(env) &&
                   !arm_el_is_aa64(env, 3)) {
            idx = ARMMMUIdx_E30_0;
        } else {
            idx = ARMMMUIdx_E10_0;
        }
        break;
    case 1:
        if (arm_pan_enabled(env)) {
            idx = ARMMMUIdx_E10_1_PAN;
        } else {
            idx = ARMMMUIdx_E10_1;
        }
        break;
    case 2:
        /* Note that TGE does not apply at EL2.  */
        if (arm_hcr_el2_eff(env) & HCR_E2H) {
            if (arm_pan_enabled(env)) {
                idx = ARMMMUIdx_E20_2_PAN;
            } else {
                idx = ARMMMUIdx_E20_2;
            }
        } else {
            idx = ARMMMUIdx_E2;
        }
        break;
    case 3:
        if (!arm_el_is_aa64(env, 3) && arm_pan_enabled(env)) {
            return ARMMMUIdx_E30_3_PAN;
        }
        return ARMMMUIdx_E3;
    default:
        g_assert_not_reached();
    }

    return idx;
}

ARMMMUIdx arm_mmu_idx(CPUARMState *env)
{
    return arm_mmu_idx_el(env, arm_current_el(env));
}

/*
 * The manual says that when SVE is enabled and VQ is widened the
 * implementation is allowed to zero the previously inaccessible
 * portion of the registers.  The corollary to that is that when
 * SVE is enabled and VQ is narrowed we are also allowed to zero
 * the now inaccessible portion of the registers.
 *
 * The intent of this is that no predicate bit beyond VQ is ever set.
 * Which means that some operations on predicate registers themselves
 * may operate on full uint64_t or even unrolled across the maximum
 * uint64_t[4].  Performing 4 bits of host arithmetic unconditionally
 * may well be cheaper than conditionals to restrict the operation
 * to the relevant portion of a uint16_t[16].
 */
void aarch64_sve_narrow_vq(CPUARMState *env, unsigned vq)
{
    int i, j;
    uint64_t pmask;

    assert(vq >= 1 && vq <= ARM_MAX_VQ);
    assert(vq <= env_archcpu(env)->sve_max_vq);

    /* Zap the high bits of the zregs.  */
    for (i = 0; i < 32; i++) {
        memset(&env->vfp.zregs[i].d[2 * vq], 0, 16 * (ARM_MAX_VQ - vq));
    }

    /* Zap the high bits of the pregs and ffr.  */
    pmask = 0;
    if (vq & 3) {
        pmask = ~(-1ULL << (16 * (vq & 3)));
    }
    for (j = vq / 4; j < ARM_MAX_VQ / 4; j++) {
        for (i = 0; i < 17; ++i) {
            env->vfp.pregs[i].p[j] &= pmask;
        }
        pmask = 0;
    }
}

static uint32_t sve_vqm1_for_el_sm_ena(CPUARMState *env, int el, bool sm)
{
    int exc_el;

    if (sm) {
        exc_el = sme_exception_el(env, el);
    } else {
        exc_el = sve_exception_el(env, el);
    }
    if (exc_el) {
        return 0; /* disabled */
    }
    return sve_vqm1_for_el_sm(env, el, sm);
}

/*
 * Notice a change in SVE vector size when changing EL.
 */
void aarch64_sve_change_el(CPUARMState *env, int old_el,
                           int new_el, bool el0_a64)
{
    ARMCPU *cpu = env_archcpu(env);
    int old_len, new_len;
    bool old_a64, new_a64, sm;

    /* Nothing to do if no SVE.  */
    if (!cpu_isar_feature(aa64_sve, cpu)) {
        return;
    }

    /* Nothing to do if FP is disabled in either EL.  */
    if (fp_exception_el(env, old_el) || fp_exception_el(env, new_el)) {
        return;
    }

    old_a64 = old_el ? arm_el_is_aa64(env, old_el) : el0_a64;
    new_a64 = new_el ? arm_el_is_aa64(env, new_el) : el0_a64;

    /*
     * Both AArch64.TakeException and AArch64.ExceptionReturn
     * invoke ResetSVEState when taking an exception from, or
     * returning to, AArch32 state when PSTATE.SM is enabled.
     */
    sm = FIELD_EX64(env->svcr, SVCR, SM);
    if (old_a64 != new_a64 && sm) {
        arm_reset_sve_state(env);
        return;
    }

    /*
     * DDI0584A.d sec 3.2: "If SVE instructions are disabled or trapped
     * at ELx, or not available because the EL is in AArch32 state, then
     * for all purposes other than a direct read, the ZCR_ELx.LEN field
     * has an effective value of 0".
     *
     * Consider EL2 (aa64, vq=4) -> EL0 (aa32) -> EL1 (aa64, vq=0).
     * If we ignore aa32 state, we would fail to see the vq4->vq0 transition
     * from EL2->EL1.  Thus we go ahead and narrow when entering aa32 so that
     * we already have the correct register contents when encountering the
     * vq0->vq0 transition between EL0->EL1.
     */
    old_len = new_len = 0;
    if (old_a64) {
        old_len = sve_vqm1_for_el_sm_ena(env, old_el, sm);
    }
    if (new_a64) {
        new_len = sve_vqm1_for_el_sm_ena(env, new_el, sm);
    }

    /* When changing vector length, clear inaccessible state.  */
    if (new_len < old_len) {
        aarch64_sve_narrow_vq(env, new_len + 1);
    }
}

#ifndef CONFIG_USER_ONLY
ARMSecuritySpace arm_security_space(CPUARMState *env)
{
    if (arm_feature(env, ARM_FEATURE_M)) {
        return arm_secure_to_space(env->v7m.secure);
    }

    /*
     * If EL3 is not supported then the secure state is implementation
     * defined, in which case QEMU defaults to non-secure.
     */
    if (!arm_feature(env, ARM_FEATURE_EL3)) {
        return ARMSS_NonSecure;
    }

    /* Check for AArch64 EL3 or AArch32 Mon. */
    if (is_a64(env)) {
        if (extract32(env->pstate, 2, 2) == 3) {
            if (cpu_isar_feature(aa64_rme, env_archcpu(env))) {
                return ARMSS_Root;
            } else {
                return ARMSS_Secure;
            }
        }
    } else {
        if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_MON) {
            return ARMSS_Secure;
        }
    }

    return arm_security_space_below_el3(env);
}

ARMSecuritySpace arm_security_space_below_el3(CPUARMState *env)
{
    assert(!arm_feature(env, ARM_FEATURE_M));

    /*
     * If EL3 is not supported then the secure state is implementation
     * defined, in which case QEMU defaults to non-secure.
     */
    if (!arm_feature(env, ARM_FEATURE_EL3)) {
        return ARMSS_NonSecure;
    }

    /*
     * Note NSE cannot be set without RME, and NSE & !NS is Reserved.
     * Ignoring NSE when !NS retains consistency without having to
     * modify other predicates.
     */
    if (!(env->cp15.scr_el3 & SCR_NS)) {
        return ARMSS_Secure;
    } else if (env->cp15.scr_el3 & SCR_NSE) {
        return ARMSS_Realm;
    } else {
        return ARMSS_NonSecure;
    }
}
#endif /* !CONFIG_USER_ONLY */
