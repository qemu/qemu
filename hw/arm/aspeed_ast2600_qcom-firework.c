/*
 * Qualcomm DC-SCM V1/Firework
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

/* Qualcomm DC-SCM Firework hardware value */
#define QCOM_DC_SCM_FIREWORK_BMC_HW_STRAP1  0x00000000
#define QCOM_DC_SCM_FIREWORK_BMC_HW_STRAP2  0x00000041

#define TYPE_LM75 TYPE_TMP105

static void qcom_dc_scm_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 15), "tmp105", 0x4d);
}

static void qcom_dc_scm_firework_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;
    I2CSlave *therm_mux, *cpuvr_mux;

    /* Create the generic DC-SCM hardware */
    qcom_dc_scm_bmc_i2c_init(bmc);

    /* Now create the Firework specific hardware */

    /* I2C7 CPUVR MUX */
    cpuvr_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 7),
                                        "pca9546", 0x70);
    i2c_slave_create_simple(pca954x_i2c_get_bus(cpuvr_mux, 0), "pca9548", 0x72);
    i2c_slave_create_simple(pca954x_i2c_get_bus(cpuvr_mux, 1), "pca9548", 0x72);
    i2c_slave_create_simple(pca954x_i2c_get_bus(cpuvr_mux, 2), "pca9548", 0x72);
    i2c_slave_create_simple(pca954x_i2c_get_bus(cpuvr_mux, 3), "pca9548", 0x72);

    /* I2C8 Thermal Diodes*/
    therm_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 8),
                                        "pca9548", 0x70);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 0), TYPE_LM75, 0x4C);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 1), TYPE_LM75, 0x4C);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 2), TYPE_LM75, 0x48);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 3), TYPE_LM75, 0x48);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 4), TYPE_LM75, 0x48);

    /* I2C9 Fan Controller (MAX31785) */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 9), "max31785", 0x52);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 9), "max31785", 0x54);
}

static void aspeed_machine_qcom_firework_class_init(ObjectClass *oc,
                                                    const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Qualcomm DC-SCM V1/Firework BMC (Cortex A7)";
    mc->deprecation_reason = "use 'ast2600-evb' instead";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = QCOM_DC_SCM_FIREWORK_BMC_HW_STRAP1;
    amc->hw_strap2 = QCOM_DC_SCM_FIREWORK_BMC_HW_STRAP2;
    amc->fmc_model = "n25q512a";
    amc->spi_model = "n25q512a";
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC2_ON | ASPEED_MAC3_ON;
    amc->i2c_init  = qcom_dc_scm_firework_i2c_init;
    mc->default_ram_size = 1 * GiB;
    aspeed_machine_class_init_cpus_defaults(mc);
};

static const TypeInfo aspeed_ast2600_qcom_firework_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("qcom-firework-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_qcom_firework_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2600_qcom_firework_types)
