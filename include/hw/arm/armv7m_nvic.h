/*
 * ARMv7M NVIC object
 *
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell <peter.maydell@linaro.org>
 *
 * This code is licensed under the GPL version 2 or later.
 */

#ifndef HW_ARM_ARMV7M_NVIC_H
#define HW_ARM_ARMV7M_NVIC_H

#include "target/arm/cpu.h"
#include "hw/sysbus.h"
#include "hw/timer/armv7m_systick.h"

#define TYPE_NVIC "armv7m_nvic"

#define NVIC(obj) \
    OBJECT_CHECK(NVICState, (obj), TYPE_NVIC)

/* Highest permitted number of exceptions (architectural limit) */
#define NVIC_MAX_VECTORS 512

typedef struct VecInfo {
    /* Exception priorities can range from -3 to 255; only the unmodifiable
     * priority values for RESET, NMI and HardFault can be negative.
     */
    int16_t prio;
    uint8_t enabled;
    uint8_t pending;
    uint8_t active;
    uint8_t level; /* exceptions <=15 never set level */
} VecInfo;

typedef struct NVICState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    ARMCPU *cpu;

    VecInfo vectors[NVIC_MAX_VECTORS];
    uint32_t prigroup;

    /* vectpending and exception_prio are both cached state that can
     * be recalculated from the vectors[] array and the prigroup field.
     */
    unsigned int vectpending; /* highest prio pending enabled exception */
    int exception_prio; /* group prio of the highest prio active exception */

    MemoryRegion sysregmem;
    MemoryRegion container;

    uint32_t num_irq;
    qemu_irq excpout;
    qemu_irq sysresetreq;

    SysTickState systick;
} NVICState;

#endif
