/*
 * Helpers for TLBI insns
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "exec/exec-all.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-features.h"
#include "cpregs.h"

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

    tlb_flush_page_by_mmuidx(cs, pageaddr, ARMMMUIdxBit_E2);
}

static void tlbimva_hyp_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = value & ~MAKE_64BIT_MASK(0, 12);

    tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr,
                                             ARMMMUIdxBit_E2);
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

    tlb_flush_by_mmuidx(cs, ARMMMUIdxBit_E2);
}

static void tlbiall_hyp_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, ARMMMUIdxBit_E2);
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

    tlb_flush_by_mmuidx(cs, ARMMMUIdxBit_E3);
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

    tlb_flush_page_by_mmuidx(cs, pageaddr, ARMMMUIdxBit_E3);
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
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVMALLE1IS,
      .writefn = tlbi_aa64_vmalle1is_write },
    { .name = "TLBI_VAE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVAE1IS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_ASIDE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 2,
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIASIDE1IS,
      .writefn = tlbi_aa64_vmalle1is_write },
    { .name = "TLBI_VAAE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVAAE1IS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVALE1IS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VAALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVAALE1IS,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VMALLE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 0,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVMALLE1,
      .writefn = tlbi_aa64_vmalle1_write },
    { .name = "TLBI_VAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVAE1,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_ASIDE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 2,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIASIDE1,
      .writefn = tlbi_aa64_vmalle1_write },
    { .name = "TLBI_VAAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVAAE1,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_VALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVALE1,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_VAALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIVAALE1,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_IPAS2E1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_ipas2e1is_write },
    { .name = "TLBI_IPAS2LE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_ipas2e1is_write },
    { .name = "TLBI_ALLE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle1is_write },
    { .name = "TLBI_VMALLS12E1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle1is_write },
    { .name = "TLBI_IPAS2E1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_ipas2e1_write },
    { .name = "TLBI_IPAS2LE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_ipas2e1_write },
    { .name = "TLBI_ALLE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle1_write },
    { .name = "TLBI_VMALLS12E1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
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
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_alle2_write },
    { .name = "TLBI_VAE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2_write },
    { .name = "TLBI_VALE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2_write },
    { .name = "TLBI_ALLE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 0,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_alle2is_write },
    { .name = "TLBI_VAE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2is_write },
    { .name = "TLBI_VALE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_vae2is_write },
};

static const ARMCPRegInfo tlbi_el3_cp_reginfo[] = {
    { .name = "TLBI_ALLE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 3, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle3is_write },
    { .name = "TLBI_VAE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 3, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae3is_write },
    { .name = "TLBI_VALE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 3, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae3is_write },
    { .name = "TLBI_ALLE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle3_write },
    { .name = "TLBI_VAE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae3_write },
    { .name = "TLBI_VALE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae3_write },
};

#ifdef TARGET_AARCH64
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

    do_rvae_write(env, value, ARMMMUIdxBit_E3, tlb_force_broadcast(env));
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

    do_rvae_write(env, value, ARMMMUIdxBit_E3, true);
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
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVAE1IS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAAE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 2, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVAAE1IS,
      .writefn = tlbi_aa64_rvae1is_write },
   { .name = "TLBI_RVALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 2, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVALE1IS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 2, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlbis, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVAALE1IS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlbos, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVAE1OS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAAE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlbos, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVAAE1OS,
      .writefn = tlbi_aa64_rvae1is_write },
   { .name = "TLBI_RVALE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlbos, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVALE1OS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAALE1OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlbos, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVAALE1OS,
      .writefn = tlbi_aa64_rvae1is_write },
    { .name = "TLBI_RVAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVAE1,
      .writefn = tlbi_aa64_rvae1_write },
    { .name = "TLBI_RVAAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVAAE1,
      .writefn = tlbi_aa64_rvae1_write },
   { .name = "TLBI_RVALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVALE1,
      .writefn = tlbi_aa64_rvae1_write },
    { .name = "TLBI_RVAALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .fgt = FGT_TLBIRVAALE1,
      .writefn = tlbi_aa64_rvae1_write },
    { .name = "TLBI_RIPAS2E1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 2,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_ripas2e1is_write },
    { .name = "TLBI_RIPAS2LE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_ripas2e1is_write },
    { .name = "TLBI_RVAE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 2, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2is_write },
   { .name = "TLBI_RVALE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 2, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2is_write },
    { .name = "TLBI_RIPAS2E1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 2,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_ripas2e1_write },
    { .name = "TLBI_RIPAS2LE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_ripas2e1_write },
   { .name = "TLBI_RVAE2OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 5, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2is_write },
   { .name = "TLBI_RVALE2OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 5, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2is_write },
    { .name = "TLBI_RVAE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 6, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2_write },
   { .name = "TLBI_RVALE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 6, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = tlbi_aa64_rvae2_write },
   { .name = "TLBI_RVAE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 2, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_rvae3is_write },
   { .name = "TLBI_RVALE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 2, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_rvae3is_write },
   { .name = "TLBI_RVAE3OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 5, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_rvae3is_write },
   { .name = "TLBI_RVALE3OS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 5, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_rvae3is_write },
   { .name = "TLBI_RVAE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 6, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_rvae3_write },
   { .name = "TLBI_RVALE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 6, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_rvae3_write },
};
#endif

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
    if (arm_feature(env, ARM_FEATURE_EL2)
        || (arm_feature(env, ARM_FEATURE_EL3)
            && arm_feature(env, ARM_FEATURE_V8))) {
        define_arm_cp_regs(cpu, tlbi_el2_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        define_arm_cp_regs(cpu, tlbi_el3_cp_reginfo);
    }
#ifdef TARGET_AARCH64
    if (cpu_isar_feature(aa64_tlbirange, cpu)) {
        define_arm_cp_regs(cpu, tlbirange_reginfo);
    }
#endif
}
