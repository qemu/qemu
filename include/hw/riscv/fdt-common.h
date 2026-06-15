/*
 * RISC-V board helpers for FDT generation.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_VIRT_FDT_H
#define RISCV_VIRT_FDT_H

void *create_board_device_tree(const char *model, const char *compatible,
                               int *fdt_size);
void create_fdt_socket_memory(void *fdt, hwaddr addr, uint64_t size,
                              int socket_id, bool numa_enabled);
void create_fdt_clint(void *fdt, hwaddr addr, uint64_t size,
                      uint32_t *intc_phandles, int num_harts);
void create_fdt_socket_clint(void *fdt, hwaddr addr, uint64_t size,
                             int socket_id, uint32_t *intc_phandles,
                             int num_harts, bool numa_enabled);
#endif
