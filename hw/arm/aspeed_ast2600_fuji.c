/*
 * Facebook Fuji
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
#include "hw/i2c/i2c_mux_pca954x.h"
#include "hw/sensor/tmp105.h"
#include "hw/nvram/eeprom_at24c.h"

#define TYPE_LM75 TYPE_TMP105
#define TYPE_TMP75 TYPE_TMP105
#define TYPE_TMP422 "tmp422"

/* Fuji hardware value */
#define FUJI_BMC_HW_STRAP1    0x00000000
#define FUJI_BMC_HW_STRAP2    0x00000000
#define FUJI_BMC_RAM_SIZE ASPEED_RAM_SIZE(2 * GiB)

static void get_pca9548_channels(I2CBus *bus, uint8_t mux_addr,
                                 I2CBus **channels)
{
    I2CSlave *mux = i2c_slave_create_simple(bus, "pca9548", mux_addr);
    for (int i = 0; i < 8; i++) {
        channels[i] = pca954x_i2c_get_bus(mux, i);
    }
}

static void fuji_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;
    I2CBus *i2c[144] = {};

    for (int i = 0; i < 16; i++) {
        i2c[i] = aspeed_i2c_get_bus(&soc->i2c, i);
    }
    I2CBus *i2c180 = i2c[2];
    I2CBus *i2c480 = i2c[8];
    I2CBus *i2c600 = i2c[11];

    get_pca9548_channels(i2c180, 0x70, &i2c[16]);
    get_pca9548_channels(i2c480, 0x70, &i2c[24]);
    /* NOTE: The device tree skips [32, 40) in the alias numbering */
    get_pca9548_channels(i2c600, 0x77, &i2c[40]);
    get_pca9548_channels(i2c[24], 0x71, &i2c[48]);
    get_pca9548_channels(i2c[25], 0x72, &i2c[56]);
    get_pca9548_channels(i2c[26], 0x76, &i2c[64]);
    get_pca9548_channels(i2c[27], 0x76, &i2c[72]);
    for (int i = 0; i < 8; i++) {
        get_pca9548_channels(i2c[40 + i], 0x76, &i2c[80 + i * 8]);
    }

    i2c_slave_create_simple(i2c[17], TYPE_LM75, 0x4c);
    i2c_slave_create_simple(i2c[17], TYPE_LM75, 0x4d);

    /*
     * EEPROM 24c64 size is 64Kbits or 8 Kbytes
     *        24c02 size is 2Kbits or 256 bytes
     */
    at24c_eeprom_init(i2c[19], 0x52, 8 * KiB);
    at24c_eeprom_init(i2c[20], 0x50, 256);
    at24c_eeprom_init(i2c[22], 0x52, 256);

    i2c_slave_create_simple(i2c[3], TYPE_LM75, 0x48);
    i2c_slave_create_simple(i2c[3], TYPE_LM75, 0x49);
    i2c_slave_create_simple(i2c[3], TYPE_LM75, 0x4a);
    i2c_slave_create_simple(i2c[3], TYPE_TMP422, 0x4c);

    at24c_eeprom_init(i2c[8], 0x51, 8 * KiB);
    i2c_slave_create_simple(i2c[8], TYPE_LM75, 0x4a);

    i2c_slave_create_simple(i2c[50], TYPE_LM75, 0x4c);
    at24c_eeprom_init(i2c[50], 0x52, 8 * KiB);
    i2c_slave_create_simple(i2c[51], TYPE_TMP75, 0x48);
    i2c_slave_create_simple(i2c[52], TYPE_TMP75, 0x49);

    i2c_slave_create_simple(i2c[59], TYPE_TMP75, 0x48);
    i2c_slave_create_simple(i2c[60], TYPE_TMP75, 0x49);

    at24c_eeprom_init(i2c[65], 0x53, 8 * KiB);
    i2c_slave_create_simple(i2c[66], TYPE_TMP75, 0x49);
    i2c_slave_create_simple(i2c[66], TYPE_TMP75, 0x48);
    at24c_eeprom_init(i2c[68], 0x52, 8 * KiB);
    at24c_eeprom_init(i2c[69], 0x52, 8 * KiB);
    at24c_eeprom_init(i2c[70], 0x52, 8 * KiB);
    at24c_eeprom_init(i2c[71], 0x52, 8 * KiB);

    at24c_eeprom_init(i2c[73], 0x53, 8 * KiB);
    i2c_slave_create_simple(i2c[74], TYPE_TMP75, 0x49);
    i2c_slave_create_simple(i2c[74], TYPE_TMP75, 0x48);
    at24c_eeprom_init(i2c[76], 0x52, 8 * KiB);
    at24c_eeprom_init(i2c[77], 0x52, 8 * KiB);
    at24c_eeprom_init(i2c[78], 0x52, 8 * KiB);
    at24c_eeprom_init(i2c[79], 0x52, 8 * KiB);
    at24c_eeprom_init(i2c[28], 0x50, 256);

    for (int i = 0; i < 8; i++) {
        at24c_eeprom_init(i2c[81 + i * 8], 0x56, 64 * KiB);
        i2c_slave_create_simple(i2c[82 + i * 8], TYPE_TMP75, 0x48);
        i2c_slave_create_simple(i2c[83 + i * 8], TYPE_TMP75, 0x4b);
        i2c_slave_create_simple(i2c[84 + i * 8], TYPE_TMP75, 0x4a);
    }
}

static void aspeed_machine_fuji_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc = "Facebook Fuji BMC (Cortex-A7)";
    amc->soc_name = "ast2600-a3";
    amc->hw_strap1 = FUJI_BMC_HW_STRAP1;
    amc->hw_strap2 = FUJI_BMC_HW_STRAP2;
    amc->fmc_model = "mx66l1g45g";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs = 2;
    amc->macs_mask = ASPEED_MAC3_ON;
    amc->i2c_init = fuji_bmc_i2c_init;
    amc->uart_default = ASPEED_DEV_UART1;
    mc->default_ram_size = FUJI_BMC_RAM_SIZE;
    aspeed_machine_class_init_cpus_defaults(mc);
};

static const TypeInfo aspeed_ast2600_fuji_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("fuji-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_fuji_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2600_fuji_types)
