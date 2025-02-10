/*
 * QEMU RISC-V NUMA Helper
 *
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/numa.h"
#include "system/device_tree.h"

static bool numa_enabled(const MachineState *ms)
{
    return (ms->numa_state && ms->numa_state->num_nodes) ? true : false;
}

int riscv_socket_count(const MachineState *ms)
{
    return (numa_enabled(ms)) ? ms->numa_state->num_nodes : 1;
}

int riscv_socket_first_hartid(const MachineState *ms, int socket_id)
{
    int i, first_hartid = ms->smp.cpus;

    if (!numa_enabled(ms)) {
        return (!socket_id) ? 0 : -1;
    }

    for (i = 0; i < ms->smp.cpus; i++) {
        if (ms->possible_cpus->cpus[i].props.node_id != socket_id) {
            continue;
        }
        if (i < first_hartid) {
            first_hartid = i;
        }
    }

    return (first_hartid < ms->smp.cpus) ? first_hartid : -1;
}

int riscv_socket_last_hartid(const MachineState *ms, int socket_id)
{
    int i, last_hartid = -1;

    if (!numa_enabled(ms)) {
        return (!socket_id) ? ms->smp.cpus - 1 : -1;
    }

    for (i = 0; i < ms->smp.cpus; i++) {
        if (ms->possible_cpus->cpus[i].props.node_id != socket_id) {
            continue;
        }
        if (i > last_hartid) {
            last_hartid = i;
        }
    }

    return (last_hartid < ms->smp.cpus) ? last_hartid : -1;
}

int riscv_socket_hart_count(const MachineState *ms, int socket_id)
{
    int first_hartid, last_hartid;

    if (!numa_enabled(ms)) {
        return (!socket_id) ? ms->smp.cpus : -1;
    }

    first_hartid = riscv_socket_first_hartid(ms, socket_id);
    if (first_hartid < 0) {
        return -1;
    }

    last_hartid = riscv_socket_last_hartid(ms, socket_id);
    if (last_hartid < 0) {
        return -1;
    }

    if (first_hartid > last_hartid) {
        return -1;
    }

    return last_hartid - first_hartid + 1;
}

bool riscv_socket_check_hartids(const MachineState *ms, int socket_id)
{
    int i, first_hartid, last_hartid;

    if (!numa_enabled(ms)) {
        return (!socket_id) ? true : false;
    }

    first_hartid = riscv_socket_first_hartid(ms, socket_id);
    if (first_hartid < 0) {
        return false;
    }

    last_hartid = riscv_socket_last_hartid(ms, socket_id);
    if (last_hartid < 0) {
        return false;
    }

    for (i = first_hartid; i <= last_hartid; i++) {
        if (ms->possible_cpus->cpus[i].props.node_id != socket_id) {
            return false;
        }
    }

    return true;
}

uint64_t riscv_socket_mem_offset(const MachineState *ms, int socket_id)
{
    int i;
    uint64_t mem_offset = 0;

    if (!numa_enabled(ms)) {
        return 0;
    }

    for (i = 0; i < ms->numa_state->num_nodes; i++) {
        if (i == socket_id) {
            break;
        }
        mem_offset += ms->numa_state->nodes[i].node_mem;
    }

    return (i == socket_id) ? mem_offset : 0;
}

uint64_t riscv_socket_mem_size(const MachineState *ms, int socket_id)
{
    if (!numa_enabled(ms)) {
        return (!socket_id) ? ms->ram_size : 0;
    }

    return (socket_id < ms->numa_state->num_nodes) ?
            ms->numa_state->nodes[socket_id].node_mem : 0;
}

void riscv_socket_fdt_write_id(const MachineState *ms, const char *node_name,
                               int socket_id)
{
    if (numa_enabled(ms)) {
        qemu_fdt_setprop_cell(ms->fdt, node_name, "numa-node-id", socket_id);
    }
}

void riscv_socket_fdt_write_distance_matrix(const MachineState *ms)
{
    int i, j, idx;
    g_autofree uint32_t *dist_matrix = NULL;
    uint32_t dist_matrix_size;

    if (numa_enabled(ms) && ms->numa_state->have_numa_distance) {
        dist_matrix_size = riscv_socket_count(ms) * riscv_socket_count(ms);
        dist_matrix_size *= (3 * sizeof(uint32_t));
        dist_matrix = g_malloc0(dist_matrix_size);

        for (i = 0; i < riscv_socket_count(ms); i++) {
            for (j = 0; j < riscv_socket_count(ms); j++) {
                idx = (i * riscv_socket_count(ms) + j) * 3;
                dist_matrix[idx + 0] = cpu_to_be32(i);
                dist_matrix[idx + 1] = cpu_to_be32(j);
                dist_matrix[idx + 2] =
                    cpu_to_be32(ms->numa_state->nodes[i].distance[j]);
            }
        }

        qemu_fdt_add_subnode(ms->fdt, "/distance-map");
        qemu_fdt_setprop_string(ms->fdt, "/distance-map", "compatible",
                                "numa-distance-map-v1");
        qemu_fdt_setprop(ms->fdt, "/distance-map", "distance-matrix",
                         dist_matrix, dist_matrix_size);
    }
}

CpuInstanceProperties
riscv_numa_cpu_index_to_props(MachineState *ms, unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

int64_t riscv_numa_get_default_cpu_node_id(const MachineState *ms, int idx)
{
    int64_t nidx = 0;

    if (ms->numa_state->num_nodes > ms->smp.cpus) {
        error_report("Number of NUMA nodes (%d)"
                     " cannot exceed the number of available CPUs (%u).",
                     ms->numa_state->num_nodes, ms->smp.cpus);
        exit(EXIT_FAILURE);
    }
    if (ms->numa_state->num_nodes) {
        nidx = idx / (ms->smp.cpus / ms->numa_state->num_nodes);
        if (ms->numa_state->num_nodes <= nidx) {
            nidx = ms->numa_state->num_nodes - 1;
        }
    }

    return nidx;
}

const CPUArchIdList *riscv_numa_possible_cpu_arch_ids(MachineState *ms)
{
    int n;
    unsigned int max_cpus = ms->smp.max_cpus;

    if (ms->possible_cpus) {
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;
    for (n = 0; n < ms->possible_cpus->len; n++) {
        ms->possible_cpus->cpus[n].type = ms->cpu_type;
        ms->possible_cpus->cpus[n].arch_id = n;
        ms->possible_cpus->cpus[n].props.has_core_id = true;
        ms->possible_cpus->cpus[n].props.core_id = n;
    }

    return ms->possible_cpus;
}
