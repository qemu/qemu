/*
 * ASPEED AST2600 EVB
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
#include "hw/sensor/tmp105.h"

/* AST2600 evb hardware value */
#define AST2600_EVB_HW_STRAP1 0x000000C0
#define AST2600_EVB_HW_STRAP2 0x00000003

static void ast2600_evb_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;
    uint8_t *eeprom_buf = g_malloc0(8 * 1024);

    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 7), 0x50,
                          eeprom_buf);

    /* LM75 is compatible with TMP105 driver */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 8),
                     TYPE_TMP105, 0x4d);
}

static void aspeed_machine_ast2600_evb_class_init(ObjectClass *oc,
                                                  const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Aspeed AST2600 EVB (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = AST2600_EVB_HW_STRAP1;
    amc->hw_strap2 = AST2600_EVB_HW_STRAP2;
    amc->fmc_model = "w25q512jv";
    amc->spi_model = "w25q512jv";
    amc->num_cs    = 1;
    amc->macs_mask = ASPEED_MAC0_ON | ASPEED_MAC1_ON | ASPEED_MAC2_ON |
                     ASPEED_MAC3_ON;
    amc->sdhci_wp_inverted = true;
    amc->i2c_init  = ast2600_evb_i2c_init;
    mc->default_ram_size = 1 * GiB;
    aspeed_machine_class_init_cpus_defaults(mc);
    aspeed_machine_ast2600_class_emmc_init(oc);
};

static const TypeInfo aspeed_ast2600_evb_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("ast2600-evb"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_ast2600_evb_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2600_evb_types)
