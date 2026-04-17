/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Magnus Kulke  <magnuskulke@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/mshv.h"
#include "system/mshv_int.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "linux/mshv.h"
#include "qemu/error-report.h"
#include "cpu.h"

#define MSHV_ENV_FIELD(env, offset) (*(uint64_t *)((char *)(env) + (offset)))

typedef struct MshvMsrEnvMap {
    uint32_t msr_index;
    uint32_t hv_name;
    ptrdiff_t env_offset;
} MshvMsrEnvMap;

/* We assert that 64bit access to sysenter_cs is safe because of padding */
QEMU_BUILD_BUG_ON(offsetof(CPUX86State, sysenter_esp) -
                  offsetof(CPUX86State, sysenter_cs)
                  < sizeof(uint64_t));

/* Those MSRs have a direct mapping to fields in CPUX86State  */
static const MshvMsrEnvMap msr_env_map[] = {
    /* Architectural */
    { IA32_MSR_EFER, HV_X64_REGISTER_EFER, offsetof(CPUX86State, efer) },
    { IA32_MSR_PAT,  HV_X64_REGISTER_PAT,  offsetof(CPUX86State, pat) },

    /* Syscall */
    { IA32_MSR_SYSENTER_CS,    HV_X64_REGISTER_SYSENTER_CS,
                               offsetof(CPUX86State, sysenter_cs) },
    { IA32_MSR_SYSENTER_ESP,   HV_X64_REGISTER_SYSENTER_ESP,
                               offsetof(CPUX86State, sysenter_esp) },
    { IA32_MSR_SYSENTER_EIP,   HV_X64_REGISTER_SYSENTER_EIP,
                               offsetof(CPUX86State, sysenter_eip) },
    { IA32_MSR_STAR,           HV_X64_REGISTER_STAR,
                               offsetof(CPUX86State, star) },
    { IA32_MSR_LSTAR,          HV_X64_REGISTER_LSTAR,
                               offsetof(CPUX86State, lstar) },
    { IA32_MSR_CSTAR,          HV_X64_REGISTER_CSTAR,
                               offsetof(CPUX86State, cstar) },
    { IA32_MSR_SFMASK,         HV_X64_REGISTER_SFMASK,
                               offsetof(CPUX86State, fmask) },
    { IA32_MSR_KERNEL_GS_BASE, HV_X64_REGISTER_KERNEL_GS_BASE,
                               offsetof(CPUX86State, kernelgsbase) },

    /* TSC-related */
    { IA32_MSR_TSC,          HV_X64_REGISTER_TSC,
                             offsetof(CPUX86State, tsc) },
    { IA32_MSR_TSC_AUX,      HV_X64_REGISTER_TSC_AUX,
                             offsetof(CPUX86State, tsc_aux) },
    { IA32_MSR_TSC_ADJUST,   HV_X64_REGISTER_TSC_ADJUST,
                             offsetof(CPUX86State, tsc_adjust) },

    /* Hyper-V per-partition MSRs */
    { HV_X64_MSR_HYPERCALL,     HV_X64_REGISTER_HYPERCALL,
                                offsetof(CPUX86State, msr_hv_hypercall) },
    { HV_X64_MSR_GUEST_OS_ID,   HV_REGISTER_GUEST_OS_ID,
                                offsetof(CPUX86State, msr_hv_guest_os_id) },
    { HV_X64_MSR_REFERENCE_TSC, HV_REGISTER_REFERENCE_TSC,
                                offsetof(CPUX86State, msr_hv_tsc) },

    /* Hyper-V MSRs (non-SINT) */
    { HV_X64_MSR_SCONTROL,  HV_REGISTER_SCONTROL,
                            offsetof(CPUX86State, msr_hv_synic_control) },
    { HV_X64_MSR_SIEFP,     HV_REGISTER_SIEFP,
                            offsetof(CPUX86State, msr_hv_synic_evt_page) },
    { HV_X64_MSR_SIMP,      HV_REGISTER_SIMP,
                            offsetof(CPUX86State, msr_hv_synic_msg_page) },

    /* MTRR default type */
    { IA32_MSR_MTRR_DEF_TYPE, HV_X64_REGISTER_MSR_MTRR_DEF_TYPE,
                              offsetof(CPUX86State, mtrr_deftype) },

    /* CET / Shadow Stack */
    { MSR_IA32_U_CET,       HV_X64_REGISTER_U_CET,
                            offsetof(CPUX86State, u_cet) },
    { MSR_IA32_S_CET,       HV_X64_REGISTER_S_CET,
                            offsetof(CPUX86State, s_cet) },
    { MSR_IA32_PL0_SSP,     HV_X64_REGISTER_PL0_SSP,
                            offsetof(CPUX86State, pl0_ssp) },
    { MSR_IA32_PL1_SSP,     HV_X64_REGISTER_PL1_SSP,
                            offsetof(CPUX86State, pl1_ssp) },
    { MSR_IA32_PL2_SSP,     HV_X64_REGISTER_PL2_SSP,
                            offsetof(CPUX86State, pl2_ssp) },
    { MSR_IA32_PL3_SSP,     HV_X64_REGISTER_PL3_SSP,
                            offsetof(CPUX86State, pl3_ssp) },
    { MSR_IA32_INT_SSP_TAB, HV_X64_REGISTER_INTERRUPT_SSP_TABLE_ADDR,
                            offsetof(CPUX86State, int_ssp_table) },

    /* XSAVE Supervisor State */
    { MSR_IA32_XSS,         HV_X64_REGISTER_U_XSS,
                            offsetof(CPUX86State, xss) },

    /* Other */

    /* TODO: find out processor features that correlate to unsupported MSRs. */
    /* { IA32_MSR_MISC_ENABLE, HV_X64_REGISTER_MSR_IA32_MISC_ENABLE, */
    /*                         offsetof(CPUX86State, msr_ia32_misc_enable) }, */
    /* { IA32_MSR_BNDCFGS,     HV_X64_REGISTER_BNDCFGS, */
    /*                         offsetof(CPUX86State, msr_bndcfgs) }, */
    { IA32_MSR_SPEC_CTRL,   HV_X64_REGISTER_SPEC_CTRL,
                            offsetof(CPUX86State, spec_ctrl) },
};

/*
 * The assocs have to be set according to this schema:
 *      8  entries for 0-7 mtrr_base
 *      8  entries for mtrr_mask 0-7
 *      11 entries for 1 x 64k, 2 x 16k, 8 x 4k fixed MTRR
 *      27 total entries
 */

#define MSHV_MTRR_MSR_COUNT 27
#define MSHV_MSR_TOTAL_COUNT (ARRAY_SIZE(msr_env_map) + MSHV_MTRR_MSR_COUNT)

static void store_in_env_mtrr_phys(CPUState *cpu,
                                   const struct hv_register_assoc *assocs,
                                   size_t n_assocs)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    size_t i, fixed_offset;
    hv_register_name hv_name;
    uint64_t base, mask;

    assert(n_assocs == MSHV_MTRR_MSR_COUNT);

    for (i = 0; i < MSR_MTRRcap_VCNT; i++) {
        hv_name = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE0 + i;
        assert(assocs[i].name == hv_name);
        hv_name = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK0 + i;
        assert(assocs[i + MSR_MTRRcap_VCNT].name == hv_name);

        base = assocs[i].value.reg64;
        mask = assocs[i + MSR_MTRRcap_VCNT].value.reg64;
        env->mtrr_var[i].base = base;
        env->mtrr_var[i].mask = mask;
    }

    /* fixed 1x 64, 2x 16, 8x 4 kB */
    fixed_offset = MSR_MTRRcap_VCNT * 2;
    for (i = 0; i < 11; i++) {
        hv_name = HV_X64_REGISTER_MSR_MTRR_FIX64K00000 + i;
        assert(assocs[fixed_offset + i].name == hv_name);
        env->mtrr_fixed[i] = assocs[fixed_offset + i].value.reg64;
    }
}

/*
 * The assocs have to be set according to this schema:
 *      8  entries for 0-7 mtrr_base
 *      8  entries for mtrr_mask 0-7
 *      11 entries for 1 x 64k, 2 x 16k, 8 x 4k fixed MTRR
 *      27 total entries
 */
static void load_from_env_mtrr_phys(const CPUState *cpu,
                                    struct hv_register_assoc *assocs,
                                    size_t n_assocs)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    size_t i, fixed_offset;
    uint64_t base, mask, fixed_value;
    hv_register_name base_name, mask_name, fixed_name;
    hv_register_assoc *assoc;

    assert(n_assocs == MSHV_MTRR_MSR_COUNT);

    for (i = 0; i < MSR_MTRRcap_VCNT; i++) {
        base = env->mtrr_var[i].base;
        mask = env->mtrr_var[i].mask;

        base_name = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE0 + i;
        mask_name = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK0 + i;

        assoc = &assocs[i];
        assoc->name = base_name;
        assoc->value.reg64 = base;

        assoc = &assocs[i + MSR_MTRRcap_VCNT];
        assoc->name = mask_name;
        assoc->value.reg64 = mask;
    }

    /* fixed 1x 64, 2x 16, 8x 4 kB */
    fixed_offset = MSR_MTRRcap_VCNT * 2;
    for (i = 0; i < 11; i++) {
        fixed_name = HV_X64_REGISTER_MSR_MTRR_FIX64K00000 + i;
        fixed_value = env->mtrr_fixed[i];

        assoc = &assocs[fixed_offset + i];
        assoc->name = fixed_name;
        assoc->value.reg64 = fixed_value;
    }
}

int mshv_init_msrs(const CPUState *cpu)
{
    int ret;
    uint64_t d_t = MSR_MTRR_ENABLE | MSR_MTRR_MEM_TYPE_WB;

    const struct hv_register_assoc assocs[] = {
        { .name = HV_X64_REGISTER_SYSENTER_CS,       .value.reg64 = 0x0 },
        { .name = HV_X64_REGISTER_SYSENTER_ESP,      .value.reg64 = 0x0 },
        { .name = HV_X64_REGISTER_SYSENTER_EIP,      .value.reg64 = 0x0 },
        { .name = HV_X64_REGISTER_STAR,              .value.reg64 = 0x0 },
        { .name = HV_X64_REGISTER_CSTAR,             .value.reg64 = 0x0 },
        { .name = HV_X64_REGISTER_LSTAR,             .value.reg64 = 0x0 },
        { .name = HV_X64_REGISTER_KERNEL_GS_BASE,    .value.reg64 = 0x0 },
        { .name = HV_X64_REGISTER_SFMASK,            .value.reg64 = 0x0 },
        { .name = HV_X64_REGISTER_MSR_MTRR_DEF_TYPE, .value.reg64 = d_t },
    };

    ret = mshv_set_generic_regs(cpu, assocs, ARRAY_SIZE(assocs));
    if (ret < 0) {
        error_report("failed to put msrs");
        return -1;
    }

    return 0;
}


/*
 * INVARIANT: this fn expects assocs in the same order as they appear in
 * msr_env_map.
 */
static void store_in_env(CPUState *cpu, const struct hv_register_assoc *assocs,
                         size_t n_assocs)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    size_t i, j;
    const MshvMsrEnvMap *mapping;
    union hv_register_value hv_value;
    ptrdiff_t offset;
    uint32_t hv_name;
    size_t mtrr_index;

    assert(n_assocs <= MSHV_MSR_TOTAL_COUNT);

    for (i = 0, j = 0; i < ARRAY_SIZE(msr_env_map); i++) {
        hv_name = assocs[j].name;
        mapping = &msr_env_map[i];
        if (hv_name != mapping->hv_name) {
            continue;
        }

        hv_value = assocs[j].value;
        offset = mapping->env_offset;
        MSHV_ENV_FIELD(env, offset) = hv_value.reg64;
        j++;
    }

    mtrr_index = j;
    store_in_env_mtrr_phys(cpu, &assocs[mtrr_index], MSHV_MTRR_MSR_COUNT);
}

static void set_hv_name_in_assocs(struct hv_register_assoc *assocs,
                                  size_t n_assocs)
{
    size_t i;
    size_t mtrr_offset, mtrr_fixed_offset;
    hv_register_name hv_name;

    assert(n_assocs == MSHV_MSR_TOTAL_COUNT);

    for (i = 0; i < ARRAY_SIZE(msr_env_map); i++) {
        assocs[i].name = msr_env_map[i].hv_name;
    }

    mtrr_offset = ARRAY_SIZE(msr_env_map);
    for (i = 0; i < MSR_MTRRcap_VCNT; i++) {
        hv_name = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE0 + i;
        assocs[mtrr_offset + i].name = hv_name;
        hv_name = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK0 + i;
        assocs[mtrr_offset + MSR_MTRRcap_VCNT + i].name = hv_name;
    }

    /* fixed 1x 64, 2x 16, 8x 4 kB */
    mtrr_fixed_offset = mtrr_offset + MSR_MTRRcap_VCNT * 2;
    for (i = 0; i < 11; i++) {
        hv_name = HV_X64_REGISTER_MSR_MTRR_FIX64K00000 + i;
        assocs[mtrr_fixed_offset + i].name = hv_name;
    }
}

static bool msr_supported(uint32_t name)
{
    /*
     * This check is not done comprehensively, it's meant to avoid hvcall
     * failures for certain MSRs on architectures that don't support them.
     */

    switch (name) {
    case HV_X64_REGISTER_SPEC_CTRL:
        return mshv_state->processor_features.ibrs_support;
    case HV_X64_REGISTER_TSC_ADJUST:
        return mshv_state->processor_features.tsc_adjust_support;
    case HV_X64_REGISTER_U_CET:
    case HV_X64_REGISTER_S_CET:
    case HV_X64_REGISTER_PL0_SSP:
    case HV_X64_REGISTER_PL1_SSP:
    case HV_X64_REGISTER_PL2_SSP:
    case HV_X64_REGISTER_PL3_SSP:
    case HV_X64_REGISTER_INTERRUPT_SSP_TABLE_ADDR:
    case HV_X64_REGISTER_U_XSS:
        return mshv_state->processor_features.cet_ss_support ||
               mshv_state->processor_features.cet_ibt_support;
    }

    return true;
}

int mshv_get_msrs(CPUState *cpu)
{
    int ret = 0;
    size_t n_assocs = MSHV_MSR_TOTAL_COUNT;
    struct hv_register_assoc assocs[MSHV_MSR_TOTAL_COUNT];
    size_t i, j;
    uint32_t name;

    set_hv_name_in_assocs(assocs, n_assocs);

    /* Filter out MSRs that cannot be read */
    for (i = 0, j = 0; i < n_assocs; i++) {
        name = assocs[i].name;

        if (!msr_supported(name)) {
            continue;
        }

        if (j != i) {
            assocs[j] = assocs[i];
        }
        j++;
    }
    n_assocs = j;

    ret = mshv_get_generic_regs(cpu, assocs, n_assocs);
    if (ret < 0) {
        error_report("Failed to get MSRs");
        return -errno;
    }

    store_in_env(cpu, assocs, n_assocs);

    return 0;
}

static void load_from_env(const CPUState *cpu, struct hv_register_assoc *assocs,
                          size_t n_assocs)
{
    size_t i;
    const MshvMsrEnvMap *mapping;
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;
    ptrdiff_t offset;
    union hv_register_value *hv_value;
    size_t mtrr_offset;

    assert(n_assocs == MSHV_MSR_TOTAL_COUNT);

    for (i = 0; i < ARRAY_SIZE(msr_env_map); i++) {
        mapping = &msr_env_map[i];
        offset = mapping->env_offset;
        assocs[i].name = mapping->hv_name;
        hv_value = &assocs[i].value;
        hv_value->reg64 = MSHV_ENV_FIELD(env, offset);
    }

    mtrr_offset = ARRAY_SIZE(msr_env_map);
    load_from_env_mtrr_phys(cpu, &assocs[mtrr_offset], MSHV_MTRR_MSR_COUNT);
}

int mshv_set_msrs(const CPUState *cpu)
{
    size_t n_assocs = MSHV_MSR_TOTAL_COUNT;
    struct hv_register_assoc assocs[MSHV_MSR_TOTAL_COUNT];
    int ret;
    size_t i, j;

    load_from_env(cpu, assocs, n_assocs);

    /* Filter out MSRs that cannot be written */
    for (i = 0, j = 0; i < n_assocs; i++) {
        uint32_t name = assocs[i].name;

        /* Partition-wide MSRs: only write on vCPU 0 */
        if (cpu->cpu_index != 0 &&
            (name == HV_X64_REGISTER_HYPERCALL ||
             name == HV_REGISTER_GUEST_OS_ID ||
             name == HV_REGISTER_REFERENCE_TSC)) {
            continue;
        }

        if (!msr_supported(name)) {
            continue;
        }

        if (j != i) {
            assocs[j] = assocs[i];
        }
        j++;
    }
    n_assocs = j;

    ret = mshv_set_generic_regs(cpu, assocs, n_assocs);
    if (ret < 0) {
        error_report("Failed to set MSRs");
        return -errno;
    }

    return 0;
}
