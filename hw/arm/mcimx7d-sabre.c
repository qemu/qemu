/*
 * Copyright (c) 2018, Impinj, Inc.
 *
 * MCIMX7D_SABRE Board System emulation.
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates a mcimx7d_sabre board, with a Freescale
 * i.MX7 SoC
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/fsl-imx7.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"

typedef struct {
    FslIMX7State soc;
    MemoryRegion ram;
} MCIMX7Sabre;

static void mcimx7d_sabre_init(MachineState *machine)
{
    static struct arm_boot_info boot_info;
    MCIMX7Sabre *s = g_new0(MCIMX7Sabre, 1);
    int i;

    if (machine->ram_size > FSL_IMX7_MMDC_SIZE) {
        error_report("RAM size " RAM_ADDR_FMT " above max supported (%08x)",
                     machine->ram_size, FSL_IMX7_MMDC_SIZE);
        exit(1);
    }

    boot_info = (struct arm_boot_info) {
        .loader_start = FSL_IMX7_MMDC_ADDR,
        .board_id = -1,
        .ram_size = machine->ram_size,
        .nb_cpus = machine->smp.cpus,
    };

    object_initialize_child(OBJECT(machine), "soc",
                            &s->soc, sizeof(s->soc),
                            TYPE_FSL_IMX7, &error_fatal, NULL);
    object_property_set_bool(OBJECT(&s->soc), true, "realized", &error_fatal);

    memory_region_allocate_system_memory(&s->ram, NULL, "mcimx7d-sabre.ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(),
                                FSL_IMX7_MMDC_ADDR, &s->ram);

    for (i = 0; i < FSL_IMX7_NUM_USDHCS; i++) {
        BusState *bus;
        DeviceState *carddev;
        DriveInfo *di;
        BlockBackend *blk;

        di = drive_get_next(IF_SD);
        blk = di ? blk_by_legacy_dinfo(di) : NULL;
        bus = qdev_get_child_bus(DEVICE(&s->soc.usdhc[i]), "sd-bus");
        carddev = qdev_create(bus, TYPE_SD_CARD);
        qdev_prop_set_drive(carddev, "drive", blk, &error_fatal);
        object_property_set_bool(OBJECT(carddev), true,
                                 "realized", &error_fatal);
    }

    if (!qtest_enabled()) {
        arm_load_kernel(&s->soc.cpu[0], machine, &boot_info);
    }
}

static void mcimx7d_sabre_machine_init(MachineClass *mc)
{
    mc->desc = "Freescale i.MX7 DUAL SABRE (Cortex A7)";
    mc->init = mcimx7d_sabre_init;
    mc->max_cpus = FSL_IMX7_NUM_CPUS;
}
DEFINE_MACHINE("mcimx7d-sabre", mcimx7d_sabre_machine_init)
