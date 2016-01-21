/*
 * Header file for the Xilinx Zynq SPI controller
 *
 * Copyright (C) 2015 Xilinx Inc
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

#ifndef XLNX_SPIPS_H
#define XLNX_SPIPS_H

#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"

typedef struct XilinxSPIPS XilinxSPIPS;

#define XLNX_SPIPS_R_MAX        (0x100 / 4)

struct XilinxSPIPS {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion mmlqspi;

    qemu_irq irq;
    int irqline;

    uint8_t num_cs;
    uint8_t num_busses;

    uint8_t snoop_state;
    qemu_irq *cs_lines;
    SSIBus **spi;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    uint8_t num_txrx_bytes;

    uint32_t regs[XLNX_SPIPS_R_MAX];
};

#define TYPE_XILINX_SPIPS "xlnx.ps7-spi"
#define TYPE_XILINX_QSPIPS "xlnx.ps7-qspi"

#define XILINX_SPIPS(obj) \
     OBJECT_CHECK(XilinxSPIPS, (obj), TYPE_XILINX_SPIPS)
#define XILINX_SPIPS_CLASS(klass) \
     OBJECT_CLASS_CHECK(XilinxSPIPSClass, (klass), TYPE_XILINX_SPIPS)
#define XILINX_SPIPS_GET_CLASS(obj) \
     OBJECT_GET_CLASS(XilinxSPIPSClass, (obj), TYPE_XILINX_SPIPS)

#define XILINX_QSPIPS(obj) \
     OBJECT_CHECK(XilinxQSPIPS, (obj), TYPE_XILINX_QSPIPS)

#endif /* XLNX_SPIPS_H */
