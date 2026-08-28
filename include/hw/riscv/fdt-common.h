/*
 * RISC-V board helpers for FDT generation.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_VIRT_FDT_H
#define RISCV_VIRT_FDT_H

#include "target/riscv/cpu.h"

void *riscv_create_board_device_tree(const char *model, const char *compatible,
                                     int *fdt_size);
void riscv_create_fdt_socket_memory(void *fdt, hwaddr addr, uint64_t size,
                                    int socket_id, bool numa_enabled);
void riscv_create_fdt_clint(void *fdt, hwaddr addr, uint64_t size,
                            uint32_t *intc_phandles, int num_harts);
void riscv_create_fdt_socket_clint(void *fdt, hwaddr addr, uint64_t size,
                                   int socket_id, uint32_t *intc_phandles,
                                   int num_harts, bool numa_enabled);
void riscv_fdt_create_cpu_socket_subnode(void *fdt,
                                         uint64_t timebase_frequency);
void riscv_create_fdt_socket_cpus(void *fdt, RISCVCPU *socket_harts,
                                  int socket_id, int num_harts_socket,
                                  int socket_hartid_base, uint32_t *phandle,
                                  uint32_t *intc_phandles, bool numa_enabled,
                                  bool is_32_bit);
void riscv_create_fdt_socket_cpu_sifive(void *fdt, char *clust_name,
                                        int cpu_id, int socket_id,
                                        int socket_hartid_base,
                                        uint32_t *phandle,
                                        uint32_t *intc_phandles);
void riscv_create_fdt_plic(void *fdt, hwaddr addr, uint64_t size,
                           uint32_t plic_phandle, uint32_t int_cells,
                           uint32_t addr_cells, uint32_t *plic_cells,
                           uint32_t cells_size, uint32_t ndev_sources,
                           bool numa_enabled, int socket);
void riscv_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name);
void riscv_create_fdt_flash(void *fdt, hwaddr flashbase, hwaddr flashsize);
void riscv_create_fdt_syscon(void *fdt, uint32_t *next_phandle,
                             hwaddr addr, hwaddr size,
                             uint32_t reboot, uint32_t poweroff,
                             bool sifive_test_compat);
#endif
