/*
 * Helpers for TLBI insns
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "exec/cputlb.h"
#include "exec/target_page.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-features.h"
#include "cpregs.h"

/* Check for traps from EL1 due to HCR_EL2.TTLB. */
static CPAccessResult access_ttlb(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TTLB)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

/* Check for traps from EL1 due to HCR_EL2.TTLB or TTLBIS. */
static CPAccessResult access_ttlbis(CPUARMState *env, const ARMCPRegInfo *ri,
                                    bool isread)
{
    if (arm_current_el(env) == 1 &&
        (arm_hcr_el2_eff(env) & (HCR_TTLB | HCR_TTLBIS))) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

/* Check for traps from EL1 due to HCR_EL2.TTLB or TTLBOS. */
static CPAccessResult access_ttlbos(CPUARMState *env, const ARMCPRegInfo *ri,
                                    bool isread)
{
    if (arm_current_el(env) == 1 &&
        (arm_hcr_el2_eff(env) & (HCR_TTLB | HCR_TTLBOS))) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

/* IS variants of TLB operations must affect all cores */
static void tlbiall_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_all_cpus_synced(cs);
}

static void tlbiasid_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_all_cpus_synced(cs);
}

static void tlbimva_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_page_all_cpus_synced(cs, value & TARGET_PAGE_MASK);
}

static void tlbimvaa_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_page_all_cpus_synced(cs, value & TARGET_PAGE_MASK);
}

/*
 * Non-IS variants of TLB operations are upgraded to
 * IS versions if we are at EL1 and HCR_EL2.FB is effectively set to
 * force broadcast of these operations.
 */
static bool tlb_force_broadcast(CPUARMState *env)
{
    return arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_FB);
}

static void tlbiall_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    /* Invalidate all (TLBIALL) */
    CPUState *cs = env_cpu(env);

    if (tlb_force_broadcast(env)) {
        tlb_flush_all_cpus_synced(cs);
    } else {
        tlb_flush(cs);
    }
}

static void tlbimva_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    /* Invalidate single TLB entry by MVA and ASID (TLBIMVA) */
    CPUState *cs = env_cpu(env);

    value &= TARGET_PAGE_MASK;
    if (tlb_force_broadcast(env)) {
        tlb_flush_page_all_cpus_synced(cs, value);
    } else {
        tlb_flush_page(cs, value);
    }
}

static void tlbiasid_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /* Invalidate by ASID (TLBIASID) */
    CPUState *cs = env_cpu(env);

    if (tlb_force_broadcast(env)) {
        tlb_flush_all_cpus_synced(cs);
    } else {
        tlb_flush(cs);
    }
}

static void tlbimvaa_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /* Invalidate single entry by MVA, all ASIDs (TLBIMVAA) */
    CPUState *cs = env_cpu(env);

    value &= TARGET_PAGE_MASK;
    if (tlb_force_broadcast(env)) {
        tlb_flush_page_all_cpus_synced(cs, value);
    } else {
        tlb_flush_page(cs, value);
    }
}

static void tlbimva_hyp_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = value & ~MAKE_64BIT_MASK(0, 12);

    tlb_flush_page_by_mmuidx(cs, pageaddr,
                             ARMMMUIdxBit_E2 | ARMMMUIdxBit_E2_GCS);
}

static void tlbimva_hyp_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = value & ~MAKE_64BIT_MASK(0, 12);

    tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr,
                                             ARMMMUIdxBit_E2 |
                                             ARMMMUIdxBit_E2_GCS);
}

static void tlbiipas2_hyp_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = (value & MAKE_64BIT_MASK(0, 28)) << 12;

    tlb_flush_page_by_mmuidx(cs, pageaddr, ARMMMUIdxBit_Stage2);
}

static void tlbiipas2is_hyp_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = (value & MAKE_64BIT_MASK(0, 28)) << 12;

    tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr, ARMMMUIdxBit_Stage2);
}

static void tlbiall_nsnh_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx(cs, alle1_tlbmask(env));
}

static void tlbiall_nsnh_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, alle1_tlbmask(env));
}


static void tlbiall_hyp_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx(cs, ARMMMUIdxBit_E2 | ARMMMUIdxBit_E2_GCS);
}

static void tlbiall_hyp_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, ARMMMUIdxBit_E2 |
                                            ARMMMUIdxBit_E2_GCS);
}

/*
 * See: D4.7.2 TLB maintenance requirements and the TLB maintenance instructions
 * Page D4-1736 (DDI0487A.b)
 */

static int vae1_tlbmask(CPUARMState *env)
{
    uint64_t hcr = arm_hcr_el2_eff(env);
    uint16_t mask;

    assert(arm_feature(env, ARM_FEATURE_AARCH64));

    if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
        mask = ARMMMUIdxBit_E20_2 |
               ARMMMUIdxBit_E20_2_PAN |
               ARMMMUIdxBit_E20_2_GCS |
               ARMMMUIdxBit_E20_0 |
               ARMMMUIdxBit_E20_0_GCS;
    } else {
        /* This is AArch64 only, so we don't need to touch the EL30_x TLBs */
        mask = ARMMMUIdxBit_E10_1 |
               ARMMMUIdxBit_E10_1_PAN |
               ARMMMUIdxBit_E10_1_GCS |
               ARMMMUIdxBit_E10_0 |
               ARMMMUIdxBit_E10_0_GCS;
    }
    return mask;
}

static int vae2_tlbmask(CPUARMState *env)
{
    uint64_t hcr = arm_hcr_el2_eff(env);
    uint16_t mask;

    if (hcr & HCR_E2H) {
        mask = ARMMMUIdxBit_E20_2 |
               ARMMMUIdxBit_E20_2_PAN |
               ARMMMUIdxBit_E20_2_GCS |
               ARMMMUIdxBit_E20_0 |
               ARMMMUIdxBit_E20_0_GCS;
    } else {
        mask = ARMMMUIdxBit_E2 | ARMMMUIdxBit_E2_GCS;
    }
    return mask;
}

static int vae3_tlbmask(void)
{
    return ARMMMUIdxBit_E3 | ARMMMUIdxBit_E3_GCS;
}

/* Return 56 if TBI is enabled, 64 otherwise. */
static int tlbbits_for_regime(CPUARMState *env, ARMMMUIdx mmu_idx,
                       uint64_t addr)
{
    uint64_t tcr = regime_tcr(env, mmu_idx);
    int tbi = aa64_va_parameter_tbi(tcr, mmu_idx);
    int select = extract64(addr, 55, 1);

    return (tbi >> select) & 1 ? 56 : 64;
}

static int vae1_tlbbits(CPUARMState *env, uint64_t addr)
{
    uint64_t hcr = arm_hcr_el2_eff(env);
    ARMMMUIdx mmu_idx;

    assert(arm_feature(env, ARM_FEATURE_AARCH64));

    /* Only the regime of the mmu_idx below is significant. */
    if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
        mmu_idx = ARMMMUIdx_E20_0;
    } else {
        mmu_idx = ARMMMUIdx_E10_0;
    }

    return tlbbits_for_regime(env, mmu_idx, addr);
}

static int vae2_tlbbits(CPUARMState *env, uint64_t addr)
{
    uint64_t hcr = arm_hcr_el2_eff(env);
    ARMMMUIdx mmu_idx;

    /*
     * Only the regime of the mmu_idx below is significant.
     * Regime EL2&0 has two ranges with separate TBI configuration, while EL2
     * only has one.
     */
    if (hcr & HCR_E2H) {
        mmu_idx = ARMMMUIdx_E20_2;
    } else {
        mmu_idx = ARMMMUIdx_E2;
    }

    return tlbbits_for_regime(env, mmu_idx, addr);
}

static void tlbi_aa64_vmalle1is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                      uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = vae1_tlbmask(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, mask);
}

static void tlbi_aa64_vmalle1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = vae1_tlbmask(env);

    if (tlb_force_broadcast(env)) {
        tlb_flush_by_mmuidx_all_cpus_synced(cs, mask);
    } else {
        tlb_flush_by_mmuidx(cs, mask);
    }
}

static int e2_tlbmask(CPUARMState *env)
{
    return (ARMMMUIdxBit_E20_0 |
            ARMMMUIdxBit_E20_0_GCS |
            ARMMMUIdxBit_E20_2 |
            ARMMMUIdxBit_E20_2_PAN |
            ARMMMUIdxBit_E20_2_GCS |
            ARMMMUIdxBit_E2 |
            ARMMMUIdxBit_E2_GCS);
}

static void tlbi_aa64_alle1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = alle1_tlbmask(env);

    tlb_flush_by_mmuidx(cs, mask);
}

static void tlbi_aa64_alle2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = e2_tlbmask(env);

    tlb_flush_by_mmuidx(cs, mask);
}

static void tlbi_aa64_alle3_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = CPU(cpu);

    tlb_flush_by_mmuidx(cs, vae3_tlbmask());
}

static void tlbi_aa64_alle1is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = alle1_tlbmask(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, mask);
}

static void tlbi_aa64_alle2is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = e2_tlbmask(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, mask);
}

static void tlbi_aa64_alle3is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, vae3_tlbmask());
}

static void tlbi_aa64_vae2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    /*
     * Invalidate by VA, EL2
     * Currently handles both VAE2 and VALE2, since we don't support
     * flush-last-level-only.
     */
    CPUState *cs = env_cpu(env);
    int mask = vae2_tlbmask(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);
    int bits = vae2_tlbbits(env, pageaddr);

    tlb_flush_page_bits_by_mmuidx(cs, pageaddr, mask, bits);
}

static void tlbi_aa64_vae3_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    /*
     * Invalidate by VA, EL3
     * Currently handles both VAE3 and VALE3, since we don't support
     * flush-last-level-only.
     */
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = CPU(cpu);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);

    tlb_flush_page_by_mmuidx(cs, pageaddr, vae3_tlbmask());
}

static void tlbi_aa64_vae1is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = vae1_tlbmask(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);
    int bits = vae1_tlbbits(env, pageaddr);

    tlb_flush_page_bits_by_mmuidx_all_cpus_synced(cs, pageaddr, mask, bits);
}

static void tlbi_aa64_vae1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    /*
     * Invalidate by VA, EL1&0 (AArch64 version).
     * Currently handles all of VAE1, VAAE1, VAALE1 and VALE1,
     * since we don't support flush-for-specific-ASID-only or
     * flush-last-level-only.
     */
    CPUState *cs = env_cpu(env);
    int mask = vae1_tlbmask(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);
    int bits = vae1_tlbbits(env, pageaddr);

    if (tlb_force_broadcast(env)) {
        tlb_flush_page_bits_by_mmuidx_all_cpus_synced(cs, pageaddr, mask, bits);
    } else {
        tlb_flush_page_bits_by_mmuidx(cs, pageaddr, mask, bits);
    }
}

static void tlbi_aa64_vae2is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = vae2_tlbmask(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);
    int bits = vae2_tlbbits(env, pageaddr);

    tlb_flush_page_bits_by_mmuidx_all_cpus_synced(cs, pageaddr, mask, bits);
}

static void tlbi_aa64_vae3is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);
    int bits = tlbbits_for_regime(env, ARMMMUIdx_E3, pageaddr);

    tlb_flush_page_bits_by_mmuidx_all_cpus_synced(cs, pageaddr,
                                                  vae3_tlbmask(), bits);
}

static int ipas2e1_tlbmask(CPUARMState *env, int64_t value)
{
    /*
     * The MSB of value is the NS field, which only applies if SEL2
     * is implemented and SCR_EL3.NS is not set (i.e. in secure mode).
     */
    return (value >= 0
            && cpu_isar_feature(aa64_sel2, env_archcpu(env))
            && arm_is_secure_below_el3(env)
            ? ARMMMUIdxBit_Stage2_S
            : ARMMMUIdxBit_Stage2);
}

static void tlbi_aa64_ipas2e1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = ipas2e1_tlbmask(env, value);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);

    if (tlb_force_broadcast(env)) {
        tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr, mask);
    } else {
        tlb_flush_page_by_mmuidx(cs, pageaddr, mask);
    }
}

static void tlbi_aa64_ipas2e1is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                      uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = ipas2e1_tlbmask(env, value);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);

    tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr, mask);
}

static const ARMCPRegInfo tlbi_not_v7_cp_reginfo[] = {
    /*
     * MMU TLB control. Note that the wildcarding means we cover not just
     * the unified TLB ops but also the dside/iside/inner-shareable variants.
     */
    { .name = "TLBIALL", .cp = 15, .crn = 8, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = 0, .access = PL1_W, .writefn = tlbiall_write,
      .type = ARM_CP_NO_RAW },
    { .name = "TLBIMVA", .cp = 15, .crn = 8, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = 1, .access = PL1_W, .writefn = tlbimva_write,
      .type = ARM_CP_NO_RAW },
    { .name = "TLBIASID", .cp = 15, .crn = 8, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = 2, .access = PL1_W, .writefn = tlbiasid_write,
      .type = ARM_CP_NO_RAW },
    { .name = "TLBIMVAA", .cp = 15, .crn = 8, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = 3, .access = PL1_W, .writefn = tlbimvaa_write,
      .type = ARM_CP_NO_RAW },
};

static const ARMCPRegInfo tlbi_v7_cp_reginfo[] = {
    /* 32 bit ITLB invalidates */
    { .name = "ITLBIALL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiall_write },
    { .name = "ITLBIMVA", .cp = 15, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_write },
    { .name = "ITLBIASID", .cp = 15, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 2,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiasid_write },
    /* 32 bit DTLB invalidates */
    { .name = "DTLBIALL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiall_write },
    { .name = "DTLBIMVA", .cp = 15, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_write },
    { .name = "DTLBIASID", .cp = 15, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 2,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiasid_write },
    /* 32 bit TLB invalidates */
    { .name = "TLBIALL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiall_write },
    { .name = "TLBIMVA", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_write },
    { .name = "TLBIASID", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 2,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiasid_write },
    { .name = "TLBIMVAA", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 3,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimvaa_write },
};

static const ARMCPRegInfo tlbi_v7mp_cp_reginfo[] = {
    /* 32 bit TLB invalidates, Inner Shareable */
    { .name = "TLBIALLIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlbis,
      .writefn = tlbiall_is_write },
    { .name = "TLBIMVAIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlbis,
      .writefn = tlbimva_is_write },
    { .name = "TLBIASIDIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 2,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlbis,
      .writefn = tlbiasid_is_write },
    { .name = "TLBIMVAAIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 3,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlbis,
      .writefn = tlbimvaa_is_write },
};

static const ARMCPRegInfo tlbi_v8_cp_reginfo[] = {
    /* AArch32 TLB invalidate last level of translation table walk */
    { .name = "TLBIMVALIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlbis,
      .writefn = tlbimva_is_write },
    { .name = "TLBIMVAALIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 7,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlbis,
      .writefn = tlbimvaa_is_write },
    { .name = "TLBIMVAL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_write },
    { .name = "TLBIMVAAL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 7,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimvaa_write },
    { .name = "TLBIMVALH", .cp = 15, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbimva_hyp_write },
    { .name = "TLBIMVALHIS",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbimva_hyp_is_write },
    { .name = "TLBIIPAS2",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiipas2_hyp_write },
    { .name = "TLBIIPAS2IS",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiipas2is_hyp_write },
    { .name = "TLBIIPAS2L",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiipas2_hyp_write },
    { .name = "TLBIIPAS2LIS",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiipas2is_hyp_write },
    /* AArch64 TLBI operations */
    { .name = "TLBI_VMALLE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 0,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVMALLE1IS,
      .writefn = tlbi_aa64_vmalle1is_write },
    { .name = "TLBI_VAE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVAE1IS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_ASIDE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 2,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIASIDE1IS,
      .writefn = tlbi_aa64_vmalle1is_write },
    { .name = "TLBI_VAAE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVAAE1IS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVALE1IS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VAALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVAALE1IS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VMALLE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 0,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVMALLE1,
      .writefn = tlbi_aa64_vmalle1_write },
    { .name = "TLBI_VAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVAE1,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_ASIDE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 2,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIASIDE1,
      .writefn = tlbi_aa64_vmalle1_write },
    { .name = "TLBI_VAAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVAAE1,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_VALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVALE1,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_VAALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVAALE1,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_IPAS2E1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_ipas2e1is_write },
    { .name = "TLBI_IPAS2LE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_ipas2e1is_write },
    { .name = "TLBI_ALLE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_alle1is_write },
    { .name = "TLBI_VMALLS12E1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_alle1is_write },
    { .name = "TLBI_IPAS2E1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_ipas2e1_write },
    { .name = "TLBI_IPAS2LE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_ipas2e1_write },
    { .name = "TLBI_ALLE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_alle1_write },
    { .name = "TLBI_VMALLS12E1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_alle1is_write },
};

static const ARMCPRegInfo tlbi_el2_cp_reginfo[] = {
    { .name = "TLBIALLNSNH",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 4,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiall_nsnh_write },
    { .name = "TLBIALLNSNHIS",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 4,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiall_nsnh_is_write },
    { .name = "TLBIALLH", .cp = 15, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiall_hyp_write },
    { .name = "TLBIALLHIS", .cp = 15, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiall_hyp_is_write },
    { .name = "TLBIMVAH", .cp = 15, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbimva_hyp_write },
    { .name = "TLBIMVAHIS", .cp = 15, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbimva_hyp_is_write },
    { .name = "TLBI_ALLE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 0,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_alle2_write },
    { .name = "TLBI_VAE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 1,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2_write },
    { .name = "TLBI_VALE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 5,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2_write },
    { .name = "TLBI_ALLE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 0,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_alle2is_write },
    { .name = "TLBI_VAE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 1,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2is_write },
    { .name = "TLBI_VALE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 5,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2is_write },
};

static const ARMCPRegInfo tlbi_el3_cp_reginfo[] = {
    { .name = "TLBI_ALLE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 3, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_alle3is_write },
    { .name = "TLBI_VAE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 3, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_vae3is_write },
    { .name = "TLBI_VALE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 3, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_vae3is_write },
    { .name = "TLBI_ALLE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_alle3_write },
    { .name = "TLBI_VAE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_vae3_write },
    { .name = "TLBI_VALE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_vae3_write },
};

typedef struct {
    uint64_t base;
    uint64_t length;
} TLBIRange;

static ARMGranuleSize tlbi_range_tg_to_gran_size(int tg)
{
    /*
     * Note that the TLBI range TG field encoding differs from both
     * TG0 and TG1 encodings.
     */
    switch (tg) {
    case 1:
        return Gran4K;
    case 2:
        return Gran16K;
    case 3:
        return Gran64K;
    default:
        return GranInvalid;
    }
}

static TLBIRange tlbi_aa64_get_range(CPUARMState *env, ARMMMUIdx mmuidx,
                                     uint64_t value)
{
    unsigned int page_size_granule, page_shift, num, scale, exponent;
    /* Extract one bit to represent the va selector in use. */
    uint64_t select = sextract64(value, 36, 1);
    ARMVAParameters param = aa64_va_parameters(env, select, mmuidx, true, false);
    TLBIRange ret = { };
    ARMGranuleSize gran;

    page_size_granule = extract64(value, 46, 2);
    gran = tlbi_range_tg_to_gran_size(page_size_granule);

    /* The granule encoded in value must match the granule in use. */
    if (gran != param.gran) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid tlbi page size granule %d\n",
                      page_size_granule);
        return ret;
    }

    page_shift = arm_granule_bits(gran);
    num = extract64(value, 39, 5);
    scale = extract64(value, 44, 2);
    exponent = (5 * scale) + 1;

    ret.length = (num + 1) << (exponent + page_shift);

    if (param.select) {
        ret.base = sextract64(value, 0, 37);
    } else {
        ret.base = extract64(value, 0, 37);
    }
    if (param.ds) {
        /*
         * With DS=1, BaseADDR is always shifted 16 so that it is able
         * to address all 52 va bits.  The input address is perforce
         * aligned on a 64k boundary regardless of translation granule.
         */
        page_shift = 16;
    }
    ret.base <<= page_shift;

    return ret;
}

static void do_rvae_write(CPUARMState *env, uint64_t value,
                          int idxmap, bool synced)
{
    ARMMMUIdx one_idx = ARM_MMU_IDX_A | ctz32(idxmap);
    TLBIRange range;
    int bits;

    range = tlbi_aa64_get_range(env, one_idx, value);
    bits = tlbbits_for_regime(env, one_idx, range.base);

    if (synced) {
        tlb_flush_range_by_mmuidx_all_cpus_synced(env_cpu(env),
                                                  range.base,
                                                  range.length,
                                                  idxmap,
                                                  bits);
    } else {
        tlb_flush_range_by_mmuidx(env_cpu(env), range.base,
                                  range.length, idxmap, bits);
    }
}

static void tlbi_aa64_rvae1_write(CPUARMState *env,
                                  const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    /*
     * Invalidate by VA range, EL1&0.
     * Currently handles all of RVAE1, RVAAE1, RVAALE1 and RVALE1,
     * since we don't support flush-for-specific-ASID-only or
     * flush-last-level-only.
     */

    do_rvae_write(env, value, vae1_tlbmask(env),
                  tlb_force_broadcast(env));
}

static void tlbi_aa64_rvae1is_write(CPUARMState *env,
                                    const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    /*
     * Invalidate by VA range, Inner/Outer Shareable EL1&0.
     * Currently handles all of RVAE1IS, RVAE1OS, RVAAE1IS, RVAAE1OS,
     * RVAALE1IS, RVAALE1OS, RVALE1IS and RVALE1OS, since we don't support
     * flush-for-specific-ASID-only, flush-last-level-only or inner/outer
     * shareable specific flushes.
     */

    do_rvae_write(env, value, vae1_tlbmask(env), true);
}

static void tlbi_aa64_rvae2_write(CPUARMState *env,
                                  const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    /*
     * Invalidate by VA range, EL2.
     * Currently handles all of RVAE2 and RVALE2,
     * since we don't support flush-for-specific-ASID-only or
     * flush-last-level-only.
     */

    do_rvae_write(env, value, vae2_tlbmask(env),
                  tlb_force_broadcast(env));


}

static void tlbi_aa64_rvae2is_write(CPUARMState *env,
                                    const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    /*
     * Invalidate by VA range, Inner/Outer Shareable, EL2.
     * Currently handles all of RVAE2IS, RVAE2OS, RVALE2IS and RVALE2OS,
     * since we don't support flush-for-specific-ASID-only,
     * flush-last-level-only or inner/outer shareable specific flushes.
     */

    do_rvae_write(env, value, vae2_tlbmask(env), true);

}

static void tlbi_aa64_rvae3_write(CPUARMState *env,
                                  const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    /*
     * Invalidate by VA range, EL3.
     * Currently handles all of RVAE3 and RVALE3,
     * since we don't support flush-for-specific-ASID-only or
     * flush-last-level-only.
     */

    do_rvae_write(env, value, vae3_tlbmask(), tlb_force_broadcast(env));
}

static void tlbi_aa64_rvae3is_write(CPUARMState *env,
                                    const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    /*
     * Invalidate by VA range, EL3, Inner/Outer Shareable.
     * Currently handles all of RVAE3IS, RVAE3OS, RVALE3IS and RVALE3OS,
     * since we don't support flush-for-specific-ASID-only,
     * flush-last-level-only or inner/outer specific flushes.
     */

    do_rvae_write(env, value, vae3_tlbmask(), true);
}

static void tlbi_aa64_ripas2e1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    do_rvae_write(env, value, ipas2e1_tlbmask(env, value),
                  tlb_force_broadcast(env));
}

static void tlbi_aa64_ripas2e1is_write(CPUARMState *env,
                                       const ARMCPRegInfo *ri,
                                       uint64_t value)
{
    do_rvae_write(env, value, ipas2e1_tlbmask(env, value), true);
}

static const ARMCPRegInfo tlbirange_reginfo[] = {
    { .name = "TLBI_RVAE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 2, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVAE1IS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAAE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 2, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVAAE1IS,
      .writefn = tlbi_aa64_rvae1is_write },
   { .name = "TLBI_RVALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 2, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVALE1IS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 2, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlbis,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVAALE1IS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVAE1OS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAAE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVAAE1OS,
      .writefn = tlbi_aa64_rvae1is_write },
   { .name = "TLBI_RVALE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVALE1OS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAALE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVAALE1OS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVAE1,
      .writefn = tlbi_aa64_rvae1_write },
    { .name = "TLBI_RVAAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVAAE1,
      .writefn = tlbi_aa64_rvae1_write },
   { .name = "TLBI_RVALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVALE1,
      .writefn = tlbi_aa64_rvae1_write },
    { .name = "TLBI_RVAALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlb,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIRVAALE1,
      .writefn = tlbi_aa64_rvae1_write },
    { .name = "TLBI_RIPAS2E1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 2,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_ripas2e1is_write },
    { .name = "TLBI_RIPAS2LE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_ripas2e1is_write },
    { .name = "TLBI_RVAE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 2, .opc2 = 1,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2is_write },
   { .name = "TLBI_RVALE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 2, .opc2 = 5,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2is_write },
    { .name = "TLBI_RIPAS2E1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 2,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_ripas2e1_write },
    { .name = "TLBI_RIPAS2LE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_ripas2e1_write },
   { .name = "TLBI_RVAE2OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 5, .opc2 = 1,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2is_write },
   { .name = "TLBI_RVALE2OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 5, .opc2 = 5,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2is_write },
    { .name = "TLBI_RVAE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 6, .opc2 = 1,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2_write },
   { .name = "TLBI_RVALE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 6, .opc2 = 5,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2_write },
   { .name = "TLBI_RVAE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 2, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_rvae3is_write },
   { .name = "TLBI_RVALE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 2, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_rvae3is_write },
   { .name = "TLBI_RVAE3OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 5, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_rvae3is_write },
   { .name = "TLBI_RVALE3OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 5, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_rvae3is_write },
   { .name = "TLBI_RVAE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 6, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_rvae3_write },
   { .name = "TLBI_RVALE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 6, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_rvae3_write },
};

static const ARMCPRegInfo tlbios_reginfo[] = {
    { .name = "TLBI_VMALLE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 1, .opc2 = 0,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVMALLE1OS,
      .writefn = tlbi_aa64_vmalle1is_write },
    { .name = "TLBI_VAE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 1, .opc2 = 1,
      .fgt = FGT_TLBIVAE1OS,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_ASIDE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 1, .opc2 = 2,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIASIDE1OS,
      .writefn = tlbi_aa64_vmalle1is_write },
    { .name = "TLBI_VAAE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 1, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVAAE1OS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VALE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 1, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVALE1OS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VAALE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 1, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlbos,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .fgt = FGT_TLBIVAALE1OS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_ALLE2OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 1, .opc2 = 0,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_alle2is_write },
    { .name = "TLBI_VAE2OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 1, .opc2 = 1,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2is_write },
   { .name = "TLBI_ALLE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 1, .opc2 = 4,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_alle1is_write },
    { .name = "TLBI_VALE2OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 1, .opc2 = 5,
      .access = PL2_W,
      .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2is_write },
    { .name = "TLBI_VMALLS12E1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 1, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_alle1is_write },
    { .name = "TLBI_IPAS2E1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 0,
      .access = PL2_W, .type = ARM_CP_NOP | ARM_CP_ADD_TLBI_NXS },
    { .name = "TLBI_RIPAS2E1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 3,
      .access = PL2_W, .type = ARM_CP_NOP | ARM_CP_ADD_TLBI_NXS },
    { .name = "TLBI_IPAS2LE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NOP | ARM_CP_ADD_TLBI_NXS },
    { .name = "TLBI_RIPAS2LE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 7,
      .access = PL2_W, .type = ARM_CP_NOP | ARM_CP_ADD_TLBI_NXS },
    { .name = "TLBI_ALLE3OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 1, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_alle3is_write },
    { .name = "TLBI_VAE3OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 1, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_vae3is_write },
    { .name = "TLBI_VALE3OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 1, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_ADD_TLBI_NXS,
      .writefn = tlbi_aa64_vae3is_write },
};

static void tlbi_aa64_paall_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush(cs);
}

static void tlbi_aa64_paallos_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_all_cpus_synced(cs);
}

static const ARMCPRegInfo tlbi_rme_reginfo[] = {
    { .name = "TLBI_PAALL", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 4,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_paall_write },
    { .name = "TLBI_PAALLOS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 1, .opc2 = 4,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_paallos_write },
    /*
     * QEMU does not have a way to invalidate by physical address, thus
     * invalidating a range of physical addresses is accomplished by
     * flushing all tlb entries in the outer shareable domain,
     * just like PAALLOS.
     */
    { .name = "TLBI_RPALOS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 4, .opc2 = 7,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_paallos_write },
    { .name = "TLBI_RPAOS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 4, .opc2 = 3,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_paallos_write },
};

void define_tlb_insn_regs(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;

    if (!arm_feature(env, ARM_FEATURE_V7)) {
        define_arm_cp_regs(cpu, tlbi_not_v7_cp_reginfo);
    } else {
        define_arm_cp_regs(cpu, tlbi_v7_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V7MP) &&
        !arm_feature(env, ARM_FEATURE_PMSA)) {
        define_arm_cp_regs(cpu, tlbi_v7mp_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V8)) {
        define_arm_cp_regs(cpu, tlbi_v8_cp_reginfo);
    }
    /*
     * We retain the existing logic for when to register these TLBI
     * ops (i.e. matching the condition for el2_cp_reginfo[] in
     * helper.c), but we will be able to simplify this later.
     */
    if (arm_feature(env, ARM_FEATURE_EL2)) {
        define_arm_cp_regs(cpu, tlbi_el2_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        define_arm_cp_regs(cpu, tlbi_el3_cp_reginfo);
    }
    if (cpu_isar_feature(aa64_tlbirange, cpu)) {
        define_arm_cp_regs(cpu, tlbirange_reginfo);
    }
    if (cpu_isar_feature(aa64_tlbios, cpu)) {
        define_arm_cp_regs(cpu, tlbios_reginfo);
    }
    if (cpu_isar_feature(aa64_rme, cpu)) {
        define_arm_cp_regs(cpu, tlbi_rme_reginfo);
    }
}
