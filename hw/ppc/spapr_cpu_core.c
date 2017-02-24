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
#include "target/ppc/cpu.h"
#include "hw/ppc/spapr.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "target/ppc/kvm_ppc.h"
#include "hw/ppc/ppc.h"
#include "target/ppc/mmu-hash64.h"
#include "sysemu/numa.h"
#include "qemu/error-report.h"

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

    /*
     * This is a hack for the benefit of KVM PR - it abuses the SDR1
     * slot in kvm_sregs to communicate the userspace address of the
     * HPT
     */
    if (kvm_enabled()) {
        env->spr[SPR_SDR1] = (target_ulong)(uintptr_t)spapr->htab
            | (spapr->htab_shift - 18);
        if (kvmppc_put_books_sregs(cpu) < 0) {
            error_report("Unable to update SDR1 in KVM");
            exit(1);
        }
    }
}

static void spapr_cpu_destroy(PowerPCCPU *cpu)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    xics_cpu_destroy(XICS_FABRIC(spapr), cpu);
    qemu_unregister_reset(spapr_cpu_reset, cpu);
}

static void spapr_cpu_init(sPAPRMachineState *spapr, PowerPCCPU *cpu,
                           Error **errp)
{
    CPUPPCState *env = &cpu->env;

    /* Set time-base frequency to 512 MHz */
    cpu_ppc_tb_init(env, SPAPR_TIMEBASE_FREQ);

    /* Enable PAPR mode in TCG or KVM */
    cpu_ppc_set_papr(cpu, PPC_VIRTUAL_HYPERVISOR(spapr));

    if (cpu->max_compat) {
        Error *local_err = NULL;

        ppc_set_compat(cpu, cpu->max_compat, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    xics_cpu_setup(XICS_FABRIC(spapr), cpu);

    qemu_register_reset(spapr_cpu_reset, cpu);
    spapr_cpu_reset(cpu);
}

/*
 * Return the sPAPR CPU core type for @model which essentially is the CPU
 * model specified with -cpu cmdline option.
 */
char *spapr_get_cpu_core_type(const char *model)
{
    char *core_type;
    gchar **model_pieces = g_strsplit(model, ",", 2);

    core_type = g_strdup_printf("%s-%s", model_pieces[0], TYPE_SPAPR_CPU_CORE);

    /* Check whether it exists or whether we have to look up an alias name */
    if (!object_class_by_name(core_type)) {
        const char *realmodel;

        g_free(core_type);
        core_type = NULL;
        realmodel = ppc_cpu_lookup_alias(model_pieces[0]);
        if (realmodel) {
            core_type = spapr_get_cpu_core_type(realmodel);
        }
    }

    g_strfreev(model_pieces);
    return core_type;
}

static void spapr_cpu_core_unrealizefn(DeviceState *dev, Error **errp)
{
    sPAPRCPUCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    sPAPRCPUCoreClass *scc = SPAPR_CPU_CORE_GET_CLASS(OBJECT(dev));
    const char *typename = object_class_get_name(scc->cpu_class);
    size_t size = object_type_get_instance_size(typename);
    CPUCore *cc = CPU_CORE(dev);
    int i;

    for (i = 0; i < cc->nr_threads; i++) {
        void *obj = sc->threads + i * size;
        DeviceState *dev = DEVICE(obj);
        CPUState *cs = CPU(dev);
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        spapr_cpu_destroy(cpu);
        cpu_remove_sync(cs);
        object_unparent(obj);
    }
    g_free(sc->threads);
}

static void spapr_cpu_core_realize_child(Object *child, Error **errp)
{
    Error *local_err = NULL;
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    CPUState *cs = CPU(child);
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    object_property_set_bool(child, true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    spapr_cpu_init(spapr, cpu, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void spapr_cpu_core_realize(DeviceState *dev, Error **errp)
{
    sPAPRCPUCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    sPAPRCPUCoreClass *scc = SPAPR_CPU_CORE_GET_CLASS(OBJECT(dev));
    CPUCore *cc = CPU_CORE(OBJECT(dev));
    const char *typename = object_class_get_name(scc->cpu_class);
    size_t size = object_type_get_instance_size(typename);
    Error *local_err = NULL;
    int core_node_id = numa_get_node_for_cpu(cc->core_id);;
    void *obj;
    int i, j;

    sc->threads = g_malloc0(size * cc->nr_threads);
    for (i = 0; i < cc->nr_threads; i++) {
        int node_id;
        char id[32];
        CPUState *cs;

        obj = sc->threads + i * size;

        object_initialize(obj, size, typename);
        cs = CPU(obj);
        cs->cpu_index = cc->core_id + i;

        /* Set NUMA node for the added CPUs  */
        node_id = numa_get_node_for_cpu(cs->cpu_index);
        if (node_id != core_node_id) {
            error_setg(&local_err, "Invalid node-id=%d of thread[cpu-index: %d]"
                " on CPU[core-id: %d, node-id: %d], node-id must be the same",
                 node_id, cs->cpu_index, cc->core_id, core_node_id);
            goto err;
        }
        if (node_id < nb_numa_nodes) {
            cs->numa_node = node_id;
        }

        snprintf(id, sizeof(id), "thread[%d]", i);
        object_property_add_child(OBJECT(sc), id, obj, &local_err);
        if (local_err) {
            goto err;
        }
        object_unref(obj);
    }

    for (j = 0; j < cc->nr_threads; j++) {
        obj = sc->threads + j * size;

        spapr_cpu_core_realize_child(obj, &local_err);
        if (local_err) {
            goto err;
        }
    }
    return;

err:
    while (--i >= 0) {
        obj = sc->threads + i * size;
        object_unparent(obj);
    }
    g_free(sc->threads);
    error_propagate(errp, local_err);
}

static const char *spapr_core_models[] = {
    /* 970 */
    "970_v2.2",

    /* 970MP variants */
    "970MP_v1.0",
    "970mp_v1.0",
    "970MP_v1.1",
    "970mp_v1.1",

    /* POWER5+ */
    "POWER5+_v2.1",

    /* POWER7 */
    "POWER7_v2.3",

    /* POWER7+ */
    "POWER7+_v2.1",

    /* POWER8 */
    "POWER8_v2.0",

    /* POWER8E */
    "POWER8E_v2.1",

    /* POWER8NVL */
    "POWER8NVL_v1.0",

    /* POWER9 */
    "POWER9_v1.0",
};

void spapr_cpu_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    sPAPRCPUCoreClass *scc = SPAPR_CPU_CORE_CLASS(oc);

    dc->realize = spapr_cpu_core_realize;
    dc->unrealize = spapr_cpu_core_unrealizefn;
    scc->cpu_class = cpu_class_by_name(TYPE_POWERPC_CPU, data);
    g_assert(scc->cpu_class);
}

static const TypeInfo spapr_cpu_core_type_info = {
    .name = TYPE_SPAPR_CPU_CORE,
    .parent = TYPE_CPU_CORE,
    .abstract = true,
    .instance_size = sizeof(sPAPRCPUCore),
    .class_size = sizeof(sPAPRCPUCoreClass),
};

static void spapr_cpu_core_register_types(void)
{
    int i;

    type_register_static(&spapr_cpu_core_type_info);

    for (i = 0; i < ARRAY_SIZE(spapr_core_models); i++) {
        TypeInfo type_info = {
            .parent = TYPE_SPAPR_CPU_CORE,
            .instance_size = sizeof(sPAPRCPUCore),
            .class_init = spapr_cpu_core_class_init,
            .class_data = (void *) spapr_core_models[i],
        };

        type_info.name = g_strdup_printf("%s-" TYPE_SPAPR_CPU_CORE,
                                         spapr_core_models[i]);
        type_register(&type_info);
        g_free((void *)type_info.name);
    }
}

type_init(spapr_cpu_core_register_types)
