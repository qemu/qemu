/*
 * RISC-V board helpers for FDT generation.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "qemu/error-report.h"
#include "system/device_tree.h"
#include "hw/core/boards.h"
#include "hw/riscv/fdt-common.h"

void *create_board_device_tree(const char *model, const char *compatible,
                               int *fdt_size)
{
    void *fdt = create_device_tree(fdt_size);

    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", model);
    qemu_fdt_setprop_string(fdt, "/", "compatible", compatible);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    return fdt;
}

void create_fdt_socket_memory(void *fdt, hwaddr addr, uint64_t size,
                              int socket_id, bool numa_enabled)
{
    g_autofree char *mem_name = g_strdup_printf("/memory@%"HWADDR_PRIx, addr);

    qemu_fdt_add_subnode(fdt, mem_name);
    qemu_fdt_setprop_sized_cells(fdt, mem_name, "reg", 2, addr, 2, size);
    qemu_fdt_setprop_string(fdt, mem_name, "device_type", "memory");

    if (numa_enabled) {
        qemu_fdt_setprop_cell(fdt, mem_name, "numa-node-id", socket_id);
    }
}
