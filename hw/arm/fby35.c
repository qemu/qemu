/*
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This code is licensed under the GPL version 2 or later. See the COPYING
 * file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/arm/aspeed_soc.h"

#define TYPE_FBY35 MACHINE_TYPE_NAME("fby35")
OBJECT_DECLARE_SIMPLE_TYPE(Fby35State, FBY35);

struct Fby35State {
    MachineState parent_obj;

    MemoryRegion bmc_memory;
    MemoryRegion bmc_dram;
    MemoryRegion bmc_boot_rom;

    AspeedSoCState bmc;
};

#define FBY35_BMC_RAM_SIZE (2 * GiB)

static void fby35_bmc_init(Fby35State *s)
{
    memory_region_init(&s->bmc_memory, OBJECT(s), "bmc-memory", UINT64_MAX);
    memory_region_init_ram(&s->bmc_dram, OBJECT(s), "bmc-dram",
                           FBY35_BMC_RAM_SIZE, &error_abort);

    object_initialize_child(OBJECT(s), "bmc", &s->bmc, "ast2600-a3");
    object_property_set_int(OBJECT(&s->bmc), "ram-size", FBY35_BMC_RAM_SIZE,
                            &error_abort);
    object_property_set_link(OBJECT(&s->bmc), "memory", OBJECT(&s->bmc_memory),
                             &error_abort);
    object_property_set_link(OBJECT(&s->bmc), "dram", OBJECT(&s->bmc_dram),
                             &error_abort);
    object_property_set_int(OBJECT(&s->bmc), "hw-strap1", 0x000000C0,
                            &error_abort);
    object_property_set_int(OBJECT(&s->bmc), "hw-strap2", 0x00000003,
                            &error_abort);
    aspeed_soc_uart_set_chr(&s->bmc, ASPEED_DEV_UART5, serial_hd(0));
    qdev_realize(DEVICE(&s->bmc), NULL, &error_abort);

    aspeed_board_init_flashes(&s->bmc.fmc, "n25q00", 2, 0);
}

static void fby35_init(MachineState *machine)
{
    Fby35State *s = FBY35(machine);

    fby35_bmc_init(s);
}

static void fby35_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Meta Platforms fby35";
    mc->init = fby35_init;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->min_cpus = mc->max_cpus = mc->default_cpus = 2;
}

static const TypeInfo fby35_types[] = {
    {
        .name = MACHINE_TYPE_NAME("fby35"),
        .parent = TYPE_MACHINE,
        .class_init = fby35_class_init,
        .instance_size = sizeof(Fby35State),
    },
};

DEFINE_TYPES(fby35_types);
