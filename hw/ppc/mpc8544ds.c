/*
 * Support for the PPC e500-based mpc8544ds board
 *
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "e500.h"
#include "hw/boards.h"
#include "sysemu/device_tree.h"
#include "hw/ppc/openpic.h"
#include "qemu/error-report.h"

static void mpc8544ds_fixup_devtree(PPCE500Params *params, void *fdt)
{
    const char model[] = "MPC8544DS";
    const char compatible[] = "MPC8544DS\0MPC85xxDS";

    qemu_fdt_setprop(fdt, "/", "model", model, sizeof(model));
    qemu_fdt_setprop(fdt, "/", "compatible", compatible,
                     sizeof(compatible));
}

static void mpc8544ds_init(MachineState *machine)
{
    PPCE500Params params = {
        .pci_first_slot = 0x11,
        .pci_nr_slots = 2,
        .fixup_devtree = mpc8544ds_fixup_devtree,
        .mpic_version = OPENPIC_MODEL_FSL_MPIC_20,
        .ccsrbar_base = 0xE0000000ULL,
        .pci_mmio_base = 0xC0000000ULL,
        .pci_mmio_bus_base = 0xC0000000ULL,
        .pci_pio_base = 0xE1000000ULL,
        .spin_base = 0xEF000000ULL,
    };

    if (machine->ram_size > 0xc0000000) {
        error_report("The MPC8544DS board only supports up to 3GB of RAM");
        exit(1);
    }

    ppce500_init(machine, &params);
}


static void ppce500_machine_init(MachineClass *mc)
{
    mc->desc = "mpc8544ds";
    mc->init = mpc8544ds_init;
    mc->max_cpus = 15;
}

DEFINE_MACHINE("mpc8544ds", ppce500_machine_init)
