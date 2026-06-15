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
#endif
