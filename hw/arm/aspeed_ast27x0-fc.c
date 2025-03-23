/*
 * ASPEED SoC 2700 family
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "system/block-backend.h"
#include "system/system.h"
#include "hw/arm/aspeed.h"
#include "hw/boards.h"
#include "hw/qdev-clock.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "hw/block/flash.h"
#include "hw/arm/aspeed_coprocessor.h"
#include "hw/arm/machines-qom.h"

#define TYPE_AST2700A1FC MACHINE_TYPE_NAME("ast2700fc")
OBJECT_DECLARE_SIMPLE_TYPE(Ast2700FCState, AST2700A1FC);

static struct arm_boot_info ast2700fc_board_info = {
    .board_id = -1, /* device-tree-only board */
};

struct Ast2700FCState {
    MachineState parent_obj;

    MemoryRegion ca35_memory;
    MemoryRegion ca35_dram;
    MemoryRegion ca35_boot_rom;
    MemoryRegion ssp_memory;
    MemoryRegion tsp_memory;

    Clock *ssp_sysclk;
    Clock *tsp_sysclk;

    Aspeed27x0SoCState ca35;
    Aspeed27x0CoprocessorState ssp;
    Aspeed27x0CoprocessorState tsp;
};

#define AST2700FC_BMC_RAM_SIZE (1 * GiB)
#define AST2700FC_CM4_DRAM_SIZE (32 * MiB)

#define AST2700FC_HW_STRAP1 0x000000C0
#define AST2700FC_HW_STRAP2 0x00000003
#define AST2700FC_FMC_MODEL "w25q01jvq"
#define AST2700FC_SPI_MODEL "w25q512jv"

static bool ast2700fc_ca35_init(MachineState *machine, Error **errp)
{
    Ast2700FCState *s = AST2700A1FC(machine);
    AspeedSoCState *soc;
    AspeedSoCClass *sc;
    const char *bios_name = NULL;
    BlockBackend *fmc0 = NULL;
    DeviceState *dev = NULL;
    uint64_t rom_size;

    object_initialize_child(OBJECT(s), "ca35", &s->ca35, "ast2700-a1");
    soc = ASPEED_SOC(&s->ca35);
    sc = ASPEED_SOC_GET_CLASS(soc);

    memory_region_init(&s->ca35_memory, OBJECT(&s->ca35), "ca35-memory",
                       UINT64_MAX);
    memory_region_add_subregion(get_system_memory(), 0, &s->ca35_memory);

    if (!memory_region_init_ram(&s->ca35_dram, OBJECT(&s->ca35), "ca35-dram",
                                AST2700FC_BMC_RAM_SIZE, errp)) {
        return false;
    }
    object_property_set_link(OBJECT(&s->ca35), "memory",
                             OBJECT(&s->ca35_memory), &error_abort);
    object_property_set_link(OBJECT(&s->ca35), "dram", OBJECT(&s->ca35_dram),
                             &error_abort);
    object_property_set_int(OBJECT(&s->ca35), "ram-size",
                            AST2700FC_BMC_RAM_SIZE, &error_abort);

    for (int i = 0; i < sc->macs_num; i++) {
        if (!qemu_configure_nic_device(DEVICE(&soc->ftgmac100[i]),
                                       true, NULL)) {
            break;
        }
    }
    object_property_set_int(OBJECT(&s->ca35), "hw-strap1",
                            AST2700FC_HW_STRAP1, &error_abort);
    object_property_set_int(OBJECT(&s->ca35), "hw-strap2",
                            AST2700FC_HW_STRAP2, &error_abort);
    aspeed_soc_uart_set_chr(soc->uart, ASPEED_DEV_UART12, sc->uarts_base,
                            sc->uarts_num, serial_hd(0));
    aspeed_soc_uart_set_chr(soc->uart, ASPEED_DEV_UART4, sc->uarts_base,
                            sc->uarts_num, serial_hd(1));
    aspeed_soc_uart_set_chr(soc->uart, ASPEED_DEV_UART7, sc->uarts_base,
                            sc->uarts_num, serial_hd(2));
    if (!qdev_realize(DEVICE(&s->ca35), NULL, errp)) {
        return false;
    }

    /*
     * AST2700 EVB has a LM75 temperature sensor on I2C bus 0 at address 0x4d.
     */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 0), "tmp105", 0x4d);

    aspeed_board_init_flashes(&soc->fmc, AST2700FC_FMC_MODEL, 2, 0);
    aspeed_board_init_flashes(&soc->spi[0], AST2700FC_SPI_MODEL, 1, 2);

    ast2700fc_board_info.ram_size = machine->ram_size;
    ast2700fc_board_info.loader_start = sc->memmap[ASPEED_DEV_SDRAM];

    dev = ssi_get_cs(soc->fmc.spi, 0);
    fmc0 = dev ? m25p80_get_blk(dev) : NULL;

    if (fmc0) {
        rom_size = memory_region_size(&soc->spi_boot);
        aspeed_install_boot_rom(soc, fmc0, &s->ca35_boot_rom, rom_size);
    }

    /* VBOOTROM */
    bios_name = machine->firmware ?: VBOOTROM_FILE_NAME;
    aspeed_load_vbootrom(soc, bios_name, errp);

    arm_load_kernel(ARM_CPU(first_cpu), machine, &ast2700fc_board_info);

    return true;
}

static bool ast2700fc_ssp_init(MachineState *machine, Error **errp)
{
    Ast2700FCState *s = AST2700A1FC(machine);
    AspeedSoCState *psp = ASPEED_SOC(&s->ca35);

    s->ssp_sysclk = clock_new(OBJECT(s), "SSP_SYSCLK");
    clock_set_hz(s->ssp_sysclk, 200000000ULL);

    object_initialize_child(OBJECT(s), "ssp", &s->ssp,
                            TYPE_ASPEED27X0SSP_COPROCESSOR);
    memory_region_init(&s->ssp_memory, OBJECT(&s->ssp), "ssp-memory",
                       UINT64_MAX);

    qdev_connect_clock_in(DEVICE(&s->ssp), "sysclk", s->ssp_sysclk);
    object_property_set_link(OBJECT(&s->ssp), "memory",
                             OBJECT(&s->ssp_memory), &error_abort);

    object_property_set_link(OBJECT(&s->ssp), "uart",
                             OBJECT(&psp->uart[4]), &error_abort);
    object_property_set_int(OBJECT(&s->ssp), "uart-dev", ASPEED_DEV_UART4,
                            &error_abort);
    object_property_set_link(OBJECT(&s->ssp), "sram",
                             OBJECT(&psp->sram), &error_abort);
    object_property_set_link(OBJECT(&s->ssp), "scu",
                             OBJECT(&psp->scu), &error_abort);
    if (!qdev_realize(DEVICE(&s->ssp), NULL, errp)) {
        return false;
    }

    return true;
}

static bool ast2700fc_tsp_init(MachineState *machine, Error **errp)
{
    Ast2700FCState *s = AST2700A1FC(machine);
    AspeedSoCState *psp = ASPEED_SOC(&s->ca35);

    s->tsp_sysclk = clock_new(OBJECT(s), "TSP_SYSCLK");
    clock_set_hz(s->tsp_sysclk, 200000000ULL);

    object_initialize_child(OBJECT(s), "tsp", &s->tsp,
                            TYPE_ASPEED27X0TSP_COPROCESSOR);
    memory_region_init(&s->tsp_memory, OBJECT(&s->tsp), "tsp-memory",
                       UINT64_MAX);

    qdev_connect_clock_in(DEVICE(&s->tsp), "sysclk", s->tsp_sysclk);
    object_property_set_link(OBJECT(&s->tsp), "memory",
                             OBJECT(&s->tsp_memory), &error_abort);

    object_property_set_link(OBJECT(&s->tsp), "uart",
                             OBJECT(&psp->uart[7]), &error_abort);
    object_property_set_int(OBJECT(&s->tsp), "uart-dev", ASPEED_DEV_UART7,
                            &error_abort);
    object_property_set_link(OBJECT(&s->tsp), "sram",
                             OBJECT(&psp->sram), &error_abort);
    object_property_set_link(OBJECT(&s->tsp), "scu",
                             OBJECT(&psp->scu), &error_abort);
    if (!qdev_realize(DEVICE(&s->tsp), NULL, errp)) {
        return false;
    }

    return true;
}

static void ast2700fc_init(MachineState *machine)
{
    ast2700fc_ca35_init(machine, &error_abort);
    ast2700fc_ssp_init(machine, &error_abort);
    ast2700fc_tsp_init(machine, &error_abort);
}

static void ast2700fc_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ast2700 full core support";
    mc->init = ast2700fc_init;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->min_cpus = mc->max_cpus = mc->default_cpus = 6;
}

static const TypeInfo ast2700fc_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("ast2700fc"),
        .parent         = TYPE_MACHINE,
        .class_init     = ast2700fc_class_init,
        .instance_size  = sizeof(Ast2700FCState),
        .interfaces     = aarch64_machine_interfaces,
    },
};

DEFINE_TYPES(ast2700fc_types)
