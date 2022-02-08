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
#include "qemu/error-report.h"
#include "sysemu/qtest.h"

static void mcimx7d_sabre_init(MachineState *machine)
{
    static struct arm_boot_info boot_info;
    FslIMX7State *s;
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
        .psci_conduit = QEMU_PSCI_CONDUIT_SMC,
    };

    s = FSL_IMX7(object_new(TYPE_FSL_IMX7));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(s));
    qdev_realize(DEVICE(s), NULL, &error_fatal);

    memory_region_add_subregion(get_system_memory(), FSL_IMX7_MMDC_ADDR,
                                machine->ram);

    for (i = 0; i < FSL_IMX7_NUM_USDHCS; i++) {
        BusState *bus;
        DeviceState *carddev;
        DriveInfo *di;
        BlockBackend *blk;

        di = drive_get(IF_SD, 0, i);
        blk = di ? blk_by_legacy_dinfo(di) : NULL;
        bus = qdev_get_child_bus(DEVICE(&s->usdhc[i]), "sd-bus");
        carddev = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
        qdev_realize_and_unref(carddev, bus, &error_fatal);
    }

    if (!qtest_enabled()) {
        arm_load_kernel(&s->cpu[0], machine, &boot_info);
    }
}

static void mcimx7d_sabre_machine_init(MachineClass *mc)
{
    mc->desc = "Freescale i.MX7 DUAL SABRE (Cortex-A7)";
    mc->init = mcimx7d_sabre_init;
    mc->max_cpus = FSL_IMX7_NUM_CPUS;
    mc->default_ram_id = "mcimx7d-sabre.ram";
}
DEFINE_MACHINE("mcimx7d-sabre", mcimx7d_sabre_machine_init)
