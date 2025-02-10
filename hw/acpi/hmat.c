/*
 * HMAT ACPI Implementation
 *
 * Copyright(C) 2019 Intel Corporation.
 *
 * Author:
 *  Liu jingqi <jingqi.liu@linux.intel.com>
 *  Tao Xu <tao3.xu@intel.com>
 *
 * HMAT is defined in ACPI 6.3: 5.2.27 Heterogeneous Memory Attribute Table
 * (HMAT)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "system/numa.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/hmat.h"

/*
 * ACPI 6.3:
 * 5.2.27.3 Memory Proximity Domain Attributes Structure: Table 5-145
 */
static void build_hmat_mpda(GArray *table_data, uint16_t flags,
                            uint32_t initiator, uint32_t mem_node)
{

    /* Memory Proximity Domain Attributes Structure */
    /* Type */
    build_append_int_noprefix(table_data, 0, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Length */
    build_append_int_noprefix(table_data, 40, 4);
    /* Flags */
    build_append_int_noprefix(table_data, flags, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Proximity Domain for the Attached Initiator */
    build_append_int_noprefix(table_data, initiator, 4);
    /* Proximity Domain for the Memory */
    build_append_int_noprefix(table_data, mem_node, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);
    /*
     * Reserved:
     * Previously defined as the Start Address of the System Physical
     * Address Range. Deprecated since ACPI Spec 6.3.
     */
    build_append_int_noprefix(table_data, 0, 8);
    /*
     * Reserved:
     * Previously defined as the Range Length of the region in bytes.
     * Deprecated since ACPI Spec 6.3.
     */
    build_append_int_noprefix(table_data, 0, 8);
}

/*
 * ACPI 6.3: 5.2.27.4 System Locality Latency and Bandwidth Information
 * Structure: Table 5-146
 */
static void build_hmat_lb(GArray *table_data, HMAT_LB_Info *hmat_lb,
                          uint32_t num_initiator, uint32_t num_target,
                          uint32_t *initiator_list)
{
    int i, index;
    uint32_t initiator_to_index[MAX_NODES] = {};
    HMAT_LB_Data *lb_data;
    uint16_t *entry_list;
    uint32_t base;
    /* Length in bytes for entire structure */
    uint32_t lb_length
        = 32 /* Table length up to and including Entry Base Unit */
        + 4 * num_initiator /* Initiator Proximity Domain List */
        + 4 * num_target /* Target Proximity Domain List */
        + 2 * num_initiator * num_target; /* Latency or Bandwidth Entries */

    /* Type */
    build_append_int_noprefix(table_data, 1, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Length */
    build_append_int_noprefix(table_data, lb_length, 4);
    /* Flags: Bits [3:0] Memory Hierarchy, Bits[7:4] Reserved */
    assert(!(hmat_lb->hierarchy >> 4));
    build_append_int_noprefix(table_data, hmat_lb->hierarchy, 1);
    /* Data Type */
    build_append_int_noprefix(table_data, hmat_lb->data_type, 1);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Number of Initiator Proximity Domains (s) */
    build_append_int_noprefix(table_data, num_initiator, 4);
    /* Number of Target Proximity Domains (t) */
    build_append_int_noprefix(table_data, num_target, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);

    /* Entry Base Unit */
    if (hmat_lb->data_type <= HMAT_LB_DATA_WRITE_LATENCY) {
        /* Convert latency base from nanoseconds to picosecond */
        base = hmat_lb->base * 1000;
    } else {
        /* Convert bandwidth base from Byte to Megabyte */
        base = hmat_lb->base / MiB;
    }
    build_append_int_noprefix(table_data, base, 8);

    /* Initiator Proximity Domain List */
    for (i = 0; i < num_initiator; i++) {
        build_append_int_noprefix(table_data, initiator_list[i], 4);
        /* Reverse mapping for array possitions */
        initiator_to_index[initiator_list[i]] = i;
    }

    /* Target Proximity Domain List */
    for (i = 0; i < num_target; i++) {
        build_append_int_noprefix(table_data, i, 4);
    }

    /* Latency or Bandwidth Entries */
    entry_list = g_new0(uint16_t, num_initiator * num_target);
    for (i = 0; i < hmat_lb->list->len; i++) {
        lb_data = &g_array_index(hmat_lb->list, HMAT_LB_Data, i);
        index = initiator_to_index[lb_data->initiator] * num_target +
            lb_data->target;

        entry_list[index] = (uint16_t)(lb_data->data / hmat_lb->base);
    }

    for (i = 0; i < num_initiator * num_target; i++) {
        build_append_int_noprefix(table_data, entry_list[i], 2);
    }

    g_free(entry_list);
}

/* ACPI 6.3: 5.2.27.5 Memory Side Cache Information Structure: Table 5-147 */
static void build_hmat_cache(GArray *table_data, uint8_t total_levels,
                             NumaHmatCacheOptions *hmat_cache)
{
    /*
     * Cache Attributes: Bits [3:0] – Total Cache Levels
     * for this Memory Proximity Domain
     */
    uint32_t cache_attr = total_levels;

    /* Bits [7:4] : Cache Level described in this structure */
    cache_attr |= (uint32_t) hmat_cache->level << 4;

    /* Bits [11:8] - Cache Associativity */
    cache_attr |= (uint32_t) hmat_cache->associativity << 8;

    /* Bits [15:12] - Write Policy */
    cache_attr |= (uint32_t) hmat_cache->policy << 12;

    /* Bits [31:16] - Cache Line size in bytes */
    cache_attr |= (uint32_t) hmat_cache->line << 16;

    /* Type */
    build_append_int_noprefix(table_data, 2, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /* Length */
    build_append_int_noprefix(table_data, 32, 4);
    /* Proximity Domain for the Memory */
    build_append_int_noprefix(table_data, hmat_cache->node_id, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);
    /* Memory Side Cache Size */
    build_append_int_noprefix(table_data, hmat_cache->size, 8);
    /* Cache Attributes */
    build_append_int_noprefix(table_data, cache_attr, 4);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 2);
    /*
     * Number of SMBIOS handles (n)
     * Linux kernel uses Memory Side Cache Information Structure
     * without SMBIOS entries for now, so set Number of SMBIOS handles
     * as 0.
     */
    build_append_int_noprefix(table_data, 0, 2);
}

/* Build HMAT sub table structures */
static void hmat_build_table_structs(GArray *table_data, NumaState *numa_state)
{
    uint16_t flags;
    uint32_t num_initiator = 0;
    uint32_t initiator_list[MAX_NODES];
    int i, hierarchy, type, cache_level, total_levels;
    HMAT_LB_Info *hmat_lb;
    NumaHmatCacheOptions *hmat_cache;

    build_append_int_noprefix(table_data, 0, 4); /* Reserved */

    for (i = 0; i < numa_state->num_nodes; i++) {
        /*
         * Linux rejects whole HMAT table if a node with no memory
         * has one of these structures listing it as a target.
         */
        if (!numa_state->nodes[i].node_mem) {
            continue;
        }
        flags = 0;

        if (numa_state->nodes[i].initiator < MAX_NODES) {
            flags |= HMAT_PROXIMITY_INITIATOR_VALID;
        }

        build_hmat_mpda(table_data, flags, numa_state->nodes[i].initiator, i);
    }

    for (i = 0; i < numa_state->num_nodes; i++) {
        if (numa_state->nodes[i].has_cpu || numa_state->nodes[i].has_gi) {
            initiator_list[num_initiator++] = i;
        }
    }

    /*
     * ACPI 6.3: 5.2.27.4 System Locality Latency and Bandwidth Information
     * Structure: Table 5-146
     */
    for (hierarchy = HMAT_LB_MEM_MEMORY;
         hierarchy <= HMAT_LB_MEM_CACHE_3RD_LEVEL; hierarchy++) {
        for (type = HMAT_LB_DATA_ACCESS_LATENCY;
             type <= HMAT_LB_DATA_WRITE_BANDWIDTH; type++) {
            hmat_lb = numa_state->hmat_lb[hierarchy][type];

            if (hmat_lb && hmat_lb->list->len) {
                build_hmat_lb(table_data, hmat_lb, num_initiator,
                              numa_state->num_nodes, initiator_list);
            }
        }
    }

    /*
     * ACPI 6.3: 5.2.27.5 Memory Side Cache Information Structure:
     * Table 5-147
     */
    for (i = 0; i < numa_state->num_nodes; i++) {
        total_levels = 0;
        for (cache_level = 1; cache_level < HMAT_LB_LEVELS; cache_level++) {
            if (numa_state->hmat_cache[i][cache_level]) {
                total_levels++;
            }
        }
        for (cache_level = 0; cache_level <= total_levels; cache_level++) {
            hmat_cache = numa_state->hmat_cache[i][cache_level];
            if (hmat_cache) {
                build_hmat_cache(table_data, total_levels, hmat_cache);
            }
        }
    }
}

void build_hmat(GArray *table_data, BIOSLinker *linker, NumaState *numa_state,
                const char *oem_id, const char *oem_table_id)
{
    AcpiTable table = { .sig = "HMAT", .rev = 2,
                        .oem_id = oem_id, .oem_table_id = oem_table_id };

    acpi_table_begin(&table, table_data);
    hmat_build_table_structs(table_data, numa_state);
    acpi_table_end(linker, &table);
}
