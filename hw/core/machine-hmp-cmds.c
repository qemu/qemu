/*
 * HMP commands related to machines and CPUs
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qmp/qdict.h"
#include "qapi/string-output-visitor.h"
#include "qemu/error-report.h"
#include "sysemu/numa.h"
#include "hw/boards.h"

void hmp_info_cpus(Monitor *mon, const QDict *qdict)
{
    CpuInfoFastList *cpu_list, *cpu;

    cpu_list = qmp_query_cpus_fast(NULL);

    for (cpu = cpu_list; cpu; cpu = cpu->next) {
        int active = ' ';

        if (cpu->value->cpu_index == monitor_get_cpu_index(mon)) {
            active = '*';
        }

        monitor_printf(mon, "%c CPU #%" PRId64 ":", active,
                       cpu->value->cpu_index);
        monitor_printf(mon, " thread_id=%" PRId64 "\n", cpu->value->thread_id);
    }

    qapi_free_CpuInfoFastList(cpu_list);
}

void hmp_hotpluggable_cpus(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    HotpluggableCPUList *l = qmp_query_hotpluggable_cpus(&err);
    HotpluggableCPUList *saved = l;
    CpuInstanceProperties *c;

    if (hmp_handle_error(mon, err)) {
        return;
    }

    monitor_printf(mon, "Hotpluggable CPUs:\n");
    while (l) {
        monitor_printf(mon, "  type: \"%s\"\n", l->value->type);
        monitor_printf(mon, "  vcpus_count: \"%" PRIu64 "\"\n",
                       l->value->vcpus_count);
        if (l->value->qom_path) {
            monitor_printf(mon, "  qom_path: \"%s\"\n", l->value->qom_path);
        }

        c = l->value->props;
        monitor_printf(mon, "  CPUInstance Properties:\n");
        if (c->has_node_id) {
            monitor_printf(mon, "    node-id: \"%" PRIu64 "\"\n", c->node_id);
        }
        if (c->has_drawer_id) {
            monitor_printf(mon, "    drawer-id: \"%" PRIu64 "\"\n", c->drawer_id);
        }
        if (c->has_book_id) {
            monitor_printf(mon, "    book-id: \"%" PRIu64 "\"\n", c->book_id);
        }
        if (c->has_socket_id) {
            monitor_printf(mon, "    socket-id: \"%" PRIu64 "\"\n", c->socket_id);
        }
        if (c->has_die_id) {
            monitor_printf(mon, "    die-id: \"%" PRIu64 "\"\n", c->die_id);
        }
        if (c->has_cluster_id) {
            monitor_printf(mon, "    cluster-id: \"%" PRIu64 "\"\n",
                           c->cluster_id);
        }
        if (c->has_core_id) {
            monitor_printf(mon, "    core-id: \"%" PRIu64 "\"\n", c->core_id);
        }
        if (c->has_thread_id) {
            monitor_printf(mon, "    thread-id: \"%" PRIu64 "\"\n", c->thread_id);
        }

        l = l->next;
    }

    qapi_free_HotpluggableCPUList(saved);
}

void hmp_info_memdev(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    MemdevList *memdev_list = qmp_query_memdev(&err);
    MemdevList *m = memdev_list;
    Visitor *v;
    char *str;

    while (m) {
        v = string_output_visitor_new(false, &str);
        visit_type_uint16List(v, NULL, &m->value->host_nodes, &error_abort);
        monitor_printf(mon, "memory backend: %s\n", m->value->id);
        monitor_printf(mon, "  size:  %" PRId64 "\n", m->value->size);
        monitor_printf(mon, "  merge: %s\n",
                       m->value->merge ? "true" : "false");
        monitor_printf(mon, "  dump: %s\n",
                       m->value->dump ? "true" : "false");
        monitor_printf(mon, "  prealloc: %s\n",
                       m->value->prealloc ? "true" : "false");
        monitor_printf(mon, "  share: %s\n",
                       m->value->share ? "true" : "false");
        if (m->value->has_reserve) {
            monitor_printf(mon, "  reserve: %s\n",
                           m->value->reserve ? "true" : "false");
        }
        monitor_printf(mon, "  policy: %s\n",
                       HostMemPolicy_str(m->value->policy));
        visit_complete(v, &str);
        monitor_printf(mon, "  host nodes: %s\n", str);

        g_free(str);
        visit_free(v);
        m = m->next;
    }

    monitor_printf(mon, "\n");

    qapi_free_MemdevList(memdev_list);
    hmp_handle_error(mon, err);
}

void hmp_info_kvm(Monitor *mon, const QDict *qdict)
{
    KvmInfo *info;

    info = qmp_query_kvm(NULL);
    monitor_printf(mon, "kvm support: ");
    if (info->present) {
        monitor_printf(mon, "%s\n", info->enabled ? "enabled" : "disabled");
    } else {
        monitor_printf(mon, "not compiled\n");
    }

    qapi_free_KvmInfo(info);
}

void hmp_info_uuid(Monitor *mon, const QDict *qdict)
{
    UuidInfo *info;

    info = qmp_query_uuid(NULL);
    monitor_printf(mon, "%s\n", info->UUID);
    qapi_free_UuidInfo(info);
}

void hmp_info_balloon(Monitor *mon, const QDict *qdict)
{
    BalloonInfo *info;
    Error *err = NULL;

    info = qmp_query_balloon(&err);
    if (hmp_handle_error(mon, err)) {
        return;
    }

    monitor_printf(mon, "balloon: actual=%" PRId64 "\n", info->actual >> 20);

    qapi_free_BalloonInfo(info);
}

void hmp_system_reset(Monitor *mon, const QDict *qdict)
{
    qmp_system_reset(NULL);
}

void hmp_system_powerdown(Monitor *mon, const QDict *qdict)
{
    qmp_system_powerdown(NULL);
}

void hmp_memsave(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    uint64_t addr = qdict_get_int(qdict, "val");
    Error *err = NULL;
    int cpu_index = monitor_get_cpu_index(mon);

    if (cpu_index < 0) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    qmp_memsave(addr, size, filename, true, cpu_index, &err);
    hmp_handle_error(mon, err);
}

void hmp_pmemsave(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    uint64_t addr = qdict_get_int(qdict, "val");
    Error *err = NULL;

    qmp_pmemsave(addr, size, filename, &err);
    hmp_handle_error(mon, err);
}

void hmp_system_wakeup(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_system_wakeup(&err);
    hmp_handle_error(mon, err);
}

void hmp_nmi(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_inject_nmi(&err);
    hmp_handle_error(mon, err);
}

void hmp_balloon(Monitor *mon, const QDict *qdict)
{
    int64_t value = qdict_get_int(qdict, "value");
    Error *err = NULL;

    qmp_balloon(value, &err);
    hmp_handle_error(mon, err);
}

void hmp_info_memory_devices(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    MemoryDeviceInfoList *info_list = qmp_query_memory_devices(&err);
    MemoryDeviceInfoList *info;
    VirtioPMEMDeviceInfo *vpi;
    VirtioMEMDeviceInfo *vmi;
    MemoryDeviceInfo *value;
    PCDIMMDeviceInfo *di;
    SgxEPCDeviceInfo *se;
    HvBalloonDeviceInfo *hi;

    for (info = info_list; info; info = info->next) {
        value = info->value;

        if (value) {
            switch (value->type) {
            case MEMORY_DEVICE_INFO_KIND_DIMM:
            case MEMORY_DEVICE_INFO_KIND_NVDIMM:
                di = value->type == MEMORY_DEVICE_INFO_KIND_DIMM ?
                     value->u.dimm.data : value->u.nvdimm.data;
                monitor_printf(mon, "Memory device [%s]: \"%s\"\n",
                               MemoryDeviceInfoKind_str(value->type),
                               di->id ? di->id : "");
                monitor_printf(mon, "  addr: 0x%" PRIx64 "\n", di->addr);
                monitor_printf(mon, "  slot: %" PRId64 "\n", di->slot);
                monitor_printf(mon, "  node: %" PRId64 "\n", di->node);
                monitor_printf(mon, "  size: %" PRIu64 "\n", di->size);
                monitor_printf(mon, "  memdev: %s\n", di->memdev);
                monitor_printf(mon, "  hotplugged: %s\n",
                               di->hotplugged ? "true" : "false");
                monitor_printf(mon, "  hotpluggable: %s\n",
                               di->hotpluggable ? "true" : "false");
                break;
            case MEMORY_DEVICE_INFO_KIND_VIRTIO_PMEM:
                vpi = value->u.virtio_pmem.data;
                monitor_printf(mon, "Memory device [%s]: \"%s\"\n",
                               MemoryDeviceInfoKind_str(value->type),
                               vpi->id ? vpi->id : "");
                monitor_printf(mon, "  memaddr: 0x%" PRIx64 "\n", vpi->memaddr);
                monitor_printf(mon, "  size: %" PRIu64 "\n", vpi->size);
                monitor_printf(mon, "  memdev: %s\n", vpi->memdev);
                break;
            case MEMORY_DEVICE_INFO_KIND_VIRTIO_MEM:
                vmi = value->u.virtio_mem.data;
                monitor_printf(mon, "Memory device [%s]: \"%s\"\n",
                               MemoryDeviceInfoKind_str(value->type),
                               vmi->id ? vmi->id : "");
                monitor_printf(mon, "  memaddr: 0x%" PRIx64 "\n", vmi->memaddr);
                monitor_printf(mon, "  node: %" PRId64 "\n", vmi->node);
                monitor_printf(mon, "  requested-size: %" PRIu64 "\n",
                               vmi->requested_size);
                monitor_printf(mon, "  size: %" PRIu64 "\n", vmi->size);
                monitor_printf(mon, "  max-size: %" PRIu64 "\n", vmi->max_size);
                monitor_printf(mon, "  block-size: %" PRIu64 "\n",
                               vmi->block_size);
                monitor_printf(mon, "  memdev: %s\n", vmi->memdev);
                break;
            case MEMORY_DEVICE_INFO_KIND_SGX_EPC:
                se = value->u.sgx_epc.data;
                monitor_printf(mon, "Memory device [%s]: \"%s\"\n",
                               MemoryDeviceInfoKind_str(value->type),
                               se->id ? se->id : "");
                monitor_printf(mon, "  memaddr: 0x%" PRIx64 "\n", se->memaddr);
                monitor_printf(mon, "  size: %" PRIu64 "\n", se->size);
                monitor_printf(mon, "  node: %" PRId64 "\n", se->node);
                monitor_printf(mon, "  memdev: %s\n", se->memdev);
                break;
            case MEMORY_DEVICE_INFO_KIND_HV_BALLOON:
                hi = value->u.hv_balloon.data;
                monitor_printf(mon, "Memory device [%s]: \"%s\"\n",
                               MemoryDeviceInfoKind_str(value->type),
                               hi->id ? hi->id : "");
                if (hi->has_memaddr) {
                    monitor_printf(mon, "  memaddr: 0x%" PRIx64 "\n",
                                   hi->memaddr);
                }
                monitor_printf(mon, "  max-size: %" PRIu64 "\n", hi->max_size);
                if (hi->memdev) {
                    monitor_printf(mon, "  memdev: %s\n", hi->memdev);
                }
                break;
            default:
                g_assert_not_reached();
            }
        }
    }

    qapi_free_MemoryDeviceInfoList(info_list);
    hmp_handle_error(mon, err);
}

void hmp_info_vm_generation_id(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    GuidInfo *info = qmp_query_vm_generation_id(&err);
    if (info) {
        monitor_printf(mon, "%s\n", info->guid);
    }
    hmp_handle_error(mon, err);
    qapi_free_GuidInfo(info);
}

void hmp_info_memory_size_summary(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    MemoryInfo *info = qmp_query_memory_size_summary(&err);
    if (info) {
        monitor_printf(mon, "base memory: %" PRIu64 "\n",
                       info->base_memory);

        if (info->has_plugged_memory) {
            monitor_printf(mon, "plugged memory: %" PRIu64 "\n",
                           info->plugged_memory);
        }

        qapi_free_MemoryInfo(info);
    }
    hmp_handle_error(mon, err);
}
