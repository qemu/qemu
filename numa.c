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
#include "exec/ramlist.h"
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
bool have_numa_distance;
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

static void parse_numa_node(MachineState *ms, NumaNodeOptions *node,
                            QemuOpts *opts, Error **errp)
{
    uint16_t nodenr;
    uint16List *cpus = NULL;
    MachineClass *mc = MACHINE_GET_CLASS(ms);

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

    if (!mc->cpu_index_to_instance_props) {
        error_report("NUMA is not supported by this machine-type");
        exit(1);
    }
    for (cpus = node->cpus; cpus; cpus = cpus->next) {
        CpuInstanceProperties props;
        if (cpus->value >= max_cpus) {
            error_setg(errp,
                       "CPU index (%" PRIu16 ")"
                       " should be smaller than maxcpus (%d)",
                       cpus->value, max_cpus);
            return;
        }
        props = mc->cpu_index_to_instance_props(ms, cpus->value);
        props.node_id = nodenr;
        props.has_node_id = true;
        machine_set_cpu_numa_node(ms, &props, &error_fatal);
    }

    if (node->has_mem && node->has_memdev) {
        error_setg(errp, "cannot specify both mem= and memdev=");
        return;
    }

    if (have_memdevs == -1) {
        have_memdevs = node->has_memdev;
    }
    if (node->has_memdev != have_memdevs) {
        error_setg(errp, "memdev option must be specified for either "
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
        numa_info[nodenr].node_mem = object_property_get_uint(o, "size", NULL);
        numa_info[nodenr].node_memdev = MEMORY_BACKEND(o);
    }
    numa_info[nodenr].present = true;
    max_numa_nodeid = MAX(max_numa_nodeid, nodenr + 1);
}

static void parse_numa_distance(NumaDistOptions *dist, Error **errp)
{
    uint16_t src = dist->src;
    uint16_t dst = dist->dst;
    uint8_t val = dist->val;

    if (src >= MAX_NODES || dst >= MAX_NODES) {
        error_setg(errp,
                   "Invalid node %d, max possible could be %d",
                   MAX(src, dst), MAX_NODES);
        return;
    }

    if (!numa_info[src].present || !numa_info[dst].present) {
        error_setg(errp, "Source/Destination NUMA node is missing. "
                   "Please use '-numa node' option to declare it first.");
        return;
    }

    if (val < NUMA_DISTANCE_MIN) {
        error_setg(errp, "NUMA distance (%" PRIu8 ") is invalid, "
                   "it shouldn't be less than %d.",
                   val, NUMA_DISTANCE_MIN);
        return;
    }

    if (src == dst && val != NUMA_DISTANCE_MIN) {
        error_setg(errp, "Local distance of node %d should be %d.",
                   src, NUMA_DISTANCE_MIN);
        return;
    }

    numa_info[src].distance[dst] = val;
    have_numa_distance = true;
}

static int parse_numa(void *opaque, QemuOpts *opts, Error **errp)
{
    NumaOptions *object = NULL;
    MachineState *ms = opaque;
    Error *err = NULL;

    {
        Visitor *v = opts_visitor_new(opts);
        visit_type_NumaOptions(v, NULL, &object, &err);
        visit_free(v);
    }

    if (err) {
        goto end;
    }

    switch (object->type) {
    case NUMA_OPTIONS_TYPE_NODE:
        parse_numa_node(ms, &object->u.node, opts, &err);
        if (err) {
            goto end;
        }
        nb_numa_nodes++;
        break;
    case NUMA_OPTIONS_TYPE_DIST:
        parse_numa_distance(&object->u.dist, &err);
        if (err) {
            goto end;
        }
        break;
    case NUMA_OPTIONS_TYPE_CPU:
        if (!object->u.cpu.has_node_id) {
            error_setg(&err, "Missing mandatory node-id property");
            goto end;
        }
        if (!numa_info[object->u.cpu.node_id].present) {
            error_setg(&err, "Invalid node-id=%" PRId64 ", NUMA node must be "
                "defined with -numa node,nodeid=ID before it's used with "
                "-numa cpu,node-id=ID", object->u.cpu.node_id);
            goto end;
        }

        machine_set_cpu_numa_node(ms, qapi_NumaCpuOptions_base(&object->u.cpu),
                                  &err);
        break;
    default:
        abort();
    }

end:
    qapi_free_NumaOptions(object);
    if (err) {
        error_report_err(err);
        return -1;
    }

    return 0;
}

/* If all node pair distances are symmetric, then only distances
 * in one direction are enough. If there is even one asymmetric
 * pair, though, then all distances must be provided. The
 * distance from a node to itself is always NUMA_DISTANCE_MIN,
 * so providing it is never necessary.
 */
static void validate_numa_distance(void)
{
    int src, dst;
    bool is_asymmetrical = false;

    for (src = 0; src < nb_numa_nodes; src++) {
        for (dst = src; dst < nb_numa_nodes; dst++) {
            if (numa_info[src].distance[dst] == 0 &&
                numa_info[dst].distance[src] == 0) {
                if (src != dst) {
                    error_report("The distance between node %d and %d is "
                                 "missing, at least one distance value "
                                 "between each nodes should be provided.",
                                 src, dst);
                    exit(EXIT_FAILURE);
                }
            }

            if (numa_info[src].distance[dst] != 0 &&
                numa_info[dst].distance[src] != 0 &&
                numa_info[src].distance[dst] !=
                numa_info[dst].distance[src]) {
                is_asymmetrical = true;
            }
        }
    }

    if (is_asymmetrical) {
        for (src = 0; src < nb_numa_nodes; src++) {
            for (dst = 0; dst < nb_numa_nodes; dst++) {
                if (src != dst && numa_info[src].distance[dst] == 0) {
                    error_report("At least one asymmetrical pair of "
                            "distances is given, please provide distances "
                            "for both directions of all node pairs.");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

static void complete_init_numa_distance(void)
{
    int src, dst;

    /* Fixup NUMA distance by symmetric policy because if it is an
     * asymmetric distance table, it should be a complete table and
     * there would not be any missing distance except local node, which
     * is verified by validate_numa_distance above.
     */
    for (src = 0; src < nb_numa_nodes; src++) {
        for (dst = 0; dst < nb_numa_nodes; dst++) {
            if (numa_info[src].distance[dst] == 0) {
                if (src == dst) {
                    numa_info[src].distance[dst] = NUMA_DISTANCE_MIN;
                } else {
                    numa_info[src].distance[dst] = numa_info[dst].distance[src];
                }
            }
        }
    }
}

void numa_legacy_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                 int nb_nodes, ram_addr_t size)
{
    int i;
    uint64_t usedmem = 0;

    /* Align each node according to the alignment
     * requirements of the machine class
     */

    for (i = 0; i < nb_nodes - 1; i++) {
        nodes[i].node_mem = (size / nb_nodes) &
                            ~((1 << mc->numa_mem_align_shift) - 1);
        usedmem += nodes[i].node_mem;
    }
    nodes[i].node_mem = size - usedmem;
}

void numa_default_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                  int nb_nodes, ram_addr_t size)
{
    int i;
    uint64_t usedmem = 0, node_mem;
    uint64_t granularity = size / nb_nodes;
    uint64_t propagate = 0;

    for (i = 0; i < nb_nodes - 1; i++) {
        node_mem = (granularity + propagate) &
                   ~((1 << mc->numa_mem_align_shift) - 1);
        propagate = granularity + propagate - node_mem;
        nodes[i].node_mem = node_mem;
        usedmem += node_mem;
    }
    nodes[i].node_mem = size - usedmem;
}

void parse_numa_opts(MachineState *ms)
{
    int i;
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    if (qemu_opts_foreach(qemu_find_opts("numa"), parse_numa, ms, NULL)) {
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
            assert(mc->numa_auto_assign_ram);
            mc->numa_auto_assign_ram(mc, numa_info, nb_numa_nodes, ram_size);
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

        /* QEMU needs at least all unique node pair distances to build
         * the whole NUMA distance table. QEMU treats the distance table
         * as symmetric by default, i.e. distance A->B == distance B->A.
         * Thus, QEMU is able to complete the distance table
         * initialization even though only distance A->B is provided and
         * distance B->A is not. QEMU knows the distance of a node to
         * itself is always 10, so A->A distances may be omitted. When
         * the distances of two nodes of a pair differ, i.e. distance
         * A->B != distance B->A, then that means the distance table is
         * asymmetric. In this case, the distances for both directions
         * of all node pairs are required.
         */
        if (have_numa_distance) {
            /* Validate enough NUMA distance information was provided. */
            validate_numa_distance();

            /* Validation succeeded, now fill in any missing distances. */
            complete_init_numa_distance();
        }
    } else {
        numa_set_mem_node_id(0, ram_size, 0);
    }
}

void numa_cpu_pre_plug(const CPUArchId *slot, DeviceState *dev, Error **errp)
{
    int node_id = object_property_get_int(OBJECT(dev), "node-id", &error_abort);

    if (node_id == CPU_UNSET_NUMA_NODE_ID) {
        /* due to bug in libvirt, it doesn't pass node-id from props on
         * device_add as expected, so we have to fix it up here */
        if (slot->props.has_node_id) {
            object_property_set_int(OBJECT(dev), slot->props.node_id,
                                    "node-id", errp);
        }
    } else if (node_id != slot->props.node_id) {
        error_setg(errp, "node-id=%d must match numa node specified "
                   "with -numa option", node_id);
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
            memory_region_init_ram_nomigrate(mr, owner, name, ram_size, &error_fatal);
        }
#else
        fprintf(stderr, "-mem-path not supported on this host\n");
        exit(1);
#endif
    } else {
        memory_region_init_ram_nomigrate(mr, owner, name, ram_size, &error_fatal);
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

        host_memory_backend_set_mapped(backend, true);
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
                node_mem[value->u.dimm.data->node] += value->u.dimm.data->size;
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

        m->value->id = object_property_get_str(obj, "id", NULL);
        m->value->has_id = !!m->value->id;

        m->value->size = object_property_get_uint(obj, "size",
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

void ram_block_notifier_add(RAMBlockNotifier *n)
{
    QLIST_INSERT_HEAD(&ram_list.ramblock_notifiers, n, next);
}

void ram_block_notifier_remove(RAMBlockNotifier *n)
{
    QLIST_REMOVE(n, next);
}

void ram_block_notify_add(void *host, size_t size)
{
    RAMBlockNotifier *notifier;

    QLIST_FOREACH(notifier, &ram_list.ramblock_notifiers, next) {
        notifier->ram_block_added(notifier, host, size);
    }
}

void ram_block_notify_remove(void *host, size_t size)
{
    RAMBlockNotifier *notifier;

    QLIST_FOREACH(notifier, &ram_list.ramblock_notifiers, next) {
        notifier->ram_block_removed(notifier, host, size);
    }
}
