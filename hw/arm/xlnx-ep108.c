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

#include "hw/arm/xlnx-zynqmp.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"

typedef struct XlnxEP108 {
    XlnxZynqMPState soc;
    MemoryRegion ddr_ram;
} XlnxEP108;

/* Max 2GB RAM */
#define EP108_MAX_RAM_SIZE 0x80000000ull

static struct arm_boot_info xlnx_ep108_binfo;

static void xlnx_ep108_init(MachineState *machine)
{
    XlnxEP108 *s = g_new0(XlnxEP108, 1);
    Error *err = NULL;

    object_initialize(&s->soc, sizeof(s->soc), TYPE_XLNX_ZYNQMP);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(&s->soc),
                              &error_abort);

    object_property_set_bool(OBJECT(&s->soc), true, "realized", &err);
    if (err) {
        error_report("%s", error_get_pretty(err));
        exit(1);
    }

    if (machine->ram_size > EP108_MAX_RAM_SIZE) {
        error_report("WARNING: RAM size " RAM_ADDR_FMT " above max supported, "
                     "reduced to %llx", machine->ram_size, EP108_MAX_RAM_SIZE);
        machine->ram_size = EP108_MAX_RAM_SIZE;
    }

    if (machine->ram_size <= 0x08000000) {
        qemu_log("WARNING: RAM size " RAM_ADDR_FMT " is small for EP108",
                 machine->ram_size);
    }

    memory_region_allocate_system_memory(&s->ddr_ram, NULL, "ddr-ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(), 0, &s->ddr_ram);

    xlnx_ep108_binfo.ram_size = machine->ram_size;
    xlnx_ep108_binfo.kernel_filename = machine->kernel_filename;
    xlnx_ep108_binfo.kernel_cmdline = machine->kernel_cmdline;
    xlnx_ep108_binfo.initrd_filename = machine->initrd_filename;
    xlnx_ep108_binfo.loader_start = 0;
    arm_load_kernel(s->soc.boot_cpu_ptr, &xlnx_ep108_binfo);
}

static QEMUMachine xlnx_ep108_machine = {
    .name = "xlnx-ep108",
    .desc = "Xilinx ZynqMP EP108 board",
    .init = xlnx_ep108_init,
};

static void xlnx_ep108_machine_init(void)
{
    qemu_register_machine(&xlnx_ep108_machine);
}

machine_init(xlnx_ep108_machine_init);
