/*
 * Header file for the Xilinx Versal's OSPI controller
 *
 * Copyright (C) 2021 Xilinx Inc
 * Written by Francisco Iglesias <francisco.iglesias@xilinx.com>
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

/*
 * This is a model of Xilinx Versal's Octal SPI flash memory controller
 * documented in Versal's Technical Reference manual [1] and the Versal ACAP
 * Register reference [2].
 *
 * References:
 *
 * [1] Versal ACAP Technical Reference Manual,
 *     https://www.xilinx.com/support/documentation/architecture-manuals/am011-versal-acap-trm.pdf
 *
 * [2] Versal ACAP Register Reference,
 *     https://docs.xilinx.com/r/en-US/am012-versal-register-reference/OSPI-Module
 *
 *
 * QEMU interface:
 * + sysbus MMIO region 0: MemoryRegion for the device's registers
 * + sysbus MMIO region 1: MemoryRegion for flash memory linear address space
 *   (data transfer).
 * + sysbus IRQ 0: Device interrupt.
 * + Named GPIO input "ospi-mux-sel": 0: enables indirect access mode
 *   and 1: enables direct access mode.
 * + Property "dac-with-indac": Allow both direct accesses and indirect
 *   accesses simultaneously.
 * + Property "indac-write-disabled": Disable indirect access writes.
 */

#ifndef XLNX_VERSAL_OSPI_H
#define XLNX_VERSAL_OSPI_H

#include "hw/register.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "hw/dma/xlnx_csu_dma.h"

#define TYPE_XILINX_VERSAL_OSPI "xlnx.versal-ospi"

OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalOspi, XILINX_VERSAL_OSPI)

#define XILINX_VERSAL_OSPI_R_MAX (0xfc / 4 + 1)

/*
 * Indirect operations
 */
typedef struct IndOp {
    uint32_t flash_addr;
    uint32_t num_bytes;
    uint32_t done_bytes;
    bool completed;
} IndOp;

struct XlnxVersalOspi {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion iomem_dac;

    uint8_t num_cs;
    qemu_irq *cs_lines;

    SSIBus *spi;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    Fifo8 rx_sram;
    Fifo8 tx_sram;

    qemu_irq irq;

    XlnxCSUDMA *dma_src;
    bool ind_write_disabled;
    bool dac_with_indac;
    bool dac_enable;
    bool src_dma_inprog;

    IndOp rd_ind_op[2];
    IndOp wr_ind_op[2];

    uint32_t regs[XILINX_VERSAL_OSPI_R_MAX];
    RegisterInfo regs_info[XILINX_VERSAL_OSPI_R_MAX];

    /* Maximum inferred membank size is 512 bytes */
    uint8_t stig_membank[512];
};

#endif /* XLNX_VERSAL_OSPI_H */
