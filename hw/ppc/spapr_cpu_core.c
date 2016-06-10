/*
 * sPAPR CPU core device, acts as container of CPU thread devices.
 *
 * Copyright (C) 2016 Bharata B Rao <bharata@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "hw/cpu/core.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "target-ppc/cpu.h"
#include "hw/ppc/spapr.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include <sysemu/cpus.h>
#include "target-ppc/kvm_ppc.h"
#include "hw/ppc/ppc.h"
#include "target-ppc/mmu-hash64.h"
#include <sysemu/numa.h>

static void spapr_cpu_reset(void *opaque)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    cpu_reset(cs);

    /* All CPUs start halted.  CPU0 is unhalted from the machine level
     * reset code and the rest are explicitly started up by the guest
     * using an RTAS call */
    cs->halted = 1;

    env->spr[SPR_HIOR] = 0;

    ppc_hash64_set_external_hpt(cpu, spapr->htab, spapr->htab_shift,
                                &error_fatal);
}

void spapr_cpu_init(sPAPRMachineState *spapr, PowerPCCPU *cpu, Error **errp)
{
    CPUPPCState *env = &cpu->env;

    /* Set time-base frequency to 512 MHz */
    cpu_ppc_tb_init(env, SPAPR_TIMEBASE_FREQ);

    /* Enable PAPR mode in TCG or KVM */
    cpu_ppc_set_papr(cpu);

    if (cpu->max_compat) {
        Error *local_err = NULL;

        ppc_set_compat(cpu, cpu->max_compat, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    xics_cpu_setup(spapr->icp, cpu);

    qemu_register_reset(spapr_cpu_reset, cpu);
}

static int spapr_cpu_core_realize_child(Object *child, void *opaque)
{
    Error **errp = opaque;
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    CPUState *cs = CPU(child);
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    object_property_set_bool(child, true, "realized", errp);
    if (*errp) {
        return 1;
    }

    spapr_cpu_init(spapr, cpu, errp);
    if (*errp) {
        return 1;
    }
    return 0;
}

static void spapr_cpu_core_realize(DeviceState *dev, Error **errp)
{
    sPAPRCPUCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    CPUCore *cc = CPU_CORE(OBJECT(dev));
    const char *typename = object_class_get_name(sc->cpu_class);
    size_t size = object_type_get_instance_size(typename);
    Error *local_err = NULL;
    Object *obj;
    int i;

    sc->threads = g_malloc0(size * cc->nr_threads);
    for (i = 0; i < cc->nr_threads; i++) {
        char id[32];
        void *obj = sc->threads + i * size;

        object_initialize(obj, size, typename);
        snprintf(id, sizeof(id), "thread[%d]", i);
        object_property_add_child(OBJECT(sc), id, obj, &local_err);
        if (local_err) {
            goto err;
        }
    }
    object_child_foreach(OBJECT(dev), spapr_cpu_core_realize_child, &local_err);
    if (local_err) {
        goto err;
    } else {
        return;
    }

err:
    while (i >= 0) {
        obj = sc->threads + i * size;
        object_unparent(obj);
        i--;
    }
    g_free(sc->threads);
    error_propagate(errp, local_err);
}

static void spapr_cpu_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = spapr_cpu_core_realize;
}

/*
 * instance_init routines from different flavours of sPAPR CPU cores.
 * TODO: Add support for 'host' core type.
 */
#define SPAPR_CPU_CORE_INITFN(_type, _fname) \
static void glue(glue(spapr_cpu_core_, _fname), _initfn(Object *obj)) \
{ \
    sPAPRCPUCore *core = SPAPR_CPU_CORE(obj); \
    char *name = g_strdup_printf("%s-" TYPE_POWERPC_CPU, stringify(_type)); \
    ObjectClass *oc = object_class_by_name(name); \
    g_assert(oc); \
    g_free((void *)name); \
    core->cpu_class = oc; \
}

SPAPR_CPU_CORE_INITFN(POWER7_v2.3, POWER7);
SPAPR_CPU_CORE_INITFN(POWER7+_v2.1, POWER7plus);
SPAPR_CPU_CORE_INITFN(POWER8_v2.0, POWER8);
SPAPR_CPU_CORE_INITFN(POWER8E_v2.1, POWER8E);

typedef struct SPAPRCoreInfo {
    const char *name;
    void (*initfn)(Object *obj);
} SPAPRCoreInfo;

static const SPAPRCoreInfo spapr_cores[] = {
    /* POWER7 and aliases */
    { .name = "POWER7_v2.3", .initfn = spapr_cpu_core_POWER7_initfn },
    { .name = "POWER7", .initfn = spapr_cpu_core_POWER7_initfn },

    /* POWER7+ and aliases */
    { .name = "POWER7+_v2.1", .initfn = spapr_cpu_core_POWER7plus_initfn },
    { .name = "POWER7+", .initfn = spapr_cpu_core_POWER7plus_initfn },

    /* POWER8 and aliases */
    { .name = "POWER8_v2.0", .initfn = spapr_cpu_core_POWER8_initfn },
    { .name = "POWER8", .initfn = spapr_cpu_core_POWER8_initfn },
    { .name = "power8", .initfn = spapr_cpu_core_POWER8_initfn },

    /* POWER8E and aliases */
    { .name = "POWER8E_v2.1", .initfn = spapr_cpu_core_POWER8E_initfn },
    { .name = "POWER8E", .initfn = spapr_cpu_core_POWER8E_initfn },

    { .name = NULL }
};

static void spapr_cpu_core_register(const SPAPRCoreInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_SPAPR_CPU_CORE,
        .instance_size = sizeof(sPAPRCPUCore),
        .instance_init = info->initfn,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_SPAPR_CPU_CORE, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo spapr_cpu_core_type_info = {
    .name = TYPE_SPAPR_CPU_CORE,
    .parent = TYPE_CPU_CORE,
    .abstract = true,
    .instance_size = sizeof(sPAPRCPUCore),
    .class_init = spapr_cpu_core_class_init,
};

static void spapr_cpu_core_register_types(void)
{
    const SPAPRCoreInfo *info = spapr_cores;

    type_register_static(&spapr_cpu_core_type_info);
    while (info->name) {
        spapr_cpu_core_register(info);
        info++;
    }
}

type_init(spapr_cpu_core_register_types)
