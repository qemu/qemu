/*
 * ASPEED Serial GPIO Controller
 *
 * Copyright 2025 Google LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_SGPIO_H
#define ASPEED_SGPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "hw/core/registerfields.h"

#define TYPE_ASPEED_SGPIO "aspeed.sgpio"
OBJECT_DECLARE_TYPE(AspeedSGPIOState, AspeedSGPIOClass, ASPEED_SGPIO)

#define ASPEED_SGPIO_MAX_PIN_PAIR 256
#define ASPEED_SGPIO_MAX_INT 8

/* AST2700 SGPIO Register Address Offsets */
REG32(SGPIO_INT_STATUS_0, 0x40)
REG32(SGPIO_INT_STATUS_1, 0x44)
REG32(SGPIO_INT_STATUS_2, 0x48)
REG32(SGPIO_INT_STATUS_3, 0x4C)
REG32(SGPIO_INT_STATUS_4, 0x50)
REG32(SGPIO_INT_STATUS_5, 0x54)
REG32(SGPIO_INT_STATUS_6, 0x58)
REG32(SGPIO_INT_STATUS_7, 0x5C)
/* AST2700 SGPIO_0 - SGPIO_255 Control Register */
REG32(SGPIO_0_CONTROL, 0x80)
    SHARED_FIELD(SGPIO_SERIAL_OUT_VAL, 0, 1)
    SHARED_FIELD(SGPIO_PARALLEL_OUT_VAL, 1, 1)
    SHARED_FIELD(SGPIO_INT_EN, 2, 1)
    SHARED_FIELD(SGPIO_INT_TYPE, 3, 3)
    SHARED_FIELD(SGPIO_RESET_POLARITY, 6, 1)
    SHARED_FIELD(SGPIO_RESERVED_1, 7, 2)
    SHARED_FIELD(SGPIO_INPUT_MASK, 9, 1)
    SHARED_FIELD(SGPIO_PARALLEL_EN, 10, 1)
    SHARED_FIELD(SGPIO_PARALLEL_IN_MODE, 11, 1)
    SHARED_FIELD(SGPIO_INT_STATUS, 12, 1)
    SHARED_FIELD(SGPIO_SERIAL_IN_VAL, 13, 1)
    SHARED_FIELD(SGPIO_PARALLEL_IN_VAL, 14, 1)
    SHARED_FIELD(SGPIO_RESERVED_2, 15, 12)
    SHARED_FIELD(SGPIO_WRITE_PROTECT, 31, 1)
REG32(SGPIO_255_CONTROL, 0x47C)

struct AspeedSGPIOClass {
    SysBusDeviceClass parent_class;
    uint32_t nr_sgpio_pin_pairs;
    uint64_t mem_size;
    const MemoryRegionOps *reg_ops;
};

struct AspeedSGPIOState {
    /* <private> */
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;
    int pending;
    qemu_irq irq;
    qemu_irq sgpios[ASPEED_SGPIO_MAX_PIN_PAIR];
    uint32_t ctrl_regs[ASPEED_SGPIO_MAX_PIN_PAIR];
    uint32_t int_regs[ASPEED_SGPIO_MAX_INT];
};

#endif /* ASPEED_SGPIO_H */
