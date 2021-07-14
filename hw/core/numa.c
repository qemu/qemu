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
#include "qemu/units.h"
#include "sysemu/hostmem.h"
#include "sysemu/numa.h"
#include "exec/cpu-common.h"
#include "exec/ramlist.h"
#include "qemu/bitmap.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/opts-visitor.h"
#include "qapi/qapi-visit-machine.h"
#include "sysemu/qtest.h"
#include "hw/core/cpu.h"
#include "hw/mem/pc-dimm.h"
#include "migration/vmstate.h"
#include "hw/boards.h"
#include "hw/mem/memory-device.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"

QemuOptsList qemu_numa_opts = {
    .name = "numa",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_numa_opts.head),
    .desc = { { 0 } } /* validated with OptsVisitor */
};

static int have_memdevs;
bool numa_uses_legacy_mem(void)
{
    return !have_memdevs;
}

static int have_mem;
static int max_numa_nodeid; /* Highest specified NUMA node ID, plus one.
                             * For all nodes, nodeid < max_numa_nodeid
                             */

static void parse_numa_node(MachineState *ms, NumaNodeOptions *node,
                            Error **errp)
{
    Error *err = NULL;
    uint16_t nodenr;
    uint16List *cpus = NULL;
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    unsigned int max_cpus = ms->smp.max_cpus;
    NodeInfo *numa_info = ms->numa_state->nodes;

    if (node->has_nodeid) {
        nodenr = node->nodeid;
    } else {
        nodenr = ms->numa_state->num_nodes;
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

    /*
     * If not set the initiator, set it to MAX_NODES. And if
     * HMAT is enabled and this node has no cpus, QEMU will raise error.
     */
    numa_info[nodenr].initiator = MAX_NODES;
    if (node->has_initiator) {
        if (!ms->numa_state->hmat_enabled) {
            error_setg(errp, "ACPI Heterogeneous Memory Attribute Table "
                       "(HMAT) is disabled, enable it with -machine hmat=on "
                       "before using any of hmat specific options");
            return;
        }

        if (node->initiator >= MAX_NODES) {
            error_report("The initiator id %" PRIu16 " expects an integer "
                         "between 0 and %d", node->initiator,
                         MAX_NODES - 1);
            return;
        }

        numa_info[nodenr].initiator = node->initiator;
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
        machine_set_cpu_numa_node(ms, &props, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    have_memdevs = have_memdevs ? : node->has_memdev;
    have_mem = have_mem ? : node->has_mem;
    if ((node->has_mem && have_memdevs) || (node->has_memdev && have_mem)) {
        error_setg(errp, "numa configuration should use either mem= or memdev=,"
                   "mixing both is not allowed");
        return;
    }

    if (node->has_mem) {
        if (!mc->numa_mem_supported) {
            error_setg(errp, "Parameter -numa node,mem is not supported by this"
                      " machine type");
            error_append_hint(errp, "Use -numa node,memdev instead\n");
            return;
        }

        numa_info[nodenr].node_mem = node->mem;
        if (!qtest_enabled()) {
            warn_report("Parameter -numa node,mem is deprecated,"
                        " use -numa node,memdev instead");
        }
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
    ms->numa_state->num_nodes++;
}

static
void parse_numa_distance(MachineState *ms, NumaDistOptions *dist, Error **errp)
{
    uint16_t src = dist->src;
    uint16_t dst = dist->dst;
    uint8_t val = dist->val;
    NodeInfo *numa_info = ms->numa_state->nodes;

    if (src >= MAX_NODES || dst >= MAX_NODES) {
        error_setg(errp, "Parameter '%s' expects an integer between 0 and %d",
                   src >= MAX_NODES ? "src" : "dst", MAX_NODES - 1);
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
    ms->numa_state->have_numa_distance = true;
}

void parse_numa_hmat_lb(NumaState *numa_state, NumaHmatLBOptions *node,
                        Error **errp)
{
    int i, first_bit, last_bit;
    uint64_t max_entry, temp_base, bitmap_copy;
    NodeInfo *numa_info = numa_state->nodes;
    HMAT_LB_Info *hmat_lb =
        numa_state->hmat_lb[node->hierarchy][node->data_type];
    HMAT_LB_Data lb_data = {};
    HMAT_LB_Data *lb_temp;

    /* Error checking */
    if (node->initiator > numa_state->num_nodes) {
        error_setg(errp, "Invalid initiator=%d, it should be less than %d",
                   node->initiator, numa_state->num_nodes);
        return;
    }
    if (node->target > numa_state->num_nodes) {
        error_setg(errp, "Invalid target=%d, it should be less than %d",
                   node->target, numa_state->num_nodes);
        return;
    }
    if (!numa_info[node->initiator].has_cpu) {
        error_setg(errp, "Invalid initiator=%d, it isn't an "
                   "initiator proximity domain", node->initiator);
        return;
    }
    if (!numa_info[node->target].present) {
        error_setg(errp, "The target=%d should point to an existing node",
                   node->target);
        return;
    }

    if (!hmat_lb) {
        hmat_lb = g_malloc0(sizeof(*hmat_lb));
        numa_state->hmat_lb[node->hierarchy][node->data_type] = hmat_lb;
        hmat_lb->list = g_array_new(false, true, sizeof(HMAT_LB_Data));
    }
    hmat_lb->hierarchy = node->hierarchy;
    hmat_lb->data_type = node->data_type;
    lb_data.initiator = node->initiator;
    lb_data.target = node->target;

    if (node->data_type <= HMATLB_DATA_TYPE_WRITE_LATENCY) {
        /* Input latency data */

        if (!node->has_latency) {
            error_setg(errp, "Missing 'latency' option");
            return;
        }
        if (node->has_bandwidth) {
            error_setg(errp, "Invalid option 'bandwidth' since "
                       "the data type is latency");
            return;
        }

        /* Detect duplicate configuration */
        for (i = 0; i < hmat_lb->list->len; i++) {
            lb_temp = &g_array_index(hmat_lb->list, HMAT_LB_Data, i);

            if (node->initiator == lb_temp->initiator &&
                node->target == lb_temp->target) {
                error_setg(errp, "Duplicate configuration of the latency for "
                    "initiator=%d and target=%d", node->initiator,
                    node->target);
                return;
            }
        }

        hmat_lb->base = hmat_lb->base ? hmat_lb->base : UINT64_MAX;

        if (node->latency) {
            /* Calculate the temporary base and compressed latency */
            max_entry = node->latency;
            temp_base = 1;
            while (QEMU_IS_ALIGNED(max_entry, 10)) {
                max_entry /= 10;
                temp_base *= 10;
            }

            /* Calculate the max compressed latency */
            temp_base = MIN(hmat_lb->base, temp_base);
            max_entry = node->latency / hmat_lb->base;
            max_entry = MAX(hmat_lb->range_bitmap, max_entry);

            /*
             * For latency hmat_lb->range_bitmap record the max compressed
             * latency which should be less than 0xFFFF (UINT16_MAX)
             */
            if (max_entry >= UINT16_MAX) {
                error_setg(errp, "Latency %" PRIu64 " between initiator=%d and "
                        "target=%d should not differ from previously entered "
                        "min or max values on more than %d", node->latency,
                        node->initiator, node->target, UINT16_MAX - 1);
                return;
            } else {
                hmat_lb->base = temp_base;
                hmat_lb->range_bitmap = max_entry;
            }

            /*
             * Set lb_info_provided bit 0 as 1,
             * latency information is provided
             */
            numa_info[node->target].lb_info_provided |= BIT(0);
        }
        lb_data.data = node->latency;
    } else if (node->data_type >= HMATLB_DATA_TYPE_ACCESS_BANDWIDTH) {
        /* Input bandwidth data */
        if (!node->has_bandwidth) {
            error_setg(errp, "Missing 'bandwidth' option");
            return;
        }
        if (node->has_latency) {
            error_setg(errp, "Invalid option 'latency' since "
                       "the data type is bandwidth");
            return;
        }
        if (!QEMU_IS_ALIGNED(node->bandwidth, MiB)) {
            error_setg(errp, "Bandwidth %" PRIu64 " between initiator=%d and "
                       "target=%d should be 1MB aligned", node->bandwidth,
                       node->initiator, node->target);
            return;
        }

        /* Detect duplicate configuration */
        for (i = 0; i < hmat_lb->list->len; i++) {
            lb_temp = &g_array_index(hmat_lb->list, HMAT_LB_Data, i);

            if (node->initiator == lb_temp->initiator &&
                node->target == lb_temp->target) {
                error_setg(errp, "Duplicate configuration of the bandwidth for "
                    "initiator=%d and target=%d", node->initiator,
                    node->target);
                return;
            }
        }

        hmat_lb->base = hmat_lb->base ? hmat_lb->base : 1;

        if (node->bandwidth) {
            /* Keep bitmap unchanged when bandwidth out of range */
            bitmap_copy = hmat_lb->range_bitmap;
            bitmap_copy |= node->bandwidth;
            first_bit = ctz64(bitmap_copy);
            temp_base = UINT64_C(1) << first_bit;
            max_entry = node->bandwidth / temp_base;
            last_bit = 64 - clz64(bitmap_copy);

            /*
             * For bandwidth, first_bit record the base unit of bandwidth bits,
             * last_bit record the last bit of the max bandwidth. The max
             * compressed bandwidth should be less than 0xFFFF (UINT16_MAX)
             */
            if ((last_bit - first_bit) > UINT16_BITS ||
                max_entry >= UINT16_MAX) {
                error_setg(errp, "Bandwidth %" PRIu64 " between initiator=%d "
                        "and target=%d should not differ from previously "
                        "entered values on more than %d", node->bandwidth,
                        node->initiator, node->target, UINT16_MAX - 1);
                return;
            } else {
                hmat_lb->base = temp_base;
                hmat_lb->range_bitmap = bitmap_copy;
            }

            /*
             * Set lb_info_provided bit 1 as 1,
             * bandwidth information is provided
             */
            numa_info[node->target].lb_info_provided |= BIT(1);
        }
        lb_data.data = node->bandwidth;
    } else {
        assert(0);
    }

    g_array_append_val(hmat_lb->list, lb_data);
}

void parse_numa_hmat_cache(MachineState *ms, NumaHmatCacheOptions *node,
                           Error **errp)
{
    int nb_numa_nodes = ms->numa_state->num_nodes;
    NodeInfo *numa_info = ms->numa_state->nodes;
    NumaHmatCacheOptions *hmat_cache = NULL;

    if (node->node_id >= nb_numa_nodes) {
        error_setg(errp, "Invalid node-id=%" PRIu32 ", it should be less "
                   "than %d", node->node_id, nb_numa_nodes);
        return;
    }

    if (numa_info[node->node_id].lb_info_provided != (BIT(0) | BIT(1))) {
        error_setg(errp, "The latency and bandwidth information of "
                   "node-id=%" PRIu32 " should be provided before memory side "
                   "cache attributes", node->node_id);
        return;
    }

    if (node->level < 1 || node->level >= HMAT_LB_LEVELS) {
        error_setg(errp, "Invalid level=%" PRIu8 ", it should be larger than 0 "
                   "and less than or equal to %d", node->level,
                   HMAT_LB_LEVELS - 1);
        return;
    }

    assert(node->associativity < HMAT_CACHE_ASSOCIATIVITY__MAX);
    assert(node->policy < HMAT_CACHE_WRITE_POLICY__MAX);
    if (ms->numa_state->hmat_cache[node->node_id][node->level]) {
        error_setg(errp, "Duplicate configuration of the side cache for "
                   "node-id=%" PRIu32 " and level=%" PRIu8,
                   node->node_id, node->level);
        return;
    }

    if ((node->level > 1) &&
        ms->numa_state->hmat_cache[node->node_id][node->level - 1] == NULL) {
        error_setg(errp, "Cache level=%u shall be defined first",
                   node->level - 1);
        return;
    }

    if ((node->level > 1) &&
        (node->size <=
            ms->numa_state->hmat_cache[node->node_id][node->level - 1]->size)) {
        error_setg(errp, "Invalid size=%" PRIu64 ", the size of level=%" PRIu8
                   " should be larger than the size(%" PRIu64 ") of "
                   "level=%u", node->size, node->level,
                   ms->numa_state->hmat_cache[node->node_id]
                                             [node->level - 1]->size,
                   node->level - 1);
        return;
    }

    if ((node->level < HMAT_LB_LEVELS - 1) &&
        ms->numa_state->hmat_cache[node->node_id][node->level + 1] &&
        (node->size >=
            ms->numa_state->hmat_cache[node->node_id][node->level + 1]->size)) {
        error_setg(errp, "Invalid size=%" PRIu64 ", the size of level=%" PRIu8
                   " should be less than the size(%" PRIu64 ") of "
                   "level=%u", node->size, node->level,
                   ms->numa_state->hmat_cache[node->node_id]
                                             [node->level + 1]->size,
                   node->level + 1);
        return;
    }

    hmat_cache = g_malloc0(sizeof(*hmat_cache));
    memcpy(hmat_cache, node, sizeof(*hmat_cache));
    ms->numa_state->hmat_cache[node->node_id][node->level] = hmat_cache;
}

void set_numa_options(MachineState *ms, NumaOptions *object, Error **errp)
{
    if (!ms->numa_state) {
        error_setg(errp, "NUMA is not supported by this machine-type");
        return;
    }

    switch (object->type) {
    case NUMA_OPTIONS_TYPE_NODE:
        parse_numa_node(ms, &object->u.node, errp);
        break;
    case NUMA_OPTIONS_TYPE_DIST:
        parse_numa_distance(ms, &object->u.dist, errp);
        break;
    case NUMA_OPTIONS_TYPE_CPU:
        if (!object->u.cpu.has_node_id) {
            error_setg(errp, "Missing mandatory node-id property");
            return;
        }
        if (!ms->numa_state->nodes[object->u.cpu.node_id].present) {
            error_setg(errp, "Invalid node-id=%" PRId64 ", NUMA node must be "
                       "defined with -numa node,nodeid=ID before it's used with "
                       "-numa cpu,node-id=ID", object->u.cpu.node_id);
            return;
        }

        machine_set_cpu_numa_node(ms,
                                  qapi_NumaCpuOptions_base(&object->u.cpu),
                                  errp);
        break;
    case NUMA_OPTIONS_TYPE_HMAT_LB:
        if (!ms->numa_state->hmat_enabled) {
            error_setg(errp, "ACPI Heterogeneous Memory Attribute Table "
                       "(HMAT) is disabled, enable it with -machine hmat=on "
                       "before using any of hmat specific options");
            return;
        }

        parse_numa_hmat_lb(ms->numa_state, &object->u.hmat_lb, errp);
        break;
    case NUMA_OPTIONS_TYPE_HMAT_CACHE:
        if (!ms->numa_state->hmat_enabled) {
            error_setg(errp, "ACPI Heterogeneous Memory Attribute Table "
                       "(HMAT) is disabled, enable it with -machine hmat=on "
                       "before using any of hmat specific options");
            return;
        }

        parse_numa_hmat_cache(ms, &object->u.hmat_cache, errp);
        break;
    default:
        abort();
    }
}

static int parse_numa(void *opaque, QemuOpts *opts, Error **errp)
{
    NumaOptions *object = NULL;
    MachineState *ms = MACHINE(opaque);
    Error *err = NULL;
    Visitor *v = opts_visitor_new(opts);

    visit_type_NumaOptions(v, NULL, &object, errp);
    visit_free(v);
    if (!object) {
        return -1;
    }

    /* Fix up legacy suffix-less format */
    if ((object->type == NUMA_OPTIONS_TYPE_NODE) && object->u.node.has_mem) {
        const char *mem_str = qemu_opt_get(opts, "mem");
        qemu_strtosz_MiB(mem_str, NULL, &object->u.node.mem);
    }

    set_numa_options(ms, object, &err);

    qapi_free_NumaOptions(object);
    if (err) {
        error_propagate(errp, err);
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
static void validate_numa_distance(MachineState *ms)
{
    int src, dst;
    bool is_asymmetrical = false;
    int nb_numa_nodes = ms->numa_state->num_nodes;
    NodeInfo *numa_info = ms->numa_state->nodes;

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

static void complete_init_numa_distance(MachineState *ms)
{
    int src, dst;
    NodeInfo *numa_info = ms->numa_state->nodes;

    /* Fixup NUMA distance by symmetric policy because if it is an
     * asymmetric distance table, it should be a complete table and
     * there would not be any missing distance except local node, which
     * is verified by validate_numa_distance above.
     */
    for (src = 0; src < ms->numa_state->num_nodes; src++) {
        for (dst = 0; dst < ms->numa_state->num_nodes; dst++) {
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

static void numa_init_memdev_container(MachineState *ms, MemoryRegion *ram)
{
    int i;
    uint64_t addr = 0;

    for (i = 0; i < ms->numa_state->num_nodes; i++) {
        uint64_t size = ms->numa_state->nodes[i].node_mem;
        HostMemoryBackend *backend = ms->numa_state->nodes[i].node_memdev;
        if (!backend) {
            continue;
        }
        MemoryRegion *seg = machine_consume_memdev(ms, backend);
        memory_region_add_subregion(ram, addr, seg);
        addr += size;
    }
}

void numa_complete_configuration(MachineState *ms)
{
    int i;
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    NodeInfo *numa_info = ms->numa_state->nodes;

    /*
     * If memory hotplug is enabled (slot > 0) or memory devices are enabled
     * (ms->maxram_size > ms->ram_size) but without '-numa' options explicitly on
     * CLI, guests will break.
     *
     *   Windows: won't enable memory hotplug without SRAT table at all
     *
     *   Linux: if QEMU is started with initial memory all below 4Gb
     *   and no SRAT table present, guest kernel will use nommu DMA ops,
     *   which breaks 32bit hw drivers when memory is hotplugged and
     *   guest tries to use it with that drivers.
     *
     * Enable NUMA implicitly by adding a new NUMA node automatically.
     *
     * Or if MachineClass::auto_enable_numa is true and no NUMA nodes,
     * assume there is just one node with whole RAM.
     */
    if (ms->numa_state->num_nodes == 0 &&
        ((ms->ram_slots && mc->auto_enable_numa_with_memhp) ||
         (ms->maxram_size > ms->ram_size && mc->auto_enable_numa_with_memdev) ||
         mc->auto_enable_numa)) {
            NumaNodeOptions node = { };
            parse_numa_node(ms, &node, &error_abort);
            numa_info[0].node_mem = ms->ram_size;
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
    assert(ms->numa_state->num_nodes == max_numa_nodeid);

    if (ms->numa_state->num_nodes > 0) {
        uint64_t numa_total;

        numa_total = 0;
        for (i = 0; i < ms->numa_state->num_nodes; i++) {
            numa_total += numa_info[i].node_mem;
        }
        if (numa_total != ms->ram_size) {
            error_report("total memory for NUMA nodes (0x%" PRIx64 ")"
                         " should equal RAM size (0x" RAM_ADDR_FMT ")",
                         numa_total, ms->ram_size);
            exit(1);
        }

        if (!numa_uses_legacy_mem() && mc->default_ram_id) {
            if (ms->ram_memdev_id) {
                error_report("'-machine memory-backend' and '-numa memdev'"
                             " properties are mutually exclusive");
                exit(1);
            }
            ms->ram = g_new(MemoryRegion, 1);
            memory_region_init(ms->ram, OBJECT(ms), mc->default_ram_id,
                               ms->ram_size);
            numa_init_memdev_container(ms, ms->ram);
        }
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
        if (ms->numa_state->have_numa_distance) {
            /* Validate enough NUMA distance information was provided. */
            validate_numa_distance(ms);

            /* Validation succeeded, now fill in any missing distances. */
            complete_init_numa_distance(ms);
        }
    }
}

void parse_numa_opts(MachineState *ms)
{
    qemu_opts_foreach(qemu_find_opts("numa"), parse_numa, ms, &error_fatal);
}

void numa_cpu_pre_plug(const CPUArchId *slot, DeviceState *dev, Error **errp)
{
    int node_id = object_property_get_int(OBJECT(dev), "node-id", &error_abort);

    if (node_id == CPU_UNSET_NUMA_NODE_ID) {
        /* due to bug in libvirt, it doesn't pass node-id from props on
         * device_add as expected, so we have to fix it up here */
        if (slot->props.has_node_id) {
            object_property_set_int(OBJECT(dev), "node-id",
                                    slot->props.node_id, errp);
        }
    } else if (node_id != slot->props.node_id) {
        error_setg(errp, "invalid node-id, must be %"PRId64,
                   slot->props.node_id);
    }
}

static void numa_stat_memory_devices(NumaNodeMem node_mem[])
{
    MemoryDeviceInfoList *info_list = qmp_memory_device_list();
    MemoryDeviceInfoList *info;
    PCDIMMDeviceInfo     *pcdimm_info;
    VirtioPMEMDeviceInfo *vpi;
    VirtioMEMDeviceInfo *vmi;

    for (info = info_list; info; info = info->next) {
        MemoryDeviceInfo *value = info->value;

        if (value) {
            switch (value->type) {
            case MEMORY_DEVICE_INFO_KIND_DIMM:
            case MEMORY_DEVICE_INFO_KIND_NVDIMM:
                pcdimm_info = value->type == MEMORY_DEVICE_INFO_KIND_DIMM ?
                              value->u.dimm.data : value->u.nvdimm.data;
                node_mem[pcdimm_info->node].node_mem += pcdimm_info->size;
                node_mem[pcdimm_info->node].node_plugged_mem +=
                    pcdimm_info->size;
                break;
            case MEMORY_DEVICE_INFO_KIND_VIRTIO_PMEM:
                vpi = value->u.virtio_pmem.data;
                /* TODO: once we support numa, assign to right node */
                node_mem[0].node_mem += vpi->size;
                node_mem[0].node_plugged_mem += vpi->size;
                break;
            case MEMORY_DEVICE_INFO_KIND_VIRTIO_MEM:
                vmi = value->u.virtio_mem.data;
                node_mem[vmi->node].node_mem += vmi->size;
                node_mem[vmi->node].node_plugged_mem += vmi->size;
                break;
            default:
                g_assert_not_reached();
            }
        }
    }
    qapi_free_MemoryDeviceInfoList(info_list);
}

void query_numa_node_mem(NumaNodeMem node_mem[], MachineState *ms)
{
    int i;

    if (ms->numa_state == NULL || ms->numa_state->num_nodes <= 0) {
        return;
    }

    numa_stat_memory_devices(node_mem);
    for (i = 0; i < ms->numa_state->num_nodes; i++) {
        node_mem[i].node_mem += ms->numa_state->nodes[i].node_mem;
    }
}

static int ram_block_notify_add_single(RAMBlock *rb, void *opaque)
{
    const ram_addr_t max_size = qemu_ram_get_max_length(rb);
    const ram_addr_t size = qemu_ram_get_used_length(rb);
    void *host = qemu_ram_get_host_addr(rb);
    RAMBlockNotifier *notifier = opaque;

    if (host) {
        notifier->ram_block_added(notifier, host, size, max_size);
    }
    return 0;
}

void ram_block_notifier_add(RAMBlockNotifier *n)
{
    QLIST_INSERT_HEAD(&ram_list.ramblock_notifiers, n, next);

    /* Notify about all existing ram blocks. */
    if (n->ram_block_added) {
        qemu_ram_foreach_block(ram_block_notify_add_single, n);
    }
}

void ram_block_notifier_remove(RAMBlockNotifier *n)
{
    QLIST_REMOVE(n, next);
}

void ram_block_notify_add(void *host, size_t size, size_t max_size)
{
    RAMBlockNotifier *notifier;

    QLIST_FOREACH(notifier, &ram_list.ramblock_notifiers, next) {
        if (notifier->ram_block_added) {
            notifier->ram_block_added(notifier, host, size, max_size);
        }
    }
}

void ram_block_notify_remove(void *host, size_t size, size_t max_size)
{
    RAMBlockNotifier *notifier;

    QLIST_FOREACH(notifier, &ram_list.ramblock_notifiers, next) {
        if (notifier->ram_block_removed) {
            notifier->ram_block_removed(notifier, host, size, max_size);
        }
    }
}

void ram_block_notify_resize(void *host, size_t old_size, size_t new_size)
{
    RAMBlockNotifier *notifier;

    QLIST_FOREACH(notifier, &ram_list.ramblock_notifiers, next) {
        if (notifier->ram_block_resized) {
            notifier->ram_block_resized(notifier, host, old_size, new_size);
        }
    }
}
