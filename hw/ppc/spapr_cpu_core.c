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
#include "sysemu/cpus.h"
#include "target-ppc/kvm_ppc.h"
#include "hw/ppc/ppc.h"
#include "target-ppc/mmu-hash64.h"
#include "sysemu/numa.h"

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

static void spapr_cpu_destroy(PowerPCCPU *cpu)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    xics_cpu_destroy(spapr->xics, cpu);
    qemu_unregister_reset(spapr_cpu_reset, cpu);
}

void spapr_cpu_init(sPAPRMachineState *spapr, PowerPCCPU *cpu, Error **errp)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    int i;

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

    /* Set NUMA node for the added CPUs  */
    for (i = 0; i < nb_numa_nodes; i++) {
        if (test_bit(cs->cpu_index, numa_info[i].node_cpu)) {
            cs->numa_node = i;
            break;
        }
    }

    xics_cpu_setup(spapr->xics, cpu);

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
    g_strfreev(model_pieces);

    /* Check whether it exists or whether we have to look up an alias name */
    if (!object_class_by_name(core_type)) {
        const char *realmodel;

        g_free(core_type);
        realmodel = ppc_cpu_lookup_alias(model);
        if (realmodel) {
            return spapr_get_cpu_core_type(realmodel);
        }
        return NULL;
    }

    return core_type;
}

static void spapr_core_release(DeviceState *dev, void *opaque)
{
    sPAPRCPUCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    const char *typename = object_class_get_name(sc->cpu_class);
    size_t size = object_type_get_instance_size(typename);
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
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

    spapr->cores[cc->core_id / smp_threads] = NULL;

    g_free(sc->threads);
    object_unparent(OBJECT(dev));
}

void spapr_core_unplug(HotplugHandler *hotplug_dev, DeviceState *dev,
                       Error **errp)
{
    CPUCore *cc = CPU_CORE(dev);
    int smt = kvmppc_smt_threads();
    int index = cc->core_id / smp_threads;
    sPAPRDRConnector *drc =
        spapr_dr_connector_by_id(SPAPR_DR_CONNECTOR_TYPE_CPU, index * smt);
    sPAPRDRConnectorClass *drck;
    Error *local_err = NULL;

    if (index == 0) {
        error_setg(errp, "Boot CPU core may not be unplugged");
        return;
    }

    g_assert(drc);

    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    drck->detach(drc, dev, spapr_core_release, NULL, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    spapr_hotplug_req_remove_by_index(drc);
}

void spapr_core_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                     Error **errp)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(OBJECT(hotplug_dev));
    sPAPRCPUCore *core = SPAPR_CPU_CORE(OBJECT(dev));
    CPUCore *cc = CPU_CORE(dev);
    CPUState *cs = CPU(core->threads);
    sPAPRDRConnector *drc;
    sPAPRDRConnectorClass *drck;
    Error *local_err = NULL;
    void *fdt = NULL;
    int fdt_offset = 0;
    int index = cc->core_id / smp_threads;
    int smt = kvmppc_smt_threads();

    drc = spapr_dr_connector_by_id(SPAPR_DR_CONNECTOR_TYPE_CPU, index * smt);
    spapr->cores[index] = OBJECT(dev);

    g_assert(drc);

    /*
     * Setup CPU DT entries only for hotplugged CPUs. For boot time or
     * coldplugged CPUs DT entries are setup in spapr_finalize_fdt().
     */
    if (dev->hotplugged) {
        fdt = spapr_populate_hotplug_cpu_dt(cs, &fdt_offset, spapr);
    }

    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    drck->attach(drc, dev, fdt, fdt_offset, !dev->hotplugged, &local_err);
    if (local_err) {
        g_free(fdt);
        spapr->cores[index] = NULL;
        error_propagate(errp, local_err);
        return;
    }

    if (dev->hotplugged) {
        /*
         * Send hotplug notification interrupt to the guest only in case
         * of hotplugged CPUs.
         */
        spapr_hotplug_req_add_by_index(drc);
    } else {
        /*
         * Set the right DRC states for cold plugged CPU.
         */
        drck->set_allocation_state(drc, SPAPR_DR_ALLOCATION_STATE_USABLE);
        drck->set_isolation_state(drc, SPAPR_DR_ISOLATION_STATE_UNISOLATED);
    }
}

void spapr_core_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                         Error **errp)
{
    MachineState *machine = MACHINE(OBJECT(hotplug_dev));
    MachineClass *mc = MACHINE_GET_CLASS(hotplug_dev);
    sPAPRMachineState *spapr = SPAPR_MACHINE(OBJECT(hotplug_dev));
    int spapr_max_cores = max_cpus / smp_threads;
    int index;
    Error *local_err = NULL;
    CPUCore *cc = CPU_CORE(dev);
    char *base_core_type = spapr_get_cpu_core_type(machine->cpu_model);
    const char *type = object_get_typename(OBJECT(dev));

    if (!mc->query_hotpluggable_cpus) {
        error_setg(&local_err, "CPU hotplug not supported for this machine");
        goto out;
    }

    if (strcmp(base_core_type, type)) {
        error_setg(&local_err, "CPU core type should be %s", base_core_type);
        goto out;
    }

    if (cc->nr_threads != smp_threads) {
        error_setg(&local_err, "threads must be %d", smp_threads);
        goto out;
    }

    if (cc->core_id % smp_threads) {
        error_setg(&local_err, "invalid core id %d", cc->core_id);
        goto out;
    }

    index = cc->core_id / smp_threads;
    if (index < 0 || index >= spapr_max_cores) {
        error_setg(&local_err, "core id %d out of range", cc->core_id);
        goto out;
    }

    if (spapr->cores[index]) {
        error_setg(&local_err, "core %d already populated", cc->core_id);
        goto out;
    }

out:
    g_free(base_core_type);
    error_propagate(errp, local_err);
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
    CPUCore *cc = CPU_CORE(OBJECT(dev));
    const char *typename = object_class_get_name(sc->cpu_class);
    size_t size = object_type_get_instance_size(typename);
    Error *local_err = NULL;
    void *obj;
    int i, j;

    sc->threads = g_malloc0(size * cc->nr_threads);
    for (i = 0; i < cc->nr_threads; i++) {
        char id[32];
        CPUState *cs;

        obj = sc->threads + i * size;

        object_initialize(obj, size, typename);
        cs = CPU(obj);
        cs->cpu_index = cc->core_id + i;
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

static void spapr_cpu_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = spapr_cpu_core_realize;
}

/*
 * instance_init routines from different flavours of sPAPR CPU cores.
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

SPAPR_CPU_CORE_INITFN(970mp_v1.0, 970MP_v10);
SPAPR_CPU_CORE_INITFN(970mp_v1.1, 970MP_v11);
SPAPR_CPU_CORE_INITFN(970_v2.2, 970);
SPAPR_CPU_CORE_INITFN(POWER5+_v2.1, POWER5plus);
SPAPR_CPU_CORE_INITFN(POWER7_v2.3, POWER7);
SPAPR_CPU_CORE_INITFN(POWER7+_v2.1, POWER7plus);
SPAPR_CPU_CORE_INITFN(POWER8_v2.0, POWER8);
SPAPR_CPU_CORE_INITFN(POWER8E_v2.1, POWER8E);
SPAPR_CPU_CORE_INITFN(POWER8NVL_v1.0, POWER8NVL);

typedef struct SPAPRCoreInfo {
    const char *name;
    void (*initfn)(Object *obj);
} SPAPRCoreInfo;

static const SPAPRCoreInfo spapr_cores[] = {
    /* 970 */
    { .name = "970_v2.2", .initfn = spapr_cpu_core_970_initfn },

    /* 970MP variants */
    { .name = "970MP_v1.0", .initfn = spapr_cpu_core_970MP_v10_initfn },
    { .name = "970mp_v1.0", .initfn = spapr_cpu_core_970MP_v10_initfn },
    { .name = "970MP_v1.1", .initfn = spapr_cpu_core_970MP_v11_initfn },
    { .name = "970mp_v1.1", .initfn = spapr_cpu_core_970MP_v11_initfn },

    /* POWER5+ */
    { .name = "POWER5+_v2.1", .initfn = spapr_cpu_core_POWER5plus_initfn },

    /* POWER7 */
    { .name = "POWER7_v2.3", .initfn = spapr_cpu_core_POWER7_initfn },

    /* POWER7+ */
    { .name = "POWER7+_v2.1", .initfn = spapr_cpu_core_POWER7plus_initfn },

    /* POWER8 */
    { .name = "POWER8_v2.0", .initfn = spapr_cpu_core_POWER8_initfn },

    /* POWER8E */
    { .name = "POWER8E_v2.1", .initfn = spapr_cpu_core_POWER8E_initfn },

    /* POWER8NVL */
    { .name = "POWER8NVL_v1.0", .initfn = spapr_cpu_core_POWER8NVL_initfn },

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
