/*
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2019 Red Hat, Inc.
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
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-common.h"
#include "qapi/qapi-visit-machine.h"
#include "qapi/visitor.h"
#include "system/qtest.h"
#include "system/numa.h"
#include "trace.h"

#include "hw/acpi/aml-build.h"
#include "hw/i386/x86.h"
#include "hw/i386/topology.h"

#include "hw/nmi.h"
#include "kvm/kvm_i386.h"


void init_topo_info(X86CPUTopoInfo *topo_info,
                    const X86MachineState *x86ms)
{
    MachineState *ms = MACHINE(x86ms);

    topo_info->dies_per_pkg = ms->smp.dies;
    /*
     * Though smp.modules means the number of modules in one cluster,
     * i386 doesn't support cluster level so that the smp.clusters
     * always defaults to 1, therefore using smp.modules directly is
     * fine here.
     */
    topo_info->modules_per_die = ms->smp.modules;
    topo_info->cores_per_module = ms->smp.cores;
    topo_info->threads_per_core = ms->smp.threads;
}

/*
 * Calculates initial APIC ID for a specific CPU index
 *
 * Currently we need to be able to calculate the APIC ID from the CPU index
 * alone (without requiring a CPU object), as the QEMU<->Seabios interfaces have
 * no concept of "CPU index", and the NUMA tables on fw_cfg need the APIC ID of
 * all CPUs up to max_cpus.
 */
uint32_t x86_cpu_apic_id_from_index(X86MachineState *x86ms,
                                    unsigned int cpu_index)
{
    X86CPUTopoInfo topo_info;

    init_topo_info(&topo_info, x86ms);

    return x86_apicid_from_cpu_idx(&topo_info, cpu_index);
}

static CpuInstanceProperties
x86_cpu_index_to_props(MachineState *ms, unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static int64_t x86_get_default_cpu_node_id(const MachineState *ms, int idx)
{
   X86CPUTopoIDs topo_ids;
   X86MachineState *x86ms = X86_MACHINE(ms);
   X86CPUTopoInfo topo_info;

   init_topo_info(&topo_info, x86ms);

   assert(idx < ms->possible_cpus->len);
   x86_topo_ids_from_apicid(ms->possible_cpus->cpus[idx].arch_id,
                            &topo_info, &topo_ids);
   return topo_ids.pkg_id % ms->numa_state->num_nodes;
}

static const CPUArchIdList *x86_possible_cpu_arch_ids(MachineState *ms)
{
    X86MachineState *x86ms = X86_MACHINE(ms);
    unsigned int max_cpus = ms->smp.max_cpus;
    X86CPUTopoInfo topo_info;
    int i;

    if (ms->possible_cpus) {
        /*
         * make sure that max_cpus hasn't changed since the first use, i.e.
         * -smp hasn't been parsed after it
         */
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;

    init_topo_info(&topo_info, x86ms);

    for (i = 0; i < ms->possible_cpus->len; i++) {
        X86CPUTopoIDs topo_ids;

        ms->possible_cpus->cpus[i].type = ms->cpu_type;
        ms->possible_cpus->cpus[i].vcpus_count = 1;
        ms->possible_cpus->cpus[i].arch_id =
            x86_cpu_apic_id_from_index(x86ms, i);
        x86_topo_ids_from_apicid(ms->possible_cpus->cpus[i].arch_id,
                                 &topo_info, &topo_ids);
        ms->possible_cpus->cpus[i].props.has_socket_id = true;
        ms->possible_cpus->cpus[i].props.socket_id = topo_ids.pkg_id;
        if (ms->smp.dies > 1) {
            ms->possible_cpus->cpus[i].props.has_die_id = true;
            ms->possible_cpus->cpus[i].props.die_id = topo_ids.die_id;
        }
        if (ms->smp.modules > 1) {
            ms->possible_cpus->cpus[i].props.has_module_id = true;
            ms->possible_cpus->cpus[i].props.module_id = topo_ids.module_id;
        }
        ms->possible_cpus->cpus[i].props.has_core_id = true;
        ms->possible_cpus->cpus[i].props.core_id = topo_ids.core_id;
        ms->possible_cpus->cpus[i].props.has_thread_id = true;
        ms->possible_cpus->cpus[i].props.thread_id = topo_ids.smt_id;
    }
    return ms->possible_cpus;
}

static void x86_nmi(NMIState *n, int cpu_index, Error **errp)
{
    /* cpu index isn't used */
    CPUState *cs;

    CPU_FOREACH(cs) {
        X86CPU *cpu = X86_CPU(cs);

        if (cpu_is_apic_enabled(cpu->apic_state)) {
            apic_deliver_nmi(cpu->apic_state);
        } else {
            cpu_interrupt(cs, CPU_INTERRUPT_NMI);
        }
    }
}

bool x86_machine_is_smm_enabled(const X86MachineState *x86ms)
{
    bool smm_available = false;

    if (x86ms->smm == ON_OFF_AUTO_OFF) {
        return false;
    }

    if (tcg_enabled() || qtest_enabled()) {
        smm_available = true;
    } else if (kvm_enabled()) {
        smm_available = kvm_has_smm();
    }

    if (smm_available) {
        return true;
    }

    if (x86ms->smm == ON_OFF_AUTO_ON) {
        error_report("System Management Mode not supported by this hypervisor.");
        exit(1);
    }
    return false;
}

static void x86_machine_get_smm(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    OnOffAuto smm = x86ms->smm;

    visit_type_OnOffAuto(v, name, &smm, errp);
}

static void x86_machine_set_smm(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &x86ms->smm, errp);
}

bool x86_machine_is_acpi_enabled(const X86MachineState *x86ms)
{
    if (x86ms->acpi == ON_OFF_AUTO_OFF) {
        return false;
    }
    return true;
}

static void x86_machine_get_acpi(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    OnOffAuto acpi = x86ms->acpi;

    visit_type_OnOffAuto(v, name, &acpi, errp);
}

static void x86_machine_set_acpi(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &x86ms->acpi, errp);
}

static void x86_machine_get_pit(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    OnOffAuto pit = x86ms->pit;

    visit_type_OnOffAuto(v, name, &pit, errp);
}

static void x86_machine_set_pit(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &x86ms->pit, errp);
}

static void x86_machine_get_pic(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    OnOffAuto pic = x86ms->pic;

    visit_type_OnOffAuto(v, name, &pic, errp);
}

static void x86_machine_set_pic(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &x86ms->pic, errp);
}

static char *x86_machine_get_oem_id(Object *obj, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    return g_strdup(x86ms->oem_id);
}

static void x86_machine_set_oem_id(Object *obj, const char *value, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    size_t len = strlen(value);

    if (len > 6) {
        error_setg(errp,
                   "User specified "X86_MACHINE_OEM_ID" value is bigger than "
                   "6 bytes in size");
        return;
    }

    strncpy(x86ms->oem_id, value, 6);
}

static char *x86_machine_get_oem_table_id(Object *obj, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    return g_strdup(x86ms->oem_table_id);
}

static void x86_machine_set_oem_table_id(Object *obj, const char *value,
                                         Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    size_t len = strlen(value);

    if (len > 8) {
        error_setg(errp,
                   "User specified "X86_MACHINE_OEM_TABLE_ID
                   " value is bigger than "
                   "8 bytes in size");
        return;
    }
    strncpy(x86ms->oem_table_id, value, 8);
}

static void x86_machine_get_bus_lock_ratelimit(Object *obj, Visitor *v,
                                const char *name, void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    uint64_t bus_lock_ratelimit = x86ms->bus_lock_ratelimit;

    visit_type_uint64(v, name, &bus_lock_ratelimit, errp);
}

static void x86_machine_set_bus_lock_ratelimit(Object *obj, Visitor *v,
                               const char *name, void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    visit_type_uint64(v, name, &x86ms->bus_lock_ratelimit, errp);
}

static void machine_get_sgx_epc(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    SgxEPCList *list = x86ms->sgx_epc_list;

    visit_type_SgxEPCList(v, name, &list, errp);
}

static void machine_set_sgx_epc(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(obj);
    SgxEPCList *list;

    list = x86ms->sgx_epc_list;
    visit_type_SgxEPCList(v, name, &x86ms->sgx_epc_list, errp);

    qapi_free_SgxEPCList(list);
}

static int x86_kvm_type(MachineState *ms, const char *vm_type)
{
    /*
     * No x86 machine has a kvm-type property.  If one is added that has
     * it, it should call kvm_get_vm_type() directly or not use it at all.
     */
    assert(vm_type == NULL);
    return kvm_enabled() ? kvm_get_vm_type(ms) : 0;
}

static void x86_machine_initfn(Object *obj)
{
    X86MachineState *x86ms = X86_MACHINE(obj);

    x86ms->smm = ON_OFF_AUTO_AUTO;
    x86ms->acpi = ON_OFF_AUTO_AUTO;
    x86ms->pit = ON_OFF_AUTO_AUTO;
    x86ms->pic = ON_OFF_AUTO_AUTO;
    x86ms->pci_irq_mask = ACPI_BUILD_PCI_IRQS;
    x86ms->oem_id = g_strndup(ACPI_BUILD_APPNAME6, 6);
    x86ms->oem_table_id = g_strndup(ACPI_BUILD_APPNAME8, 8);
    x86ms->bus_lock_ratelimit = 0;
    x86ms->above_4g_mem_start = 4 * GiB;
}

static void x86_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    X86MachineClass *x86mc = X86_MACHINE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->cpu_index_to_instance_props = x86_cpu_index_to_props;
    mc->get_default_cpu_node_id = x86_get_default_cpu_node_id;
    mc->possible_cpu_arch_ids = x86_possible_cpu_arch_ids;
    mc->kvm_type = x86_kvm_type;
    x86mc->fwcfg_dma_enabled = true;
    nc->nmi_monitor_handler = x86_nmi;

    object_class_property_add(oc, X86_MACHINE_SMM, "OnOffAuto",
        x86_machine_get_smm, x86_machine_set_smm,
        NULL, NULL);
    object_class_property_set_description(oc, X86_MACHINE_SMM,
        "Enable SMM");

    object_class_property_add(oc, X86_MACHINE_ACPI, "OnOffAuto",
        x86_machine_get_acpi, x86_machine_set_acpi,
        NULL, NULL);
    object_class_property_set_description(oc, X86_MACHINE_ACPI,
        "Enable ACPI");

    object_class_property_add(oc, X86_MACHINE_PIT, "OnOffAuto",
                              x86_machine_get_pit,
                              x86_machine_set_pit,
                              NULL, NULL);
    object_class_property_set_description(oc, X86_MACHINE_PIT,
        "Enable i8254 PIT");

    object_class_property_add(oc, X86_MACHINE_PIC, "OnOffAuto",
                              x86_machine_get_pic,
                              x86_machine_set_pic,
                              NULL, NULL);
    object_class_property_set_description(oc, X86_MACHINE_PIC,
        "Enable i8259 PIC");

    object_class_property_add_str(oc, X86_MACHINE_OEM_ID,
                                  x86_machine_get_oem_id,
                                  x86_machine_set_oem_id);
    object_class_property_set_description(oc, X86_MACHINE_OEM_ID,
                                          "Override the default value of field OEMID "
                                          "in ACPI table header."
                                          "The string may be up to 6 bytes in size");


    object_class_property_add_str(oc, X86_MACHINE_OEM_TABLE_ID,
                                  x86_machine_get_oem_table_id,
                                  x86_machine_set_oem_table_id);
    object_class_property_set_description(oc, X86_MACHINE_OEM_TABLE_ID,
                                          "Override the default value of field OEM Table ID "
                                          "in ACPI table header."
                                          "The string may be up to 8 bytes in size");

    object_class_property_add(oc, X86_MACHINE_BUS_LOCK_RATELIMIT, "uint64_t",
                                x86_machine_get_bus_lock_ratelimit,
                                x86_machine_set_bus_lock_ratelimit, NULL, NULL);
    object_class_property_set_description(oc, X86_MACHINE_BUS_LOCK_RATELIMIT,
            "Set the ratelimit for the bus locks acquired in VMs");

    object_class_property_add(oc, "sgx-epc", "SgxEPC",
        machine_get_sgx_epc, machine_set_sgx_epc,
        NULL, NULL);
    object_class_property_set_description(oc, "sgx-epc",
        "SGX EPC device");
}

static const TypeInfo x86_machine_info = {
    .name = TYPE_X86_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(X86MachineState),
    .instance_init = x86_machine_initfn,
    .class_size = sizeof(X86MachineClass),
    .class_init = x86_machine_class_init,
    .interfaces = (const InterfaceInfo[]) {
         { TYPE_NMI },
         { }
    },
};

static void x86_machine_register_types(void)
{
    type_register_static(&x86_machine_info);
}

type_init(x86_machine_register_types)
