/*
 * Device model for Zynq ADC controller
 *
 * Copyright (c) 2015 Guenter Roeck <linux@roeck-us.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ZYNQ_XADC_H
#define ZYNQ_XADC_H

#include "hw/sysbus.h"

#define ZYNQ_XADC_MMIO_SIZE     0x0020
#define ZYNQ_XADC_NUM_IO_REGS   (ZYNQ_XADC_MMIO_SIZE / 4)
#define ZYNQ_XADC_NUM_ADC_REGS  128
#define ZYNQ_XADC_FIFO_DEPTH    15

#define TYPE_ZYNQ_XADC          "xlnx,zynq-xadc"
#define ZYNQ_XADC(obj) \
    OBJECT_CHECK(ZynqXADCState, (obj), TYPE_ZYNQ_XADC)

typedef struct ZynqXADCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[ZYNQ_XADC_NUM_IO_REGS];
    uint16_t xadc_regs[ZYNQ_XADC_NUM_ADC_REGS];
    uint16_t xadc_read_reg_previous;
    uint16_t xadc_dfifo[ZYNQ_XADC_FIFO_DEPTH];
    uint16_t xadc_dfifo_entries;

    struct IRQState *qemu_irq;

} ZynqXADCState;

#endif /* ZYNQ_XADC_H */
