/*
 * OCP SonoraPass
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

/* Sonorapass hardware value: 0xF100D216 */
#define SONORAPASS_BMC_HW_STRAP1 (                                      \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_RESERVED28 |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_LPC_RESET_PIN |                                    \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_MASTER) |                \
        SCU_AST2500_HW_STRAP_SET_AXI_AHB_RATIO(AXI_AHB_RATIO_2_1) |     \
        SCU_HW_STRAP_VGA_BIOS_ROM |                                     \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_16M_DRAM) |                       \
        SCU_AST2500_HW_STRAP_RESERVED1)

static void sonorapass_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;

    /* bus 2 : */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), "tmp105", 0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), "tmp105", 0x49);
    /* bus 2 : pca9546 @ 0x73 */

    /* bus 3 : pca9548 @ 0x70 */

    /* bus 4 : */
    uint8_t *eeprom4_54 = g_malloc0(8 * 1024);
    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 4), 0x54,
                          eeprom4_54);
    /* PCA9539 @ 0x76, but PCA9552 is compatible */
    aspeed_create_pca9552(soc, 4, 0x76);
    /* PCA9539 @ 0x77, but PCA9552 is compatible */
    aspeed_create_pca9552(soc, 4, 0x77);

    /* bus 6 : */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 6), "tmp105", 0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 6), "tmp105", 0x49);
    /* bus 6 : pca9546 @ 0x73 */

    /* bus 8 : */
    uint8_t *eeprom8_56 = g_malloc0(8 * 1024);
    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 8), 0x56,
                          eeprom8_56);
    aspeed_create_pca9552(soc, 8, 0x60);
    aspeed_create_pca9552(soc, 8, 0x61);
    /* bus 8 : adc128d818 @ 0x1d */
    /* bus 8 : adc128d818 @ 0x1f */

    /*
     * bus 13 : pca9548 @ 0x71
     *      - channel 3:
     *          - tmm421 @ 0x4c
     *          - tmp421 @ 0x4e
     *          - tmp421 @ 0x4f
     */

}

static void aspeed_machine_sonorapass_class_init(ObjectClass *oc,
                                                 const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "OCP SonoraPass BMC (ARM1176)";
    mc->deprecation_reason = "use 'ast2500-evb' instead";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = SONORAPASS_BMC_HW_STRAP1;
    amc->fmc_model = "mx66l1g45g";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs    = 2;
    amc->i2c_init  = sonorapass_bmc_i2c_init;
    mc->default_ram_size       = 512 * MiB;
    aspeed_machine_class_init_cpus_defaults(mc);
};

static const TypeInfo aspeed_ast2500_sonorapass_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("sonorapass-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_sonorapass_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2500_sonorapass_types)
