/*
 *  PowerPC CPU initialization for qemu.
 *
 *  Copyright 2016, David Gibson, Red Hat Inc. <dgibson@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "sysemu/cpus.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "cpu-models.h"

typedef struct {
    const char *name;
    uint32_t pvr;
    uint64_t pcr;
    uint64_t pcr_level;
    int max_threads;
} CompatInfo;

static const CompatInfo compat_table[] = {
    /*
     * Ordered from oldest to newest - the code relies on this
     */
    { /* POWER6, ISA2.05 */
        .name = "power6",
        .pvr = CPU_POWERPC_LOGICAL_2_05,
        .pcr = PCR_COMPAT_3_00 | PCR_COMPAT_2_07 | PCR_COMPAT_2_06 |
               PCR_COMPAT_2_05 | PCR_TM_DIS | PCR_VSX_DIS,
        .pcr_level = PCR_COMPAT_2_05,
        .max_threads = 2,
    },
    { /* POWER7, ISA2.06 */
        .name = "power7",
        .pvr = CPU_POWERPC_LOGICAL_2_06,
        .pcr = PCR_COMPAT_3_00 | PCR_COMPAT_2_07 | PCR_COMPAT_2_06 | PCR_TM_DIS,
        .pcr_level = PCR_COMPAT_2_06,
        .max_threads = 4,
    },
    {
        .name = "power7+",
        .pvr = CPU_POWERPC_LOGICAL_2_06_PLUS,
        .pcr = PCR_COMPAT_3_00 | PCR_COMPAT_2_07 | PCR_COMPAT_2_06 | PCR_TM_DIS,
        .pcr_level = PCR_COMPAT_2_06,
        .max_threads = 4,
    },
    { /* POWER8, ISA2.07 */
        .name = "power8",
        .pvr = CPU_POWERPC_LOGICAL_2_07,
        .pcr = PCR_COMPAT_3_00 | PCR_COMPAT_2_07,
        .pcr_level = PCR_COMPAT_2_07,
        .max_threads = 8,
    },
    { /* POWER9, ISA3.00 */
        .name = "power9",
        .pvr = CPU_POWERPC_LOGICAL_3_00,
        .pcr = PCR_COMPAT_3_00,
        .pcr_level = PCR_COMPAT_3_00,
        .max_threads = 4,
    },
};

static const CompatInfo *compat_by_pvr(uint32_t pvr)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(compat_table); i++) {
        if (compat_table[i].pvr == pvr) {
            return &compat_table[i];
        }
    }
    return NULL;
}

bool ppc_check_compat(PowerPCCPU *cpu, uint32_t compat_pvr,
                      uint32_t min_compat_pvr, uint32_t max_compat_pvr)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    const CompatInfo *compat = compat_by_pvr(compat_pvr);
    const CompatInfo *min = compat_by_pvr(min_compat_pvr);
    const CompatInfo *max = compat_by_pvr(max_compat_pvr);

#if !defined(CONFIG_USER_ONLY)
    g_assert(cpu->vhyp);
#endif
    g_assert(!min_compat_pvr || min);
    g_assert(!max_compat_pvr || max);

    if (!compat) {
        /* Not a recognized logical PVR */
        return false;
    }
    if ((min && (compat < min)) || (max && (compat > max))) {
        /* Outside specified range */
        return false;
    }
    if (!(pcc->pcr_supported & compat->pcr_level)) {
        /* Not supported by this CPU */
        return false;
    }
    return true;
}

void ppc_set_compat(PowerPCCPU *cpu, uint32_t compat_pvr, Error **errp)
{
    const CompatInfo *compat = compat_by_pvr(compat_pvr);
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    uint64_t pcr;

    if (!compat_pvr) {
        pcr = 0;
    } else if (!compat) {
        error_setg(errp, "Unknown compatibility PVR 0x%08"PRIx32, compat_pvr);
        return;
    } else if (!ppc_check_compat(cpu, compat_pvr, 0, 0)) {
        error_setg(errp, "Compatibility PVR 0x%08"PRIx32" not valid for CPU",
                   compat_pvr);
        return;
    } else {
        pcr = compat->pcr;
    }

    cpu_synchronize_state(CPU(cpu));

    if (kvm_enabled() && cpu->compat_pvr != compat_pvr) {
        int ret = kvmppc_set_compat(cpu, cpu->compat_pvr);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                             "Unable to set CPU compatibility mode in KVM");
            return;
        }
    }

    cpu->compat_pvr = compat_pvr;
    env->spr[SPR_PCR] = pcr & pcc->pcr_mask;
}

typedef struct {
    uint32_t compat_pvr;
    Error *err;
} SetCompatState;

static void do_set_compat(CPUState *cs, run_on_cpu_data arg)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    SetCompatState *s = arg.host_ptr;

    ppc_set_compat(cpu, s->compat_pvr, &s->err);
}

void ppc_set_compat_all(uint32_t compat_pvr, Error **errp)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        SetCompatState s = {
            .compat_pvr = compat_pvr,
            .err = NULL,
        };

        run_on_cpu(cs, do_set_compat, RUN_ON_CPU_HOST_PTR(&s));

        if (s.err) {
            error_propagate(errp, s.err);
            return;
        }
    }
}

int ppc_compat_max_threads(PowerPCCPU *cpu)
{
    const CompatInfo *compat = compat_by_pvr(cpu->compat_pvr);
    int n_threads = CPU(cpu)->nr_threads;

    if (cpu->compat_pvr) {
        g_assert(compat);
        n_threads = MIN(n_threads, compat->max_threads);
    }

    return n_threads;
}

static void ppc_compat_prop_get(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    uint32_t compat_pvr = *((uint32_t *)opaque);
    const char *value;

    if (!compat_pvr) {
        value = "";
    } else {
        const CompatInfo *compat = compat_by_pvr(compat_pvr);

        g_assert(compat);

        value = compat->name;
    }

    visit_type_str(v, name, (char **)&value, errp);
}

static void ppc_compat_prop_set(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    Error *local_err = NULL;
    char *value;
    uint32_t compat_pvr;

    visit_type_str(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (strcmp(value, "") == 0) {
        compat_pvr = 0;
    } else {
        int i;
        const CompatInfo *compat = NULL;

        for (i = 0; i < ARRAY_SIZE(compat_table); i++) {
            if (strcmp(value, compat_table[i].name) == 0) {
                compat = &compat_table[i];
                break;

            }
        }

        if (!compat) {
            error_setg(errp, "Invalid compatibility mode \"%s\"", value);
            goto out;
        }
        compat_pvr = compat->pvr;
    }

    *((uint32_t *)opaque) = compat_pvr;

out:
    g_free(value);
}

void ppc_compat_add_property(Object *obj, const char *name,
                             uint32_t *compat_pvr, const char *basedesc,
                             Error **errp)
{
    Error *local_err = NULL;
    gchar *namesv[ARRAY_SIZE(compat_table) + 1];
    gchar *names, *desc;
    int i;

    object_property_add(obj, name, "string",
                        ppc_compat_prop_get, ppc_compat_prop_set, NULL,
                        compat_pvr, &local_err);
    if (local_err) {
        goto out;
    }

    for (i = 0; i < ARRAY_SIZE(compat_table); i++) {
        /*
         * Have to discard const here, because g_strjoinv() takes
         * (gchar **), not (const gchar **) :(
         */
        namesv[i] = (gchar *)compat_table[i].name;
    }
    namesv[ARRAY_SIZE(compat_table)] = NULL;

    names = g_strjoinv(", ", namesv);
    desc = g_strdup_printf("%s. Valid values are %s.", basedesc, names);
    object_property_set_description(obj, name, desc, &local_err);

    g_free(names);
    g_free(desc);

out:
    error_propagate(errp, local_err);
}
