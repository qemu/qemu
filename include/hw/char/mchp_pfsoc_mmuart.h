/*
 * Microchip PolarFire SoC MMUART emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_MCHP_PFSOC_MMUART_H
#define HW_MCHP_PFSOC_MMUART_H

#include "hw/sysbus.h"
#include "hw/char/serial.h"

#define MCHP_PFSOC_MMUART_REG_COUNT 13

#define TYPE_MCHP_PFSOC_UART "mchp.pfsoc.uart"
OBJECT_DECLARE_SIMPLE_TYPE(MchpPfSoCMMUartState, MCHP_PFSOC_UART)

typedef struct MchpPfSoCMMUartState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion container;
    MemoryRegion iomem;

    SerialMM serial_mm;

    uint32_t reg[MCHP_PFSOC_MMUART_REG_COUNT];
} MchpPfSoCMMUartState;

/**
 * mchp_pfsoc_mmuart_create - Create a Microchip PolarFire SoC MMUART
 *
 * This is a helper routine for board to create a MMUART device that is
 * compatible with Microchip PolarFire SoC.
 *
 * @sysmem: system memory region to map
 * @base: base address of the MMUART registers
 * @irq: IRQ number of the MMUART device
 * @chr: character device to associate to
 *
 * @return: a pointer to the device specific control structure
 */
MchpPfSoCMMUartState *mchp_pfsoc_mmuart_create(MemoryRegion *sysmem,
    hwaddr base, qemu_irq irq, Chardev *chr);

#endif /* HW_MCHP_PFSOC_MMUART_H */
