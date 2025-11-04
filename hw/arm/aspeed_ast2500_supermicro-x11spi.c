/*
 * Supermicro X11 SPI
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
#include "hw/i2c/smbus_eeprom.h"

/* TODO: Find the actual hardware value */
#define SUPERMICRO_X11SPI_BMC_HW_STRAP1 (                               \
        AST2500_HW_STRAP1_DEFAULTS |                                    \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_HW_STRAP_SPI_WIDTH |                                        \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_M_S_EN))

static void supermicro_x11spi_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;
    DeviceState *dev;
    uint8_t *eeprom_buf = g_malloc0(32 * 1024);

    /*
     * The palmetto platform expects a ds3231 RTC but a ds1338 is
     * enough to provide basic RTC features. Alarms will be missing
     */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 0), "ds1338", 0x68);

    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 0), 0x50,
                          eeprom_buf);

    /* add a TMP423 temperature sensor */
    dev = DEVICE(i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2),
                                         "tmp423", 0x4c));
    object_property_set_int(OBJECT(dev), "temperature0", 31000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature1", 28000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature2", 20000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature3", 110000, &error_abort);
}

static void aspeed_machine_supermicro_x11spi_bmc_class_init(ObjectClass *oc,
                                                            const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Supermicro X11 SPI BMC (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = SUPERMICRO_X11SPI_BMC_HW_STRAP1;
    amc->fmc_model = "mx25l25635e";
    amc->spi_model = "mx25l25635e";
    amc->num_cs    = 1;
    amc->macs_mask = ASPEED_MAC0_ON | ASPEED_MAC1_ON;
    amc->i2c_init  = supermicro_x11spi_bmc_i2c_init;
    mc->default_ram_size = 512 * MiB;
    aspeed_machine_class_init_cpus_defaults(mc);
}

static const TypeInfo aspeed_ast2500_supermicro_x11spi_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("supermicro-x11spi-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_supermicro_x11spi_bmc_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2500_supermicro_x11spi_types)
