/*
 * Qualcomm DC-SCM V1
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

/* Qualcomm DC-SCM hardware value */
#define QCOM_DC_SCM_V1_BMC_HW_STRAP1  0x00000000
#define QCOM_DC_SCM_V1_BMC_HW_STRAP2  0x00000041

static void qcom_dc_scm_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 15), "tmp105", 0x4d);
}

static void aspeed_machine_qcom_dc_scm_v1_class_init(ObjectClass *oc,
                                                     const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Qualcomm DC-SCM V1 BMC (Cortex A7)";
    mc->deprecation_reason = "use 'ast2600-evb' instead";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = QCOM_DC_SCM_V1_BMC_HW_STRAP1;
    amc->hw_strap2 = QCOM_DC_SCM_V1_BMC_HW_STRAP2;
    amc->fmc_model = "n25q512a";
    amc->spi_model = "n25q512a";
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC2_ON | ASPEED_MAC3_ON;
    amc->i2c_init  = qcom_dc_scm_bmc_i2c_init;
    mc->default_ram_size = 1 * GiB;
    aspeed_machine_class_init_cpus_defaults(mc);
};

static const TypeInfo aspeed_ast2600_qcom_dc_scm_v1_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("qcom-dc-scm-v1-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_qcom_dc_scm_v1_class_init,
        .interfaces    = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast2600_qcom_dc_scm_v1_types)
