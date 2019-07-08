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

typedef struct X86CPUTopoInfo {
    unsigned pkg_id;
    unsigned die_id;
    unsigned core_id;
    unsigned smt_id;
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
static inline unsigned apicid_smt_width(unsigned nr_dies,
                                        unsigned nr_cores,
                                        unsigned nr_threads)
{
    return apicid_bitwidth_for_count(nr_threads);
}

/* Bit width of the Core_ID field
 */
static inline unsigned apicid_core_width(unsigned nr_dies,
                                         unsigned nr_cores,
                                         unsigned nr_threads)
{
    return apicid_bitwidth_for_count(nr_cores);
}

/* Bit width of the Die_ID field */
static inline unsigned apicid_die_width(unsigned nr_dies,
                                        unsigned nr_cores,
                                        unsigned nr_threads)
{
    return apicid_bitwidth_for_count(nr_dies);
}

/* Bit offset of the Core_ID field
 */
static inline unsigned apicid_core_offset(unsigned nr_dies,
                                          unsigned nr_cores,
                                          unsigned nr_threads)
{
    return apicid_smt_width(nr_dies, nr_cores, nr_threads);
}

/* Bit offset of the Die_ID field */
static inline unsigned apicid_die_offset(unsigned nr_dies,
                                          unsigned nr_cores,
                                           unsigned nr_threads)
{
    return apicid_core_offset(nr_dies, nr_cores, nr_threads) +
           apicid_core_width(nr_dies, nr_cores, nr_threads);
}

/* Bit offset of the Pkg_ID (socket ID) field
 */
static inline unsigned apicid_pkg_offset(unsigned nr_dies,
                                         unsigned nr_cores,
                                         unsigned nr_threads)
{
    return apicid_die_offset(nr_dies, nr_cores, nr_threads) +
           apicid_die_width(nr_dies, nr_cores, nr_threads);
}

/* Make APIC ID for the CPU based on Pkg_ID, Core_ID, SMT_ID
 *
 * The caller must make sure core_id < nr_cores and smt_id < nr_threads.
 */
static inline apic_id_t apicid_from_topo_ids(unsigned nr_dies,
                                             unsigned nr_cores,
                                             unsigned nr_threads,
                                             const X86CPUTopoInfo *topo)
{
    return (topo->pkg_id  << apicid_pkg_offset(nr_dies, nr_cores, nr_threads)) |
           (topo->die_id  << apicid_die_offset(nr_dies, nr_cores, nr_threads)) |
          (topo->core_id << apicid_core_offset(nr_dies, nr_cores, nr_threads)) |
           topo->smt_id;
}

/* Calculate thread/core/package IDs for a specific topology,
 * based on (contiguous) CPU index
 */
static inline void x86_topo_ids_from_idx(unsigned nr_dies,
                                         unsigned nr_cores,
                                         unsigned nr_threads,
                                         unsigned cpu_index,
                                         X86CPUTopoInfo *topo)
{
    topo->pkg_id = cpu_index / (nr_dies * nr_cores * nr_threads);
    topo->die_id = cpu_index / (nr_cores * nr_threads) % nr_dies;
    topo->core_id = cpu_index / nr_threads % nr_cores;
    topo->smt_id = cpu_index % nr_threads;
}

/* Calculate thread/core/package IDs for a specific topology,
 * based on APIC ID
 */
static inline void x86_topo_ids_from_apicid(apic_id_t apicid,
                                            unsigned nr_dies,
                                            unsigned nr_cores,
                                            unsigned nr_threads,
                                            X86CPUTopoInfo *topo)
{
    topo->smt_id = apicid &
            ~(0xFFFFFFFFUL << apicid_smt_width(nr_dies, nr_cores, nr_threads));
    topo->core_id =
            (apicid >> apicid_core_offset(nr_dies, nr_cores, nr_threads)) &
            ~(0xFFFFFFFFUL << apicid_core_width(nr_dies, nr_cores, nr_threads));
    topo->die_id =
            (apicid >> apicid_die_offset(nr_dies, nr_cores, nr_threads)) &
            ~(0xFFFFFFFFUL << apicid_die_width(nr_dies, nr_cores, nr_threads));
    topo->pkg_id = apicid >> apicid_pkg_offset(nr_dies, nr_cores, nr_threads);
}

/* Make APIC ID for the CPU 'cpu_index'
 *
 * 'cpu_index' is a sequential, contiguous ID for the CPU.
 */
static inline apic_id_t x86_apicid_from_cpu_idx(unsigned nr_dies,
                                                unsigned nr_cores,
                                                unsigned nr_threads,
                                                unsigned cpu_index)
{
    X86CPUTopoInfo topo;
    x86_topo_ids_from_idx(nr_dies, nr_cores, nr_threads, cpu_index, &topo);
    return apicid_from_topo_ids(nr_dies, nr_cores, nr_threads, &topo);
}

#endif /* HW_I386_TOPOLOGY_H */
