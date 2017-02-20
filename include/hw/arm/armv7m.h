/*
 * ARMv7M CPU object
 *
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell <peter.maydell@linaro.org>
 *
 * This code is licensed under the GPL version 2 or later.
 */

#ifndef HW_ARM_ARMV7M_H
#define HW_ARM_ARMV7M_H

#include "hw/sysbus.h"
#include "hw/arm/armv7m_nvic.h"

#define TYPE_BITBAND "ARM,bitband-memory"
#define BITBAND(obj) OBJECT_CHECK(BitBandState, (obj), TYPE_BITBAND)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t base;
} BitBandState;

#define TYPE_ARMV7M "armv7m"
#define ARMV7M(obj) OBJECT_CHECK(ARMv7MState, (obj), TYPE_ARMV7M)

#define ARMV7M_NUM_BITBANDS 2

/* ARMv7M container object.
 * + Unnamed GPIO input lines: external IRQ lines for the NVIC
 * + Named GPIO output SYSRESETREQ: signalled for guest AIRCR.SYSRESETREQ
 * + Property "cpu-model": CPU model to instantiate
 * + Property "num-irq": number of external IRQ lines
 */
typedef struct ARMv7MState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    NVICState nvic;
    BitBandState bitband[ARMV7M_NUM_BITBANDS];
    ARMCPU *cpu;

    /* Properties */
    char *cpu_model;
} ARMv7MState;

#endif
