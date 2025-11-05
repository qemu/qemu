/*
 * OpenPOWER Romulus
 *
 * Copyright 2016 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/aspeed.h"
#include "hw/arm/aspeed_soc.h"

/* Romulus hardware value: 0xF10AD206 */
#define ROMULUS_BMC_HW_STRAP1 (                                         \
        AST2500_HW_STRAP1_DEFAULTS |                                    \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_AST2500_HW_STRAP_ACPI_ENABLE |                              \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_MASTER))

static void romulus_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;

    /*
     * The romulus board expects Epson RX8900 I2C RTC but a ds1338 is
     * good enough
     */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 11), "ds1338", 0x32);
}

static void aspeed_machine_romulus_class_init(ObjectClass *oc,
                                              const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "OpenPOWER Romulus BMC (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = ROMULUS_BMC_HW_STRAP1;
    amc->fmc_model = "n25q256a";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs    = 2;
    amc->i2c_init  = romulus_bmc_i2c_init;
    mc->default_ram_size       = 512 * MiB;
    aspeed_machine_class_init_cpus_defaults(mc);
};

static const TypeInfo aspeed_ast2500_romulus_types[] = {
     {
        .name          = MACHINE_TYPE_NAME("romulus-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_romulus_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2500_romulus_types)
