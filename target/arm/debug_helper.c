/*
 * ARM debug helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-features.h"
#include "cpregs.h"
#include "exec/watchpoint.h"
#include "system/tcg.h"

/*
 * Check for traps to "powerdown debug" registers, which are controlled
 * by MDCR.TDOSA
 */
static CPAccessResult access_tdosa(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    int el = arm_current_el(env);
    uint64_t mdcr_el2 = arm_mdcr_el2_eff(env);
    bool mdcr_el2_tdosa = (mdcr_el2 & MDCR_TDOSA) || (mdcr_el2 & MDCR_TDE) ||
        (arm_hcr_el2_eff(env) & HCR_TGE);

    if (el < 2 && mdcr_el2_tdosa) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TDOSA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

/*
 * Check for traps to "debug ROM" registers, which are controlled
 * by MDCR_EL2.TDRA for EL2 but by the more general MDCR_EL3.TDA for EL3.
 */
static CPAccessResult access_tdra(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    int el = arm_current_el(env);
    uint64_t mdcr_el2 = arm_mdcr_el2_eff(env);
    bool mdcr_el2_tdra = (mdcr_el2 & MDCR_TDRA) || (mdcr_el2 & MDCR_TDE) ||
        (arm_hcr_el2_eff(env) & HCR_TGE);

    if (el < 2 && mdcr_el2_tdra) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TDA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

/*
 * Check for traps to general debug registers, which are controlled
 * by MDCR_EL2.TDA for EL2 and MDCR_EL3.TDA for EL3.
 */
static CPAccessResult access_tda(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    int el = arm_current_el(env);
    uint64_t mdcr_el2 = arm_mdcr_el2_eff(env);
    bool mdcr_el2_tda = (mdcr_el2 & MDCR_TDA) || (mdcr_el2 & MDCR_TDE) ||
        (arm_hcr_el2_eff(env) & HCR_TGE);

    if (el < 2 && mdcr_el2_tda) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TDA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_dbgvcr32(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    /* MCDR_EL3.TDMA doesn't apply for FEAT_NV traps */
    if (arm_current_el(env) == 2 && (env->cp15.mdcr_el3 & MDCR_TDA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

/*
 * Check for traps to Debug Comms Channel registers. If FEAT_FGT
 * is implemented then these are controlled by MDCR_EL2.TDCC for
 * EL2 and MDCR_EL3.TDCC for EL3. They are also controlled by
 * the general debug access trap bits MDCR_EL2.TDA and MDCR_EL3.TDA.
 * For EL0, they are also controlled by MDSCR_EL1.TDCC.
 */
static CPAccessResult access_tdcc(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    int el = arm_current_el(env);
    uint64_t mdcr_el2 = arm_mdcr_el2_eff(env);
    bool mdscr_el1_tdcc = extract32(env->cp15.mdscr_el1, 12, 1);
    bool mdcr_el2_tda = (mdcr_el2 & MDCR_TDA) || (mdcr_el2 & MDCR_TDE) ||
        (arm_hcr_el2_eff(env) & HCR_TGE);
    bool mdcr_el2_tdcc = cpu_isar_feature(aa64_fgt, env_archcpu(env)) &&
                                          (mdcr_el2 & MDCR_TDCC);
    bool mdcr_el3_tdcc = cpu_isar_feature(aa64_fgt, env_archcpu(env)) &&
                                          (env->cp15.mdcr_el3 & MDCR_TDCC);

    if (el < 1 && mdscr_el1_tdcc) {
        return CP_ACCESS_TRAP_EL1;
    }
    if (el < 2 && (mdcr_el2_tda || mdcr_el2_tdcc)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (!arm_is_el3_or_mon(env) &&
        ((env->cp15.mdcr_el3 & MDCR_TDA) || mdcr_el3_tdcc)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static void oslar_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    /*
     * Writes to OSLAR_EL1 may update the OS lock status, which can be
     * read via a bit in OSLSR_EL1.
     */
    int oslock;

    if (ri->state == ARM_CP_STATE_AA32) {
        oslock = (value == 0xC5ACCE55);
    } else {
        oslock = value & 1;
    }

    env->cp15.oslsr_el1 = deposit32(env->cp15.oslsr_el1, 1, 1, oslock);
}

static void osdlr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    /*
     * Only defined bit is bit 0 (DLK); if Feat_DoubleLock is not
     * implemented this is RAZ/WI.
     */
    if(arm_feature(env, ARM_FEATURE_AARCH64)
       ? cpu_isar_feature(aa64_doublelock, cpu)
       : cpu_isar_feature(aa32_doublelock, cpu)) {
        env->cp15.osdlr_el1 = value & 1;
    }
}

static void dbgclaimset_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    env->cp15.dbgclaim |= (value & 0xFF);
}

static uint64_t dbgclaimset_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* CLAIM bits are RAO */
    return 0xFF;
}

static void dbgclaimclr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    env->cp15.dbgclaim &= ~(value & 0xFF);
}

static CPAccessResult access_bogus(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    /* Always UNDEF, as if this cpreg didn't exist */
    return CP_ACCESS_UNDEFINED;
}

static const ARMCPRegInfo debug_cp_reginfo[] = {
    /*
     * DBGDRAR, DBGDSAR: always RAZ since we don't implement memory mapped
     * debug components. The AArch64 version of DBGDRAR is named MDRAR_EL1;
     * unlike DBGDRAR it is never accessible from EL0.
     * DBGDSAR is deprecated and must RAZ from v8 anyway, so it has no AArch64
     * accessor.
     */
    { .name = "DBGDRAR", .cp = 14, .crn = 1, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL0_R, .accessfn = access_tdra,
      .type = ARM_CP_CONST | ARM_CP_NO_GDB, .resetvalue = 0 },
    { .name = "MDRAR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 2, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 0,
      .access = PL1_R, .accessfn = access_tdra,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "DBGDSAR", .cp = 14, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL0_R, .accessfn = access_tdra,
      .type = ARM_CP_CONST | ARM_CP_NO_GDB, .resetvalue = 0 },
    /* Monitor debug system control register; the 32-bit alias is DBGDSCRext. */
    { .name = "MDSCR_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tda,
      .fgt = FGT_MDSCR_EL1,
      .nv2_redirect_offset = 0x158,
      .fieldoffset = offsetof(CPUARMState, cp15.mdscr_el1),
      .resetvalue = 0 },
    /*
     * MDCCSR_EL0[30:29] map to EDSCR[30:29].  Simply RAZ as the external
     * Debug Communication Channel is not implemented.
     */
    { .name = "MDCCSR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 2, .opc1 = 3, .crn = 0, .crm = 1, .opc2 = 0,
      .access = PL0_R, .accessfn = access_tdcc,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /*
     * These registers belong to the Debug Communications Channel,
     * which is not implemented. However we implement RAZ/WI behaviour
     * with trapping to prevent spurious SIGILLs if the guest OS does
     * access them as the support cannot be probed for.
     */
    { .name = "OSDTRRX_EL1", .state = ARM_CP_STATE_BOTH, .cp = 14,
      .opc0 = 2, .opc1 = 0, .crn = 0, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tdcc,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "OSDTRTX_EL1", .state = ARM_CP_STATE_BOTH, .cp = 14,
      .opc0 = 2, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tdcc,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /* Architecturally DBGDTRTX is named DBGDTRRX when used for reads */
    { .name = "DBGDTRTX_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 2, .opc1 = 3, .crn = 0, .crm = 5, .opc2 = 0,
      .access = PL0_RW, .accessfn = access_tdcc,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "DBGDTRTX", .state = ARM_CP_STATE_AA32, .cp = 14,
      .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 0,
      .access = PL0_RW, .accessfn = access_tdcc,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /* This is AArch64-only and is a combination of DBGDTRTX and DBGDTRRX */
    { .name = "DBGDTR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 2, .opc1 = 3, .crn = 0, .crm = 4, .opc2 = 0,
      .access = PL0_RW, .accessfn = access_tdcc,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /*
     * This is not a real AArch32 register. We used to incorrectly expose
     * this due to a QEMU bug; to avoid breaking migration compatibility we
     * need to continue to provide it so that we don't fail the inbound
     * migration when it tells us about a sysreg that we don't have.
     * We set an always-fails .accessfn, which means that the guest doesn't
     * actually see this register (it will always UNDEF, identically to if
     * there were no cpreg definition for it other than that we won't print
     * a LOG_UNIMP message about it), and we set the ARM_CP_NO_GDB flag so the
     * gdbstub won't see it either.
     * (We can't just set .access = 0, because add_cpreg_to_hashtable()
     * helpfully ignores cpregs which aren't accessible to the highest
     * implemented EL.)
     *
     * TODO: implement a system for being able to describe "this register
     * can be ignored if it appears in the inbound stream"; then we can
     * remove this temporary hack.
     */
    { .name = "BOGUS_DBGDTR_EL0", .state = ARM_CP_STATE_AA32,
      .cp = 14, .opc1 = 3, .crn = 0, .crm = 5, .opc2 = 0,
      .access = PL0_RW, .accessfn = access_bogus,
      .type = ARM_CP_CONST | ARM_CP_NO_GDB, .resetvalue = 0 },
    /*
     * OSECCR_EL1 provides a mechanism for an operating system
     * to access the contents of EDECCR. EDECCR is not implemented though,
     * as is the rest of external device mechanism.
     */
    { .name = "OSECCR_EL1", .state = ARM_CP_STATE_BOTH, .cp = 14,
      .opc0 = 2, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tda,
      .fgt = FGT_OSECCR_EL1,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /*
     * DBGDSCRint[15,12,5:2] map to MDSCR_EL1[15,12,5:2].  Map all bits as
     * it is unlikely a guest will care.
     * We don't implement the configurable EL0 access.
     */
    { .name = "DBGDSCRint", .state = ARM_CP_STATE_AA32,
      .cp = 14, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 0,
      .type = ARM_CP_ALIAS,
      .access = PL1_R, .accessfn = access_tda,
      .fieldoffset = offsetof(CPUARMState, cp15.mdscr_el1), },
    { .name = "OSLAR_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 4,
      .access = PL1_W, .type = ARM_CP_NO_RAW,
      .accessfn = access_tdosa,
      .fgt = FGT_OSLAR_EL1,
      .writefn = oslar_write },
    { .name = "OSLSR_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 4,
      .access = PL1_R, .resetvalue = 10,
      .accessfn = access_tdosa,
      .fgt = FGT_OSLSR_EL1,
      .fieldoffset = offsetof(CPUARMState, cp15.oslsr_el1) },
    /* Dummy OSDLR_EL1: 32-bit Linux will read this */
    { .name = "OSDLR_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 1, .crm = 3, .opc2 = 4,
      .access = PL1_RW, .accessfn = access_tdosa,
      .fgt = FGT_OSDLR_EL1,
      .writefn = osdlr_write,
      .fieldoffset = offsetof(CPUARMState, cp15.osdlr_el1) },
    /*
     * Dummy DBGVCR: Linux wants to clear this on startup, but we don't
     * implement vector catch debug events yet.
     */
    { .name = "DBGVCR",
      .cp = 14, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tda,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /*
     * Dummy MDCCINT_EL1, since we don't implement the Debug Communications
     * Channel but Linux may try to access this register. The 32-bit
     * alias is DBGDCCINT.
     */
    { .name = "MDCCINT_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tdcc,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /*
     * Dummy DBGCLAIM registers.
     * "The architecture does not define any functionality for the CLAIM tag bits.",
     * so we only keep the raw bits
     */
    { .name = "DBGCLAIMSET_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 6,
      .type = ARM_CP_ALIAS,
      .access = PL1_RW, .accessfn = access_tda,
      .fgt = FGT_DBGCLAIM,
      .writefn = dbgclaimset_write, .readfn = dbgclaimset_read },
    { .name = "DBGCLAIMCLR_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 6,
      .access = PL1_RW, .accessfn = access_tda,
      .fgt = FGT_DBGCLAIM,
      .writefn = dbgclaimclr_write, .raw_writefn = raw_write,
      .fieldoffset = offsetof(CPUARMState, cp15.dbgclaim) },
};

/* These are present only when EL1 supports AArch32 */
static const ARMCPRegInfo debug_aa32_el1_reginfo[] = {
    /*
     * Dummy DBGVCR32_EL2 (which is only for a 64-bit hypervisor
     * to save and restore a 32-bit guest's DBGVCR)
     */
    { .name = "DBGVCR32_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 2, .opc1 = 4, .crn = 0, .crm = 7, .opc2 = 0,
      .access = PL2_RW, .accessfn = access_dbgvcr32,
      .type = ARM_CP_CONST | ARM_CP_EL3_NO_EL2_KEEP,
      .resetvalue = 0 },
};

static const ARMCPRegInfo debug_lpae_cp_reginfo[] = {
    /* 64 bit access versions of the (dummy) debug registers */
    { .name = "DBGDRAR", .cp = 14, .crm = 1, .opc1 = 0,
      .access = PL0_R, .type = ARM_CP_CONST | ARM_CP_64BIT | ARM_CP_NO_GDB,
      .resetvalue = 0 },
    { .name = "DBGDSAR", .cp = 14, .crm = 2, .opc1 = 0,
      .access = PL0_R, .type = ARM_CP_CONST | ARM_CP_64BIT | ARM_CP_NO_GDB,
      .resetvalue = 0 },
};

static void dbgwvr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    int i = ri->crm;

    /*
     * Bits [1:0] are RES0.
     *
     * It is IMPLEMENTATION DEFINED whether [63:49] ([63:53] with FEAT_LVA)
     * are hardwired to the value of bit [48] ([52] with FEAT_LVA), or if
     * they contain the value written.  It is CONSTRAINED UNPREDICTABLE
     * whether the RESS bits are ignored when comparing an address.
     *
     * Therefore we are allowed to compare the entire register, which lets
     * us avoid considering whether or not FEAT_LVA is actually enabled.
     */
    value &= ~3ULL;

    raw_write(env, ri, value);
    if (tcg_enabled()) {
        hw_watchpoint_update(cpu, i);
    }
}

static void dbgwcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    int i = ri->crm;

    raw_write(env, ri, value);
    if (tcg_enabled()) {
        hw_watchpoint_update(cpu, i);
    }
}

static void dbgbvr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    int i = ri->crm;

    raw_write(env, ri, value);
    if (tcg_enabled()) {
        hw_breakpoint_update(cpu, i);
    }
}

static void dbgbcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    int i = ri->crm;

    /*
     * BAS[3] is a read-only copy of BAS[2], and BAS[1] a read-only
     * copy of BAS[0].
     */
    value = deposit64(value, 6, 1, extract64(value, 5, 1));
    value = deposit64(value, 8, 1, extract64(value, 7, 1));

    raw_write(env, ri, value);
    if (tcg_enabled()) {
        hw_breakpoint_update(cpu, i);
    }
}

void define_debug_regs(ARMCPU *cpu)
{
    /*
     * Define v7 and v8 architectural debug registers.
     * These are just dummy implementations for now.
     */
    int i;
    int wrps, brps, ctx_cmps;

    /*
     * The Arm ARM says DBGDIDR is optional and deprecated if EL1 cannot
     * use AArch32.  Given that bit 15 is RES1, if the value is 0 then
     * the register must not exist for this cpu.
     */
    if (cpu->isar.dbgdidr != 0) {
        ARMCPRegInfo dbgdidr = {
            .name = "DBGDIDR", .cp = 14, .crn = 0, .crm = 0,
            .opc1 = 0, .opc2 = 0,
            .access = PL0_R, .accessfn = access_tda,
            .type = ARM_CP_CONST, .resetvalue = cpu->isar.dbgdidr,
        };
        define_one_arm_cp_reg(cpu, &dbgdidr);
    }

    /*
     * DBGDEVID is present in the v7 debug architecture if
     * DBGDIDR.DEVID_imp is 1 (bit 15); from v7.1 and on it is
     * mandatory (and bit 15 is RES1). DBGDEVID1 and DBGDEVID2 exist
     * from v7.1 of the debug architecture. Because no fields have yet
     * been defined in DBGDEVID2 (and quite possibly none will ever
     * be) we don't define an ARMISARegisters field for it.
     * These registers exist only if EL1 can use AArch32, but that
     * happens naturally because they are only PL1 accessible anyway.
     */
    if (extract32(cpu->isar.dbgdidr, 15, 1)) {
        ARMCPRegInfo dbgdevid = {
            .name = "DBGDEVID",
            .cp = 14, .opc1 = 0, .crn = 7, .opc2 = 2, .crn = 7,
            .access = PL1_R, .accessfn = access_tda,
            .type = ARM_CP_CONST, .resetvalue = cpu->isar.dbgdevid,
        };
        define_one_arm_cp_reg(cpu, &dbgdevid);
    }
    if (cpu_isar_feature(aa32_debugv7p1, cpu)) {
        ARMCPRegInfo dbgdevid12[] = {
            {
                .name = "DBGDEVID1",
                .cp = 14, .opc1 = 0, .crn = 7, .opc2 = 1, .crn = 7,
                .access = PL1_R, .accessfn = access_tda,
                .type = ARM_CP_CONST, .resetvalue = cpu->isar.dbgdevid1,
            }, {
                .name = "DBGDEVID2",
                .cp = 14, .opc1 = 0, .crn = 7, .opc2 = 0, .crn = 7,
                .access = PL1_R, .accessfn = access_tda,
                .type = ARM_CP_CONST, .resetvalue = 0,
            },
        };
        define_arm_cp_regs(cpu, dbgdevid12);
    }

    brps = arm_num_brps(cpu);
    wrps = arm_num_wrps(cpu);
    ctx_cmps = arm_num_ctx_cmps(cpu);

    assert(ctx_cmps <= brps);

    define_arm_cp_regs(cpu, debug_cp_reginfo);
    if (cpu_isar_feature(aa64_aa32_el1, cpu)) {
        define_arm_cp_regs(cpu, debug_aa32_el1_reginfo);
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_LPAE)) {
        define_arm_cp_regs(cpu, debug_lpae_cp_reginfo);
    }

    for (i = 0; i < brps; i++) {
        char *dbgbvr_el1_name = g_strdup_printf("DBGBVR%d_EL1", i);
        char *dbgbcr_el1_name = g_strdup_printf("DBGBCR%d_EL1", i);
        ARMCPRegInfo dbgregs[] = {
            { .name = dbgbvr_el1_name, .state = ARM_CP_STATE_BOTH,
              .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = i, .opc2 = 4,
              .access = PL1_RW, .accessfn = access_tda,
              .fgt = FGT_DBGBVRN_EL1,
              .fieldoffset = offsetof(CPUARMState, cp15.dbgbvr[i]),
              .writefn = dbgbvr_write, .raw_writefn = raw_write
            },
            { .name = dbgbcr_el1_name, .state = ARM_CP_STATE_BOTH,
              .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = i, .opc2 = 5,
              .access = PL1_RW, .accessfn = access_tda,
              .fgt = FGT_DBGBCRN_EL1,
              .fieldoffset = offsetof(CPUARMState, cp15.dbgbcr[i]),
              .writefn = dbgbcr_write, .raw_writefn = raw_write
            },
        };
        define_arm_cp_regs(cpu, dbgregs);
        g_free(dbgbvr_el1_name);
        g_free(dbgbcr_el1_name);
    }

    for (i = 0; i < wrps; i++) {
        char *dbgwvr_el1_name = g_strdup_printf("DBGWVR%d_EL1", i);
        char *dbgwcr_el1_name = g_strdup_printf("DBGWCR%d_EL1", i);
        ARMCPRegInfo dbgregs[] = {
            { .name = dbgwvr_el1_name, .state = ARM_CP_STATE_BOTH,
              .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = i, .opc2 = 6,
              .access = PL1_RW, .accessfn = access_tda,
              .fgt = FGT_DBGWVRN_EL1,
              .fieldoffset = offsetof(CPUARMState, cp15.dbgwvr[i]),
              .writefn = dbgwvr_write, .raw_writefn = raw_write
            },
            { .name = dbgwcr_el1_name, .state = ARM_CP_STATE_BOTH,
              .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = i, .opc2 = 7,
              .access = PL1_RW, .accessfn = access_tda,
              .fgt = FGT_DBGWCRN_EL1,
              .fieldoffset = offsetof(CPUARMState, cp15.dbgwcr[i]),
              .writefn = dbgwcr_write, .raw_writefn = raw_write
            },
        };
        define_arm_cp_regs(cpu, dbgregs);
        g_free(dbgwvr_el1_name);
        g_free(dbgwcr_el1_name);
    }
}
