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
#include "qapi/error.h"
#include "qapi/visitor.h"

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

static sPAPRCapabilityInfo capability_table[] = {
};

static sPAPRCapabilities default_caps_with_cpu(sPAPRMachineState *spapr,
                                               CPUState *cs)
{
    sPAPRMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    sPAPRCapabilities caps;

    caps = smc->default_caps;

    /* TODO: clamp according to cpu model */

    return caps;
}

void spapr_caps_reset(sPAPRMachineState *spapr)
{
    Error *local_err = NULL;
    sPAPRCapabilities caps;
    int i;

    /* First compute the actual set of caps we're running with.. */
    caps = default_caps_with_cpu(spapr, first_cpu);

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

    /* Check for any caps incompatible with other caps.  Nothing to do
     * yet */
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
