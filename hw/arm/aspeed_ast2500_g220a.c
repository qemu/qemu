/*
 * Bytedance G220A
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

#define G220A_BMC_HW_STRAP1 (                                      \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_RESERVED28 |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_HW_STRAP_2ND_BOOT_WDT |                                     \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_LPC_RESET_PIN |                                    \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_MASTER) |                \
        SCU_AST2500_HW_STRAP_SET_AXI_AHB_RATIO(AXI_AHB_RATIO_2_1) |     \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_64M_DRAM) |                       \
        SCU_AST2500_HW_STRAP_RESERVED1)

static void g220a_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;
    DeviceState *dev;

    dev = DEVICE(i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 3),
                                         "emc1413", 0x4c));
    object_property_set_int(OBJECT(dev), "temperature0", 31000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature1", 28000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature2", 20000, &error_abort);

    dev = DEVICE(i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 12),
                                         "emc1413", 0x4c));
    object_property_set_int(OBJECT(dev), "temperature0", 31000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature1", 28000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature2", 20000, &error_abort);

    dev = DEVICE(i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 13),
                                         "emc1413", 0x4c));
    object_property_set_int(OBJECT(dev), "temperature0", 31000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature1", 28000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature2", 20000, &error_abort);

    static uint8_t eeprom_buf[2 * 1024] = {
            0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0xfe,
            0x01, 0x06, 0x00, 0xc9, 0x42, 0x79, 0x74, 0x65,
            0x64, 0x61, 0x6e, 0x63, 0x65, 0xc5, 0x47, 0x32,
            0x32, 0x30, 0x41, 0xc4, 0x41, 0x41, 0x42, 0x42,
            0xc4, 0x43, 0x43, 0x44, 0x44, 0xc4, 0x45, 0x45,
            0x46, 0x46, 0xc4, 0x48, 0x48, 0x47, 0x47, 0xc1,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa7,
    };
    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 4), 0x57,
                          eeprom_buf);
}

static void aspeed_machine_g220a_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Bytedance G220A BMC (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = G220A_BMC_HW_STRAP1;
    amc->fmc_model = "n25q512a";
    amc->spi_model = "mx25l25635e";
    amc->num_cs    = 2;
    amc->macs_mask  = ASPEED_MAC0_ON | ASPEED_MAC1_ON;
    amc->i2c_init  = g220a_bmc_i2c_init;
    mc->default_ram_size = 1024 * MiB;
    aspeed_machine_class_init_cpus_defaults(mc);
};

static const TypeInfo aspeed_ast2500_g220a_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("g220a-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_g220a_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2500_g220a_types)
