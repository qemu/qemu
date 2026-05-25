/*
 * ASPEED AST1040 EVB
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/aspeed.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/core/qdev-clock.h"
#include "system/system.h"

#define AST1040_INTERNAL_FLASH_SIZE (4 * MiB)
/* Main SYSCLK frequency in Hz (400MHz) */
#define SYSCLK_FRQ 400000000ULL

static void aspeed_bic_machine_init(MachineState *machine)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(machine);
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(machine);
    Clock *sysclk;

    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);

    bmc->soc = ASPEED_SOC(object_new(amc->soc_name));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(bmc->soc));
    object_unref(OBJECT(bmc->soc));
    qdev_connect_clock_in(DEVICE(bmc->soc), "sysclk", sysclk);

    object_property_set_link(OBJECT(bmc->soc), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    aspeed_connect_serial_hds_to_uarts(bmc);
    qdev_realize(DEVICE(bmc->soc), NULL, &error_abort);

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0,
                       AST1040_INTERNAL_FLASH_SIZE);
}

static void aspeed_machine_ast1040_evb_class_init(ObjectClass *oc,
                                                  const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc = "Aspeed AST1040 BIC EVB (Cortex-M4F)";
    amc->soc_name = "ast1040-a0";
    amc->hw_strap1 = 0;
    amc->hw_strap2 = 0;
    mc->init = aspeed_bic_machine_init;
    mc->default_ram_size = 0;
    amc->macs_mask = 0;
    amc->uart_default = ASPEED_DEV_UART12;
    aspeed_machine_class_init_cpus_defaults(mc);
}

static const TypeInfo aspeed_ast1040_evb_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("ast1040-evb"),
        .parent         = TYPE_ASPEED_MACHINE,
        .class_init     = aspeed_machine_ast1040_evb_class_init,
        .interfaces     = arm_machine_interfaces,
    }
};

DEFINE_TYPES(aspeed_ast1040_evb_types)
