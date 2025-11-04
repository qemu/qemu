/*
 * Inspur FP5280G2
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
#include "hw/i2c/i2c_mux_pca954x.h"
#include "hw/sensor/tmp105.h"

/* FP5280G2 hardware value: 0XF100D286 */
#define FP5280G2_BMC_HW_STRAP1 (                                      \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_RESERVED28 |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_LPC_RESET_PIN |                                    \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_MASTER) |                \
        SCU_AST2500_HW_STRAP_SET_AXI_AHB_RATIO(AXI_AHB_RATIO_2_1) |     \
        SCU_HW_STRAP_MAC1_RGMII |                                       \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_16M_DRAM) |                       \
        SCU_AST2500_HW_STRAP_RESERVED1)

static void fp5280g2_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;
    I2CSlave *i2c_mux;

    /* The at24c256 */
    at24c_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 1), 0x50, 32768);

    /* The fp5280g2 expects a TMP112 but a TMP105 is compatible */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), TYPE_TMP105,
                     0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), TYPE_TMP105,
                     0x49);

    i2c_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2),
                     "pca9546", 0x70);
    /* It expects a TMP112 but a TMP105 is compatible */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 0), TYPE_TMP105,
                     0x4a);

    /* It expects a ds3232 but a ds1338 is good enough */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 4), "ds1338", 0x68);

    /* It expects a pca9555 but a pca9552 is compatible */
    aspeed_create_pca9552(soc, 8, 0x30);
}

static void aspeed_machine_fp5280g2_class_init(ObjectClass *oc,
                                               const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Inspur FP5280G2 BMC (ARM1176)";
    mc->deprecation_reason = "use 'ast2500-evb' instead";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = FP5280G2_BMC_HW_STRAP1;
    amc->fmc_model = "n25q512a";
    amc->spi_model = "mx25l25635e";
    amc->num_cs    = 2;
    amc->macs_mask  = ASPEED_MAC0_ON | ASPEED_MAC1_ON;
    amc->i2c_init  = fp5280g2_bmc_i2c_init;
    mc->default_ram_size = 512 * MiB;
    aspeed_machine_class_init_cpus_defaults(mc);
};

static const TypeInfo aspeed_ast2500_fp5280g2_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("fp5280g2-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_fp5280g2_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2500_fp5280g2_types)
