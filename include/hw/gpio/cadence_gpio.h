/*
 * Cadence GPIO registers definition.
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CADENCE_GPIO_H
#define CADENCE_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_CADENCE_GPIO "cadence_gpio"
OBJECT_DECLARE_SIMPLE_TYPE(CadenceGPIOState, CADENCE_GPIO)

#define CDNS_GPIO_REG_SIZE      0x400
#define CDNS_GPIO_NUM           32

#define CDNS_GPIO_BYPASS_MODE           0x00
#define CDNS_GPIO_DIRECTION_MODE        0x04
#define CDNS_GPIO_OUTPUT_EN             0x08
#define CDNS_GPIO_OUTPUT_VALUE          0x0c
#define CDNS_GPIO_INPUT_VALUE           0x10
#define CDNS_GPIO_IRQ_MASK              0x14
#define CDNS_GPIO_IRQ_EN                0x18
#define CDNS_GPIO_IRQ_DIS               0x1c
#define CDNS_GPIO_IRQ_STATUS            0x20
#define CDNS_GPIO_IRQ_TYPE              0x24
#define CDNS_GPIO_IRQ_VALUE             0x28
#define CDNS_GPIO_IRQ_ANY_EDGE          0x2c

struct CadenceGPIOState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t bmr;
    uint32_t dmr;
    uint32_t oer;
    uint32_t ovr;
    uint32_t inpvr;
    uint32_t imr;
    uint32_t isr;
    uint32_t itr;
    uint32_t ivr;
    uint32_t ioar;
    qemu_irq irq;
    qemu_irq output[CDNS_GPIO_NUM];
};

#endif /* CADENCE_GPIO_H */
