/*
 * Quanta Q71l
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

/* Quanta-Q71l hardware value */
#define QUANTA_Q71L_BMC_HW_STRAP1 (                                     \
        SCU_AST2400_HW_STRAP_DRAM_SIZE(DRAM_SIZE_128MB) |               \
        SCU_AST2400_HW_STRAP_DRAM_CONFIG(2/* DDR3 with CL=6, CWL=5 */) | \
        SCU_AST2400_HW_STRAP_ACPI_DIS |                                 \
        SCU_AST2400_HW_STRAP_SET_CLK_SOURCE(AST2400_CLK_24M_IN) |       \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_PASS_THROUGH) |          \
        SCU_AST2400_HW_STRAP_SET_CPU_AHB_RATIO(AST2400_CPU_AHB_RATIO_2_1) | \
        SCU_HW_STRAP_SPI_WIDTH |                                        \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_8M_DRAM) |                        \
        SCU_AST2400_HW_STRAP_BOOT_MODE(AST2400_SPI_BOOT))

static void quanta_q71l_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;

    /*
     * The quanta-q71l platform expects tmp75s which are compatible with
     * tmp105s.
     */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 1), "tmp105", 0x4c);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 1), "tmp105", 0x4e);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 1), "tmp105", 0x4f);

    /* TODO: i2c-1: Add baseboard FRU eeprom@54 24c64 */
    /* TODO: i2c-1: Add Frontpanel FRU eeprom@57 24c64 */
    /* TODO: Add Memory Riser i2c mux and eeproms. */

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), "pca9546", 0x74);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), "pca9548", 0x77);

    /* TODO: i2c-3: Add BIOS FRU eeprom@56 24c64 */

    /* i2c-7 */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 7), "pca9546", 0x70);
    /*        - i2c@0: pmbus@59 */
    /*        - i2c@1: pmbus@58 */
    /*        - i2c@2: pmbus@58 */
    /*        - i2c@3: pmbus@59 */

    /* TODO: i2c-7: Add PDB FRU eeprom@52 */
    /* TODO: i2c-8: Add BMC FRU eeprom@50 */
}

static void aspeed_machine_quanta_q71l_class_init(ObjectClass *oc,
                                                  const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Quanta-Q71l BMC (ARM926EJ-S)";
    amc->soc_name  = "ast2400-a1";
    amc->hw_strap1 = QUANTA_Q71L_BMC_HW_STRAP1;
    amc->fmc_model = "n25q256a";
    amc->spi_model = "mx25l25635e";
    amc->num_cs    = 1;
    amc->i2c_init  = quanta_q71l_bmc_i2c_init;
    mc->default_ram_size       = 128 * MiB;
    aspeed_machine_class_init_cpus_defaults(mc);
}

static const TypeInfo aspeed_ast2400_quanta_q71l_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("quanta-q71l-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_quanta_q71l_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2400_quanta_q71l_types)
