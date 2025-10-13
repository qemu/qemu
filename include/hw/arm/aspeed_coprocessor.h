/*
 * ASPEED Coprocessor
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_COPROCESSOR_H
#define ASPEED_COPROCESSOR_H

#include "qom/object.h"
#include "hw/arm/aspeed_soc.h"

struct AspeedCoprocessorState {
    DeviceState parent;

    MemoryRegion *memory;
    MemoryRegion sram;
    Clock *sysclk;

    AspeedSCUState scu;
    AspeedSCUState scuio;
    AspeedTimerCtrlState timerctrl;
    SerialMM uart[ASPEED_UARTS_NUM];
};

#define TYPE_ASPEED_COPROCESSOR "aspeed-coprocessor"
OBJECT_DECLARE_TYPE(AspeedCoprocessorState, AspeedCoprocessorClass,
                    ASPEED_COPROCESSOR)

struct AspeedCoprocessorClass {
    DeviceClass parent_class;

    /** valid_cpu_types: NULL terminated array of a single CPU type. */
    const char * const *valid_cpu_types;
    uint32_t silicon_rev;
    const hwaddr *memmap;
    const int *irqmap;
    int uarts_base;
    int uarts_num;
};

struct Aspeed27x0CoprocessorState {
    AspeedCoprocessorState parent;
    AspeedINTCState intc[2];
    UnimplementedDeviceState ipc[2];
    UnimplementedDeviceState scuio;

    ARMv7MState armv7m;
};

#define TYPE_ASPEED27X0SSP_SOC "aspeed27x0ssp-soc"
OBJECT_DECLARE_SIMPLE_TYPE(Aspeed27x0CoprocessorState, ASPEED27X0SSP_SOC)

#define TYPE_ASPEED27X0TSP_SOC "aspeed27x0tsp-soc"
DECLARE_OBJ_CHECKERS(Aspeed27x0CoprocessorState, AspeedCoprocessorClass,
                     ASPEED27X0TSP_SOC, TYPE_ASPEED27X0TSP_SOC)

#endif /* ASPEED_COPROCESSOR_H */
