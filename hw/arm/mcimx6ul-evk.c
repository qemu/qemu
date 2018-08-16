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
#include "qemu-common.h"
#include "hw/arm/fsl-imx6ul.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"

typedef struct {
    FslIMX6ULState soc;
    MemoryRegion ram;
} MCIMX6ULEVK;

static void mcimx6ul_evk_init(MachineState *machine)
{
    static struct arm_boot_info boot_info;
    MCIMX6ULEVK *s = g_new0(MCIMX6ULEVK, 1);
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
        .kernel_filename = machine->kernel_filename,
        .kernel_cmdline = machine->kernel_cmdline,
        .initrd_filename = machine->initrd_filename,
        .nb_cpus = smp_cpus,
    };

    object_initialize_child(OBJECT(machine), "soc", &s->soc,  sizeof(s->soc),
                            TYPE_FSL_IMX6UL, &error_fatal, NULL);

    object_property_set_bool(OBJECT(&s->soc), true, "realized", &error_fatal);

    memory_region_allocate_system_memory(&s->ram, NULL, "mcimx6ul-evk.ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(),
                                FSL_IMX6UL_MMDC_ADDR, &s->ram);

    for (i = 0; i < FSL_IMX6UL_NUM_USDHCS; i++) {
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
        arm_load_kernel(&s->soc.cpu[0], &boot_info);
    }
}

static void mcimx6ul_evk_machine_init(MachineClass *mc)
{
    mc->desc = "Freescale i.MX6UL Evaluation Kit (Cortex A7)";
    mc->init = mcimx6ul_evk_init;
    mc->max_cpus = FSL_IMX6UL_NUM_CPUS;
}
DEFINE_MACHINE("mcimx6ul-evk", mcimx6ul_evk_machine_init)
