/*
 * ASPEED AST27x0 EVB
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
#include "hw/sensor/tmp105.h"

/* AST2700 evb hardware value */
/* SCU HW Strap1 */
#define AST2700_EVB_HW_STRAP1 0x00000800
/* SCUIO HW Strap1 */
#define AST2700_EVB_HW_STRAP2 0x00000700

static void ast2700_evb_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = bmc->soc;

    /* LM75 is compatible with TMP105 driver */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 0),
                            TYPE_TMP105, 0x4d);
}

static void aspeed_machine_ast2700a0_evb_class_init(ObjectClass *oc,
                                                    const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc = "Aspeed AST2700 A0 EVB (Cortex-A35)";
    amc->soc_name  = "ast2700-a0";
    amc->hw_strap1 = AST2700_EVB_HW_STRAP1;
    amc->hw_strap2 = AST2700_EVB_HW_STRAP2;
    amc->fmc_model = "w25q01jvq";
    amc->spi_model = "w25q512jv";
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC0_ON | ASPEED_MAC1_ON | ASPEED_MAC2_ON;
    amc->uart_default = ASPEED_DEV_UART12;
    amc->i2c_init  = ast2700_evb_i2c_init;
    amc->vbootrom = true;
    mc->default_ram_size = 1 * GiB;
    aspeed_machine_class_init_cpus_defaults(mc);
}

static void aspeed_machine_ast2700a1_evb_class_init(ObjectClass *oc,
                                                    const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->alias = "ast2700-evb";
    mc->desc = "Aspeed AST2700 A1 EVB (Cortex-A35)";
    amc->soc_name  = "ast2700-a1";
    amc->hw_strap1 = AST2700_EVB_HW_STRAP1;
    amc->hw_strap2 = AST2700_EVB_HW_STRAP2;
    amc->fmc_model = "w25q01jvq";
    amc->spi_model = "w25q512jv";
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC0_ON | ASPEED_MAC1_ON | ASPEED_MAC2_ON;
    amc->uart_default = ASPEED_DEV_UART12;
    amc->i2c_init  = ast2700_evb_i2c_init;
    amc->vbootrom = true;
    mc->default_ram_size = 1 * GiB;
    aspeed_machine_class_init_cpus_defaults(mc);
}

static const TypeInfo aspeed_ast27x0_evb_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("ast2700a0-evb"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_ast2700a0_evb_class_init,
        .interfaces    = aarch64_machine_interfaces,
    }, {
        .name          = MACHINE_TYPE_NAME("ast2700a1-evb"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_ast2700a1_evb_class_init,
        .interfaces    = aarch64_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast27x0_evb_types)
