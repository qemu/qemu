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
#include "e500.h"
#include "sysemu/device_tree.h"
#include "hw/ppc/openpic.h"
#include "qemu/error-report.h"
#include "cpu.h"

static void mpc8544ds_fixup_devtree(void *fdt)
{
    const char model[] = "MPC8544DS";
    const char compatible[] = "MPC8544DS\0MPC85xxDS";

    qemu_fdt_setprop(fdt, "/", "model", model, sizeof(model));
    qemu_fdt_setprop(fdt, "/", "compatible", compatible,
                     sizeof(compatible));
}

static void mpc8544ds_init(MachineState *machine)
{
    if (machine->ram_size > 0xc0000000) {
        error_report("The MPC8544DS board only supports up to 3GB of RAM");
        exit(1);
    }

    ppce500_init(machine);
}

static void e500plat_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PPCE500MachineClass *pmc = PPCE500_MACHINE_CLASS(oc);

    pmc->pci_first_slot = 0x11;
    pmc->pci_nr_slots = 2;
    pmc->fixup_devtree = mpc8544ds_fixup_devtree;
    pmc->mpic_version = OPENPIC_MODEL_FSL_MPIC_20;
    pmc->ccsrbar_base = 0xE0000000ULL;
    pmc->pci_mmio_base = 0xC0000000ULL;
    pmc->pci_mmio_bus_base = 0xC0000000ULL;
    pmc->pci_pio_base = 0xE1000000ULL;
    pmc->spin_base = 0xEF000000ULL;

    mc->desc = "mpc8544ds";
    mc->init = mpc8544ds_init;
    mc->max_cpus = 15;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("e500v2_v30");
    mc->default_ram_id = "mpc8544ds.ram";
}

#define TYPE_MPC8544DS_MACHINE  MACHINE_TYPE_NAME("mpc8544ds")

static const TypeInfo mpc8544ds_info = {
    .name          = TYPE_MPC8544DS_MACHINE,
    .parent        = TYPE_PPCE500_MACHINE,
    .class_init    = e500plat_machine_class_init,
};

static void mpc8544ds_register_types(void)
{
    type_register_static(&mpc8544ds_info);
}

type_init(mpc8544ds_register_types)
