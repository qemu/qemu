/*
 *  Allwinner GPIO registers definition
 *
 *  Copyright (C) 2026 Strahinja Jankovic. <strahinja.p.jankovic@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ALLWINNER_GPIO_H
#define ALLWINNER_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

/** Size of register I/O address space used by GPIO device */
#define AW_GPIO_IOSIZE 0x400

/** Total number of known registers */
#define AW_GPIO_REGS_NUM    (AW_GPIO_IOSIZE / sizeof(uint32_t))

/** Max number of ports */
#define AW_GPIO_PORTS_NUM   9
/** Max number of pins per ports */
#define AW_GPIO_PIN_COUNT 32

#define TYPE_AW_GPIO        "allwinner.gpio"
OBJECT_DECLARE_SIMPLE_TYPE(AWGPIOState, AW_GPIO)

struct AWGPIOState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq output[AW_GPIO_PORTS_NUM][AW_GPIO_PIN_COUNT];

    uint32_t regs[AW_GPIO_REGS_NUM];
};

#endif /* ALLWINNER_GPIO_H */
