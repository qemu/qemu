/*
 * Nvidia GB200NVL
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
#include "hw/nvram/eeprom_at24c.h"

/* GB200NVL hardware value */
#define GB200NVL_BMC_HW_STRAP1 0x000000C0
#define GB200NVL_BMC_HW_STRAP2 0x00000003
#define GB200NVL_BMC_RAM_SIZE ASPEED_RAM_SIZE(1 * GiB)

static const uint8_t gb200nvl_bmc_fruid[] = {
    0x01, 0x00, 0x00, 0x01, 0x0b, 0x00, 0x00, 0xf3, 0x01, 0x0a, 0x19, 0x1f,
    0x0f, 0xe6, 0xc6, 0x4e, 0x56, 0x49, 0x44, 0x49, 0x41, 0xc5, 0x50, 0x33,
    0x38, 0x30, 0x39, 0xcd, 0x31, 0x35, 0x38, 0x33, 0x33, 0x32, 0x34, 0x38,
    0x30, 0x30, 0x31, 0x35, 0x30, 0xd2, 0x36, 0x39, 0x39, 0x2d, 0x31, 0x33,
    0x38, 0x30, 0x39, 0x2d, 0x30, 0x34, 0x30, 0x34, 0x2d, 0x36, 0x30, 0x30,
    0xc0, 0x01, 0x01, 0xd6, 0x4d, 0x41, 0x43, 0x3a, 0x20, 0x33, 0x43, 0x3a,
    0x36, 0x44, 0x3a, 0x36, 0x36, 0x3a, 0x31, 0x34, 0x3a, 0x43, 0x38, 0x3a,
    0x37, 0x41, 0xc1, 0x3b, 0x01, 0x09, 0x19, 0xc6, 0x4e, 0x56, 0x49, 0x44,
    0x49, 0x41, 0xc9, 0x50, 0x33, 0x38, 0x30, 0x39, 0x2d, 0x42, 0x4d, 0x43,
    0xd2, 0x36, 0x39, 0x39, 0x2d, 0x31, 0x33, 0x38, 0x30, 0x39, 0x2d, 0x30,
    0x34, 0x30, 0x34, 0x2d, 0x36, 0x30, 0x30, 0xc4, 0x41, 0x45, 0x2e, 0x31,
    0xcd, 0x31, 0x35, 0x38, 0x33, 0x33, 0x32, 0x34, 0x38, 0x30, 0x30, 0x31,
    0x35, 0x30, 0xc0, 0xc4, 0x76, 0x30, 0x2e, 0x31, 0xc1, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xb4, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff

};
static const size_t gb200nvl_bmc_fruid_len = sizeof(gb200nvl_bmc_fruid);

static void gb200nvl_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;
    I2CBus *i2c[15] = {};
    DeviceState *dev;
    for (int i = 0; i < sizeof(i2c) / sizeof(i2c[0]); i++) {
        if ((i == 11) || (i == 12) || (i == 13)) {
            continue;
        }
        i2c[i] = aspeed_i2c_get_bus(&soc->i2c, i);
    }

    /* Bus 5 Expander */
    aspeed_create_pca9554(soc, 4, 0x21);

    /* Mux I2c Expanders */
    i2c_slave_create_simple(i2c[5], "pca9546", 0x71);
    i2c_slave_create_simple(i2c[5], "pca9546", 0x72);
    i2c_slave_create_simple(i2c[5], "pca9546", 0x73);
    i2c_slave_create_simple(i2c[5], "pca9546", 0x75);
    i2c_slave_create_simple(i2c[5], "pca9546", 0x76);
    i2c_slave_create_simple(i2c[5], "pca9546", 0x77);

    /* Bus 10 */
    dev = DEVICE(aspeed_create_pca9554(soc, 9, 0x20));

    /* Set FPGA_READY */
    object_property_set_str(OBJECT(dev), "pin1", "high", &error_fatal);

    aspeed_create_pca9554(soc, 9, 0x21);
    at24c_eeprom_init(i2c[9], 0x50, 64 * KiB);
    at24c_eeprom_init(i2c[9], 0x51, 64 * KiB);

    /* Bus 11 */
    at24c_eeprom_init_rom(i2c[10], 0x50, 256, gb200nvl_bmc_fruid,
                          gb200nvl_bmc_fruid_len);
}

static void aspeed_machine_gb200nvl_class_init(ObjectClass *oc,
                                               const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Nvidia GB200NVL BMC (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = GB200NVL_BMC_HW_STRAP1;
    amc->hw_strap2 = GB200NVL_BMC_HW_STRAP2;
    amc->fmc_model = "mx66u51235f";
    amc->spi_model = "mx66u51235f";
    amc->num_cs    = 2;

    amc->spi2_model = "mx66u51235f";
    amc->num_cs2   = 1;
    amc->macs_mask = ASPEED_MAC0_ON | ASPEED_MAC1_ON;
    amc->i2c_init  = gb200nvl_bmc_i2c_init;
    mc->default_ram_size = GB200NVL_BMC_RAM_SIZE;
    aspeed_machine_class_init_cpus_defaults(mc);
    aspeed_machine_ast2600_class_emmc_init(oc);
}

static const TypeInfo aspeed_ast2600_gb200nvl_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("gb200nvl-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_gb200nvl_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2600_gb200nvl_types)
