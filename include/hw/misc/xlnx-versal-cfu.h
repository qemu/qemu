/*
 * QEMU model of the CFU Configuration Unit.
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * Written by Francisco Iglesias <francisco.iglesias@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * References:
 * [1] Versal ACAP Technical Reference Manual,
 *     https://www.xilinx.com/support/documentation/architecture-manuals/am011-versal-acap-trm.pdf
 *
 * [2] Versal ACAP Register Reference,
 *     https://docs.xilinx.com/r/en-US/am012-versal-register-reference/CFU_CSR-Module
 */
#ifndef HW_MISC_XLNX_VERSAL_CFU_APB_H
#define HW_MISC_XLNX_VERSAL_CFU_APB_H

#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/misc/xlnx-cfi-if.h"
#include "qemu/fifo32.h"

#define TYPE_XLNX_VERSAL_CFU_APB "xlnx-versal-cfu-apb"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalCFUAPB, XLNX_VERSAL_CFU_APB)

#define TYPE_XLNX_VERSAL_CFU_FDRO "xlnx-versal-cfu-fdro"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalCFUFDRO, XLNX_VERSAL_CFU_FDRO)

#define TYPE_XLNX_VERSAL_CFU_SFR "xlnx-versal-cfu-sfr"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalCFUSFR, XLNX_VERSAL_CFU_SFR)

REG32(CFU_ISR, 0x0)
    FIELD(CFU_ISR, USR_GTS_EVENT, 9, 1)
    FIELD(CFU_ISR, USR_GSR_EVENT, 8, 1)
    FIELD(CFU_ISR, SLVERR, 7, 1)
    FIELD(CFU_ISR, DECOMP_ERROR, 6, 1)
    FIELD(CFU_ISR, BAD_CFI_PACKET, 5, 1)
    FIELD(CFU_ISR, AXI_ALIGN_ERROR, 4, 1)
    FIELD(CFU_ISR, CFI_ROW_ERROR, 3, 1)
    FIELD(CFU_ISR, CRC32_ERROR, 2, 1)
    FIELD(CFU_ISR, CRC8_ERROR, 1, 1)
    FIELD(CFU_ISR, SEU_ENDOFCALIB, 0, 1)
REG32(CFU_IMR, 0x4)
    FIELD(CFU_IMR, USR_GTS_EVENT, 9, 1)
    FIELD(CFU_IMR, USR_GSR_EVENT, 8, 1)
    FIELD(CFU_IMR, SLVERR, 7, 1)
    FIELD(CFU_IMR, DECOMP_ERROR, 6, 1)
    FIELD(CFU_IMR, BAD_CFI_PACKET, 5, 1)
    FIELD(CFU_IMR, AXI_ALIGN_ERROR, 4, 1)
    FIELD(CFU_IMR, CFI_ROW_ERROR, 3, 1)
    FIELD(CFU_IMR, CRC32_ERROR, 2, 1)
    FIELD(CFU_IMR, CRC8_ERROR, 1, 1)
    FIELD(CFU_IMR, SEU_ENDOFCALIB, 0, 1)
REG32(CFU_IER, 0x8)
    FIELD(CFU_IER, USR_GTS_EVENT, 9, 1)
    FIELD(CFU_IER, USR_GSR_EVENT, 8, 1)
    FIELD(CFU_IER, SLVERR, 7, 1)
    FIELD(CFU_IER, DECOMP_ERROR, 6, 1)
    FIELD(CFU_IER, BAD_CFI_PACKET, 5, 1)
    FIELD(CFU_IER, AXI_ALIGN_ERROR, 4, 1)
    FIELD(CFU_IER, CFI_ROW_ERROR, 3, 1)
    FIELD(CFU_IER, CRC32_ERROR, 2, 1)
    FIELD(CFU_IER, CRC8_ERROR, 1, 1)
    FIELD(CFU_IER, SEU_ENDOFCALIB, 0, 1)
REG32(CFU_IDR, 0xc)
    FIELD(CFU_IDR, USR_GTS_EVENT, 9, 1)
    FIELD(CFU_IDR, USR_GSR_EVENT, 8, 1)
    FIELD(CFU_IDR, SLVERR, 7, 1)
    FIELD(CFU_IDR, DECOMP_ERROR, 6, 1)
    FIELD(CFU_IDR, BAD_CFI_PACKET, 5, 1)
    FIELD(CFU_IDR, AXI_ALIGN_ERROR, 4, 1)
    FIELD(CFU_IDR, CFI_ROW_ERROR, 3, 1)
    FIELD(CFU_IDR, CRC32_ERROR, 2, 1)
    FIELD(CFU_IDR, CRC8_ERROR, 1, 1)
    FIELD(CFU_IDR, SEU_ENDOFCALIB, 0, 1)
REG32(CFU_ITR, 0x10)
    FIELD(CFU_ITR, USR_GTS_EVENT, 9, 1)
    FIELD(CFU_ITR, USR_GSR_EVENT, 8, 1)
    FIELD(CFU_ITR, SLVERR, 7, 1)
    FIELD(CFU_ITR, DECOMP_ERROR, 6, 1)
    FIELD(CFU_ITR, BAD_CFI_PACKET, 5, 1)
    FIELD(CFU_ITR, AXI_ALIGN_ERROR, 4, 1)
    FIELD(CFU_ITR, CFI_ROW_ERROR, 3, 1)
    FIELD(CFU_ITR, CRC32_ERROR, 2, 1)
    FIELD(CFU_ITR, CRC8_ERROR, 1, 1)
    FIELD(CFU_ITR, SEU_ENDOFCALIB, 0, 1)
REG32(CFU_PROTECT, 0x14)
    FIELD(CFU_PROTECT, ACTIVE, 0, 1)
REG32(CFU_FGCR, 0x18)
    FIELD(CFU_FGCR, GCLK_CAL, 14, 1)
    FIELD(CFU_FGCR, SC_HBC_TRIGGER, 13, 1)
    FIELD(CFU_FGCR, GLOW, 12, 1)
    FIELD(CFU_FGCR, GPWRDWN, 11, 1)
    FIELD(CFU_FGCR, GCAP, 10, 1)
    FIELD(CFU_FGCR, GSCWE, 9, 1)
    FIELD(CFU_FGCR, GHIGH_B, 8, 1)
    FIELD(CFU_FGCR, GMC_B, 7, 1)
    FIELD(CFU_FGCR, GWE, 6, 1)
    FIELD(CFU_FGCR, GRESTORE, 5, 1)
    FIELD(CFU_FGCR, GTS_CFG_B, 4, 1)
    FIELD(CFU_FGCR, GLUTMASK, 3, 1)
    FIELD(CFU_FGCR, EN_GLOBS_B, 2, 1)
    FIELD(CFU_FGCR, EOS, 1, 1)
    FIELD(CFU_FGCR, INIT_COMPLETE, 0, 1)
REG32(CFU_CTL, 0x1c)
    FIELD(CFU_CTL, GSR_GSC, 15, 1)
    FIELD(CFU_CTL, SLVERR_EN, 14, 1)
    FIELD(CFU_CTL, CRC32_RESET, 13, 1)
    FIELD(CFU_CTL, AXI_ERROR_EN, 12, 1)
    FIELD(CFU_CTL, FLUSH_AXI, 11, 1)
    FIELD(CFU_CTL, SSI_PER_SLR_PR, 10, 1)
    FIELD(CFU_CTL, GCAP_CLK_EN, 9, 1)
    FIELD(CFU_CTL, STATUS_SYNC_DISABLE, 8, 1)
    FIELD(CFU_CTL, IGNORE_CFI_ERROR, 7, 1)
    FIELD(CFU_CTL, CFRAME_DISABLE, 6, 1)
    FIELD(CFU_CTL, QWORD_CNT_RESET, 5, 1)
    FIELD(CFU_CTL, CRC8_DISABLE, 4, 1)
    FIELD(CFU_CTL, CRC32_CHECK, 3, 1)
    FIELD(CFU_CTL, DECOMPRESS, 2, 1)
    FIELD(CFU_CTL, SEU_GO, 1, 1)
    FIELD(CFU_CTL, CFI_LOCAL_RESET, 0, 1)
REG32(CFU_CRAM_RW, 0x20)
    FIELD(CFU_CRAM_RW, RFIFO_AFULL_DEPTH, 18, 9)
    FIELD(CFU_CRAM_RW, RD_WAVE_CNT_LEFT, 12, 6)
    FIELD(CFU_CRAM_RW, RD_WAVE_CNT, 6, 6)
    FIELD(CFU_CRAM_RW, WR_WAVE_CNT, 0, 6)
REG32(CFU_MASK, 0x28)
REG32(CFU_CRC_EXPECT, 0x2c)
REG32(CFU_CFRAME_LEFT_T0, 0x60)
    FIELD(CFU_CFRAME_LEFT_T0, NUM, 0, 20)
REG32(CFU_CFRAME_LEFT_T1, 0x64)
    FIELD(CFU_CFRAME_LEFT_T1, NUM, 0, 20)
REG32(CFU_CFRAME_LEFT_T2, 0x68)
    FIELD(CFU_CFRAME_LEFT_T2, NUM, 0, 20)
REG32(CFU_ROW_RANGE, 0x6c)
    FIELD(CFU_ROW_RANGE, HALF_FSR, 5, 1)
    FIELD(CFU_ROW_RANGE, NUM, 0, 5)
REG32(CFU_STATUS, 0x100)
    FIELD(CFU_STATUS, SEU_WRITE_ERROR, 30, 1)
    FIELD(CFU_STATUS, FRCNT_ERROR, 29, 1)
    FIELD(CFU_STATUS, RSVD_ERROR, 28, 1)
    FIELD(CFU_STATUS, FDRO_ERROR, 27, 1)
    FIELD(CFU_STATUS, FDRI_ERROR, 26, 1)
    FIELD(CFU_STATUS, FDRI_READ_ERROR, 25, 1)
    FIELD(CFU_STATUS, READ_FDRI_ERROR, 24, 1)
    FIELD(CFU_STATUS, READ_SFR_ERROR, 23, 1)
    FIELD(CFU_STATUS, READ_STREAM_ERROR, 22, 1)
    FIELD(CFU_STATUS, UNKNOWN_STREAM_PKT, 21, 1)
    FIELD(CFU_STATUS, USR_GTS, 20, 1)
    FIELD(CFU_STATUS, USR_GSR, 19, 1)
    FIELD(CFU_STATUS, AXI_BAD_WSTRB, 18, 1)
    FIELD(CFU_STATUS, AXI_BAD_AR_SIZE, 17, 1)
    FIELD(CFU_STATUS, AXI_BAD_AW_SIZE, 16, 1)
    FIELD(CFU_STATUS, AXI_BAD_ARADDR, 15, 1)
    FIELD(CFU_STATUS, AXI_BAD_AWADDR, 14, 1)
    FIELD(CFU_STATUS, SCAN_CLEAR_PASS, 13, 1)
    FIELD(CFU_STATUS, HC_SEC_ERROR, 12, 1)
    FIELD(CFU_STATUS, GHIGH_B_ISHIGH, 11, 1)
    FIELD(CFU_STATUS, GHIGH_B_ISLOW, 10, 1)
    FIELD(CFU_STATUS, GMC_B_ISHIGH, 9, 1)
    FIELD(CFU_STATUS, GMC_B_ISLOW, 8, 1)
    FIELD(CFU_STATUS, GPWRDWN_B_ISHIGH, 7, 1)
    FIELD(CFU_STATUS, CFI_SEU_CRC_ERROR, 6, 1)
    FIELD(CFU_STATUS, CFI_SEU_ECC_ERROR, 5, 1)
    FIELD(CFU_STATUS, CFI_SEU_HEARTBEAT, 4, 1)
    FIELD(CFU_STATUS, SCAN_CLEAR_DONE, 3, 1)
    FIELD(CFU_STATUS, HC_COMPLETE, 2, 1)
    FIELD(CFU_STATUS, CFI_CFRAME_BUSY, 1, 1)
    FIELD(CFU_STATUS, CFU_STREAM_BUSY, 0, 1)
REG32(CFU_INTERNAL_STATUS, 0x104)
    FIELD(CFU_INTERNAL_STATUS, SSI_EOS, 22, 1)
    FIELD(CFU_INTERNAL_STATUS, SSI_GWE, 21, 1)
    FIELD(CFU_INTERNAL_STATUS, RFIFO_EMPTY, 20, 1)
    FIELD(CFU_INTERNAL_STATUS, RFIFO_FULL, 19, 1)
    FIELD(CFU_INTERNAL_STATUS, SEL_SFR, 18, 1)
    FIELD(CFU_INTERNAL_STATUS, STREAM_CFRAME, 17, 1)
    FIELD(CFU_INTERNAL_STATUS, FDRI_PHASE, 16, 1)
    FIELD(CFU_INTERNAL_STATUS, CFI_PIPE_EN, 15, 1)
    FIELD(CFU_INTERNAL_STATUS, AWFIFO_DCNT, 10, 5)
    FIELD(CFU_INTERNAL_STATUS, WFIFO_DCNT, 5, 5)
    FIELD(CFU_INTERNAL_STATUS, REPAIR_BUSY, 4, 1)
    FIELD(CFU_INTERNAL_STATUS, TRIMU_BUSY, 3, 1)
    FIELD(CFU_INTERNAL_STATUS, TRIMB_BUSY, 2, 1)
    FIELD(CFU_INTERNAL_STATUS, HCLEANR_BUSY, 1, 1)
    FIELD(CFU_INTERNAL_STATUS, HCLEAN_BUSY, 0, 1)
REG32(CFU_QWORD_CNT, 0x108)
REG32(CFU_CRC_LIVE, 0x10c)
REG32(CFU_PENDING_READ_CNT, 0x110)
    FIELD(CFU_PENDING_READ_CNT, NUM, 0, 25)
REG32(CFU_FDRI_CNT, 0x114)
REG32(CFU_ECO1, 0x118)
REG32(CFU_ECO2, 0x11c)

#define R_MAX (R_CFU_ECO2 + 1)

#define NUM_STREAM 2
#define WFIFO_SZ 4

struct XlnxVersalCFUAPB {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    MemoryRegion iomem_stream[NUM_STREAM];
    qemu_irq irq_cfu_imr;

    /* 128-bit wfifo.  */
    uint32_t wfifo[WFIFO_SZ];

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];

    uint8_t fdri_row_addr;

    struct {
        XlnxCfiIf *cframe[15];
    } cfg;
};


struct XlnxVersalCFUFDRO {
    SysBusDevice parent_obj;
    MemoryRegion iomem_fdro;

    Fifo32 fdro_data;
};

struct XlnxVersalCFUSFR {
    SysBusDevice parent_obj;
    MemoryRegion iomem_sfr;

    /* 128-bit wfifo. */
    uint32_t wfifo[WFIFO_SZ];

    struct {
        XlnxVersalCFUAPB *cfu;
    } cfg;
};

/**
 * This is a helper function for updating a CFI data write fifo, an array of 4
 * uint32_t and 128 bits of data that are allowed to be written through 4
 * sequential 32 bit accesses. After the last index has been written into the
 * write fifo (wfifo), the data is copied to and returned in a secondary fifo
 * provided to the function (wfifo_ret), and the write fifo is cleared
 * (zeroized).
 *
 * @addr: the address used when calculating the wfifo array index to update
 * @value: the value to write into the wfifo array
 * @wfifo: the wfifo to update
 * @wfifo_out: will return the wfifo data when all 128 bits have been written
 *
 * @return: true if all 128 bits have been updated.
 */
bool update_wfifo(hwaddr addr, uint64_t value,
                  uint32_t *wfifo, uint32_t *wfifo_ret);

#endif
