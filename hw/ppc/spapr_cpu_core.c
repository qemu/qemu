/*
 * sPAPR CPU core device, acts as container of CPU thread devices.
 *
 * Copyright (C) 2016 Bharata B Rao <bharata@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
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
#include "sysemu/hw_accel.h"
#include "qemu/error-report.h"

static void spapr_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    sPAPRCPUState *spapr_cpu = spapr_cpu_state(cpu);
    target_ulong lpcr;

    cpu_reset(cs);

    /* Set compatibility mode to match the boot CPU, which was either set
     * by the machine reset code or by CAS. This should never fail.
     */
    ppc_set_compat(cpu, POWERPC_CPU(first_cpu)->compat_pvr, &error_abort);

    /* All CPUs start halted.  CPU0 is unhalted from the machine level
     * reset code and the rest are explicitly started up by the guest
     * using an RTAS call */
    cs->halted = 1;

    env->spr[SPR_HIOR] = 0;

    lpcr = env->spr[SPR_LPCR];

    /* Set emulated LPCR to not send interrupts to hypervisor. Note that
     * under KVM, the actual HW LPCR will be set differently by KVM itself,
     * the settings below ensure proper operations with TCG in absence of
     * a real hypervisor.
     *
     * Clearing VPM0 will also cause us to use RMOR in mmu-hash64.c for
     * real mode accesses, which thankfully defaults to 0 and isn't
     * accessible in guest mode.
     *
     * Disable Power-saving mode Exit Cause exceptions for the CPU, so
     * we don't get spurious wakups before an RTAS start-cpu call.
     */
    lpcr &= ~(LPCR_VPM0 | LPCR_VPM1 | LPCR_ISL | LPCR_KBV | pcc->lpcr_pm);
    lpcr |= LPCR_LPES0 | LPCR_LPES1;

    /* Set RMLS to the max (ie, 16G) */
    lpcr &= ~LPCR_RMLS;
    lpcr |= 1ull << LPCR_RMLS_SHIFT;

    ppc_store_lpcr(cpu, lpcr);

    /* Set a full AMOR so guest can use the AMR as it sees fit */
    env->spr[SPR_AMOR] = 0xffffffffffffffffull;

    spapr_cpu->vpa_addr = 0;
    spapr_cpu->slb_shadow_addr = 0;
    spapr_cpu->slb_shadow_size = 0;
    spapr_cpu->dtl_addr = 0;
    spapr_cpu->dtl_size = 0;

    spapr_caps_cpu_apply(SPAPR_MACHINE(qdev_get_machine()), cpu);

    kvm_check_mmu(cpu, &error_fatal);
}

void spapr_cpu_set_entry_state(PowerPCCPU *cpu, target_ulong nip, target_ulong r3)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;

    env->nip = nip;
    env->gpr[3] = r3;
    CPU(cpu)->halted = 0;
    /* Enable Power-saving mode Exit Cause exceptions */
    ppc_store_lpcr(cpu, env->spr[SPR_LPCR] | pcc->lpcr_pm);
}

/*
 * Return the sPAPR CPU core type for @model which essentially is the CPU
 * model specified with -cpu cmdline option.
 */
const char *spapr_get_cpu_core_type(const char *cpu_type)
{
    int len = strlen(cpu_type) - strlen(POWERPC_CPU_TYPE_SUFFIX);
    char *core_type = g_strdup_printf(SPAPR_CPU_CORE_TYPE_NAME("%.*s"),
                                      len, cpu_type);
    ObjectClass *oc = object_class_by_name(core_type);

    g_free(core_type);
    if (!oc) {
        return NULL;
    }

    return object_class_get_name(oc);
}

static bool slb_shadow_needed(void *opaque)
{
    sPAPRCPUState *spapr_cpu = opaque;

    return spapr_cpu->slb_shadow_addr != 0;
}

static const VMStateDescription vmstate_spapr_cpu_slb_shadow = {
    .name = "spapr_cpu/vpa/slb_shadow",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = slb_shadow_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(slb_shadow_addr, sPAPRCPUState),
        VMSTATE_UINT64(slb_shadow_size, sPAPRCPUState),
        VMSTATE_END_OF_LIST()
    }
};

static bool dtl_needed(void *opaque)
{
    sPAPRCPUState *spapr_cpu = opaque;

    return spapr_cpu->dtl_addr != 0;
}

static const VMStateDescription vmstate_spapr_cpu_dtl = {
    .name = "spapr_cpu/vpa/dtl",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = dtl_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(dtl_addr, sPAPRCPUState),
        VMSTATE_UINT64(dtl_size, sPAPRCPUState),
        VMSTATE_END_OF_LIST()
    }
};

static bool vpa_needed(void *opaque)
{
    sPAPRCPUState *spapr_cpu = opaque;

    return spapr_cpu->vpa_addr != 0;
}

static const VMStateDescription vmstate_spapr_cpu_vpa = {
    .name = "spapr_cpu/vpa",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vpa_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(vpa_addr, sPAPRCPUState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_spapr_cpu_slb_shadow,
        &vmstate_spapr_cpu_dtl,
        NULL
    }
};

static const VMStateDescription vmstate_spapr_cpu_state = {
    .name = "spapr_cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_spapr_cpu_vpa,
        NULL
    }
};

static void spapr_unrealize_vcpu(PowerPCCPU *cpu, sPAPRCPUCore *sc)
{
    if (!sc->pre_3_0_migration) {
        vmstate_unregister(NULL, &vmstate_spapr_cpu_state, cpu->machine_data);
    }
    qemu_unregister_reset(spapr_cpu_reset, cpu);
    object_unparent(cpu->intc);
    cpu_remove_sync(CPU(cpu));
    object_unparent(OBJECT(cpu));
}

static void spapr_cpu_core_unrealize(DeviceState *dev, Error **errp)
{
    sPAPRCPUCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    CPUCore *cc = CPU_CORE(dev);
    int i;

    for (i = 0; i < cc->nr_threads; i++) {
        spapr_unrealize_vcpu(sc->threads[i], sc);
    }
    g_free(sc->threads);
}

static void spapr_realize_vcpu(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                               sPAPRCPUCore *sc, Error **errp)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    Error *local_err = NULL;

    object_property_set_bool(OBJECT(cpu), true, "realized", &local_err);
    if (local_err) {
        goto error;
    }

    /* Set time-base frequency to 512 MHz */
    cpu_ppc_tb_init(env, SPAPR_TIMEBASE_FREQ);

    cpu_ppc_set_vhyp(cpu, PPC_VIRTUAL_HYPERVISOR(spapr));
    kvmppc_set_papr(cpu);

    qemu_register_reset(spapr_cpu_reset, cpu);
    spapr_cpu_reset(cpu);

    cpu->intc = icp_create(OBJECT(cpu), spapr->icp_type, XICS_FABRIC(spapr),
                           &local_err);
    if (local_err) {
        goto error_unregister;
    }

    if (!sc->pre_3_0_migration) {
        vmstate_register(NULL, cs->cpu_index, &vmstate_spapr_cpu_state,
                         cpu->machine_data);
    }

    return;

error_unregister:
    qemu_unregister_reset(spapr_cpu_reset, cpu);
    cpu_remove_sync(CPU(cpu));
error:
    error_propagate(errp, local_err);
}

static PowerPCCPU *spapr_create_vcpu(sPAPRCPUCore *sc, int i, Error **errp)
{
    sPAPRCPUCoreClass *scc = SPAPR_CPU_CORE_GET_CLASS(sc);
    CPUCore *cc = CPU_CORE(sc);
    Object *obj;
    char *id;
    CPUState *cs;
    PowerPCCPU *cpu;
    Error *local_err = NULL;

    obj = object_new(scc->cpu_type);

    cs = CPU(obj);
    cpu = POWERPC_CPU(obj);
    cs->cpu_index = cc->core_id + i;
    spapr_set_vcpu_id(cpu, cs->cpu_index, &local_err);
    if (local_err) {
        goto err;
    }

    cpu->node_id = sc->node_id;

    id = g_strdup_printf("thread[%d]", i);
    object_property_add_child(OBJECT(sc), id, obj, &local_err);
    g_free(id);
    if (local_err) {
        goto err;
    }

    cpu->machine_data = g_new0(sPAPRCPUState, 1);

    object_unref(obj);
    return cpu;

err:
    object_unref(obj);
    error_propagate(errp, local_err);
    return NULL;
}

static void spapr_delete_vcpu(PowerPCCPU *cpu, sPAPRCPUCore *sc)
{
    sPAPRCPUState *spapr_cpu = spapr_cpu_state(cpu);

    cpu->machine_data = NULL;
    g_free(spapr_cpu);
    object_unparent(OBJECT(cpu));
}

static void spapr_cpu_core_realize(DeviceState *dev, Error **errp)
{
    /* We don't use SPAPR_MACHINE() in order to exit gracefully if the user
     * tries to add a sPAPR CPU core to a non-pseries machine.
     */
    sPAPRMachineState *spapr =
        (sPAPRMachineState *) object_dynamic_cast(qdev_get_machine(),
                                                  TYPE_SPAPR_MACHINE);
    sPAPRCPUCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    CPUCore *cc = CPU_CORE(OBJECT(dev));
    Error *local_err = NULL;
    int i, j;

    if (!spapr) {
        error_setg(errp, TYPE_SPAPR_CPU_CORE " needs a pseries machine");
        return;
    }

    sc->threads = g_new(PowerPCCPU *, cc->nr_threads);
    for (i = 0; i < cc->nr_threads; i++) {
        sc->threads[i] = spapr_create_vcpu(sc, i, &local_err);
        if (local_err) {
            goto err;
        }
    }

    for (j = 0; j < cc->nr_threads; j++) {
        spapr_realize_vcpu(sc->threads[j], spapr, sc, &local_err);
        if (local_err) {
            goto err_unrealize;
        }
    }
    return;

err_unrealize:
    while (--j >= 0) {
        spapr_unrealize_vcpu(sc->threads[j], sc);
    }
err:
    while (--i >= 0) {
        spapr_delete_vcpu(sc->threads[i], sc);
    }
    g_free(sc->threads);
    error_propagate(errp, local_err);
}

static Property spapr_cpu_core_properties[] = {
    DEFINE_PROP_INT32("node-id", sPAPRCPUCore, node_id, CPU_UNSET_NUMA_NODE_ID),
    DEFINE_PROP_BOOL("pre-3.0-migration", sPAPRCPUCore, pre_3_0_migration,
                     false),
    DEFINE_PROP_END_OF_LIST()
};

static void spapr_cpu_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    sPAPRCPUCoreClass *scc = SPAPR_CPU_CORE_CLASS(oc);

    dc->realize = spapr_cpu_core_realize;
    dc->unrealize = spapr_cpu_core_unrealize;
    dc->props = spapr_cpu_core_properties;
    scc->cpu_type = data;
}

#define DEFINE_SPAPR_CPU_CORE_TYPE(cpu_model) \
    {                                                   \
        .parent = TYPE_SPAPR_CPU_CORE,                  \
        .class_data = (void *) POWERPC_CPU_TYPE_NAME(cpu_model), \
        .class_init = spapr_cpu_core_class_init,        \
        .name = SPAPR_CPU_CORE_TYPE_NAME(cpu_model),    \
    }

static const TypeInfo spapr_cpu_core_type_infos[] = {
    {
        .name = TYPE_SPAPR_CPU_CORE,
        .parent = TYPE_CPU_CORE,
        .abstract = true,
        .instance_size = sizeof(sPAPRCPUCore),
        .class_size = sizeof(sPAPRCPUCoreClass),
    },
    DEFINE_SPAPR_CPU_CORE_TYPE("970_v2.2"),
    DEFINE_SPAPR_CPU_CORE_TYPE("970mp_v1.0"),
    DEFINE_SPAPR_CPU_CORE_TYPE("970mp_v1.1"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power5+_v2.1"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power7_v2.3"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power7+_v2.1"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power8_v2.0"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power8e_v2.1"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power8nvl_v1.0"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power9_v1.0"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power9_v2.0"),
#ifdef CONFIG_KVM
    DEFINE_SPAPR_CPU_CORE_TYPE("host"),
#endif
};

DEFINE_TYPES(spapr_cpu_core_type_infos)
