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

        if (cpu->value->cpu_index == monitor_get_cpu_index()) {
            active = '*';
        }

        monitor_printf(mon, "%c CPU #%" PRId64 ":", active,
                       cpu->value->cpu_index);
        monitor_printf(mon, " thread_id=%" PRId64 "\n", cpu->value->thread_id);
    }

    qapi_free_CpuInfoFastList(cpu_list);
}

void hmp_cpu_add(Monitor *mon, const QDict *qdict)
{
    int cpuid;
    Error *err = NULL;

    error_report("cpu_add is deprecated, please use device_add instead");

    cpuid = qdict_get_int(qdict, "id");
    qmp_cpu_add(cpuid, &err);
    hmp_handle_error(mon, err);
}

void hmp_hotpluggable_cpus(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    HotpluggableCPUList *l = qmp_query_hotpluggable_cpus(&err);
    HotpluggableCPUList *saved = l;
    CpuInstanceProperties *c;

    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "Hotpluggable CPUs:\n");
    while (l) {
        monitor_printf(mon, "  type: \"%s\"\n", l->value->type);
        monitor_printf(mon, "  vcpus_count: \"%" PRIu64 "\"\n",
                       l->value->vcpus_count);
        if (l->value->has_qom_path) {
            monitor_printf(mon, "  qom_path: \"%s\"\n", l->value->qom_path);
        }

        c = l->value->props;
        monitor_printf(mon, "  CPUInstance Properties:\n");
        if (c->has_node_id) {
            monitor_printf(mon, "    node-id: \"%" PRIu64 "\"\n", c->node_id);
        }
        if (c->has_socket_id) {
            monitor_printf(mon, "    socket-id: \"%" PRIu64 "\"\n", c->socket_id);
        }
        if (c->has_die_id) {
            monitor_printf(mon, "    die-id: \"%" PRIu64 "\"\n", c->die_id);
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

void hmp_info_numa(Monitor *mon, const QDict *qdict)
{
    int i, nb_numa_nodes;
    NumaNodeMem *node_mem;
    CpuInfoList *cpu_list, *cpu;
    MachineState *ms = MACHINE(qdev_get_machine());

    nb_numa_nodes = ms->numa_state ? ms->numa_state->num_nodes : 0;
    monitor_printf(mon, "%d nodes\n", nb_numa_nodes);
    if (!nb_numa_nodes) {
        return;
    }

    cpu_list = qmp_query_cpus(&error_abort);
    node_mem = g_new0(NumaNodeMem, nb_numa_nodes);

    query_numa_node_mem(node_mem, ms);
    for (i = 0; i < nb_numa_nodes; i++) {
        monitor_printf(mon, "node %d cpus:", i);
        for (cpu = cpu_list; cpu; cpu = cpu->next) {
            if (cpu->value->has_props && cpu->value->props->has_node_id &&
                cpu->value->props->node_id == i) {
                monitor_printf(mon, " %" PRIi64, cpu->value->CPU);
            }
        }
        monitor_printf(mon, "\n");
        monitor_printf(mon, "node %d size: %" PRId64 " MB\n", i,
                       node_mem[i].node_mem >> 20);
        monitor_printf(mon, "node %d plugged: %" PRId64 " MB\n", i,
                       node_mem[i].node_plugged_mem >> 20);
    }
    qapi_free_CpuInfoList(cpu_list);
    g_free(node_mem);
}
