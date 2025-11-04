/*
 * ASPEED AST2500 EVB
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

/* AST2500 evb hardware value: 0xF100C2E6 */
#define AST2500_EVB_HW_STRAP1 ((                                        \
        AST2500_HW_STRAP1_DEFAULTS |                                    \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_HW_STRAP_MAC1_RGMII |                                       \
        SCU_HW_STRAP_MAC0_RGMII) &                                      \
        ~SCU_HW_STRAP_2ND_BOOT_WDT)

static void ast2500_evb_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;
    uint8_t *eeprom_buf = g_malloc0(8 * 1024);

    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 3), 0x50,
                          eeprom_buf);

    /* The AST2500 EVB expects a LM75 but a TMP105 is compatible */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 7),
                     TYPE_TMP105, 0x4d);
}

static void aspeed_machine_ast2500_evb_class_init(ObjectClass *oc,
                                                  const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Aspeed AST2500 EVB (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = AST2500_EVB_HW_STRAP1;
    amc->fmc_model = "mx25l25635e";
    amc->spi_model = "mx25l25635f";
    amc->num_cs    = 1;
    amc->i2c_init  = ast2500_evb_i2c_init;
    mc->default_ram_size       = 512 * MiB;
    aspeed_machine_class_init_cpus_defaults(mc);
};

static const TypeInfo aspeed_ast2500_evb_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("ast2500-evb"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_ast2500_evb_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2500_evb_types)
