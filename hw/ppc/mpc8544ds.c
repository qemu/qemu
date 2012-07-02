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

#include "config.h"
#include "qemu-common.h"
#include "e500.h"
#include "../boards.h"
#include "device_tree.h"

static void mpc8544ds_fixup_devtree(PPCE500Params *params, void *fdt)
{
    const char model[] = "MPC8544DS";
    const char compatible[] = "MPC8544DS\0MPC85xxDS";

    qemu_devtree_setprop(fdt, "/", "model", model, sizeof(model));
    qemu_devtree_setprop(fdt, "/", "compatible", compatible,
                         sizeof(compatible));
}

static void mpc8544ds_init(ram_addr_t ram_size,
                           const char *boot_device,
                           const char *kernel_filename,
                           const char *kernel_cmdline,
                           const char *initrd_filename,
                           const char *cpu_model)
{
    PPCE500Params params = {
        .ram_size = ram_size,
        .boot_device = boot_device,
        .kernel_filename = kernel_filename,
        .kernel_cmdline = kernel_cmdline,
        .initrd_filename = initrd_filename,
        .cpu_model = cpu_model,
        .fixup_devtree = mpc8544ds_fixup_devtree,
    };

    ppce500_init(&params);
}


static QEMUMachine ppce500_machine = {
    .name = "mpc8544ds",
    .desc = "mpc8544ds",
    .init = mpc8544ds_init,
    .max_cpus = 15,
};

static void ppce500_machine_init(void)
{
    qemu_register_machine(&ppce500_machine);
}

machine_init(ppce500_machine_init);
