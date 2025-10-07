/*
 * AMD/Xilinx Versal family SoC model.
 *
 * Copyright (c) 2018 Xilinx Inc.
 * Copyright (c) 2025 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef XLNX_VERSAL_H
#define XLNX_VERSAL_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "net/can_emu.h"
#include "hw/arm/xlnx-versal-version.h"

#define TYPE_XLNX_VERSAL_BASE "xlnx-versal-base"
OBJECT_DECLARE_TYPE(Versal, VersalClass, XLNX_VERSAL_BASE)

#define TYPE_XLNX_VERSAL "xlnx-versal"
#define TYPE_XLNX_VERSAL2 "xlnx-versal2"

struct Versal {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    GArray *intc;
    MemoryRegion mr_ps;

    struct {
        uint32_t clk_25mhz;
        uint32_t clk_125mhz;
        uint32_t gic;
    } phandle;

    struct {
        MemoryRegion *mr_ddr;
        CanBusState **canbus;
        void *fdt;
    } cfg;
};

struct VersalClass {
    SysBusDeviceClass parent;

    VersalVersion version;
};

static inline void versal_set_fdt(Versal *s, void *fdt)
{
    g_assert(!qdev_is_realized(DEVICE(s)));
    s->cfg.fdt = fdt;
}

void versal_fdt_add_memory_nodes(Versal *s, uint64_t ram_size);

DeviceState *versal_get_boot_cpu(Versal *s);
void versal_sdhci_plug_card(Versal *s, int sd_idx, BlockBackend *blk);
void versal_efuse_attach_drive(Versal *s, BlockBackend *blk);
void versal_bbram_attach_drive(Versal *s, BlockBackend *blk);
void versal_ospi_create_flash(Versal *s, int flash_idx, const char *flash_mdl,
                              BlockBackend *blk);

qemu_irq versal_get_reserved_irq(Versal *s, int idx, int *dtb_idx);
hwaddr versal_get_reserved_mmio_addr(Versal *s);

int versal_get_num_cpu(VersalVersion version);
int versal_get_num_can(VersalVersion version);
int versal_get_num_sdhci(VersalVersion version);

static inline const char *versal_get_class(VersalVersion version)
{
    switch (version) {
    case VERSAL_VER_VERSAL:
        return TYPE_XLNX_VERSAL;

    case VERSAL_VER_VERSAL2:
        return TYPE_XLNX_VERSAL2;

    default:
        g_assert_not_reached();
    }
}

#endif
