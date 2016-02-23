/*
 * NUMA parameter parsing routines
 *
 * Copyright (c) 2014 Fujitsu Ltd.
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
#include "sysemu/numa.h"
#include "exec/cpu-common.h"
#include "qemu/bitmap.h"
#include "qom/cpu.h"
#include "qemu/error-report.h"
#include "include/exec/cpu-common.h" /* for RAM_ADDR_FMT */
#include "qapi-visit.h"
#include "qapi/opts-visitor.h"
#include "hw/boards.h"
#include "sysemu/hostmem.h"
#include "qmp-commands.h"
#include "hw/mem/pc-dimm.h"
#include "qemu/option.h"
#include "qemu/config-file.h"

QemuOptsList qemu_numa_opts = {
    .name = "numa",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_numa_opts.head),
    .desc = { { 0 } } /* validated with OptsVisitor */
};

static int have_memdevs = -1;
static int max_numa_nodeid; /* Highest specified NUMA node ID, plus one.
                             * For all nodes, nodeid < max_numa_nodeid
                             */
int nb_numa_nodes;
NodeInfo numa_info[MAX_NODES];

void numa_set_mem_node_id(ram_addr_t addr, uint64_t size, uint32_t node)
{
    struct numa_addr_range *range;

    /*
     * Memory-less nodes can come here with 0 size in which case,
     * there is nothing to do.
     */
    if (!size) {
        return;
    }

    range = g_malloc0(sizeof(*range));
    range->mem_start = addr;
    range->mem_end = addr + size - 1;
    QLIST_INSERT_HEAD(&numa_info[node].addr, range, entry);
}

void numa_unset_mem_node_id(ram_addr_t addr, uint64_t size, uint32_t node)
{
    struct numa_addr_range *range, *next;

    QLIST_FOREACH_SAFE(range, &numa_info[node].addr, entry, next) {
        if (addr == range->mem_start && (addr + size - 1) == range->mem_end) {
            QLIST_REMOVE(range, entry);
            g_free(range);
            return;
        }
    }
}

static void numa_set_mem_ranges(void)
{
    int i;
    ram_addr_t mem_start = 0;

    /*
     * Deduce start address of each node and use it to store
     * the address range info in numa_info address range list
     */
    for (i = 0; i < nb_numa_nodes; i++) {
        numa_set_mem_node_id(mem_start, numa_info[i].node_mem, i);
        mem_start += numa_info[i].node_mem;
    }
}

/*
 * Check if @addr falls under NUMA @node.
 */
static bool numa_addr_belongs_to_node(ram_addr_t addr, uint32_t node)
{
    struct numa_addr_range *range;

    QLIST_FOREACH(range, &numa_info[node].addr, entry) {
        if (addr >= range->mem_start && addr <= range->mem_end) {
            return true;
        }
    }
    return false;
}

/*
 * Given an address, return the index of the NUMA node to which the
 * address belongs to.
 */
uint32_t numa_get_node(ram_addr_t addr, Error **errp)
{
    uint32_t i;

    /* For non NUMA configurations, check if the addr falls under node 0 */
    if (!nb_numa_nodes) {
        if (numa_addr_belongs_to_node(addr, 0)) {
            return 0;
        }
    }

    for (i = 0; i < nb_numa_nodes; i++) {
        if (numa_addr_belongs_to_node(addr, i)) {
            return i;
        }
    }

    error_setg(errp, "Address 0x" RAM_ADDR_FMT " doesn't belong to any "
                "NUMA node", addr);
    return -1;
}

static void numa_node_parse(NumaNodeOptions *node, QemuOpts *opts, Error **errp)
{
    uint16_t nodenr;
    uint16List *cpus = NULL;

    if (node->has_nodeid) {
        nodenr = node->nodeid;
    } else {
        nodenr = nb_numa_nodes;
    }

    if (nodenr >= MAX_NODES) {
        error_setg(errp, "Max number of NUMA nodes reached: %"
                   PRIu16 "", nodenr);
        return;
    }

    if (numa_info[nodenr].present) {
        error_setg(errp, "Duplicate NUMA nodeid: %" PRIu16, nodenr);
        return;
    }

    for (cpus = node->cpus; cpus; cpus = cpus->next) {
        if (cpus->value >= max_cpus) {
            error_setg(errp,
                       "CPU index (%" PRIu16 ")"
                       " should be smaller than maxcpus (%d)",
                       cpus->value, max_cpus);
            return;
        }
        bitmap_set(numa_info[nodenr].node_cpu, cpus->value, 1);
    }

    if (node->has_mem && node->has_memdev) {
        error_setg(errp, "qemu: cannot specify both mem= and memdev=");
        return;
    }

    if (have_memdevs == -1) {
        have_memdevs = node->has_memdev;
    }
    if (node->has_memdev != have_memdevs) {
        error_setg(errp, "qemu: memdev option must be specified for either "
                   "all or no nodes");
        return;
    }

    if (node->has_mem) {
        uint64_t mem_size = node->mem;
        const char *mem_str = qemu_opt_get(opts, "mem");
        /* Fix up legacy suffix-less format */
        if (g_ascii_isdigit(mem_str[strlen(mem_str) - 1])) {
            mem_size <<= 20;
        }
        numa_info[nodenr].node_mem = mem_size;
    }
    if (node->has_memdev) {
        Object *o;
        o = object_resolve_path_type(node->memdev, TYPE_MEMORY_BACKEND, NULL);
        if (!o) {
            error_setg(errp, "memdev=%s is ambiguous", node->memdev);
            return;
        }

        object_ref(o);
        numa_info[nodenr].node_mem = object_property_get_int(o, "size", NULL);
        numa_info[nodenr].node_memdev = MEMORY_BACKEND(o);
    }
    numa_info[nodenr].present = true;
    max_numa_nodeid = MAX(max_numa_nodeid, nodenr + 1);
}

static int parse_numa(void *opaque, QemuOpts *opts, Error **errp)
{
    NumaOptions *object = NULL;
    Error *err = NULL;

    {
        OptsVisitor *ov = opts_visitor_new(opts);
        visit_type_NumaOptions(opts_get_visitor(ov), NULL, &object, &err);
        opts_visitor_cleanup(ov);
    }

    if (err) {
        goto error;
    }

    switch (object->type) {
    case NUMA_OPTIONS_KIND_NODE:
        numa_node_parse(object->u.node, opts, &err);
        if (err) {
            goto error;
        }
        nb_numa_nodes++;
        break;
    default:
        abort();
    }

    return 0;

error:
    error_report_err(err);
    qapi_free_NumaOptions(object);

    return -1;
}

static char *enumerate_cpus(unsigned long *cpus, int max_cpus)
{
    int cpu;
    bool first = true;
    GString *s = g_string_new(NULL);

    for (cpu = find_first_bit(cpus, max_cpus);
        cpu < max_cpus;
        cpu = find_next_bit(cpus, max_cpus, cpu + 1)) {
        g_string_append_printf(s, "%s%d", first ? "" : " ", cpu);
        first = false;
    }
    return g_string_free(s, FALSE);
}

static void validate_numa_cpus(void)
{
    int i;
    DECLARE_BITMAP(seen_cpus, MAX_CPUMASK_BITS);

    bitmap_zero(seen_cpus, MAX_CPUMASK_BITS);
    for (i = 0; i < nb_numa_nodes; i++) {
        if (bitmap_intersects(seen_cpus, numa_info[i].node_cpu,
                              MAX_CPUMASK_BITS)) {
            bitmap_and(seen_cpus, seen_cpus,
                       numa_info[i].node_cpu, MAX_CPUMASK_BITS);
            error_report("CPU(s) present in multiple NUMA nodes: %s",
                         enumerate_cpus(seen_cpus, max_cpus));
            exit(EXIT_FAILURE);
        }
        bitmap_or(seen_cpus, seen_cpus,
                  numa_info[i].node_cpu, MAX_CPUMASK_BITS);
    }

    if (!bitmap_full(seen_cpus, max_cpus)) {
        char *msg;
        bitmap_complement(seen_cpus, seen_cpus, max_cpus);
        msg = enumerate_cpus(seen_cpus, max_cpus);
        error_report("warning: CPU(s) not present in any NUMA nodes: %s", msg);
        error_report("warning: All CPU(s) up to maxcpus should be described "
                     "in NUMA config");
        g_free(msg);
    }
}

void parse_numa_opts(MachineClass *mc)
{
    int i;

    if (qemu_opts_foreach(qemu_find_opts("numa"), parse_numa, NULL, NULL)) {
        exit(1);
    }

    assert(max_numa_nodeid <= MAX_NODES);

    /* No support for sparse NUMA node IDs yet: */
    for (i = max_numa_nodeid - 1; i >= 0; i--) {
        /* Report large node IDs first, to make mistakes easier to spot */
        if (!numa_info[i].present) {
            error_report("numa: Node ID missing: %d", i);
            exit(1);
        }
    }

    /* This must be always true if all nodes are present: */
    assert(nb_numa_nodes == max_numa_nodeid);

    if (nb_numa_nodes > 0) {
        uint64_t numa_total;

        if (nb_numa_nodes > MAX_NODES) {
            nb_numa_nodes = MAX_NODES;
        }

        /* If no memory size is given for any node, assume the default case
         * and distribute the available memory equally across all nodes
         */
        for (i = 0; i < nb_numa_nodes; i++) {
            if (numa_info[i].node_mem != 0) {
                break;
            }
        }
        if (i == nb_numa_nodes) {
            uint64_t usedmem = 0;

            /* On Linux, each node's border has to be 8MB aligned,
             * the final node gets the rest.
             */
            for (i = 0; i < nb_numa_nodes - 1; i++) {
                numa_info[i].node_mem = (ram_size / nb_numa_nodes) &
                                        ~((1 << 23UL) - 1);
                usedmem += numa_info[i].node_mem;
            }
            numa_info[i].node_mem = ram_size - usedmem;
        }

        numa_total = 0;
        for (i = 0; i < nb_numa_nodes; i++) {
            numa_total += numa_info[i].node_mem;
        }
        if (numa_total != ram_size) {
            error_report("total memory for NUMA nodes (0x%" PRIx64 ")"
                         " should equal RAM size (0x" RAM_ADDR_FMT ")",
                         numa_total, ram_size);
            exit(1);
        }

        for (i = 0; i < nb_numa_nodes; i++) {
            QLIST_INIT(&numa_info[i].addr);
        }

        numa_set_mem_ranges();

        for (i = 0; i < nb_numa_nodes; i++) {
            if (!bitmap_empty(numa_info[i].node_cpu, MAX_CPUMASK_BITS)) {
                break;
            }
        }
        /* Historically VCPUs were assigned in round-robin order to NUMA
         * nodes. However it causes issues with guest not handling it nice
         * in case where cores/threads from a multicore CPU appear on
         * different nodes. So allow boards to override default distribution
         * rule grouping VCPUs by socket so that VCPUs from the same socket
         * would be on the same node.
         */
        if (i == nb_numa_nodes) {
            for (i = 0; i < max_cpus; i++) {
                unsigned node_id = i % nb_numa_nodes;
                if (mc->cpu_index_to_socket_id) {
                    node_id = mc->cpu_index_to_socket_id(i) % nb_numa_nodes;
                }

                set_bit(i, numa_info[node_id].node_cpu);
            }
        }

        validate_numa_cpus();
    } else {
        numa_set_mem_node_id(0, ram_size, 0);
    }
}

void numa_post_machine_init(void)
{
    CPUState *cpu;
    int i;

    CPU_FOREACH(cpu) {
        for (i = 0; i < nb_numa_nodes; i++) {
            if (test_bit(cpu->cpu_index, numa_info[i].node_cpu)) {
                cpu->numa_node = i;
            }
        }
    }
}

static void allocate_system_memory_nonnuma(MemoryRegion *mr, Object *owner,
                                           const char *name,
                                           uint64_t ram_size)
{
    if (mem_path) {
#ifdef __linux__
        Error *err = NULL;
        memory_region_init_ram_from_file(mr, owner, name, ram_size, false,
                                         mem_path, &err);
        if (err) {
            error_report_err(err);
            if (mem_prealloc) {
                exit(1);
            }

            /* Legacy behavior: if allocation failed, fall back to
             * regular RAM allocation.
             */
            memory_region_init_ram(mr, owner, name, ram_size, &error_fatal);
        }
#else
        fprintf(stderr, "-mem-path not supported on this host\n");
        exit(1);
#endif
    } else {
        memory_region_init_ram(mr, owner, name, ram_size, &error_fatal);
    }
    vmstate_register_ram_global(mr);
}

void memory_region_allocate_system_memory(MemoryRegion *mr, Object *owner,
                                          const char *name,
                                          uint64_t ram_size)
{
    uint64_t addr = 0;
    int i;

    if (nb_numa_nodes == 0 || !have_memdevs) {
        allocate_system_memory_nonnuma(mr, owner, name, ram_size);
        return;
    }

    memory_region_init(mr, owner, name, ram_size);
    for (i = 0; i < MAX_NODES; i++) {
        uint64_t size = numa_info[i].node_mem;
        HostMemoryBackend *backend = numa_info[i].node_memdev;
        if (!backend) {
            continue;
        }
        MemoryRegion *seg = host_memory_backend_get_memory(backend,
                                                           &error_fatal);

        if (memory_region_is_mapped(seg)) {
            char *path = object_get_canonical_path_component(OBJECT(backend));
            error_report("memory backend %s is used multiple times. Each "
                         "-numa option must use a different memdev value.",
                         path);
            exit(1);
        }

        memory_region_add_subregion(mr, addr, seg);
        vmstate_register_ram_global(seg);
        addr += size;
    }
}

static void numa_stat_memory_devices(uint64_t node_mem[])
{
    MemoryDeviceInfoList *info_list = NULL;
    MemoryDeviceInfoList **prev = &info_list;
    MemoryDeviceInfoList *info;

    qmp_pc_dimm_device_list(qdev_get_machine(), &prev);
    for (info = info_list; info; info = info->next) {
        MemoryDeviceInfo *value = info->value;

        if (value) {
            switch (value->type) {
            case MEMORY_DEVICE_INFO_KIND_DIMM:
                node_mem[value->u.dimm->node] += value->u.dimm->size;
                break;
            default:
                break;
            }
        }
    }
    qapi_free_MemoryDeviceInfoList(info_list);
}

void query_numa_node_mem(uint64_t node_mem[])
{
    int i;

    if (nb_numa_nodes <= 0) {
        return;
    }

    numa_stat_memory_devices(node_mem);
    for (i = 0; i < nb_numa_nodes; i++) {
        node_mem[i] += numa_info[i].node_mem;
    }
}

static int query_memdev(Object *obj, void *opaque)
{
    MemdevList **list = opaque;
    MemdevList *m = NULL;

    if (object_dynamic_cast(obj, TYPE_MEMORY_BACKEND)) {
        m = g_malloc0(sizeof(*m));

        m->value = g_malloc0(sizeof(*m->value));

        m->value->size = object_property_get_int(obj, "size",
                                                 &error_abort);
        m->value->merge = object_property_get_bool(obj, "merge",
                                                   &error_abort);
        m->value->dump = object_property_get_bool(obj, "dump",
                                                  &error_abort);
        m->value->prealloc = object_property_get_bool(obj,
                                                      "prealloc",
                                                      &error_abort);
        m->value->policy = object_property_get_enum(obj,
                                                    "policy",
                                                    "HostMemPolicy",
                                                    &error_abort);
        object_property_get_uint16List(obj, "host-nodes",
                                       &m->value->host_nodes,
                                       &error_abort);

        m->next = *list;
        *list = m;
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
