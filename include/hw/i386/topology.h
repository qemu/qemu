/*
 *  x86 CPU topology data structures and functions
 *
 *  Copyright (c) 2012 Red Hat Inc.
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
#ifndef HW_I386_TOPOLOGY_H
#define HW_I386_TOPOLOGY_H

/* This file implements the APIC-ID-based CPU topology enumeration logic,
 * documented at the following document:
 *   Intel® 64 Architecture Processor Topology Enumeration
 *   http://software.intel.com/en-us/articles/intel-64-architecture-processor-topology-enumeration/
 *
 * This code should be compatible with AMD's "Extended Method" described at:
 *   AMD CPUID Specification (Publication #25481)
 *   Section 3: Multiple Core Calcuation
 * as long as:
 *  nr_threads is set to 1;
 *  OFFSET_IDX is assumed to be 0;
 *  CPUID Fn8000_0008_ECX[ApicIdCoreIdSize[3:0]] is set to apicid_core_width().
 */


#include "qemu/bitops.h"

/* APIC IDs can be 32-bit, but beware: APIC IDs > 255 require x2APIC support
 */
typedef uint32_t apic_id_t;

typedef struct X86CPUTopoIDs {
    unsigned pkg_id;
    unsigned node_id;
    unsigned die_id;
    unsigned core_id;
    unsigned smt_id;
} X86CPUTopoIDs;

typedef struct X86CPUTopoInfo {
    unsigned nodes_per_pkg;
    unsigned dies_per_pkg;
    unsigned cores_per_die;
    unsigned threads_per_core;
} X86CPUTopoInfo;

/* Return the bit width needed for 'count' IDs
 */
static unsigned apicid_bitwidth_for_count(unsigned count)
{
    g_assert(count >= 1);
    count -= 1;
    return count ? 32 - clz32(count) : 0;
}

/* Bit width of the SMT_ID (thread ID) field on the APIC ID
 */
static inline unsigned apicid_smt_width(X86CPUTopoInfo *topo_info)
{
    return apicid_bitwidth_for_count(topo_info->threads_per_core);
}

/* Bit width of the Core_ID field
 */
static inline unsigned apicid_core_width(X86CPUTopoInfo *topo_info)
{
    return apicid_bitwidth_for_count(topo_info->cores_per_die);
}

/* Bit width of the Die_ID field */
static inline unsigned apicid_die_width(X86CPUTopoInfo *topo_info)
{
    return apicid_bitwidth_for_count(topo_info->dies_per_pkg);
}

/* Bit width of the node_id field per socket */
static inline unsigned apicid_node_width_epyc(X86CPUTopoInfo *topo_info)
{
    return apicid_bitwidth_for_count(MAX(topo_info->nodes_per_pkg, 1));
}
/* Bit offset of the Core_ID field
 */
static inline unsigned apicid_core_offset(X86CPUTopoInfo *topo_info)
{
    return apicid_smt_width(topo_info);
}

/* Bit offset of the Die_ID field */
static inline unsigned apicid_die_offset(X86CPUTopoInfo *topo_info)
{
    return apicid_core_offset(topo_info) + apicid_core_width(topo_info);
}

/* Bit offset of the Pkg_ID (socket ID) field
 */
static inline unsigned apicid_pkg_offset(X86CPUTopoInfo *topo_info)
{
    return apicid_die_offset(topo_info) + apicid_die_width(topo_info);
}

#define NODE_ID_OFFSET 3 /* Minimum node_id offset if numa configured */

/*
 * Bit offset of the node_id field
 *
 * Make sure nodes_per_pkg >  0 if numa configured else zero.
 */
static inline unsigned apicid_node_offset_epyc(X86CPUTopoInfo *topo_info)
{
    unsigned offset = apicid_die_offset(topo_info) +
                      apicid_die_width(topo_info);

    if (topo_info->nodes_per_pkg) {
        return MAX(NODE_ID_OFFSET, offset);
    } else {
        return offset;
    }
}

/* Bit offset of the Pkg_ID (socket ID) field */
static inline unsigned apicid_pkg_offset_epyc(X86CPUTopoInfo *topo_info)
{
    return apicid_node_offset_epyc(topo_info) +
           apicid_node_width_epyc(topo_info);
}

/*
 * Make APIC ID for the CPU based on Pkg_ID, Core_ID, SMT_ID
 *
 * The caller must make sure core_id < nr_cores and smt_id < nr_threads.
 */
static inline apic_id_t
x86_apicid_from_topo_ids_epyc(X86CPUTopoInfo *topo_info,
                              const X86CPUTopoIDs *topo_ids)
{
    return (topo_ids->pkg_id  << apicid_pkg_offset_epyc(topo_info)) |
           (topo_ids->node_id << apicid_node_offset_epyc(topo_info)) |
           (topo_ids->die_id  << apicid_die_offset(topo_info)) |
           (topo_ids->core_id << apicid_core_offset(topo_info)) |
           topo_ids->smt_id;
}

static inline void x86_topo_ids_from_idx_epyc(X86CPUTopoInfo *topo_info,
                                              unsigned cpu_index,
                                              X86CPUTopoIDs *topo_ids)
{
    unsigned nr_nodes = MAX(topo_info->nodes_per_pkg, 1);
    unsigned nr_dies = topo_info->dies_per_pkg;
    unsigned nr_cores = topo_info->cores_per_die;
    unsigned nr_threads = topo_info->threads_per_core;
    unsigned cores_per_node = DIV_ROUND_UP((nr_dies * nr_cores * nr_threads),
                                            nr_nodes);

    topo_ids->pkg_id = cpu_index / (nr_dies * nr_cores * nr_threads);
    topo_ids->node_id = (cpu_index / cores_per_node) % nr_nodes;
    topo_ids->die_id = cpu_index / (nr_cores * nr_threads) % nr_dies;
    topo_ids->core_id = cpu_index / nr_threads % nr_cores;
    topo_ids->smt_id = cpu_index % nr_threads;
}

/*
 * Calculate thread/core/package IDs for a specific topology,
 * based on APIC ID
 */
static inline void x86_topo_ids_from_apicid_epyc(apic_id_t apicid,
                                            X86CPUTopoInfo *topo_info,
                                            X86CPUTopoIDs *topo_ids)
{
    topo_ids->smt_id = apicid &
            ~(0xFFFFFFFFUL << apicid_smt_width(topo_info));
    topo_ids->core_id =
            (apicid >> apicid_core_offset(topo_info)) &
            ~(0xFFFFFFFFUL << apicid_core_width(topo_info));
    topo_ids->die_id =
            (apicid >> apicid_die_offset(topo_info)) &
            ~(0xFFFFFFFFUL << apicid_die_width(topo_info));
    topo_ids->node_id =
            (apicid >> apicid_node_offset_epyc(topo_info)) &
            ~(0xFFFFFFFFUL << apicid_node_width_epyc(topo_info));
    topo_ids->pkg_id = apicid >> apicid_pkg_offset_epyc(topo_info);
}

/*
 * Make APIC ID for the CPU 'cpu_index'
 *
 * 'cpu_index' is a sequential, contiguous ID for the CPU.
 */
static inline apic_id_t x86_apicid_from_cpu_idx_epyc(X86CPUTopoInfo *topo_info,
                                                     unsigned cpu_index)
{
    X86CPUTopoIDs topo_ids;
    x86_topo_ids_from_idx_epyc(topo_info, cpu_index, &topo_ids);
    return x86_apicid_from_topo_ids_epyc(topo_info, &topo_ids);
}
/* Make APIC ID for the CPU based on Pkg_ID, Core_ID, SMT_ID
 *
 * The caller must make sure core_id < nr_cores and smt_id < nr_threads.
 */
static inline apic_id_t x86_apicid_from_topo_ids(X86CPUTopoInfo *topo_info,
                                                 const X86CPUTopoIDs *topo_ids)
{
    return (topo_ids->pkg_id  << apicid_pkg_offset(topo_info)) |
           (topo_ids->die_id  << apicid_die_offset(topo_info)) |
           (topo_ids->core_id << apicid_core_offset(topo_info)) |
           topo_ids->smt_id;
}

/* Calculate thread/core/package IDs for a specific topology,
 * based on (contiguous) CPU index
 */
static inline void x86_topo_ids_from_idx(X86CPUTopoInfo *topo_info,
                                         unsigned cpu_index,
                                         X86CPUTopoIDs *topo_ids)
{
    unsigned nr_dies = topo_info->dies_per_pkg;
    unsigned nr_cores = topo_info->cores_per_die;
    unsigned nr_threads = topo_info->threads_per_core;

    topo_ids->pkg_id = cpu_index / (nr_dies * nr_cores * nr_threads);
    topo_ids->die_id = cpu_index / (nr_cores * nr_threads) % nr_dies;
    topo_ids->core_id = cpu_index / nr_threads % nr_cores;
    topo_ids->smt_id = cpu_index % nr_threads;
}

/* Calculate thread/core/package IDs for a specific topology,
 * based on APIC ID
 */
static inline void x86_topo_ids_from_apicid(apic_id_t apicid,
                                            X86CPUTopoInfo *topo_info,
                                            X86CPUTopoIDs *topo_ids)
{
    topo_ids->smt_id = apicid &
            ~(0xFFFFFFFFUL << apicid_smt_width(topo_info));
    topo_ids->core_id =
            (apicid >> apicid_core_offset(topo_info)) &
            ~(0xFFFFFFFFUL << apicid_core_width(topo_info));
    topo_ids->die_id =
            (apicid >> apicid_die_offset(topo_info)) &
            ~(0xFFFFFFFFUL << apicid_die_width(topo_info));
    topo_ids->pkg_id = apicid >> apicid_pkg_offset(topo_info);
}

/* Make APIC ID for the CPU 'cpu_index'
 *
 * 'cpu_index' is a sequential, contiguous ID for the CPU.
 */
static inline apic_id_t x86_apicid_from_cpu_idx(X86CPUTopoInfo *topo_info,
                                                unsigned cpu_index)
{
    X86CPUTopoIDs topo_ids;
    x86_topo_ids_from_idx(topo_info, cpu_index, &topo_ids);
    return x86_apicid_from_topo_ids(topo_info, &topo_ids);
}

#endif /* HW_I386_TOPOLOGY_H */
