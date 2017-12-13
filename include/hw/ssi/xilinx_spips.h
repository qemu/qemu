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

#ifndef XILINX_SPIPS_H
#define XILINX_SPIPS_H

#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"

typedef struct XilinxSPIPS XilinxSPIPS;

#define XLNX_SPIPS_R_MAX        (0x100 / 4)

/* Bite off 4k chunks at a time */
#define LQSPI_CACHE_SIZE 1024

typedef enum {
    READ = 0x3,         READ_4 = 0x13,
    FAST_READ = 0xb,    FAST_READ_4 = 0x0c,
    DOR = 0x3b,         DOR_4 = 0x3c,
    QOR = 0x6b,         QOR_4 = 0x6c,
    DIOR = 0xbb,        DIOR_4 = 0xbc,
    QIOR = 0xeb,        QIOR_4 = 0xec,

    PP = 0x2,           PP_4 = 0x12,
    DPP = 0xa2,
    QPP = 0x32,         QPP_4 = 0x34,
} FlashCMD;

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

typedef struct {
    XilinxSPIPS parent_obj;

    uint8_t lqspi_buf[LQSPI_CACHE_SIZE];
    hwaddr lqspi_cached_addr;
    Error *migration_blocker;
    bool mmio_execution_enabled;
} XilinxQSPIPS;

typedef struct XilinxSPIPSClass {
    SysBusDeviceClass parent_class;

    const MemoryRegionOps *reg_ops;

    uint32_t rx_fifo_size;
    uint32_t tx_fifo_size;
} XilinxSPIPSClass;

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

#endif /* XILINX_SPIPS_H */
