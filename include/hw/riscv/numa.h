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

#ifndef RISCV_NUMA_H
#define RISCV_NUMA_H

#include "hw/boards.h"
#include "hw/sysbus.h"
#include "sysemu/numa.h"

/**
 * riscv_socket_count:
 * @ms: pointer to machine state
 *
 * Returns: number of sockets for a numa system and 1 for a non-numa system
 */
int riscv_socket_count(const MachineState *ms);

/**
 * riscv_socket_first_hartid:
 * @ms: pointer to machine state
 * @socket_id: socket index
 *
 * Returns: first hartid for a valid socket and -1 for an invalid socket
 */
int riscv_socket_first_hartid(const MachineState *ms, int socket_id);

/**
 * riscv_socket_last_hartid:
 * @ms: pointer to machine state
 * @socket_id: socket index
 *
 * Returns: last hartid for a valid socket and -1 for an invalid socket
 */
int riscv_socket_last_hartid(const MachineState *ms, int socket_id);

/**
 * riscv_socket_hart_count:
 * @ms: pointer to machine state
 * @socket_id: socket index
 *
 * Returns: number of harts for a valid socket and -1 for an invalid socket
 */
int riscv_socket_hart_count(const MachineState *ms, int socket_id);

/**
 * riscv_socket_mem_offset:
 * @ms: pointer to machine state
 * @socket_id: socket index
 *
 * Returns: offset of ram belonging to given socket
 */
uint64_t riscv_socket_mem_offset(const MachineState *ms, int socket_id);

/**
 * riscv_socket_mem_size:
 * @ms: pointer to machine state
 * @socket_id: socket index
 *
 * Returns: size of ram belonging to given socket
 */
uint64_t riscv_socket_mem_size(const MachineState *ms, int socket_id);

/**
 * riscv_socket_check_hartids:
 * @ms: pointer to machine state
 * @socket_id: socket index
 *
 * Returns: true if hardids belonging to given socket are contiguous else false
 */
bool riscv_socket_check_hartids(const MachineState *ms, int socket_id);

/**
 * riscv_socket_fdt_write_id:
 * @ms: pointer to machine state
 * @socket_id: socket index
 *
 * Write NUMA node-id FDT property in MachineState->fdt
 */
void riscv_socket_fdt_write_id(const MachineState *ms, const char *node_name,
                               int socket_id);

/**
 * riscv_socket_fdt_write_distance_matrix:
 * @ms: pointer to machine state
 * @socket_id: socket index
 *
 * Write NUMA distance matrix in MachineState->fdt
 */
void riscv_socket_fdt_write_distance_matrix(const MachineState *ms);

CpuInstanceProperties
riscv_numa_cpu_index_to_props(MachineState *ms, unsigned cpu_index);

int64_t riscv_numa_get_default_cpu_node_id(const MachineState *ms, int idx);

const CPUArchIdList *riscv_numa_possible_cpu_arch_ids(MachineState *ms);

#endif /* RISCV_NUMA_H */
