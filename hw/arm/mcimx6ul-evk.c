/*
 * Copyright (c) 2018 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * MCIMX6UL_EVK Board System emulation.
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates a mcimx6ul_evk board, with a Freescale
 * i.MX6ul SoC
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/fsl-imx6ul.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"

static void mcimx6ul_evk_init(MachineState *machine)
{
    static struct arm_boot_info boot_info;
    FslIMX6ULState *s;
    int i;

    if (machine->ram_size > FSL_IMX6UL_MMDC_SIZE) {
        error_report("RAM size " RAM_ADDR_FMT " above max supported (%08x)",
                     machine->ram_size, FSL_IMX6UL_MMDC_SIZE);
        exit(1);
    }

    boot_info = (struct arm_boot_info) {
        .loader_start = FSL_IMX6UL_MMDC_ADDR,
        .board_id = -1,
        .ram_size = machine->ram_size,
        .psci_conduit = QEMU_PSCI_CONDUIT_SMC,
    };

    s = FSL_IMX6UL(object_new(TYPE_FSL_IMX6UL));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(s));
    object_property_set_uint(OBJECT(s), "fec1-phy-num", 2, &error_fatal);
    object_property_set_uint(OBJECT(s), "fec2-phy-num", 1, &error_fatal);
    object_property_set_bool(OBJECT(s), "fec1-phy-connected", false,
                             &error_fatal);
    qdev_realize(DEVICE(s), NULL, &error_fatal);

    memory_region_add_subregion(get_system_memory(), FSL_IMX6UL_MMDC_ADDR,
                                machine->ram);

    for (i = 0; i < FSL_IMX6UL_NUM_USDHCS; i++) {
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
        arm_load_kernel(&s->cpu, machine, &boot_info);
    }
}

static void mcimx6ul_evk_machine_init(MachineClass *mc)
{
    mc->desc = "Freescale i.MX6UL Evaluation Kit (Cortex-A7)";
    mc->init = mcimx6ul_evk_init;
    mc->max_cpus = FSL_IMX6UL_NUM_CPUS;
    mc->default_ram_id = "mcimx6ul-evk.ram";
}
DEFINE_MACHINE("mcimx6ul-evk", mcimx6ul_evk_machine_init)
