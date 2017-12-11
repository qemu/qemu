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
#include "target/ppc/cpu.h"
#include "cpu-models.h"
#include "kvm_ppc.h"

#include "hw/ppc/spapr.h"

typedef struct sPAPRCapabilityInfo {
    const char *name;
    const char *description;
    uint64_t flag;

    /* Make sure the virtual hardware can support this capability */
    void (*allow)(sPAPRMachineState *spapr, Error **errp);

    /* If possible, tell the virtual hardware not to allow the cap to
     * be used at all */
    void (*disallow)(sPAPRMachineState *spapr, Error **errp);
} sPAPRCapabilityInfo;

static void cap_htm_allow(sPAPRMachineState *spapr, Error **errp)
{
    if (tcg_enabled()) {
        error_setg(errp,
                   "No Transactional Memory support in TCG, try cap-htm=off");
    } else if (kvm_enabled() && !kvmppc_has_cap_htm()) {
        error_setg(errp,
"KVM implementation does not support Transactional Memory, try cap-htm=off"
            );
    }
}

static sPAPRCapabilityInfo capability_table[] = {
    {
        .name = "htm",
        .description = "Allow Hardware Transactional Memory (HTM)",
        .flag = SPAPR_CAP_HTM,
        .allow = cap_htm_allow,
        /* TODO: add cap_htm_disallow */
    },
};

static sPAPRCapabilities default_caps_with_cpu(sPAPRMachineState *spapr,
                                               CPUState *cs)
{
    sPAPRMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    sPAPRCapabilities caps;

    caps = smc->default_caps;

    if (!ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_2_07,
                          0, spapr->max_compat_pvr)) {
        caps.mask &= ~SPAPR_CAP_HTM;
    }

    return caps;
}

static bool spapr_caps_needed(void *opaque)
{
    sPAPRMachineState *spapr = opaque;

    return (spapr->forced_caps.mask != 0) || (spapr->forbidden_caps.mask != 0);
}

/* This has to be called from the top-level spapr post_load, not the
 * caps specific one.  Otherwise it wouldn't be called when the source
 * caps are all defaults, which could still conflict with overridden
 * caps on the destination */
int spapr_caps_post_migration(sPAPRMachineState *spapr)
{
    uint64_t allcaps = 0;
    int i;
    bool ok = true;
    sPAPRCapabilities dstcaps = spapr->effective_caps;
    sPAPRCapabilities srccaps;

    srccaps = default_caps_with_cpu(spapr, first_cpu);
    srccaps.mask |= spapr->mig_forced_caps.mask;
    srccaps.mask &= ~spapr->mig_forbidden_caps.mask;

    for (i = 0; i < ARRAY_SIZE(capability_table); i++) {
        sPAPRCapabilityInfo *info = &capability_table[i];

        allcaps |= info->flag;

        if ((srccaps.mask & info->flag) && !(dstcaps.mask & info->flag)) {
            error_report("cap-%s=on in incoming stream, but off in destination",
                         info->name);
            ok = false;
        }

        if (!(srccaps.mask & info->flag) && (dstcaps.mask & info->flag)) {
            warn_report("cap-%s=off in incoming stream, but on in destination",
                         info->name);
        }
    }

    if (spapr->mig_forced_caps.mask & ~allcaps) {
        error_report(
            "Unknown capabilities 0x%"PRIx64" enabled in incoming stream",
            spapr->mig_forced_caps.mask & ~allcaps);
        ok = false;
    }
    if (spapr->mig_forbidden_caps.mask & ~allcaps) {
        warn_report(
            "Unknown capabilities 0x%"PRIx64" disabled in incoming stream",
            spapr->mig_forbidden_caps.mask & ~allcaps);
    }

    return ok ? 0 : -EINVAL;
}

static int spapr_caps_pre_save(void *opaque)
{
    sPAPRMachineState *spapr = opaque;

    spapr->mig_forced_caps = spapr->forced_caps;
    spapr->mig_forbidden_caps = spapr->forbidden_caps;
    return 0;
}

static int spapr_caps_pre_load(void *opaque)
{
    sPAPRMachineState *spapr = opaque;

    spapr->mig_forced_caps = spapr_caps(0);
    spapr->mig_forbidden_caps = spapr_caps(0);
    return 0;
}

const VMStateDescription vmstate_spapr_caps = {
    .name = "spapr/caps",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_caps_needed,
    .pre_save = spapr_caps_pre_save,
    .pre_load = spapr_caps_pre_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(mig_forced_caps.mask, sPAPRMachineState),
        VMSTATE_UINT64(mig_forbidden_caps.mask, sPAPRMachineState),
        VMSTATE_END_OF_LIST()
    },
};

void spapr_caps_reset(sPAPRMachineState *spapr)
{
    Error *local_err = NULL;
    sPAPRCapabilities caps;
    int i;

    /* First compute the actual set of caps we're running with.. */
    caps = default_caps_with_cpu(spapr, first_cpu);

    /* Remove unnecessary forced/forbidden bits (this will help us
     * with migration) */
    spapr->forced_caps.mask &= ~caps.mask;
    spapr->forbidden_caps.mask &= caps.mask;

    caps.mask |= spapr->forced_caps.mask;
    caps.mask &= ~spapr->forbidden_caps.mask;

    spapr->effective_caps = caps;

    /* .. then apply those caps to the virtual hardware */

    for (i = 0; i < ARRAY_SIZE(capability_table); i++) {
        sPAPRCapabilityInfo *info = &capability_table[i];

        if (spapr->effective_caps.mask & info->flag) {
            /* Failure to allow a cap is fatal - if the guest doesn't
             * have it, we'll be supplying an incorrect environment */
            if (info->allow) {
                info->allow(spapr, &error_fatal);
            }
        } else {
            /* Failure to enforce a cap is only a warning.  The guest
             * shouldn't be using it, since it's not advertised, so it
             * doesn't get to complain about weird behaviour if it
             * goes ahead anyway */
            if (info->disallow) {
                info->disallow(spapr, &local_err);
            }
            if (local_err) {
                warn_report_err(local_err);
                local_err = NULL;
            }
        }
    }
}

static void spapr_cap_get(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    sPAPRCapabilityInfo *cap = opaque;
    sPAPRMachineState *spapr = SPAPR_MACHINE(obj);
    bool value = spapr_has_cap(spapr, cap->flag);

    /* TODO: Could this get called before effective_caps is finalized
     * in spapr_caps_reset()? */

    visit_type_bool(v, name, &value, errp);
}

static void spapr_cap_set(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    sPAPRCapabilityInfo *cap = opaque;
    sPAPRMachineState *spapr = SPAPR_MACHINE(obj);
    bool value;
    Error *local_err = NULL;

    visit_type_bool(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (value) {
        spapr->forced_caps.mask |= cap->flag;
    } else {
        spapr->forbidden_caps.mask |= cap->flag;
    }
}

void spapr_caps_validate(sPAPRMachineState *spapr, Error **errp)
{
    uint64_t allcaps = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(capability_table); i++) {
        g_assert((allcaps & capability_table[i].flag) == 0);
        allcaps |= capability_table[i].flag;
    }

    g_assert((spapr->forced_caps.mask & ~allcaps) == 0);
    g_assert((spapr->forbidden_caps.mask & ~allcaps) == 0);

    if (spapr->forced_caps.mask & spapr->forbidden_caps.mask) {
        error_setg(errp, "Some sPAPR capabilities set both on and off");
        return;
    }
}

void spapr_caps_add_properties(sPAPRMachineClass *smc, Error **errp)
{
    Error *local_err = NULL;
    ObjectClass *klass = OBJECT_CLASS(smc);
    int i;

    for (i = 0; i < ARRAY_SIZE(capability_table); i++) {
        sPAPRCapabilityInfo *cap = &capability_table[i];
        const char *name = g_strdup_printf("cap-%s", cap->name);

        object_class_property_add(klass, name, "bool",
                                  spapr_cap_get, spapr_cap_set, NULL,
                                  cap, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        object_class_property_set_description(klass, name, cap->description,
                                              &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}
