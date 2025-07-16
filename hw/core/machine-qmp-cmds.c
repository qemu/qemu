/*
 * QMP commands related to machines and CPUs
 *
 * Copyright (C) 2014 Red Hat Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/acpi/vmgenid.h"
#include "hw/boards.h"
#include "hw/intc/intc.h"
#include "hw/mem/memory-device.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "qapi/qapi-commands-accelerator.h"
#include "qapi/qapi-commands-machine.h"
#include "qobject/qobject.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/type-helpers.h"
#include "qemu/uuid.h"
#include "qemu/target-info-qapi.h"
#include "qom/qom-qobject.h"
#include "system/hostmem.h"
#include "system/hw_accel.h"
#include "system/numa.h"
#include "system/runstate.h"
#include "system/system.h"
#include "hw/s390x/storage-keys.h"

/*
 * fast means: we NEVER interrupt vCPU threads to retrieve
 * information from KVM.
 */
CpuInfoFastList *qmp_query_cpus_fast(Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    CpuInfoFastList *head = NULL, **tail = &head;
    SysEmuTarget target = target_arch();
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        CpuInfoFast *value = g_malloc0(sizeof(*value));

        value->cpu_index = cpu->cpu_index;
        value->qom_path = object_get_canonical_path(OBJECT(cpu));
        value->thread_id = cpu->thread_id;
        value->qom_type = g_strdup(object_get_typename(OBJECT(cpu)));

        if (mc->cpu_index_to_instance_props) {
            CpuInstanceProperties *props;
            props = g_malloc0(sizeof(*props));
            *props = mc->cpu_index_to_instance_props(ms, cpu->cpu_index);
            value->props = props;
        }

        value->target = target;
        if (cpu->cc->query_cpu_fast) {
            cpu->cc->query_cpu_fast(cpu, value);
        }

        QAPI_LIST_APPEND(tail, value);
    }

    return head;
}

MachineInfoList *qmp_query_machines(bool has_compat_props, bool compat_props,
                                    Error **errp)
{
    GSList *el, *machines = object_class_get_list(TYPE_MACHINE, false);
    MachineInfoList *mach_list = NULL;

    for (el = machines; el; el = el->next) {
        MachineClass *mc = el->data;
        const char *default_cpu_type = machine_class_default_cpu_type(mc);
        MachineInfo *info;

        info = g_malloc0(sizeof(*info));
        if (mc->is_default) {
            info->has_is_default = true;
            info->is_default = true;
        }

        if (mc->alias) {
            info->alias = g_strdup(mc->alias);
        }

        info->name = g_strdup(mc->name);
        info->cpu_max = !mc->max_cpus ? 1 : mc->max_cpus;
        info->hotpluggable_cpus = mc->has_hotpluggable_cpus;
        info->numa_mem_supported = mc->numa_mem_supported;
        info->deprecated = !!mc->deprecation_reason;
        info->acpi = !!object_class_property_find(OBJECT_CLASS(mc), "acpi");
        if (default_cpu_type) {
            info->default_cpu_type = g_strdup(default_cpu_type);
        }
        if (mc->default_ram_id) {
            info->default_ram_id = g_strdup(mc->default_ram_id);
        }

        if (compat_props && mc->compat_props) {
            int i;
            info->compat_props = NULL;
            CompatPropertyList **tail = &(info->compat_props);
            info->has_compat_props = true;

            for (i = 0; i < mc->compat_props->len; i++) {
                GlobalProperty *mt_prop = g_ptr_array_index(mc->compat_props,
                                                            i);
                CompatProperty *prop;

                prop = g_malloc0(sizeof(*prop));
                prop->qom_type = g_strdup(mt_prop->driver);
                prop->property = g_strdup(mt_prop->property);
                prop->value = g_strdup(mt_prop->value);

                QAPI_LIST_APPEND(tail, prop);
            }
        }

        QAPI_LIST_PREPEND(mach_list, info);
    }

    g_slist_free(machines);
    return mach_list;
}

CurrentMachineParams *qmp_query_current_machine(Error **errp)
{
    CurrentMachineParams *params = g_malloc0(sizeof(*params));
    params->wakeup_suspend_support = qemu_wakeup_suspend_enabled();

    return params;
}

QemuTargetInfo *qmp_query_target(Error **errp)
{
    QemuTargetInfo *info = g_malloc0(sizeof(*info));

    info->arch = target_arch();

    return info;
}

HotpluggableCPUList *qmp_query_hotpluggable_cpus(Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    if (!mc->has_hotpluggable_cpus) {
        error_setg(errp, "machine does not support hot-plugging CPUs");
        return NULL;
    }

    return machine_query_hotpluggable_cpus(ms);
}

void qmp_set_numa_node(NumaOptions *cmd, Error **errp)
{
    if (phase_check(PHASE_MACHINE_INITIALIZED)) {
        error_setg(errp, "The command is permitted only before the machine has been created");
        return;
    }

    set_numa_options(MACHINE(qdev_get_machine()), cmd, errp);
}

static int query_memdev(Object *obj, void *opaque)
{
    Error *err = NULL;
    MemdevList **list = opaque;
    Memdev *m;
    QObject *host_nodes;
    Visitor *v;

    if (object_dynamic_cast(obj, TYPE_MEMORY_BACKEND)) {
        m = g_malloc0(sizeof(*m));

        m->id = g_strdup(object_get_canonical_path_component(obj));

        m->size = object_property_get_uint(obj, "size", &error_abort);
        m->merge = object_property_get_bool(obj, "merge", &error_abort);
        m->dump = object_property_get_bool(obj, "dump", &error_abort);
        m->prealloc = object_property_get_bool(obj, "prealloc", &error_abort);
        m->share = object_property_get_bool(obj, "share", &error_abort);
        m->reserve = object_property_get_bool(obj, "reserve", &err);
        if (err) {
            error_free_or_abort(&err);
        } else {
            m->has_reserve = true;
        }
        m->policy = object_property_get_enum(obj, "policy", "HostMemPolicy",
                                             &error_abort);
        host_nodes = object_property_get_qobject(obj,
                                                 "host-nodes",
                                                 &error_abort);
        v = qobject_input_visitor_new(host_nodes);
        visit_type_uint16List(v, NULL, &m->host_nodes, &error_abort);
        visit_free(v);
        qobject_unref(host_nodes);

        QAPI_LIST_PREPEND(*list, m);
    }

    return 0;
}

MemdevList *qmp_query_memdev(Error **errp)
{
    Object *obj = object_get_objects_root();
    MemdevList *list = NULL;

    object_child_foreach(obj, query_memdev, &list);
    return list;
}

HumanReadableText *qmp_x_query_numa(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");
    int i, nb_numa_nodes;
    NumaNodeMem *node_mem;
    CpuInfoFastList *cpu_list, *cpu;
    MachineState *ms = MACHINE(qdev_get_machine());

    nb_numa_nodes = ms->numa_state ? ms->numa_state->num_nodes : 0;
    g_string_append_printf(buf, "%d nodes\n", nb_numa_nodes);
    if (!nb_numa_nodes) {
        goto done;
    }

    cpu_list = qmp_query_cpus_fast(&error_abort);
    node_mem = g_new0(NumaNodeMem, nb_numa_nodes);

    query_numa_node_mem(node_mem, ms);
    for (i = 0; i < nb_numa_nodes; i++) {
        g_string_append_printf(buf, "node %d cpus:", i);
        for (cpu = cpu_list; cpu; cpu = cpu->next) {
            if (cpu->value->props && cpu->value->props->has_node_id &&
                cpu->value->props->node_id == i) {
                g_string_append_printf(buf, " %" PRIi64, cpu->value->cpu_index);
            }
        }
        g_string_append_printf(buf, "\n");
        g_string_append_printf(buf, "node %d size: %" PRId64 " MB\n", i,
                               node_mem[i].node_mem >> 20);
        g_string_append_printf(buf, "node %d plugged: %" PRId64 " MB\n", i,
                               node_mem[i].node_plugged_mem >> 20);
    }
    qapi_free_CpuInfoFastList(cpu_list);
    g_free(node_mem);

 done:
    return human_readable_text_from_str(buf);
}

KvmInfo *qmp_query_kvm(Error **errp)
{
    KvmInfo *info = g_malloc0(sizeof(*info));

    info->enabled = kvm_enabled();
    info->present = accel_find("kvm");

    return info;
}

UuidInfo *qmp_query_uuid(Error **errp)
{
    UuidInfo *info = g_malloc0(sizeof(*info));

    info->UUID = qemu_uuid_unparse_strdup(&qemu_uuid);
    return info;
}

void qmp_system_reset(Error **errp)
{
    qemu_system_reset_request(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
}

void qmp_system_powerdown(Error **errp)
{
    qemu_system_powerdown_request();
}

void qmp_system_wakeup(Error **errp)
{
    if (!qemu_wakeup_suspend_enabled()) {
        error_setg(errp,
                   "wake-up from suspend is not supported by this guest");
        return;
    }

    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, errp);
}

MemoryDeviceInfoList *qmp_query_memory_devices(Error **errp)
{
    return qmp_memory_device_list();
}

MemoryInfo *qmp_query_memory_size_summary(Error **errp)
{
    MemoryInfo *mem_info = g_new0(MemoryInfo, 1);
    MachineState *ms = MACHINE(qdev_get_machine());

    mem_info->base_memory = ms->ram_size;

    mem_info->plugged_memory = get_plugged_memory_size();
    mem_info->has_plugged_memory =
        mem_info->plugged_memory != (uint64_t)-1;

    return mem_info;
}

HumanReadableText *qmp_x_query_ramblock(Error **errp)
{
    g_autoptr(GString) buf = ram_block_format();

    return human_readable_text_from_str(buf);
}

static int qmp_x_query_irq_foreach(Object *obj, void *opaque)
{
    InterruptStatsProvider *intc;
    InterruptStatsProviderClass *k;
    GString *buf = opaque;

    if (object_dynamic_cast(obj, TYPE_INTERRUPT_STATS_PROVIDER)) {
        intc = INTERRUPT_STATS_PROVIDER(obj);
        k = INTERRUPT_STATS_PROVIDER_GET_CLASS(obj);
        uint64_t *irq_counts;
        unsigned int nb_irqs, i;
        if (k->get_statistics &&
            k->get_statistics(intc, &irq_counts, &nb_irqs)) {
            if (nb_irqs > 0) {
                g_string_append_printf(buf, "IRQ statistics for %s:\n",
                                       object_get_typename(obj));
                for (i = 0; i < nb_irqs; i++) {
                    if (irq_counts[i] > 0) {
                        g_string_append_printf(buf, "%2d: %" PRId64 "\n", i,
                                               irq_counts[i]);
                    }
                }
            }
        } else {
            g_string_append_printf(buf,
                                   "IRQ statistics not available for %s.\n",
                                   object_get_typename(obj));
        }
    }

    return 0;
}

HumanReadableText *qmp_x_query_irq(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    object_child_foreach_recursive(object_get_root(),
                                   qmp_x_query_irq_foreach, buf);

    return human_readable_text_from_str(buf);
}

static int qmp_x_query_intc_foreach(Object *obj, void *opaque)
{
    InterruptStatsProvider *intc;
    InterruptStatsProviderClass *k;
    GString *buf = opaque;

    if (object_dynamic_cast(obj, TYPE_INTERRUPT_STATS_PROVIDER)) {
        intc = INTERRUPT_STATS_PROVIDER(obj);
        k = INTERRUPT_STATS_PROVIDER_GET_CLASS(obj);
        if (k->print_info) {
            k->print_info(intc, buf);
        } else {
            g_string_append_printf(buf,
                                   "Interrupt controller information not available for %s.\n",
                                   object_get_typename(obj));
        }
    }

    return 0;
}

HumanReadableText *qmp_x_query_interrupt_controllers(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");
    object_child_foreach_recursive(object_get_root(),
                                   qmp_x_query_intc_foreach, buf);
    return human_readable_text_from_str(buf);
}

GuidInfo *qmp_query_vm_generation_id(Error **errp)
{
    GuidInfo *info;
    VmGenIdState *vms;
    Object *obj = find_vmgenid_dev();

    if (!obj) {
        error_setg(errp, "VM Generation ID device not found");
        return NULL;
    }
    vms = VMGENID(obj);

    info = g_malloc0(sizeof(*info));
    info->guid = qemu_uuid_unparse_strdup(&vms->guid);
    return info;
}

void qmp_dump_skeys(const char *filename, Error **errp)
{
    ObjectClass *mc = object_get_class(qdev_get_machine());
    ObjectClass *oc = object_class_dynamic_cast(mc, TYPE_DUMP_SKEYS_INTERFACE);

    if (!oc) {
        error_setg(errp, "Storage keys information not available"
                         " for this architecture");
        return;
    }
    DUMP_SKEYS_INTERFACE_CLASS(oc)->qmp_dump_skeys(filename, errp);
}
