/*
 * Xilinx ZynqMP EP108 board
 *
 * Copyright (C) 2015 Xilinx Inc
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "hw/arm/xlnx-zynqmp.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"

typedef struct XlnxEP108 {
    XlnxZynqMPState soc;
    MemoryRegion ddr_ram;
} XlnxEP108;

static struct arm_boot_info xlnx_ep108_binfo;

static void xlnx_ep108_init(MachineState *machine)
{
    XlnxEP108 *s = g_new0(XlnxEP108, 1);
    Error *err = NULL;
    uint64_t ram_size = machine->ram_size;

    /* Create the memory region to pass to the SoC */
    if (ram_size > XLNX_ZYNQMP_MAX_RAM_SIZE) {
        error_report("ERROR: RAM size 0x%" PRIx64 " above max supported of "
                     "0x%llx", ram_size,
                     XLNX_ZYNQMP_MAX_RAM_SIZE);
        exit(1);
    }

    if (ram_size < 0x08000000) {
        qemu_log("WARNING: RAM size 0x%" PRIx64 " is small for EP108",
                 ram_size);
    }

    memory_region_allocate_system_memory(&s->ddr_ram, NULL, "ddr-ram",
                                         ram_size);

    object_initialize(&s->soc, sizeof(s->soc), TYPE_XLNX_ZYNQMP);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(&s->soc),
                              &error_abort);

    object_property_set_link(OBJECT(&s->soc), OBJECT(&s->ddr_ram),
                         "ddr-ram", &error_abort);

    object_property_set_bool(OBJECT(&s->soc), true, "realized", &err);
    if (err) {
        error_report_err(err);
        exit(1);
    }

    xlnx_ep108_binfo.ram_size = ram_size;
    xlnx_ep108_binfo.kernel_filename = machine->kernel_filename;
    xlnx_ep108_binfo.kernel_cmdline = machine->kernel_cmdline;
    xlnx_ep108_binfo.initrd_filename = machine->initrd_filename;
    xlnx_ep108_binfo.loader_start = 0;
    arm_load_kernel(s->soc.boot_cpu_ptr, &xlnx_ep108_binfo);
}

static void xlnx_ep108_machine_init(MachineClass *mc)
{
    mc->desc = "Xilinx ZynqMP EP108 board";
    mc->init = xlnx_ep108_init;
}

DEFINE_MACHINE("xlnx-ep108", xlnx_ep108_machine_init)
