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
    int i;
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

    object_property_set_bool(OBJECT(&s->soc), true, "realized", &error_fatal);

    /* Create and plug in the SD cards */
    for (i = 0; i < XLNX_ZYNQMP_NUM_SDHCI; i++) {
        BusState *bus;
        DriveInfo *di = drive_get_next(IF_SD);
        BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
        DeviceState *carddev;
        char *bus_name;

        bus_name = g_strdup_printf("sd-bus%d", i);
        bus = qdev_get_child_bus(DEVICE(&s->soc), bus_name);
        g_free(bus_name);
        if (!bus) {
            error_report("No SD bus found for SD card %d", i);
            exit(1);
        }
        carddev = qdev_create(bus, TYPE_SD_CARD);
        qdev_prop_set_drive(carddev, "drive", blk, &error_fatal);
        object_property_set_bool(OBJECT(carddev), true, "realized",
                                 &error_fatal);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_SPIS; i++) {
        SSIBus *spi_bus;
        DeviceState *flash_dev;
        qemu_irq cs_line;
        gchar *bus_name = g_strdup_printf("spi%d", i);

        spi_bus = (SSIBus *)qdev_get_child_bus(DEVICE(&s->soc), bus_name);
        g_free(bus_name);

        flash_dev = ssi_create_slave(spi_bus, "sst25wf080");
        cs_line = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->soc.spi[i]), 1, cs_line);
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
