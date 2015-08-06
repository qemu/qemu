/*
 * Dynamic sysbus device tree node generation API
 *
 * Copyright Linaro Limited, 2014
 *
 * Authors:
 *  Alex Graf <agraf@suse.de>
 *  Eric Auger <eric.auger@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HW_ARM_SYSBUS_FDT_H
#define HW_ARM_SYSBUS_FDT_H

#include "hw/arm/arm.h"
#include "qemu-common.h"
#include "hw/sysbus.h"

/*
 * struct that contains dimensioning parameters of the platform bus
 */
typedef struct {
    hwaddr platform_bus_base; /* start address of the bus */
    hwaddr platform_bus_size; /* size of the bus */
    int platform_bus_first_irq; /* first hwirq assigned to the bus */
    int platform_bus_num_irqs; /* number of hwirq assigned to the bus */
} ARMPlatformBusSystemParams;

/*
 * struct that contains all relevant info to build the fdt nodes of
 * platform bus and attached dynamic sysbus devices
 * in the future might be augmented with additional info
 * such as PHY, CLK handles ...
 */
typedef struct {
    const ARMPlatformBusSystemParams *system_params;
    struct arm_boot_info *binfo;
    const char *intc; /* parent interrupt controller name */
} ARMPlatformBusFDTParams;

/**
 * arm_register_platform_bus_fdt_creator - register a machine init done
 * notifier that creates the device tree nodes of the platform bus and
 * associated dynamic sysbus devices
 */
void arm_register_platform_bus_fdt_creator(ARMPlatformBusFDTParams *fdt_params);

#endif
