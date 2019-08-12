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
#include "qemu/fifo32.h"
#include "hw/stream.h"
#include "hw/sysbus.h"

typedef struct XilinxSPIPS XilinxSPIPS;

#define XLNX_SPIPS_R_MAX        (0x100 / 4)
#define XLNX_ZYNQMP_SPIPS_R_MAX (0x830 / 4)

/* Bite off 4k chunks at a time */
#define LQSPI_CACHE_SIZE 1024

#define QSPI_DMA_MAX_BURST_SIZE 2048

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
    int cmd_dummies;
    uint8_t link_state;
    uint8_t link_state_next;
    uint8_t link_state_next_when;
    qemu_irq *cs_lines;
    bool *cs_lines_state;
    SSIBus **spi;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    uint8_t num_txrx_bytes;
    uint32_t rx_discard;

    uint32_t regs[XLNX_SPIPS_R_MAX];

    bool man_start_com;
};

typedef struct {
    XilinxSPIPS parent_obj;

    uint8_t lqspi_buf[LQSPI_CACHE_SIZE];
    hwaddr lqspi_cached_addr;
    Error *migration_blocker;
    bool mmio_execution_enabled;
} XilinxQSPIPS;

typedef struct {
    XilinxQSPIPS parent_obj;

    StreamSlave *dma;
    int gqspi_irqline;

    uint32_t regs[XLNX_ZYNQMP_SPIPS_R_MAX];

    /* GQSPI has seperate tx/rx fifos */
    Fifo8 rx_fifo_g;
    Fifo8 tx_fifo_g;
    Fifo32 fifo_g;
    /*
     * At the end of each generic command, misaligned extra bytes are discard
     * or padded to tx and rx respectively to round it out (and avoid need for
     * individual byte access. Since we use byte fifos, keep track of the
     * alignment WRT to word access.
     */
    uint8_t rx_fifo_g_align;
    uint8_t tx_fifo_g_align;
    bool man_start_com_g;
    uint32_t dma_burst_size;
    uint8_t dma_buf[QSPI_DMA_MAX_BURST_SIZE];
} XlnxZynqMPQSPIPS;

typedef struct XilinxSPIPSClass {
    SysBusDeviceClass parent_class;

    const MemoryRegionOps *reg_ops;

    uint32_t rx_fifo_size;
    uint32_t tx_fifo_size;
} XilinxSPIPSClass;

#define TYPE_XILINX_SPIPS "xlnx.ps7-spi"
#define TYPE_XILINX_QSPIPS "xlnx.ps7-qspi"
#define TYPE_XLNX_ZYNQMP_QSPIPS "xlnx.usmp-gqspi"

#define XILINX_SPIPS(obj) \
     OBJECT_CHECK(XilinxSPIPS, (obj), TYPE_XILINX_SPIPS)
#define XILINX_SPIPS_CLASS(klass) \
     OBJECT_CLASS_CHECK(XilinxSPIPSClass, (klass), TYPE_XILINX_SPIPS)
#define XILINX_SPIPS_GET_CLASS(obj) \
     OBJECT_GET_CLASS(XilinxSPIPSClass, (obj), TYPE_XILINX_SPIPS)

#define XILINX_QSPIPS(obj) \
     OBJECT_CHECK(XilinxQSPIPS, (obj), TYPE_XILINX_QSPIPS)

#define XLNX_ZYNQMP_QSPIPS(obj) \
     OBJECT_CHECK(XlnxZynqMPQSPIPS, (obj), TYPE_XLNX_ZYNQMP_QSPIPS)

#endif /* XILINX_SPIPS_H */
