/*
 * QEMU PowerPC pSeries Logical Partition capabilities handling
 *
 * Copyright (c) 2017 David Gibson, Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "sysemu/hw_accel.h"
#include "exec/ram_addr.h"
#include "target/ppc/cpu.h"
#include "target/ppc/mmu-hash64.h"
#include "cpu-models.h"
#include "kvm_ppc.h"
#include "migration/vmstate.h"
#include "sysemu/qtest.h"
#include "sysemu/tcg.h"

#include "hw/ppc/spapr.h"

typedef struct SpaprCapPossible {
    int num;            /* size of vals array below */
    const char *help;   /* help text for vals */
    /*
     * Note:
     * - because of the way compatibility is determined vals MUST be ordered
     *   such that later options are a superset of all preceding options.
     * - the order of vals must be preserved, that is their index is important,
     *   however vals may be added to the end of the list so long as the above
     *   point is observed
     */
    const char *vals[];
} SpaprCapPossible;

typedef struct SpaprCapabilityInfo {
    const char *name;
    const char *description;
    int index;

    /* Getter and Setter Function Pointers */
    ObjectPropertyAccessor *get;
    ObjectPropertyAccessor *set;
    const char *type;
    /* Possible values if this is a custom string type */
    SpaprCapPossible *possible;
    /* Make sure the virtual hardware can support this capability */
    void (*apply)(SpaprMachineState *spapr, uint8_t val, Error **errp);
    void (*cpu_apply)(SpaprMachineState *spapr, PowerPCCPU *cpu,
                      uint8_t val, Error **errp);
    bool (*migrate_needed)(void *opaque);
} SpaprCapabilityInfo;

static void spapr_cap_get_bool(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    SpaprCapabilityInfo *cap = opaque;
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);
    bool value = spapr_get_cap(spapr, cap->index) == SPAPR_CAP_ON;

    visit_type_bool(v, name, &value, errp);
}

static void spapr_cap_set_bool(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    SpaprCapabilityInfo *cap = opaque;
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    spapr->cmd_line_caps[cap->index] = true;
    spapr->eff.caps[cap->index] = value ? SPAPR_CAP_ON : SPAPR_CAP_OFF;
}


static void  spapr_cap_get_string(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    SpaprCapabilityInfo *cap = opaque;
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);
    char *val = NULL;
    uint8_t value = spapr_get_cap(spapr, cap->index);

    if (value >= cap->possible->num) {
        error_setg(errp, "Invalid value (%d) for cap-%s", value, cap->name);
        return;
    }

    val = g_strdup(cap->possible->vals[value]);

    visit_type_str(v, name, &val, errp);
    g_free(val);
}

static void spapr_cap_set_string(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    SpaprCapabilityInfo *cap = opaque;
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);
    uint8_t i;
    char *val;

    if (!visit_type_str(v, name, &val, errp)) {
        return;
    }

    if (!strcmp(val, "?")) {
        error_setg(errp, "%s", cap->possible->help);
        goto out;
    }
    for (i = 0; i < cap->possible->num; i++) {
        if (!strcasecmp(val, cap->possible->vals[i])) {
            spapr->cmd_line_caps[cap->index] = true;
            spapr->eff.caps[cap->index] = i;
            goto out;
        }
    }

    error_setg(errp, "Invalid capability mode \"%s\" for cap-%s", val,
               cap->name);
out:
    g_free(val);
}

static void spapr_cap_get_pagesize(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    SpaprCapabilityInfo *cap = opaque;
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);
    uint8_t val = spapr_get_cap(spapr, cap->index);
    uint64_t pagesize = (1ULL << val);

    visit_type_size(v, name, &pagesize, errp);
}

static void spapr_cap_set_pagesize(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    SpaprCapabilityInfo *cap = opaque;
    SpaprMachineState *spapr = SPAPR_MACHINE(obj);
    uint64_t pagesize;
    uint8_t val;

    if (!visit_type_size(v, name, &pagesize, errp)) {
        return;
    }

    if (!is_power_of_2(pagesize)) {
        error_setg(errp, "cap-%s must be a power of 2", cap->name);
        return;
    }

    val = ctz64(pagesize);
    spapr->cmd_line_caps[cap->index] = true;
    spapr->eff.caps[cap->index] = val;
}

static void cap_htm_apply(SpaprMachineState *spapr, uint8_t val, Error **errp)
{
    ERRP_GUARD();
    if (!val) {
        /* TODO: We don't support disabling htm yet */
        return;
    }
    if (tcg_enabled()) {
        error_setg(errp, "No Transactional Memory support in TCG");
        error_append_hint(errp, "Try appending -machine cap-htm=off\n");
    } else if (kvm_enabled() && !kvmppc_has_cap_htm()) {
        error_setg(errp,
                   "KVM implementation does not support Transactional Memory");
        error_append_hint(errp, "Try appending -machine cap-htm=off\n");
    }
}

static void cap_vsx_apply(SpaprMachineState *spapr, uint8_t val, Error **errp)
{
    ERRP_GUARD();
    PowerPCCPU *cpu = POWERPC_CPU(first_cpu);
    CPUPPCState *env = &cpu->env;

    if (!val) {
        /* TODO: We don't support disabling vsx yet */
        return;
    }
    /* Allowable CPUs in spapr_cpu_core.c should already have gotten
     * rid of anything that doesn't do VMX */
    g_assert(env->insns_flags & PPC_ALTIVEC);
    if (!(env->insns_flags2 & PPC2_VSX)) {
        error_setg(errp, "VSX support not available");
        error_append_hint(errp, "Try appending -machine cap-vsx=off\n");
    }
}

static void cap_dfp_apply(SpaprMachineState *spapr, uint8_t val, Error **errp)
{
    ERRP_GUARD();
    PowerPCCPU *cpu = POWERPC_CPU(first_cpu);
    CPUPPCState *env = &cpu->env;

    if (!val) {
        /* TODO: We don't support disabling dfp yet */
        return;
    }
    if (!(env->insns_flags2 & PPC2_DFP)) {
        error_setg(errp, "DFP support not available");
        error_append_hint(errp, "Try appending -machine cap-dfp=off\n");
    }
}

SpaprCapPossible cap_cfpc_possible = {
    .num = 3,
    .vals = {"broken", "workaround", "fixed"},
    .help = "broken - no protection, workaround - workaround available,"
            " fixed - fixed in hardware",
};

static void cap_safe_cache_apply(SpaprMachineState *spapr, uint8_t val,
                                 Error **errp)
{
    ERRP_GUARD();
    uint8_t kvm_val =  kvmppc_get_cap_safe_cache();

    if (tcg_enabled() && val) {
        /* TCG only supports broken, allow other values and print a warning */
        warn_report("TCG doesn't support requested feature, cap-cfpc=%s",
                    cap_cfpc_possible.vals[val]);
    } else if (kvm_enabled() && (val > kvm_val)) {
        error_setg(errp,
                   "Requested safe cache capability level not supported by KVM");
        error_append_hint(errp, "Try appending -machine cap-cfpc=%s\n",
                          cap_cfpc_possible.vals[kvm_val]);
    }
}

SpaprCapPossible cap_sbbc_possible = {
    .num = 3,
    .vals = {"broken", "workaround", "fixed"},
    .help = "broken - no protection, workaround - workaround available,"
            " fixed - fixed in hardware",
};

static void cap_safe_bounds_check_apply(SpaprMachineState *spapr, uint8_t val,
                                        Error **errp)
{
    ERRP_GUARD();
    uint8_t kvm_val =  kvmppc_get_cap_safe_bounds_check();

    if (tcg_enabled() && val) {
        /* TCG only supports broken, allow other values and print a warning */
        warn_report("TCG doesn't support requested feature, cap-sbbc=%s",
                    cap_sbbc_possible.vals[val]);
    } else if (kvm_enabled() && (val > kvm_val)) {
        error_setg(errp,
"Requested safe bounds check capability level not supported by KVM");
        error_append_hint(errp, "Try appending -machine cap-sbbc=%s\n",
                          cap_sbbc_possible.vals[kvm_val]);
    }
}

SpaprCapPossible cap_ibs_possible = {
    .num = 5,
    /* Note workaround only maintained for compatibility */
    .vals = {"broken", "workaround", "fixed-ibs", "fixed-ccd", "fixed-na"},
    .help = "broken - no protection, workaround - count cache flush"
            ", fixed-ibs - indirect branch serialisation,"
            " fixed-ccd - cache count disabled,"
            " fixed-na - fixed in hardware (no longer applicable)",
};

static void cap_safe_indirect_branch_apply(SpaprMachineState *spapr,
                                           uint8_t val, Error **errp)
{
    ERRP_GUARD();
    uint8_t kvm_val = kvmppc_get_cap_safe_indirect_branch();

    if (tcg_enabled() && val) {
        /* TCG only supports broken, allow other values and print a warning */
        warn_report("TCG doesn't support requested feature, cap-ibs=%s",
                    cap_ibs_possible.vals[val]);
    } else if (kvm_enabled() && (val > kvm_val)) {
        error_setg(errp,
"Requested safe indirect branch capability level not supported by KVM");
        error_append_hint(errp, "Try appending -machine cap-ibs=%s\n",
                          cap_ibs_possible.vals[kvm_val]);
    }
}

#define VALUE_DESC_TRISTATE     " (broken, workaround, fixed)"

bool spapr_check_pagesize(SpaprMachineState *spapr, hwaddr pagesize,
                          Error **errp)
{
    hwaddr maxpagesize = (1ULL << spapr->eff.caps[SPAPR_CAP_HPT_MAXPAGESIZE]);

    if (!kvmppc_hpt_needs_host_contiguous_pages()) {
        return true;
    }

    if (maxpagesize > pagesize) {
        error_setg(errp,
                   "Can't support %"HWADDR_PRIu" kiB guest pages with %"
                   HWADDR_PRIu" kiB host pages with this KVM implementation",
                   maxpagesize >> 10, pagesize >> 10);
        return false;
    }

    return true;
}

static void cap_hpt_maxpagesize_apply(SpaprMachineState *spapr,
                                      uint8_t val, Error **errp)
{
    if (val < 12) {
        error_setg(errp, "Require at least 4kiB hpt-max-page-size");
        return;
    } else if (val < 16) {
        warn_report("Many guests require at least 64kiB hpt-max-page-size");
    }

    spapr_check_pagesize(spapr, qemu_minrampagesize(), errp);
}

static bool cap_hpt_maxpagesize_migrate_needed(void *opaque)
{
    return !SPAPR_MACHINE_GET_CLASS(opaque)->pre_4_1_migration;
}

static bool spapr_pagesize_cb(void *opaque, uint32_t seg_pshift,
                              uint32_t pshift)
{
    unsigned maxshift = *((unsigned *)opaque);

    assert(pshift >= seg_pshift);

    /* Don't allow the guest to use pages bigger than the configured
     * maximum size */
    if (pshift > maxshift) {
        return false;
    }

    /* For whatever reason, KVM doesn't allow multiple pagesizes
     * within a segment, *except* for the case of 16M pages in a 4k or
     * 64k segment.  Always exclude other cases, so that TCG and KVM
     * guests see a consistent environment */
    if ((pshift != seg_pshift) && (pshift != 24)) {
        return false;
    }

    return true;
}

static void cap_hpt_maxpagesize_cpu_apply(SpaprMachineState *spapr,
                                          PowerPCCPU *cpu,
                                          uint8_t val, Error **errp)
{
    unsigned maxshift = val;

    ppc_hash64_filter_pagesizes(cpu, spapr_pagesize_cb, &maxshift);
}

static void cap_nested_kvm_hv_apply(SpaprMachineState *spapr,
                                    uint8_t val, Error **errp)
{
    ERRP_GUARD();
    PowerPCCPU *cpu = POWERPC_CPU(first_cpu);

    if (!val) {
        /* capability disabled by default */
        return;
    }

    if (tcg_enabled()) {
        error_setg(errp, "No Nested KVM-HV support in TCG");
        error_append_hint(errp, "Try appending -machine cap-nested-hv=off\n");
    } else if (kvm_enabled()) {
        if (!ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_00, 0,
                              spapr->max_compat_pvr)) {
            error_setg(errp, "Nested KVM-HV only supported on POWER9");
            error_append_hint(errp,
                              "Try appending -machine max-cpu-compat=power9\n");
            return;
        }

        if (!kvmppc_has_cap_nested_kvm_hv()) {
            error_setg(errp,
                       "KVM implementation does not support Nested KVM-HV");
            error_append_hint(errp,
                              "Try appending -machine cap-nested-hv=off\n");
        } else if (kvmppc_set_cap_nested_kvm_hv(val) < 0) {
                error_setg(errp, "Error enabling cap-nested-hv with KVM");
                error_append_hint(errp,
                                  "Try appending -machine cap-nested-hv=off\n");
        }
    }
}

static void cap_large_decr_apply(SpaprMachineState *spapr,
                                 uint8_t val, Error **errp)
{
    ERRP_GUARD();
    PowerPCCPU *cpu = POWERPC_CPU(first_cpu);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    if (!val) {
        return; /* Disabled by default */
    }

    if (tcg_enabled()) {
        if (!ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_00, 0,
                              spapr->max_compat_pvr)) {
            error_setg(errp, "Large decrementer only supported on POWER9");
            error_append_hint(errp, "Try -cpu POWER9\n");
            return;
        }
    } else if (kvm_enabled()) {
        int kvm_nr_bits = kvmppc_get_cap_large_decr();

        if (!kvm_nr_bits) {
            error_setg(errp, "No large decrementer support");
            error_append_hint(errp,
                              "Try appending -machine cap-large-decr=off\n");
        } else if (pcc->lrg_decr_bits != kvm_nr_bits) {
            error_setg(errp,
                       "KVM large decrementer size (%d) differs to model (%d)",
                       kvm_nr_bits, pcc->lrg_decr_bits);
            error_append_hint(errp,
                              "Try appending -machine cap-large-decr=off\n");
        }
    }
}

static void cap_large_decr_cpu_apply(SpaprMachineState *spapr,
                                     PowerPCCPU *cpu,
                                     uint8_t val, Error **errp)
{
    ERRP_GUARD();
    CPUPPCState *env = &cpu->env;
    target_ulong lpcr = env->spr[SPR_LPCR];

    if (kvm_enabled()) {
        if (kvmppc_enable_cap_large_decr(cpu, val)) {
            error_setg(errp, "No large decrementer support");
            error_append_hint(errp,
                              "Try appending -machine cap-large-decr=off\n");
        }
    }

    if (val) {
        lpcr |= LPCR_LD;
    } else {
        lpcr &= ~LPCR_LD;
    }
    ppc_store_lpcr(cpu, lpcr);
}

static void cap_ccf_assist_apply(SpaprMachineState *spapr, uint8_t val,
                                 Error **errp)
{
    ERRP_GUARD();
    uint8_t kvm_val = kvmppc_get_cap_count_cache_flush_assist();

    if (tcg_enabled() && val) {
        /* TCG doesn't implement anything here, but allow with a warning */
        warn_report("TCG doesn't support requested feature, cap-ccf-assist=on");
    } else if (kvm_enabled() && (val > kvm_val)) {
        uint8_t kvm_ibs = kvmppc_get_cap_safe_indirect_branch();

        if (kvm_ibs == SPAPR_CAP_FIXED_CCD) {
            /*
             * If we don't have CCF assist on the host, the assist
             * instruction is a harmless no-op.  It won't correctly
             * implement the cache count flush *but* if we have
             * count-cache-disabled in the host, that flush is
             * unnnecessary.  So, specifically allow this case.  This
             * allows us to have better performance on POWER9 DD2.3,
             * while still working on POWER9 DD2.2 and POWER8 host
             * cpus.
             */
            return;
        }
        error_setg(errp,
                   "Requested count cache flush assist capability level not supported by KVM");
        error_append_hint(errp, "Try appending -machine cap-ccf-assist=off\n");
    }
}

static void cap_fwnmi_apply(SpaprMachineState *spapr, uint8_t val,
                                Error **errp)
{
    ERRP_GUARD();
    if (!val) {
        return; /* Disabled by default */
    }

    if (kvm_enabled()) {
        if (!kvmppc_get_fwnmi()) {
            error_setg(errp,
"Firmware Assisted Non-Maskable Interrupts(FWNMI) not supported by KVM.");
            error_append_hint(errp, "Try appending -machine cap-fwnmi=off\n");
        }
    }
}

SpaprCapabilityInfo capability_table[SPAPR_CAP_NUM] = {
    [SPAPR_CAP_HTM] = {
        .name = "htm",
        .description = "Allow Hardware Transactional Memory (HTM)",
        .index = SPAPR_CAP_HTM,
        .get = spapr_cap_get_bool,
        .set = spapr_cap_set_bool,
        .type = "bool",
        .apply = cap_htm_apply,
    },
    [SPAPR_CAP_VSX] = {
        .name = "vsx",
        .description = "Allow Vector Scalar Extensions (VSX)",
        .index = SPAPR_CAP_VSX,
        .get = spapr_cap_get_bool,
        .set = spapr_cap_set_bool,
        .type = "bool",
        .apply = cap_vsx_apply,
    },
    [SPAPR_CAP_DFP] = {
        .name = "dfp",
        .description = "Allow Decimal Floating Point (DFP)",
        .index = SPAPR_CAP_DFP,
        .get = spapr_cap_get_bool,
        .set = spapr_cap_set_bool,
        .type = "bool",
        .apply = cap_dfp_apply,
    },
    [SPAPR_CAP_CFPC] = {
        .name = "cfpc",
        .description = "Cache Flush on Privilege Change" VALUE_DESC_TRISTATE,
        .index = SPAPR_CAP_CFPC,
        .get = spapr_cap_get_string,
        .set = spapr_cap_set_string,
        .type = "string",
        .possible = &cap_cfpc_possible,
        .apply = cap_safe_cache_apply,
    },
    [SPAPR_CAP_SBBC] = {
        .name = "sbbc",
        .description = "Speculation Barrier Bounds Checking" VALUE_DESC_TRISTATE,
        .index = SPAPR_CAP_SBBC,
        .get = spapr_cap_get_string,
        .set = spapr_cap_set_string,
        .type = "string",
        .possible = &cap_sbbc_possible,
        .apply = cap_safe_bounds_check_apply,
    },
    [SPAPR_CAP_IBS] = {
        .name = "ibs",
        .description =
            "Indirect Branch Speculation (broken, workaround, fixed-ibs,"
            "fixed-ccd, fixed-na)",
        .index = SPAPR_CAP_IBS,
        .get = spapr_cap_get_string,
        .set = spapr_cap_set_string,
        .type = "string",
        .possible = &cap_ibs_possible,
        .apply = cap_safe_indirect_branch_apply,
    },
    [SPAPR_CAP_HPT_MAXPAGESIZE] = {
        .name = "hpt-max-page-size",
        .description = "Maximum page size for Hash Page Table guests",
        .index = SPAPR_CAP_HPT_MAXPAGESIZE,
        .get = spapr_cap_get_pagesize,
        .set = spapr_cap_set_pagesize,
        .type = "int",
        .apply = cap_hpt_maxpagesize_apply,
        .cpu_apply = cap_hpt_maxpagesize_cpu_apply,
        .migrate_needed = cap_hpt_maxpagesize_migrate_needed,
    },
    [SPAPR_CAP_NESTED_KVM_HV] = {
        .name = "nested-hv",
        .description = "Allow Nested KVM-HV",
        .index = SPAPR_CAP_NESTED_KVM_HV,
        .get = spapr_cap_get_bool,
        .set = spapr_cap_set_bool,
        .type = "bool",
        .apply = cap_nested_kvm_hv_apply,
    },
    [SPAPR_CAP_LARGE_DECREMENTER] = {
        .name = "large-decr",
        .description = "Allow Large Decrementer",
        .index = SPAPR_CAP_LARGE_DECREMENTER,
        .get = spapr_cap_get_bool,
        .set = spapr_cap_set_bool,
        .type = "bool",
        .apply = cap_large_decr_apply,
        .cpu_apply = cap_large_decr_cpu_apply,
    },
    [SPAPR_CAP_CCF_ASSIST] = {
        .name = "ccf-assist",
        .description = "Count Cache Flush Assist via HW Instruction",
        .index = SPAPR_CAP_CCF_ASSIST,
        .get = spapr_cap_get_bool,
        .set = spapr_cap_set_bool,
        .type = "bool",
        .apply = cap_ccf_assist_apply,
    },
    [SPAPR_CAP_FWNMI] = {
        .name = "fwnmi",
        .description = "Implements PAPR FWNMI option",
        .index = SPAPR_CAP_FWNMI,
        .get = spapr_cap_get_bool,
        .set = spapr_cap_set_bool,
        .type = "bool",
        .apply = cap_fwnmi_apply,
    },
};

static SpaprCapabilities default_caps_with_cpu(SpaprMachineState *spapr,
                                               const char *cputype)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    SpaprCapabilities caps;

    caps = smc->default_caps;

    if (!ppc_type_check_compat(cputype, CPU_POWERPC_LOGICAL_3_00,
                               0, spapr->max_compat_pvr)) {
        caps.caps[SPAPR_CAP_LARGE_DECREMENTER] = SPAPR_CAP_OFF;
    }

    if (!ppc_type_check_compat(cputype, CPU_POWERPC_LOGICAL_2_07,
                               0, spapr->max_compat_pvr)) {
        caps.caps[SPAPR_CAP_HTM] = SPAPR_CAP_OFF;
        caps.caps[SPAPR_CAP_CFPC] = SPAPR_CAP_BROKEN;
    }

    if (!ppc_type_check_compat(cputype, CPU_POWERPC_LOGICAL_2_06_PLUS,
                               0, spapr->max_compat_pvr)) {
        caps.caps[SPAPR_CAP_SBBC] = SPAPR_CAP_BROKEN;
    }

    if (!ppc_type_check_compat(cputype, CPU_POWERPC_LOGICAL_2_06,
                               0, spapr->max_compat_pvr)) {
        caps.caps[SPAPR_CAP_VSX] = SPAPR_CAP_OFF;
        caps.caps[SPAPR_CAP_DFP] = SPAPR_CAP_OFF;
        caps.caps[SPAPR_CAP_IBS] = SPAPR_CAP_BROKEN;
    }

    /* This is for pseries-2.12 and older */
    if (smc->default_caps.caps[SPAPR_CAP_HPT_MAXPAGESIZE] == 0) {
        uint8_t mps;

        if (kvmppc_hpt_needs_host_contiguous_pages()) {
            mps = ctz64(qemu_minrampagesize());
        } else {
            mps = 34; /* allow everything up to 16GiB, i.e. everything */
        }

        caps.caps[SPAPR_CAP_HPT_MAXPAGESIZE] = mps;
    }

    return caps;
}

int spapr_caps_pre_load(void *opaque)
{
    SpaprMachineState *spapr = opaque;

    /* Set to default so we can tell if this came in with the migration */
    spapr->mig = spapr->def;
    return 0;
}

int spapr_caps_pre_save(void *opaque)
{
    SpaprMachineState *spapr = opaque;

    spapr->mig = spapr->eff;
    return 0;
}

/* This has to be called from the top-level spapr post_load, not the
 * caps specific one.  Otherwise it wouldn't be called when the source
 * caps are all defaults, which could still conflict with overridden
 * caps on the destination */
int spapr_caps_post_migration(SpaprMachineState *spapr)
{
    int i;
    bool ok = true;
    SpaprCapabilities dstcaps = spapr->eff;
    SpaprCapabilities srccaps;

    srccaps = default_caps_with_cpu(spapr, MACHINE(spapr)->cpu_type);
    for (i = 0; i < SPAPR_CAP_NUM; i++) {
        /* If not default value then assume came in with the migration */
        if (spapr->mig.caps[i] != spapr->def.caps[i]) {
            srccaps.caps[i] = spapr->mig.caps[i];
        }
    }

    for (i = 0; i < SPAPR_CAP_NUM; i++) {
        SpaprCapabilityInfo *info = &capability_table[i];

        if (srccaps.caps[i] > dstcaps.caps[i]) {
            error_report("cap-%s higher level (%d) in incoming stream than on destination (%d)",
                         info->name, srccaps.caps[i], dstcaps.caps[i]);
            ok = false;
        }

        if (srccaps.caps[i] < dstcaps.caps[i]) {
            warn_report("cap-%s lower level (%d) in incoming stream than on destination (%d)",
                         info->name, srccaps.caps[i], dstcaps.caps[i]);
        }
    }

    return ok ? 0 : -EINVAL;
}

/* Used to generate the migration field and needed function for a spapr cap */
#define SPAPR_CAP_MIG_STATE(sname, cap)                 \
static bool spapr_cap_##sname##_needed(void *opaque)    \
{                                                       \
    SpaprMachineState *spapr = opaque;                  \
    bool (*needed)(void *opaque) =                      \
        capability_table[cap].migrate_needed;           \
                                                        \
    return needed ? needed(opaque) : true &&            \
           spapr->cmd_line_caps[cap] &&                 \
           (spapr->eff.caps[cap] !=                     \
            spapr->def.caps[cap]);                      \
}                                                       \
                                                        \
const VMStateDescription vmstate_spapr_cap_##sname = {  \
    .name = "spapr/cap/" #sname,                        \
    .version_id = 1,                                    \
    .minimum_version_id = 1,                            \
    .needed = spapr_cap_##sname##_needed,               \
    .fields = (VMStateField[]) {                        \
        VMSTATE_UINT8(mig.caps[cap],                    \
                      SpaprMachineState),               \
        VMSTATE_END_OF_LIST()                           \
    },                                                  \
}

SPAPR_CAP_MIG_STATE(htm, SPAPR_CAP_HTM);
SPAPR_CAP_MIG_STATE(vsx, SPAPR_CAP_VSX);
SPAPR_CAP_MIG_STATE(dfp, SPAPR_CAP_DFP);
SPAPR_CAP_MIG_STATE(cfpc, SPAPR_CAP_CFPC);
SPAPR_CAP_MIG_STATE(sbbc, SPAPR_CAP_SBBC);
SPAPR_CAP_MIG_STATE(ibs, SPAPR_CAP_IBS);
SPAPR_CAP_MIG_STATE(hpt_maxpagesize, SPAPR_CAP_HPT_MAXPAGESIZE);
SPAPR_CAP_MIG_STATE(nested_kvm_hv, SPAPR_CAP_NESTED_KVM_HV);
SPAPR_CAP_MIG_STATE(large_decr, SPAPR_CAP_LARGE_DECREMENTER);
SPAPR_CAP_MIG_STATE(ccf_assist, SPAPR_CAP_CCF_ASSIST);
SPAPR_CAP_MIG_STATE(fwnmi, SPAPR_CAP_FWNMI);

void spapr_caps_init(SpaprMachineState *spapr)
{
    SpaprCapabilities default_caps;
    int i;

    /* Compute the actual set of caps we should run with */
    default_caps = default_caps_with_cpu(spapr, MACHINE(spapr)->cpu_type);

    for (i = 0; i < SPAPR_CAP_NUM; i++) {
        /* Store the defaults */
        spapr->def.caps[i] = default_caps.caps[i];
        /* If not set on the command line then apply the default value */
        if (!spapr->cmd_line_caps[i]) {
            spapr->eff.caps[i] = default_caps.caps[i];
        }
    }
}

void spapr_caps_apply(SpaprMachineState *spapr)
{
    int i;

    for (i = 0; i < SPAPR_CAP_NUM; i++) {
        SpaprCapabilityInfo *info = &capability_table[i];

        /*
         * If the apply function can't set the desired level and thinks it's
         * fatal, it should cause that.
         */
        info->apply(spapr, spapr->eff.caps[i], &error_fatal);
    }
}

void spapr_caps_cpu_apply(SpaprMachineState *spapr, PowerPCCPU *cpu)
{
    int i;

    for (i = 0; i < SPAPR_CAP_NUM; i++) {
        SpaprCapabilityInfo *info = &capability_table[i];

        /*
         * If the apply function can't set the desired level and thinks it's
         * fatal, it should cause that.
         */
        if (info->cpu_apply) {
            info->cpu_apply(spapr, cpu, spapr->eff.caps[i], &error_fatal);
        }
    }
}

void spapr_caps_add_properties(SpaprMachineClass *smc)
{
    ObjectClass *klass = OBJECT_CLASS(smc);
    int i;

    for (i = 0; i < ARRAY_SIZE(capability_table); i++) {
        SpaprCapabilityInfo *cap = &capability_table[i];
        char *name = g_strdup_printf("cap-%s", cap->name);
        char *desc;

        object_class_property_add(klass, name, cap->type,
                                  cap->get, cap->set,
                                  NULL, cap);

        desc = g_strdup_printf("%s", cap->description);
        object_class_property_set_description(klass, name, desc);
        g_free(name);
        g_free(desc);
    }
}
