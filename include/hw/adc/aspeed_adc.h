/*
 * Aspeed ADC
 *
 * Copyright 2017-2021 IBM Corp.
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ADC_ASPEED_ADC_H
#define HW_ADC_ASPEED_ADC_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_ADC "aspeed.adc"
#define TYPE_ASPEED_2400_ADC TYPE_ASPEED_ADC "-ast2400"
#define TYPE_ASPEED_2500_ADC TYPE_ASPEED_ADC "-ast2500"
#define TYPE_ASPEED_2600_ADC TYPE_ASPEED_ADC "-ast2600"
#define TYPE_ASPEED_1030_ADC TYPE_ASPEED_ADC "-ast1030"
#define TYPE_ASPEED_2700_ADC TYPE_ASPEED_ADC "-ast2700"
OBJECT_DECLARE_TYPE(AspeedADCState, AspeedADCClass, ASPEED_ADC)

#define TYPE_ASPEED_ADC_ENGINE "aspeed.adc.engine"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedADCEngineState, ASPEED_ADC_ENGINE)

#define ASPEED_ADC_NR_CHANNELS 16
#define ASPEED_ADC_NR_REGS     (0xD0 >> 2)

struct AspeedADCEngineState {
    /* <private> */
    SysBusDevice parent;

    MemoryRegion mmio;
    qemu_irq irq;
    uint32_t engine_id;
    uint32_t nr_channels;
    uint32_t regs[ASPEED_ADC_NR_REGS];
};

struct AspeedADCState {
    /* <private> */
    SysBusDevice parent;

    MemoryRegion mmio;
    qemu_irq irq;

    AspeedADCEngineState engines[2];
};

struct AspeedADCClass {
    SysBusDeviceClass parent_class;

    uint32_t nr_engines;
};

#endif /* HW_ADC_ASPEED_ADC_H */
