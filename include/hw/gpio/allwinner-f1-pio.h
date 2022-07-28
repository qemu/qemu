/*
 * Allwinner PIO Unit emulation
 *
 * Copyright (C) 2022 froloff
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_GPIO_ALLWINNER_F1_PIO_H
#define HW_GPIO_ALLWINNER_F1_PIO_H

#include "qom/object.h"
#include "hw/sysbus.h"

/**
 * @name Constants
 * @{
 */

/** Size of register I/O address space used by CCU device */
#define AW_PIO_IOSIZE        (0x400)

/** Total number of known registers */
#define AW_PIO_REGS_NUM      (AW_PIO_IOSIZE / sizeof(uint32_t))

/** @} */

/**
 * @name Object model
 * @{
 */

#define TYPE_AW_F1_PIO "allwinner-f1-pio"

OBJECT_DECLARE_SIMPLE_TYPE(AwPIOState, AW_F1_PIO)

#define AW_F1_PORTS     6
#define AW_F1_PORTS_IRQ 3

enum {
    PIO_A      = 0,
    PIO_B      = 1,
    PIO_C      = 2,
    PIO_D      = 3,
    PIO_E      = 4,
    PIO_F      = 5,
};

/* PIO register offsets */
enum {
    REG_PIO_CFG0             = 0x0000, /* Configure Register 0 */
    REG_PIO_CFG1             = 0x0004, /* Configure Register 1 */
    REG_PIO_CFG2             = 0x0008, /* Configure Register 2 */
    REG_PIO_CFG3             = 0x000c, /* Configure Register 3 */
    REG_PIO_DATA             = 0x0010, /* Data Register */
    REG_PIO_DRV0             = 0x0014, /* Multi-Driving Register 0 */
    REG_PIO_DRV1             = 0x0018, /* Multi-Driving Register 1 */
    REG_PIO_PUL0             = 0x001c, /* Pull Register 0 */
    REG_PIO_PUL1             = 0x0020, /* Pull Register 1 */
};

typedef void     (*fn_pio_read) (void *opaque, const uint32_t *regs, uint32_t ofs);
typedef uint32_t (*fn_pio_write)(void *opaque, uint32_t *regs, uint32_t ofs, uint32_t value);

typedef struct AwPioCallback {
    void *opaque;
    fn_pio_read  fn_rd;
    fn_pio_write fn_wr;
    // TODO: Interrupt configuration access
} AwPioCallback;

/** @} */

/**
 * Allwinner PIO object instance state.
 */
struct AwPIOState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion  iomem;
    AwPioCallback cb[AW_F1_PORTS]; 

    /** Array of hardware registers */
    uint32_t regs[AW_PIO_REGS_NUM];
};

void allwinner_set_pio_port_cb(AwPIOState *s, uint32_t port, 
                               void *opaque,
                               fn_pio_read  fn_rd,
                               fn_pio_write fn_wr);

#endif /* HW_GPIO_ALLWINNER_F1_PIO_H */
