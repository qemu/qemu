/*
 * Facebook Bletchley
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
#include "hw/gpio/pca9552.h"
#include "hw/nvram/eeprom_at24c.h"

#define TYPE_TMP421 "tmp421"
/* Bletchley hardware value */
#define BLETCHLEY_BMC_HW_STRAP1 0x00002000
#define BLETCHLEY_BMC_HW_STRAP2 0x00000801
#define BLETCHLEY_BMC_RAM_SIZE ASPEED_RAM_SIZE(2 * GiB)

static void bletchley_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;
    I2CBus *i2c[13] = {};
    for (int i = 0; i < 13; i++) {
        if ((i == 8) || (i == 11)) {
            continue;
        }
        i2c[i] = aspeed_i2c_get_bus(&soc->i2c, i);
    }

    /* Bus 0 - 5 all have the same config. */
    for (int i = 0; i < 6; i++) {
        /* Missing model: ti,ina230 @ 0x45 */
        /* Missing model: mps,mp5023 @ 0x40 */
        i2c_slave_create_simple(i2c[i], TYPE_TMP421, 0x4f);
        /* Missing model: nxp,pca9539 @ 0x76, but PCA9552 works enough */
        i2c_slave_create_simple(i2c[i], TYPE_PCA9552, 0x76);
        i2c_slave_create_simple(i2c[i], TYPE_PCA9552, 0x67);
        /* Missing model: fsc,fusb302 @ 0x22 */
    }

    /* Bus 6 */
    at24c_eeprom_init(i2c[6], 0x56, 65536);
    /* Missing model: nxp,pcf85263 @ 0x51 , but ds1338 works enough */
    i2c_slave_create_simple(i2c[6], "ds1338", 0x51);


    /* Bus 7 */
    at24c_eeprom_init(i2c[7], 0x54, 65536);

    /* Bus 9 */
    i2c_slave_create_simple(i2c[9], TYPE_TMP421, 0x4f);

    /* Bus 10 */
    i2c_slave_create_simple(i2c[10], TYPE_TMP421, 0x4f);
    /* Missing model: ti,hdc1080 @ 0x40 */
    i2c_slave_create_simple(i2c[10], TYPE_PCA9552, 0x67);

    /* Bus 12 */
    /* Missing model: adi,adm1278 @ 0x11 */
    i2c_slave_create_simple(i2c[12], TYPE_TMP421, 0x4c);
    i2c_slave_create_simple(i2c[12], TYPE_TMP421, 0x4d);
    i2c_slave_create_simple(i2c[12], TYPE_PCA9552, 0x67);
}

static void aspeed_machine_bletchley_class_init(ObjectClass *oc,
                                                const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Facebook Bletchley BMC (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = BLETCHLEY_BMC_HW_STRAP1;
    amc->hw_strap2 = BLETCHLEY_BMC_HW_STRAP2;
    amc->fmc_model = "w25q01jvq";
    amc->spi_model = NULL;
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC2_ON;
    amc->i2c_init  = bletchley_bmc_i2c_init;
    mc->default_ram_size = BLETCHLEY_BMC_RAM_SIZE;
    aspeed_machine_class_init_cpus_defaults(mc);
}

static const TypeInfo aspeed_ast2600_bletchley_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("bletchley-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_bletchley_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2600_bletchley_types)
