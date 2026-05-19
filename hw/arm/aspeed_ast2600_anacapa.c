/*
 * Facebook Anacapa
 *
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/aspeed.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/i2c/i2c_mux_pca954x.h"
#include "hw/gpio/pca9552.h"
#include "hw/nvram/eeprom_at24c.h"

/* Anacapa hardware value */
#define ANACAPA_BMC_HW_STRAP1 0x00002002
#define ANACAPA_BMC_HW_STRAP2 0x00000000
#define ANACAPA_BMC_RAM_SIZE ASPEED_RAM_SIZE(2 * GiB)

static void anacapa_bmc_i2c_init(AspeedMachineState *bmc)
{
    /* Reference: aspeed-bmc-facebook-anacapa.dts */

    AspeedSoCState *soc = bmc->soc;
    I2CBus *i2c[16] = {};
    I2CSlave *i2c_mux;

    for (int i = 0; i < ARRAY_SIZE(i2c); i++) {
        i2c[i] = aspeed_i2c_get_bus(&soc->i2c, i);
    }

    /* &i2c0 */
    /* eeprom@50 */
    at24c_eeprom_init(i2c[0], 0x50, 256 * KiB);
    /* i2c-mux@70 */
    i2c_slave_create_simple(i2c[0], TYPE_PCA9546, 0x70);

    /* &i2c1 */
    /* eeprom@50 */
    at24c_eeprom_init(i2c[1], 0x50, 256 * KiB);
    /* i2c-mux@70 (PCA9546) — 4 channels, empty */
    i2c_slave_create_simple(i2c[1], TYPE_PCA9546, 0x70);

    /* &i2c4 */
    /* i2c-mux@70 (PCA9548) */
    i2c_slave_create_simple(i2c[4], TYPE_PCA9548, 0x70);

    /* &i2c6 */
    /* eeprom@50 */
    at24c_eeprom_init(i2c[6], 0x50, 32 * KiB);

    /* &i2c8 */
    /* i2c-mux@72 (PCA9546) */
    i2c_mux = i2c_slave_create_simple(i2c[8], TYPE_PCA9546, 0x72);

    /* i2c8mux ch0 */
    /* adc128d818@1f — no model */
    /* pca9555@22 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 0),
                            TYPE_PCA9552, 0x22);
    /* pca9555@24 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 0),
                            TYPE_PCA9552, 0x24);
    /* eeprom@50 */
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 0), 0x50, 16 * KiB);

    /* i2c8mux ch1 */
    /* pca9555@22 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 1),
                            TYPE_PCA9552, 0x22);
    /* pca9555@24 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 1),
                            TYPE_PCA9552, 0x24);
    /* eeprom@50 */
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 1), 0x50, 16 * KiB);

    /* &i2c9 */
    /* eeprom@50 */
    at24c_eeprom_init(i2c[9], 0x50, 16 * KiB);
    /* eeprom@56 */
    at24c_eeprom_init(i2c[9], 0x56, 8 * KiB);

    /* &i2c10 */
    /* i2c-mux@71 (PCA9548) */
    i2c_mux = i2c_slave_create_simple(i2c[10], TYPE_PCA9548, 0x71);

    /* i2c10mux ch5 */
    /* pca9555@22*/
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 5),
                            TYPE_PCA9552, 0x22);
    /* eeprom@52 */
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 5), 0x52, 32 * KiB);

    /* &i2c11 */
    /* i2c-mux@71 (PCA9548) */
    i2c_mux = i2c_slave_create_simple(i2c[11], TYPE_PCA9548, 0x71);

    /* i2c11mux ch0-ch4 — empty */

    /* i2c11mux ch5 */
    /* pca9555@22 */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 5),
                            TYPE_PCA9552, 0x22);
    /* eeprom@52 */
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 5), 0x52, 32 * KiB);

    /* &i2c13 */
    /* i2c-mux@70 (PCA9548) */
    i2c_mux = i2c_slave_create_simple(i2c[13], TYPE_PCA9548, 0x70);

    /* i2c13mux ch3 */
    /* adc128d818@1f - no model */

    /* i2c13mux ch4 */
    /* eeprom@51 */
    at24c_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 4), 0x51, 32 * KiB);

    /* i2c13mux ch7 */
    /* nfc@28 — no model */
}

static void aspeed_machine_anacapa_class_init(ObjectClass *oc,
                                               const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Facebook Anacapa BMC (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = ANACAPA_BMC_HW_STRAP1;
    amc->hw_strap2 = ANACAPA_BMC_HW_STRAP2;
    amc->fmc_model = "mx66l1g45g";
    amc->spi_model = NULL;
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC2_ON;
    amc->i2c_init  = anacapa_bmc_i2c_init;
    mc->default_ram_size = ANACAPA_BMC_RAM_SIZE;
    aspeed_machine_class_init_cpus_defaults(mc);
}

static const TypeInfo aspeed_ast2600_anacapa_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("anacapa-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_anacapa_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2600_anacapa_types)
