/*
 * OpenPOWER Palmetto BMC
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "hw/arm/arm.h"
#include "hw/arm/ast2400.h"
#include "hw/boards.h"
#include "qemu/log.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"

static struct arm_boot_info palmetto_bmc_binfo = {
    .loader_start = AST2400_SDRAM_BASE,
    .board_id = 0,
    .nb_cpus = 1,
};

typedef struct PalmettoBMCState {
    AST2400State soc;
    MemoryRegion ram;
} PalmettoBMCState;

static void palmetto_bmc_init_flashes(AspeedSMCState *s, const char *flashtype,
                                      Error **errp)
{
    int i ;

    for (i = 0; i < s->num_cs; ++i) {
        AspeedSMCFlash *fl = &s->flashes[i];
        DriveInfo *dinfo = drive_get_next(IF_MTD);
        qemu_irq cs_line;

        /*
         * FIXME: check that we are not using a flash module exceeding
         * the controller segment size
         */
        fl->flash = ssi_create_slave_no_init(s->spi, flashtype);
        if (dinfo) {
            qdev_prop_set_drive(fl->flash, "drive", blk_by_legacy_dinfo(dinfo),
                                errp);
        }
        qdev_init_nofail(fl->flash);

        cs_line = qdev_get_gpio_in_named(fl->flash, SSI_GPIO_CS, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(s), i + 1, cs_line);
    }
}

static void palmetto_bmc_init(MachineState *machine)
{
    PalmettoBMCState *bmc;

    bmc = g_new0(PalmettoBMCState, 1);
    object_initialize(&bmc->soc, (sizeof(bmc->soc)), TYPE_AST2400);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(&bmc->soc),
                              &error_abort);

    memory_region_allocate_system_memory(&bmc->ram, NULL, "ram", ram_size);
    memory_region_add_subregion(get_system_memory(), AST2400_SDRAM_BASE,
                                &bmc->ram);
    object_property_add_const_link(OBJECT(&bmc->soc), "ram", OBJECT(&bmc->ram),
                                   &error_abort);
    object_property_set_int(OBJECT(&bmc->soc), 0x120CE416, "hw-strap1",
                            &error_abort);
    object_property_set_bool(OBJECT(&bmc->soc), true, "realized",
                             &error_abort);

    palmetto_bmc_init_flashes(&bmc->soc.smc, "n25q256a", &error_abort);
    palmetto_bmc_init_flashes(&bmc->soc.spi, "mx25l25635e", &error_abort);

    palmetto_bmc_binfo.kernel_filename = machine->kernel_filename;
    palmetto_bmc_binfo.initrd_filename = machine->initrd_filename;
    palmetto_bmc_binfo.kernel_cmdline = machine->kernel_cmdline;
    palmetto_bmc_binfo.ram_size = ram_size;
    arm_load_kernel(ARM_CPU(first_cpu), &palmetto_bmc_binfo);
}

static void palmetto_bmc_machine_init(MachineClass *mc)
{
    mc->desc = "OpenPOWER Palmetto BMC";
    mc->init = palmetto_bmc_init;
    mc->max_cpus = 1;
    mc->no_sdcard = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;
    mc->no_parallel = 1;
}

DEFINE_MACHINE("palmetto-bmc", palmetto_bmc_machine_init);
