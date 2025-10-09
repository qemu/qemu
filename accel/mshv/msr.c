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

static uint32_t supported_msrs[64] = {
    IA32_MSR_TSC,
    IA32_MSR_EFER,
    IA32_MSR_KERNEL_GS_BASE,
    IA32_MSR_APIC_BASE,
    IA32_MSR_PAT,
    IA32_MSR_SYSENTER_CS,
    IA32_MSR_SYSENTER_ESP,
    IA32_MSR_SYSENTER_EIP,
    IA32_MSR_STAR,
    IA32_MSR_LSTAR,
    IA32_MSR_CSTAR,
    IA32_MSR_SFMASK,
    IA32_MSR_MTRR_DEF_TYPE,
    IA32_MSR_MTRR_PHYSBASE0,
    IA32_MSR_MTRR_PHYSMASK0,
    IA32_MSR_MTRR_PHYSBASE1,
    IA32_MSR_MTRR_PHYSMASK1,
    IA32_MSR_MTRR_PHYSBASE2,
    IA32_MSR_MTRR_PHYSMASK2,
    IA32_MSR_MTRR_PHYSBASE3,
    IA32_MSR_MTRR_PHYSMASK3,
    IA32_MSR_MTRR_PHYSBASE4,
    IA32_MSR_MTRR_PHYSMASK4,
    IA32_MSR_MTRR_PHYSBASE5,
    IA32_MSR_MTRR_PHYSMASK5,
    IA32_MSR_MTRR_PHYSBASE6,
    IA32_MSR_MTRR_PHYSMASK6,
    IA32_MSR_MTRR_PHYSBASE7,
    IA32_MSR_MTRR_PHYSMASK7,
    IA32_MSR_MTRR_FIX64K_00000,
    IA32_MSR_MTRR_FIX16K_80000,
    IA32_MSR_MTRR_FIX16K_A0000,
    IA32_MSR_MTRR_FIX4K_C0000,
    IA32_MSR_MTRR_FIX4K_C8000,
    IA32_MSR_MTRR_FIX4K_D0000,
    IA32_MSR_MTRR_FIX4K_D8000,
    IA32_MSR_MTRR_FIX4K_E0000,
    IA32_MSR_MTRR_FIX4K_E8000,
    IA32_MSR_MTRR_FIX4K_F0000,
    IA32_MSR_MTRR_FIX4K_F8000,
    IA32_MSR_TSC_AUX,
    IA32_MSR_DEBUG_CTL,
    HV_X64_MSR_GUEST_OS_ID,
    HV_X64_MSR_SINT0,
    HV_X64_MSR_SINT1,
    HV_X64_MSR_SINT2,
    HV_X64_MSR_SINT3,
    HV_X64_MSR_SINT4,
    HV_X64_MSR_SINT5,
    HV_X64_MSR_SINT6,
    HV_X64_MSR_SINT7,
    HV_X64_MSR_SINT8,
    HV_X64_MSR_SINT9,
    HV_X64_MSR_SINT10,
    HV_X64_MSR_SINT11,
    HV_X64_MSR_SINT12,
    HV_X64_MSR_SINT13,
    HV_X64_MSR_SINT14,
    HV_X64_MSR_SINT15,
    HV_X64_MSR_SCONTROL,
    HV_X64_MSR_SIEFP,
    HV_X64_MSR_SIMP,
    HV_X64_MSR_REFERENCE_TSC,
    HV_X64_MSR_EOM,
};
static const size_t msr_count = ARRAY_SIZE(supported_msrs);

static int compare_msr_index(const void *a, const void *b)
{
    return *(uint32_t *)a - *(uint32_t *)b;
}

__attribute__((constructor))
static void init_sorted_msr_map(void)
{
    qsort(supported_msrs, msr_count, sizeof(uint32_t), compare_msr_index);
}

static int mshv_is_supported_msr(uint32_t msr)
{
    return bsearch(&msr, supported_msrs, msr_count, sizeof(uint32_t),
                   compare_msr_index) != NULL;
}

static int mshv_msr_to_hv_reg_name(uint32_t msr, uint32_t *hv_reg)
{
    switch (msr) {
    case IA32_MSR_TSC:
        *hv_reg = HV_X64_REGISTER_TSC;
        return 0;
    case IA32_MSR_EFER:
        *hv_reg = HV_X64_REGISTER_EFER;
        return 0;
    case IA32_MSR_KERNEL_GS_BASE:
        *hv_reg = HV_X64_REGISTER_KERNEL_GS_BASE;
        return 0;
    case IA32_MSR_APIC_BASE:
        *hv_reg = HV_X64_REGISTER_APIC_BASE;
        return 0;
    case IA32_MSR_PAT:
        *hv_reg = HV_X64_REGISTER_PAT;
        return 0;
    case IA32_MSR_SYSENTER_CS:
        *hv_reg = HV_X64_REGISTER_SYSENTER_CS;
        return 0;
    case IA32_MSR_SYSENTER_ESP:
        *hv_reg = HV_X64_REGISTER_SYSENTER_ESP;
        return 0;
    case IA32_MSR_SYSENTER_EIP:
        *hv_reg = HV_X64_REGISTER_SYSENTER_EIP;
        return 0;
    case IA32_MSR_STAR:
        *hv_reg = HV_X64_REGISTER_STAR;
        return 0;
    case IA32_MSR_LSTAR:
        *hv_reg = HV_X64_REGISTER_LSTAR;
        return 0;
    case IA32_MSR_CSTAR:
        *hv_reg = HV_X64_REGISTER_CSTAR;
        return 0;
    case IA32_MSR_SFMASK:
        *hv_reg = HV_X64_REGISTER_SFMASK;
        return 0;
    case IA32_MSR_MTRR_CAP:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_CAP;
        return 0;
    case IA32_MSR_MTRR_DEF_TYPE:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_DEF_TYPE;
        return 0;
    case IA32_MSR_MTRR_PHYSBASE0:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE0;
        return 0;
    case IA32_MSR_MTRR_PHYSMASK0:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK0;
        return 0;
    case IA32_MSR_MTRR_PHYSBASE1:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE1;
        return 0;
    case IA32_MSR_MTRR_PHYSMASK1:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK1;
        return 0;
    case IA32_MSR_MTRR_PHYSBASE2:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE2;
        return 0;
    case IA32_MSR_MTRR_PHYSMASK2:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK2;
        return 0;
    case IA32_MSR_MTRR_PHYSBASE3:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE3;
        return 0;
    case IA32_MSR_MTRR_PHYSMASK3:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK3;
        return 0;
    case IA32_MSR_MTRR_PHYSBASE4:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE4;
        return 0;
    case IA32_MSR_MTRR_PHYSMASK4:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK4;
        return 0;
    case IA32_MSR_MTRR_PHYSBASE5:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE5;
        return 0;
    case IA32_MSR_MTRR_PHYSMASK5:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK5;
        return 0;
    case IA32_MSR_MTRR_PHYSBASE6:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE6;
        return 0;
    case IA32_MSR_MTRR_PHYSMASK6:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK6;
        return 0;
    case IA32_MSR_MTRR_PHYSBASE7:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_BASE7;
        return 0;
    case IA32_MSR_MTRR_PHYSMASK7:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_PHYS_MASK7;
        return 0;
    case IA32_MSR_MTRR_FIX64K_00000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX64K00000;
        return 0;
    case IA32_MSR_MTRR_FIX16K_80000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX16K80000;
        return 0;
    case IA32_MSR_MTRR_FIX16K_A0000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX16KA0000;
        return 0;
    case IA32_MSR_MTRR_FIX4K_C0000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX4KC0000;
        return 0;
    case IA32_MSR_MTRR_FIX4K_C8000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX4KC8000;
        return 0;
    case IA32_MSR_MTRR_FIX4K_D0000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX4KD0000;
        return 0;
    case IA32_MSR_MTRR_FIX4K_D8000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX4KD8000;
        return 0;
    case IA32_MSR_MTRR_FIX4K_E0000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX4KE0000;
        return 0;
    case IA32_MSR_MTRR_FIX4K_E8000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX4KE8000;
        return 0;
    case IA32_MSR_MTRR_FIX4K_F0000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX4KF0000;
        return 0;
    case IA32_MSR_MTRR_FIX4K_F8000:
        *hv_reg = HV_X64_REGISTER_MSR_MTRR_FIX4KF8000;
        return 0;
    case IA32_MSR_TSC_AUX:
        *hv_reg = HV_X64_REGISTER_TSC_AUX;
        return 0;
    case IA32_MSR_BNDCFGS:
        *hv_reg = HV_X64_REGISTER_BNDCFGS;
        return 0;
    case IA32_MSR_DEBUG_CTL:
        *hv_reg = HV_X64_REGISTER_DEBUG_CTL;
        return 0;
    case IA32_MSR_TSC_ADJUST:
        *hv_reg = HV_X64_REGISTER_TSC_ADJUST;
        return 0;
    case IA32_MSR_SPEC_CTRL:
        *hv_reg = HV_X64_REGISTER_SPEC_CTRL;
        return 0;
    case HV_X64_MSR_GUEST_OS_ID:
        *hv_reg = HV_REGISTER_GUEST_OS_ID;
        return 0;
    case HV_X64_MSR_SINT0:
        *hv_reg = HV_REGISTER_SINT0;
        return 0;
    case HV_X64_MSR_SINT1:
        *hv_reg = HV_REGISTER_SINT1;
        return 0;
    case HV_X64_MSR_SINT2:
        *hv_reg = HV_REGISTER_SINT2;
        return 0;
    case HV_X64_MSR_SINT3:
        *hv_reg = HV_REGISTER_SINT3;
        return 0;
    case HV_X64_MSR_SINT4:
        *hv_reg = HV_REGISTER_SINT4;
        return 0;
    case HV_X64_MSR_SINT5:
        *hv_reg = HV_REGISTER_SINT5;
        return 0;
    case HV_X64_MSR_SINT6:
        *hv_reg = HV_REGISTER_SINT6;
        return 0;
    case HV_X64_MSR_SINT7:
        *hv_reg = HV_REGISTER_SINT7;
        return 0;
    case HV_X64_MSR_SINT8:
        *hv_reg = HV_REGISTER_SINT8;
        return 0;
    case HV_X64_MSR_SINT9:
        *hv_reg = HV_REGISTER_SINT9;
        return 0;
    case HV_X64_MSR_SINT10:
        *hv_reg = HV_REGISTER_SINT10;
        return 0;
    case HV_X64_MSR_SINT11:
        *hv_reg = HV_REGISTER_SINT11;
        return 0;
    case HV_X64_MSR_SINT12:
        *hv_reg = HV_REGISTER_SINT12;
        return 0;
    case HV_X64_MSR_SINT13:
        *hv_reg = HV_REGISTER_SINT13;
        return 0;
    case HV_X64_MSR_SINT14:
        *hv_reg = HV_REGISTER_SINT14;
        return 0;
    case HV_X64_MSR_SINT15:
        *hv_reg = HV_REGISTER_SINT15;
        return 0;
    case IA32_MSR_MISC_ENABLE:
        *hv_reg = HV_X64_REGISTER_MSR_IA32_MISC_ENABLE;
        return 0;
    case HV_X64_MSR_SCONTROL:
        *hv_reg = HV_REGISTER_SCONTROL;
        return 0;
    case HV_X64_MSR_SIEFP:
        *hv_reg = HV_REGISTER_SIEFP;
        return 0;
    case HV_X64_MSR_SIMP:
        *hv_reg = HV_REGISTER_SIMP;
        return 0;
    case HV_X64_MSR_REFERENCE_TSC:
        *hv_reg = HV_REGISTER_REFERENCE_TSC;
        return 0;
    case HV_X64_MSR_EOM:
        *hv_reg = HV_REGISTER_EOM;
        return 0;
    default:
        error_report("failed to map MSR %u to HV register name", msr);
        return -1;
    }
}

static int set_msrs(const CPUState *cpu, GList *msrs)
{
    size_t n_msrs;
    GList *entries;
    MshvMsrEntry *entry;
    enum hv_register_name name;
    struct hv_register_assoc *assoc;
    int ret;
    size_t i = 0;

    n_msrs = g_list_length(msrs);
    hv_register_assoc *assocs = g_new0(hv_register_assoc, n_msrs);

    entries = msrs;
    for (const GList *elem = entries; elem != NULL; elem = elem->next) {
        entry = elem->data;
        ret = mshv_msr_to_hv_reg_name(entry->index, &name);
        if (ret < 0) {
            g_free(assocs);
            return ret;
        }
        assoc = &assocs[i];
        assoc->name = name;
        /* the union has been initialized to 0 */
        assoc->value.reg64 = entry->data;
        i++;
    }
    ret = mshv_set_generic_regs(cpu, assocs, n_msrs);
    g_free(assocs);
    if (ret < 0) {
        error_report("failed to set msrs");
        return -1;
    }
    return 0;
}


int mshv_configure_msr(const CPUState *cpu, const MshvMsrEntry *msrs,
                       size_t n_msrs)
{
    GList *valid_msrs = NULL;
    uint32_t msr_index;
    int ret;

    for (size_t i = 0; i < n_msrs; i++) {
        msr_index = msrs[i].index;
        /* check whether index of msrs is in SUPPORTED_MSRS */
        if (mshv_is_supported_msr(msr_index)) {
            valid_msrs = g_list_append(valid_msrs, (void *) &msrs[i]);
        }
    }

    ret = set_msrs(cpu, valid_msrs);
    g_list_free(valid_msrs);

    return ret;
}
