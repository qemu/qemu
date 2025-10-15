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
    MemoryRegion sdram;
    MemoryRegion *sram;
    MemoryRegion sram_alias;
    MemoryRegion uart_alias;
    MemoryRegion scu_alias;
    Clock *sysclk;

    AspeedSCUState *scu;
    AspeedSCUState scuio;
    AspeedTimerCtrlState timerctrl;
    SerialMM *uart;
    int uart_dev;
};

#define TYPE_ASPEED_COPROCESSOR "aspeed-coprocessor"
OBJECT_DECLARE_TYPE(AspeedCoprocessorState, AspeedCoprocessorClass,
                    ASPEED_COPROCESSOR)

struct AspeedCoprocessorClass {
    DeviceClass parent_class;

    /** valid_cpu_types: NULL terminated array of a single CPU type. */
    const char * const *valid_cpu_types;
    const hwaddr *memmap;
    const int *irqmap;
};

struct Aspeed27x0CoprocessorState {
    AspeedCoprocessorState parent;
    AspeedINTCState intc[2];
    UnimplementedDeviceState ipc[2];
    UnimplementedDeviceState scuio;

    ARMv7MState armv7m;
};

#define TYPE_ASPEED27X0SSP_COPROCESSOR "aspeed27x0ssp-coprocessor"
OBJECT_DECLARE_SIMPLE_TYPE(Aspeed27x0CoprocessorState,
                           ASPEED27X0SSP_COPROCESSOR)

#define TYPE_ASPEED27X0TSP_COPROCESSOR "aspeed27x0tsp-coprocessor"
DECLARE_OBJ_CHECKERS(Aspeed27x0CoprocessorState, AspeedCoprocessorClass,
                     ASPEED27X0TSP_COPROCESSOR, TYPE_ASPEED27X0TSP_COPROCESSOR)

#endif /* ASPEED_COPROCESSOR_H */
