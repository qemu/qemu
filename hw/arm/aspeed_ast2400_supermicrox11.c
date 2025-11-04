/*
 * Supermicro X11
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
#define SUPERMICROX11_BMC_HW_STRAP1 (                                   \
        SCU_AST2400_HW_STRAP_DRAM_SIZE(DRAM_SIZE_128MB) |               \
        SCU_AST2400_HW_STRAP_DRAM_CONFIG(2) |                           \
        SCU_AST2400_HW_STRAP_ACPI_DIS |                                 \
        SCU_AST2400_HW_STRAP_SET_CLK_SOURCE(AST2400_CLK_48M_IN) |       \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_LPC_RESET_PIN |                                    \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_M_S_EN) |                \
        SCU_AST2400_HW_STRAP_SET_CPU_AHB_RATIO(AST2400_CPU_AHB_RATIO_2_1) | \
        SCU_HW_STRAP_SPI_WIDTH |                                        \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_16M_DRAM) |                       \
        SCU_AST2400_HW_STRAP_BOOT_MODE(AST2400_SPI_BOOT))

static void supermicrox11_bmc_i2c_init(AspeedMachineState *bmc)
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

static void aspeed_machine_supermicrox11_bmc_class_init(ObjectClass *oc,
                                                        const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Supermicro X11 BMC (ARM926EJ-S)";
    amc->soc_name  = "ast2400-a1";
    amc->hw_strap1 = SUPERMICROX11_BMC_HW_STRAP1;
    amc->fmc_model = "mx25l25635e";
    amc->spi_model = "mx25l25635e";
    amc->num_cs    = 1;
    amc->macs_mask = ASPEED_MAC0_ON | ASPEED_MAC1_ON;
    amc->i2c_init  = supermicrox11_bmc_i2c_init;
    mc->default_ram_size = 256 * MiB;
    aspeed_machine_class_init_cpus_defaults(mc);
}

static const TypeInfo aspeed_ast2400_supermicrox11_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("supermicrox11-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_supermicrox11_bmc_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2400_supermicrox11_types)
