/*
 * ASPEED AST10x0 EVB
 *
 * Copyright 2016 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/aspeed.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/qdev-clock.h"
#include "system/system.h"
#include "hw/i2c/smbus_eeprom.h"

#define AST1030_INTERNAL_FLASH_SIZE (1024 * 1024)
/* Main SYSCLK frequency in Hz (200MHz) */
#define SYSCLK_FRQ 200000000ULL

static void aspeed_minibmc_machine_init(MachineState *machine)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(machine);
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(machine);
    Clock *sysclk;

    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);

    bmc->soc = ASPEED_SOC(object_new(amc->soc_name));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(bmc->soc));
    object_unref(OBJECT(bmc->soc));
    qdev_connect_clock_in(DEVICE(bmc->soc), "sysclk", sysclk);

    object_property_set_link(OBJECT(bmc->soc), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    aspeed_connect_serial_hds_to_uarts(bmc);
    qdev_realize(DEVICE(bmc->soc), NULL, &error_abort);

    if (defaults_enabled()) {
        aspeed_board_init_flashes(&bmc->soc->fmc,
                            bmc->fmc_model ? bmc->fmc_model : amc->fmc_model,
                            amc->num_cs,
                            0);

        aspeed_board_init_flashes(&bmc->soc->spi[0],
                            bmc->spi_model ? bmc->spi_model : amc->spi_model,
                            amc->num_cs, amc->num_cs);

        aspeed_board_init_flashes(&bmc->soc->spi[1],
                            bmc->spi_model ? bmc->spi_model : amc->spi_model,
                            amc->num_cs, (amc->num_cs * 2));
    }

    if (amc->i2c_init) {
        amc->i2c_init(bmc);
    }

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0,
                       AST1030_INTERNAL_FLASH_SIZE);
}

static void ast1030_evb_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;

    /* U10 24C08 connects to SDA/SCL Group 1 by default */
    uint8_t *eeprom_buf = g_malloc0(32 * 1024);
    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 0), 0x50, eeprom_buf);

    /* U11 LM75 connects to SDA/SCL Group 2 by default */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 1), "tmp105", 0x4d);
}

static void aspeed_minibmc_machine_ast1030_evb_class_init(ObjectClass *oc,
                                                          const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc = "Aspeed AST1030 MiniBMC (Cortex-M4)";
    amc->soc_name = "ast1030-a1";
    amc->hw_strap1 = 0;
    amc->hw_strap2 = 0;
    mc->init = aspeed_minibmc_machine_init;
    amc->i2c_init = ast1030_evb_i2c_init;
    mc->default_ram_size = 0;
    amc->fmc_model = "w25q80bl";
    amc->spi_model = "w25q256";
    amc->num_cs = 2;
    amc->macs_mask = 0;
    aspeed_machine_class_init_cpus_defaults(mc);
}

static const TypeInfo aspeed_ast10x0_evb_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("ast1030-evb"),
        .parent         = TYPE_ASPEED_MACHINE,
        .class_init     = aspeed_minibmc_machine_ast1030_evb_class_init,
        .interfaces     = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast10x0_evb_types)
