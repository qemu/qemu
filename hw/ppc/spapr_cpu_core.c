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
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "target/ppc/cpu.h"
#include "hw/ppc/spapr.h"
#include "qapi/error.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "target/ppc/kvm_ppc.h"
#include "hw/ppc/ppc.h"
#include "target/ppc/mmu-hash64.h"
#include "target/ppc/power8-pmu.h"
#include "sysemu/numa.h"
#include "sysemu/reset.h"
#include "sysemu/hw_accel.h"
#include "qemu/error-report.h"

static void spapr_reset_vcpu(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    target_ulong lpcr;
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    cpu_reset(cs);

    /*
     * "PowerPC Processor binding to IEEE 1275" defines the initial MSR state
     * as 32bit (MSR_SF=0) in "8.2.1. Initial Register Values".
     */
    env->msr &= ~(1ULL << MSR_SF);
    env->spr[SPR_HIOR] = 0;

    lpcr = env->spr[SPR_LPCR];

    /* Set emulated LPCR to not send interrupts to hypervisor. Note that
     * under KVM, the actual HW LPCR will be set differently by KVM itself,
     * the settings below ensure proper operations with TCG in absence of
     * a real hypervisor.
     *
     * Disable Power-saving mode Exit Cause exceptions for the CPU, so
     * we don't get spurious wakups before an RTAS start-cpu call.
     * For the same reason, set PSSCR_EC.
     */
    lpcr &= ~(LPCR_VPM1 | LPCR_ISL | LPCR_KBV | pcc->lpcr_pm);
    lpcr |= LPCR_LPES0 | LPCR_LPES1;
    env->spr[SPR_PSSCR] |= PSSCR_EC;

    ppc_store_lpcr(cpu, lpcr);

    /* Set a full AMOR so guest can use the AMR as it sees fit */
    env->spr[SPR_AMOR] = 0xffffffffffffffffull;

    spapr_cpu->vpa_addr = 0;
    spapr_cpu->slb_shadow_addr = 0;
    spapr_cpu->slb_shadow_size = 0;
    spapr_cpu->dtl_addr = 0;
    spapr_cpu->dtl_size = 0;

    spapr_caps_cpu_apply(spapr, cpu);

    kvm_check_mmu(cpu, &error_fatal);

    spapr_irq_cpu_intc_reset(spapr, cpu);
}

void spapr_cpu_set_entry_state(PowerPCCPU *cpu, target_ulong nip,
                               target_ulong r1, target_ulong r3,
                               target_ulong r4)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;

    env->nip = nip;
    env->gpr[1] = r1;
    env->gpr[3] = r3;
    env->gpr[4] = r4;
    kvmppc_set_reg_ppc_online(cpu, 1);
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
    SpaprCpuState *spapr_cpu = opaque;

    return spapr_cpu->slb_shadow_addr != 0;
}

static const VMStateDescription vmstate_spapr_cpu_slb_shadow = {
    .name = "spapr_cpu/vpa/slb_shadow",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = slb_shadow_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(slb_shadow_addr, SpaprCpuState),
        VMSTATE_UINT64(slb_shadow_size, SpaprCpuState),
        VMSTATE_END_OF_LIST()
    }
};

static bool dtl_needed(void *opaque)
{
    SpaprCpuState *spapr_cpu = opaque;

    return spapr_cpu->dtl_addr != 0;
}

static const VMStateDescription vmstate_spapr_cpu_dtl = {
    .name = "spapr_cpu/vpa/dtl",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = dtl_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(dtl_addr, SpaprCpuState),
        VMSTATE_UINT64(dtl_size, SpaprCpuState),
        VMSTATE_END_OF_LIST()
    }
};

static bool vpa_needed(void *opaque)
{
    SpaprCpuState *spapr_cpu = opaque;

    return spapr_cpu->vpa_addr != 0;
}

static const VMStateDescription vmstate_spapr_cpu_vpa = {
    .name = "spapr_cpu/vpa",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vpa_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(vpa_addr, SpaprCpuState),
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

static void spapr_unrealize_vcpu(PowerPCCPU *cpu, SpaprCpuCore *sc)
{
    CPUPPCState *env = &cpu->env;

    if (!sc->pre_3_0_migration) {
        vmstate_unregister(NULL, &vmstate_spapr_cpu_state, cpu->machine_data);
    }
    spapr_irq_cpu_intc_destroy(SPAPR_MACHINE(qdev_get_machine()), cpu);
    cpu_ppc_tb_free(env);
    qdev_unrealize(DEVICE(cpu));
}

/*
 * Called when CPUs are hot-plugged.
 */
static void spapr_cpu_core_reset(DeviceState *dev)
{
    CPUCore *cc = CPU_CORE(dev);
    SpaprCpuCore *sc = SPAPR_CPU_CORE(dev);
    int i;

    for (i = 0; i < cc->nr_threads; i++) {
        spapr_reset_vcpu(sc->threads[i]);
    }
}

/*
 * Called by the machine reset.
 */
static void spapr_cpu_core_reset_handler(void *opaque)
{
    spapr_cpu_core_reset(opaque);
}

static void spapr_delete_vcpu(PowerPCCPU *cpu)
{
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    cpu->machine_data = NULL;
    g_free(spapr_cpu);
    object_unparent(OBJECT(cpu));
}

static void spapr_cpu_core_unrealize(DeviceState *dev)
{
    SpaprCpuCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    CPUCore *cc = CPU_CORE(dev);
    int i;

    for (i = 0; i < cc->nr_threads; i++) {
        if (sc->threads[i]) {
            /*
             * Since this we can get here from the error path of
             * spapr_cpu_core_realize(), make sure we only unrealize
             * vCPUs that have already been realized.
             */
            if (object_property_get_bool(OBJECT(sc->threads[i]), "realized",
                                         &error_abort)) {
                spapr_unrealize_vcpu(sc->threads[i], sc);
            }
            spapr_delete_vcpu(sc->threads[i]);
        }
    }
    g_free(sc->threads);
    qemu_unregister_reset(spapr_cpu_core_reset_handler, sc);
}

static bool spapr_realize_vcpu(PowerPCCPU *cpu, SpaprMachineState *spapr,
                               SpaprCpuCore *sc, Error **errp)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    if (!qdev_realize(DEVICE(cpu), NULL, errp)) {
        return false;
    }

    cpu_ppc_set_vhyp(cpu, PPC_VIRTUAL_HYPERVISOR(spapr));
    kvmppc_set_papr(cpu);

    /* Set time-base frequency to 512 MHz. vhyp must be set first. */
    cpu_ppc_tb_init(env, SPAPR_TIMEBASE_FREQ);

    if (spapr_irq_cpu_intc_create(spapr, cpu, errp) < 0) {
        qdev_unrealize(DEVICE(cpu));
        return false;
    }

    if (!sc->pre_3_0_migration) {
        vmstate_register(NULL, cs->cpu_index, &vmstate_spapr_cpu_state,
                         cpu->machine_data);
    }
    return true;
}

static PowerPCCPU *spapr_create_vcpu(SpaprCpuCore *sc, int i, Error **errp)
{
    SpaprCpuCoreClass *scc = SPAPR_CPU_CORE_GET_CLASS(sc);
    CPUCore *cc = CPU_CORE(sc);
    g_autoptr(Object) obj = NULL;
    g_autofree char *id = NULL;
    CPUState *cs;
    PowerPCCPU *cpu;

    obj = object_new(scc->cpu_type);

    cs = CPU(obj);
    cpu = POWERPC_CPU(obj);
    /*
     * All CPUs start halted. CPU0 is unhalted from the machine level reset code
     * and the rest are explicitly started up by the guest using an RTAS call.
     */
    cs->start_powered_off = true;
    cs->cpu_index = cc->core_id + i;
    if (!spapr_set_vcpu_id(cpu, cs->cpu_index, errp)) {
        return NULL;
    }

    cpu->node_id = sc->node_id;

    id = g_strdup_printf("thread[%d]", i);
    object_property_add_child(OBJECT(sc), id, obj);

    cpu->machine_data = g_new0(SpaprCpuState, 1);

    return cpu;
}

static void spapr_cpu_core_realize(DeviceState *dev, Error **errp)
{
    /* We don't use SPAPR_MACHINE() in order to exit gracefully if the user
     * tries to add a sPAPR CPU core to a non-pseries machine.
     */
    SpaprMachineState *spapr =
        (SpaprMachineState *) object_dynamic_cast(qdev_get_machine(),
                                                  TYPE_SPAPR_MACHINE);
    SpaprCpuCore *sc = SPAPR_CPU_CORE(OBJECT(dev));
    CPUCore *cc = CPU_CORE(OBJECT(dev));
    int i;

    if (!spapr) {
        error_setg(errp, TYPE_SPAPR_CPU_CORE " needs a pseries machine");
        return;
    }

    qemu_register_reset(spapr_cpu_core_reset_handler, sc);
    sc->threads = g_new0(PowerPCCPU *, cc->nr_threads);
    for (i = 0; i < cc->nr_threads; i++) {
        sc->threads[i] = spapr_create_vcpu(sc, i, errp);
        if (!sc->threads[i] ||
            !spapr_realize_vcpu(sc->threads[i], spapr, sc, errp)) {
            spapr_cpu_core_unrealize(dev);
            return;
        }
    }
}

static Property spapr_cpu_core_properties[] = {
    DEFINE_PROP_INT32("node-id", SpaprCpuCore, node_id, CPU_UNSET_NUMA_NODE_ID),
    DEFINE_PROP_BOOL("pre-3.0-migration", SpaprCpuCore, pre_3_0_migration,
                     false),
    DEFINE_PROP_END_OF_LIST()
};

static void spapr_cpu_core_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    SpaprCpuCoreClass *scc = SPAPR_CPU_CORE_CLASS(oc);

    dc->realize = spapr_cpu_core_realize;
    dc->unrealize = spapr_cpu_core_unrealize;
    dc->reset = spapr_cpu_core_reset;
    device_class_set_props(dc, spapr_cpu_core_properties);
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
        .instance_size = sizeof(SpaprCpuCore),
        .class_size = sizeof(SpaprCpuCoreClass),
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
    DEFINE_SPAPR_CPU_CORE_TYPE("power9_v2.2"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power10_v1.0"),
    DEFINE_SPAPR_CPU_CORE_TYPE("power10_v2.0"),
#ifdef CONFIG_KVM
    DEFINE_SPAPR_CPU_CORE_TYPE("host"),
#endif
};

DEFINE_TYPES(spapr_cpu_core_type_infos)
