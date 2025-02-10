/*
 * QEMU model of Xilinx Versal's OSPI controller.
 *
 * Copyright (c) 2021 Xilinx Inc.
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
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/ssi/xlnx-versal-ospi.h"

#ifndef XILINX_VERSAL_OSPI_ERR_DEBUG
#define XILINX_VERSAL_OSPI_ERR_DEBUG 0
#endif

REG32(CONFIG_REG, 0x0)
    FIELD(CONFIG_REG, IDLE_FLD, 31, 1)
    FIELD(CONFIG_REG, DUAL_BYTE_OPCODE_EN_FLD, 30, 1)
    FIELD(CONFIG_REG, CRC_ENABLE_FLD, 29, 1)
    FIELD(CONFIG_REG, CONFIG_RESV2_FLD, 26, 3)
    FIELD(CONFIG_REG, PIPELINE_PHY_FLD, 25, 1)
    FIELD(CONFIG_REG, ENABLE_DTR_PROTOCOL_FLD, 24, 1)
    FIELD(CONFIG_REG, ENABLE_AHB_DECODER_FLD, 23, 1)
    FIELD(CONFIG_REG, MSTR_BAUD_DIV_FLD, 19, 4)
    FIELD(CONFIG_REG, ENTER_XIP_MODE_IMM_FLD, 18, 1)
    FIELD(CONFIG_REG, ENTER_XIP_MODE_FLD, 17, 1)
    FIELD(CONFIG_REG, ENB_AHB_ADDR_REMAP_FLD, 16, 1)
    FIELD(CONFIG_REG, ENB_DMA_IF_FLD, 15, 1)
    FIELD(CONFIG_REG, WR_PROT_FLASH_FLD, 14, 1)
    FIELD(CONFIG_REG, PERIPH_CS_LINES_FLD, 10, 4)
    FIELD(CONFIG_REG, PERIPH_SEL_DEC_FLD, 9, 1)
    FIELD(CONFIG_REG, ENB_LEGACY_IP_MODE_FLD, 8, 1)
    FIELD(CONFIG_REG, ENB_DIR_ACC_CTLR_FLD, 7, 1)
    FIELD(CONFIG_REG, RESET_CFG_FLD, 6, 1)
    FIELD(CONFIG_REG, RESET_PIN_FLD, 5, 1)
    FIELD(CONFIG_REG, HOLD_PIN_FLD, 4, 1)
    FIELD(CONFIG_REG, PHY_MODE_ENABLE_FLD, 3, 1)
    FIELD(CONFIG_REG, SEL_CLK_PHASE_FLD, 2, 1)
    FIELD(CONFIG_REG, SEL_CLK_POL_FLD, 1, 1)
    FIELD(CONFIG_REG, ENB_SPI_FLD, 0, 1)
REG32(DEV_INSTR_RD_CONFIG_REG, 0x4)
    FIELD(DEV_INSTR_RD_CONFIG_REG, RD_INSTR_RESV5_FLD, 29, 3)
    FIELD(DEV_INSTR_RD_CONFIG_REG, DUMMY_RD_CLK_CYCLES_FLD, 24, 5)
    FIELD(DEV_INSTR_RD_CONFIG_REG, RD_INSTR_RESV4_FLD, 21, 3)
    FIELD(DEV_INSTR_RD_CONFIG_REG, MODE_BIT_ENABLE_FLD, 20, 1)
    FIELD(DEV_INSTR_RD_CONFIG_REG, RD_INSTR_RESV3_FLD, 18, 2)
    FIELD(DEV_INSTR_RD_CONFIG_REG, DATA_XFER_TYPE_EXT_MODE_FLD, 16, 2)
    FIELD(DEV_INSTR_RD_CONFIG_REG, RD_INSTR_RESV2_FLD, 14, 2)
    FIELD(DEV_INSTR_RD_CONFIG_REG, ADDR_XFER_TYPE_STD_MODE_FLD, 12, 2)
    FIELD(DEV_INSTR_RD_CONFIG_REG, PRED_DIS_FLD, 11, 1)
    FIELD(DEV_INSTR_RD_CONFIG_REG, DDR_EN_FLD, 10, 1)
    FIELD(DEV_INSTR_RD_CONFIG_REG, INSTR_TYPE_FLD, 8, 2)
    FIELD(DEV_INSTR_RD_CONFIG_REG, RD_OPCODE_NON_XIP_FLD, 0, 8)
REG32(DEV_INSTR_WR_CONFIG_REG, 0x8)
    FIELD(DEV_INSTR_WR_CONFIG_REG, WR_INSTR_RESV4_FLD, 29, 3)
    FIELD(DEV_INSTR_WR_CONFIG_REG, DUMMY_WR_CLK_CYCLES_FLD, 24, 5)
    FIELD(DEV_INSTR_WR_CONFIG_REG, WR_INSTR_RESV3_FLD, 18, 6)
    FIELD(DEV_INSTR_WR_CONFIG_REG, DATA_XFER_TYPE_EXT_MODE_FLD, 16, 2)
    FIELD(DEV_INSTR_WR_CONFIG_REG, WR_INSTR_RESV2_FLD, 14, 2)
    FIELD(DEV_INSTR_WR_CONFIG_REG, ADDR_XFER_TYPE_STD_MODE_FLD, 12, 2)
    FIELD(DEV_INSTR_WR_CONFIG_REG, WR_INSTR_RESV1_FLD, 9, 3)
    FIELD(DEV_INSTR_WR_CONFIG_REG, WEL_DIS_FLD, 8, 1)
    FIELD(DEV_INSTR_WR_CONFIG_REG, WR_OPCODE_FLD, 0, 8)
REG32(DEV_DELAY_REG, 0xc)
    FIELD(DEV_DELAY_REG, D_NSS_FLD, 24, 8)
    FIELD(DEV_DELAY_REG, D_BTWN_FLD, 16, 8)
    FIELD(DEV_DELAY_REG, D_AFTER_FLD, 8, 8)
    FIELD(DEV_DELAY_REG, D_INIT_FLD, 0, 8)
REG32(RD_DATA_CAPTURE_REG, 0x10)
    FIELD(RD_DATA_CAPTURE_REG, RD_DATA_RESV3_FLD, 20, 12)
    FIELD(RD_DATA_CAPTURE_REG, DDR_READ_DELAY_FLD, 16, 4)
    FIELD(RD_DATA_CAPTURE_REG, RD_DATA_RESV2_FLD, 9, 7)
    FIELD(RD_DATA_CAPTURE_REG, DQS_ENABLE_FLD, 8, 1)
    FIELD(RD_DATA_CAPTURE_REG, RD_DATA_RESV1_FLD, 6, 2)
    FIELD(RD_DATA_CAPTURE_REG, SAMPLE_EDGE_SEL_FLD, 5, 1)
    FIELD(RD_DATA_CAPTURE_REG, DELAY_FLD, 1, 4)
    FIELD(RD_DATA_CAPTURE_REG, BYPASS_FLD, 0, 1)
REG32(DEV_SIZE_CONFIG_REG, 0x14)
    FIELD(DEV_SIZE_CONFIG_REG, DEV_SIZE_RESV_FLD, 29, 3)
    FIELD(DEV_SIZE_CONFIG_REG, MEM_SIZE_ON_CS3_FLD, 27, 2)
    FIELD(DEV_SIZE_CONFIG_REG, MEM_SIZE_ON_CS2_FLD, 25, 2)
    FIELD(DEV_SIZE_CONFIG_REG, MEM_SIZE_ON_CS1_FLD, 23, 2)
    FIELD(DEV_SIZE_CONFIG_REG, MEM_SIZE_ON_CS0_FLD, 21, 2)
    FIELD(DEV_SIZE_CONFIG_REG, BYTES_PER_SUBSECTOR_FLD, 16, 5)
    FIELD(DEV_SIZE_CONFIG_REG, BYTES_PER_DEVICE_PAGE_FLD, 4, 12)
    FIELD(DEV_SIZE_CONFIG_REG, NUM_ADDR_BYTES_FLD, 0, 4)
REG32(SRAM_PARTITION_CFG_REG, 0x18)
    FIELD(SRAM_PARTITION_CFG_REG, SRAM_PARTITION_RESV_FLD, 8, 24)
    FIELD(SRAM_PARTITION_CFG_REG, ADDR_FLD, 0, 8)
REG32(IND_AHB_ADDR_TRIGGER_REG, 0x1c)
REG32(DMA_PERIPH_CONFIG_REG, 0x20)
    FIELD(DMA_PERIPH_CONFIG_REG, DMA_PERIPH_RESV2_FLD, 12, 20)
    FIELD(DMA_PERIPH_CONFIG_REG, NUM_BURST_REQ_BYTES_FLD, 8, 4)
    FIELD(DMA_PERIPH_CONFIG_REG, DMA_PERIPH_RESV1_FLD, 4, 4)
    FIELD(DMA_PERIPH_CONFIG_REG, NUM_SINGLE_REQ_BYTES_FLD, 0, 4)
REG32(REMAP_ADDR_REG, 0x24)
REG32(MODE_BIT_CONFIG_REG, 0x28)
    FIELD(MODE_BIT_CONFIG_REG, RX_CRC_DATA_LOW_FLD, 24, 8)
    FIELD(MODE_BIT_CONFIG_REG, RX_CRC_DATA_UP_FLD, 16, 8)
    FIELD(MODE_BIT_CONFIG_REG, CRC_OUT_ENABLE_FLD, 15, 1)
    FIELD(MODE_BIT_CONFIG_REG, MODE_BIT_RESV1_FLD, 11, 4)
    FIELD(MODE_BIT_CONFIG_REG, CHUNK_SIZE_FLD, 8, 3)
    FIELD(MODE_BIT_CONFIG_REG, MODE_FLD, 0, 8)
REG32(SRAM_FILL_REG, 0x2c)
    FIELD(SRAM_FILL_REG, SRAM_FILL_INDAC_WRITE_FLD, 16, 16)
    FIELD(SRAM_FILL_REG, SRAM_FILL_INDAC_READ_FLD, 0, 16)
REG32(TX_THRESH_REG, 0x30)
    FIELD(TX_THRESH_REG, TX_THRESH_RESV_FLD, 5, 27)
    FIELD(TX_THRESH_REG, LEVEL_FLD, 0, 5)
REG32(RX_THRESH_REG, 0x34)
    FIELD(RX_THRESH_REG, RX_THRESH_RESV_FLD, 5, 27)
    FIELD(RX_THRESH_REG, LEVEL_FLD, 0, 5)
REG32(WRITE_COMPLETION_CTRL_REG, 0x38)
    FIELD(WRITE_COMPLETION_CTRL_REG, POLL_REP_DELAY_FLD, 24, 8)
    FIELD(WRITE_COMPLETION_CTRL_REG, POLL_COUNT_FLD, 16, 8)
    FIELD(WRITE_COMPLETION_CTRL_REG, ENABLE_POLLING_EXP_FLD, 15, 1)
    FIELD(WRITE_COMPLETION_CTRL_REG, DISABLE_POLLING_FLD, 14, 1)
    FIELD(WRITE_COMPLETION_CTRL_REG, POLLING_POLARITY_FLD, 13, 1)
    FIELD(WRITE_COMPLETION_CTRL_REG, WR_COMP_CTRL_RESV1_FLD, 12, 1)
    FIELD(WRITE_COMPLETION_CTRL_REG, POLLING_ADDR_EN_FLD, 11, 1)
    FIELD(WRITE_COMPLETION_CTRL_REG, POLLING_BIT_INDEX_FLD, 8, 3)
    FIELD(WRITE_COMPLETION_CTRL_REG, OPCODE_FLD, 0, 8)
REG32(NO_OF_POLLS_BEF_EXP_REG, 0x3c)
REG32(IRQ_STATUS_REG, 0x40)
    FIELD(IRQ_STATUS_REG, IRQ_STAT_RESV_FLD, 20, 12)
    FIELD(IRQ_STATUS_REG, ECC_FAIL_FLD, 19, 1)
    FIELD(IRQ_STATUS_REG, TX_CRC_CHUNK_BRK_FLD, 18, 1)
    FIELD(IRQ_STATUS_REG, RX_CRC_DATA_VAL_FLD, 17, 1)
    FIELD(IRQ_STATUS_REG, RX_CRC_DATA_ERR_FLD, 16, 1)
    FIELD(IRQ_STATUS_REG, IRQ_STAT_RESV1_FLD, 15, 1)
    FIELD(IRQ_STATUS_REG, STIG_REQ_INT_FLD, 14, 1)
    FIELD(IRQ_STATUS_REG, POLL_EXP_INT_FLD, 13, 1)
    FIELD(IRQ_STATUS_REG, INDRD_SRAM_FULL_FLD, 12, 1)
    FIELD(IRQ_STATUS_REG, RX_FIFO_FULL_FLD, 11, 1)
    FIELD(IRQ_STATUS_REG, RX_FIFO_NOT_EMPTY_FLD, 10, 1)
    FIELD(IRQ_STATUS_REG, TX_FIFO_FULL_FLD, 9, 1)
    FIELD(IRQ_STATUS_REG, TX_FIFO_NOT_FULL_FLD, 8, 1)
    FIELD(IRQ_STATUS_REG, RECV_OVERFLOW_FLD, 7, 1)
    FIELD(IRQ_STATUS_REG, INDIRECT_XFER_LEVEL_BREACH_FLD, 6, 1)
    FIELD(IRQ_STATUS_REG, ILLEGAL_ACCESS_DET_FLD, 5, 1)
    FIELD(IRQ_STATUS_REG, PROT_WR_ATTEMPT_FLD, 4, 1)
    FIELD(IRQ_STATUS_REG, INDIRECT_TRANSFER_REJECT_FLD, 3, 1)
    FIELD(IRQ_STATUS_REG, INDIRECT_OP_DONE_FLD, 2, 1)
    FIELD(IRQ_STATUS_REG, UNDERFLOW_DET_FLD, 1, 1)
    FIELD(IRQ_STATUS_REG, MODE_M_FAIL_FLD, 0, 1)
REG32(IRQ_MASK_REG, 0x44)
    FIELD(IRQ_MASK_REG, IRQ_MASK_RESV_FLD, 20, 12)
    FIELD(IRQ_MASK_REG, ECC_FAIL_MASK_FLD, 19, 1)
    FIELD(IRQ_MASK_REG, TX_CRC_CHUNK_BRK_MASK_FLD, 18, 1)
    FIELD(IRQ_MASK_REG, RX_CRC_DATA_VAL_MASK_FLD, 17, 1)
    FIELD(IRQ_MASK_REG, RX_CRC_DATA_ERR_MASK_FLD, 16, 1)
    FIELD(IRQ_MASK_REG, IRQ_MASK_RESV1_FLD, 15, 1)
    FIELD(IRQ_MASK_REG, STIG_REQ_MASK_FLD, 14, 1)
    FIELD(IRQ_MASK_REG, POLL_EXP_INT_MASK_FLD, 13, 1)
    FIELD(IRQ_MASK_REG, INDRD_SRAM_FULL_MASK_FLD, 12, 1)
    FIELD(IRQ_MASK_REG, RX_FIFO_FULL_MASK_FLD, 11, 1)
    FIELD(IRQ_MASK_REG, RX_FIFO_NOT_EMPTY_MASK_FLD, 10, 1)
    FIELD(IRQ_MASK_REG, TX_FIFO_FULL_MASK_FLD, 9, 1)
    FIELD(IRQ_MASK_REG, TX_FIFO_NOT_FULL_MASK_FLD, 8, 1)
    FIELD(IRQ_MASK_REG, RECV_OVERFLOW_MASK_FLD, 7, 1)
    FIELD(IRQ_MASK_REG, INDIRECT_XFER_LEVEL_BREACH_MASK_FLD, 6, 1)
    FIELD(IRQ_MASK_REG, ILLEGAL_ACCESS_DET_MASK_FLD, 5, 1)
    FIELD(IRQ_MASK_REG, PROT_WR_ATTEMPT_MASK_FLD, 4, 1)
    FIELD(IRQ_MASK_REG, INDIRECT_TRANSFER_REJECT_MASK_FLD, 3, 1)
    FIELD(IRQ_MASK_REG, INDIRECT_OP_DONE_MASK_FLD, 2, 1)
    FIELD(IRQ_MASK_REG, UNDERFLOW_DET_MASK_FLD, 1, 1)
    FIELD(IRQ_MASK_REG, MODE_M_FAIL_MASK_FLD, 0, 1)
REG32(LOWER_WR_PROT_REG, 0x50)
REG32(UPPER_WR_PROT_REG, 0x54)
REG32(WR_PROT_CTRL_REG, 0x58)
    FIELD(WR_PROT_CTRL_REG, WR_PROT_CTRL_RESV_FLD, 2, 30)
    FIELD(WR_PROT_CTRL_REG, ENB_FLD, 1, 1)
    FIELD(WR_PROT_CTRL_REG, INV_FLD, 0, 1)
REG32(INDIRECT_READ_XFER_CTRL_REG, 0x60)
    FIELD(INDIRECT_READ_XFER_CTRL_REG, INDIR_RD_XFER_RESV_FLD, 8, 24)
    FIELD(INDIRECT_READ_XFER_CTRL_REG, NUM_IND_OPS_DONE_FLD, 6, 2)
    FIELD(INDIRECT_READ_XFER_CTRL_REG, IND_OPS_DONE_STATUS_FLD, 5, 1)
    FIELD(INDIRECT_READ_XFER_CTRL_REG, RD_QUEUED_FLD, 4, 1)
    FIELD(INDIRECT_READ_XFER_CTRL_REG, SRAM_FULL_FLD, 3, 1)
    FIELD(INDIRECT_READ_XFER_CTRL_REG, RD_STATUS_FLD, 2, 1)
    FIELD(INDIRECT_READ_XFER_CTRL_REG, CANCEL_FLD, 1, 1)
    FIELD(INDIRECT_READ_XFER_CTRL_REG, START_FLD, 0, 1)
REG32(INDIRECT_READ_XFER_WATERMARK_REG, 0x64)
REG32(INDIRECT_READ_XFER_START_REG, 0x68)
REG32(INDIRECT_READ_XFER_NUM_BYTES_REG, 0x6c)
REG32(INDIRECT_WRITE_XFER_CTRL_REG, 0x70)
    FIELD(INDIRECT_WRITE_XFER_CTRL_REG, INDIR_WR_XFER_RESV2_FLD, 8, 24)
    FIELD(INDIRECT_WRITE_XFER_CTRL_REG, NUM_IND_OPS_DONE_FLD, 6, 2)
    FIELD(INDIRECT_WRITE_XFER_CTRL_REG, IND_OPS_DONE_STATUS_FLD, 5, 1)
    FIELD(INDIRECT_WRITE_XFER_CTRL_REG, WR_QUEUED_FLD, 4, 1)
    FIELD(INDIRECT_WRITE_XFER_CTRL_REG, INDIR_WR_XFER_RESV1_FLD, 3, 1)
    FIELD(INDIRECT_WRITE_XFER_CTRL_REG, WR_STATUS_FLD, 2, 1)
    FIELD(INDIRECT_WRITE_XFER_CTRL_REG, CANCEL_FLD, 1, 1)
    FIELD(INDIRECT_WRITE_XFER_CTRL_REG, START_FLD, 0, 1)
REG32(INDIRECT_WRITE_XFER_WATERMARK_REG, 0x74)
REG32(INDIRECT_WRITE_XFER_START_REG, 0x78)
REG32(INDIRECT_WRITE_XFER_NUM_BYTES_REG, 0x7c)
REG32(INDIRECT_TRIGGER_ADDR_RANGE_REG, 0x80)
    FIELD(INDIRECT_TRIGGER_ADDR_RANGE_REG, IND_RANGE_RESV1_FLD, 4, 28)
    FIELD(INDIRECT_TRIGGER_ADDR_RANGE_REG, IND_RANGE_WIDTH_FLD, 0, 4)
REG32(FLASH_COMMAND_CTRL_MEM_REG, 0x8c)
    FIELD(FLASH_COMMAND_CTRL_MEM_REG, FLASH_COMMAND_CTRL_MEM_RESV1_FLD, 29, 3)
    FIELD(FLASH_COMMAND_CTRL_MEM_REG, MEM_BANK_ADDR_FLD, 20, 9)
    FIELD(FLASH_COMMAND_CTRL_MEM_REG, FLASH_COMMAND_CTRL_MEM_RESV2_FLD, 19, 1)
    FIELD(FLASH_COMMAND_CTRL_MEM_REG, NB_OF_STIG_READ_BYTES_FLD, 16, 3)
    FIELD(FLASH_COMMAND_CTRL_MEM_REG, MEM_BANK_READ_DATA_FLD, 8, 8)
    FIELD(FLASH_COMMAND_CTRL_MEM_REG, FLASH_COMMAND_CTRL_MEM_RESV3_FLD, 2, 6)
    FIELD(FLASH_COMMAND_CTRL_MEM_REG, MEM_BANK_REQ_IN_PROGRESS_FLD, 1, 1)
    FIELD(FLASH_COMMAND_CTRL_MEM_REG, TRIGGER_MEM_BANK_REQ_FLD, 0, 1)
REG32(FLASH_CMD_CTRL_REG, 0x90)
    FIELD(FLASH_CMD_CTRL_REG, CMD_OPCODE_FLD, 24, 8)
    FIELD(FLASH_CMD_CTRL_REG, ENB_READ_DATA_FLD, 23, 1)
    FIELD(FLASH_CMD_CTRL_REG, NUM_RD_DATA_BYTES_FLD, 20, 3)
    FIELD(FLASH_CMD_CTRL_REG, ENB_COMD_ADDR_FLD, 19, 1)
    FIELD(FLASH_CMD_CTRL_REG, ENB_MODE_BIT_FLD, 18, 1)
    FIELD(FLASH_CMD_CTRL_REG, NUM_ADDR_BYTES_FLD, 16, 2)
    FIELD(FLASH_CMD_CTRL_REG, ENB_WRITE_DATA_FLD, 15, 1)
    FIELD(FLASH_CMD_CTRL_REG, NUM_WR_DATA_BYTES_FLD, 12, 3)
    FIELD(FLASH_CMD_CTRL_REG, NUM_DUMMY_CYCLES_FLD, 7, 5)
    FIELD(FLASH_CMD_CTRL_REG, FLASH_CMD_CTRL_RESV1_FLD, 3, 4)
    FIELD(FLASH_CMD_CTRL_REG, STIG_MEM_BANK_EN_FLD, 2, 1)
    FIELD(FLASH_CMD_CTRL_REG, CMD_EXEC_STATUS_FLD, 1, 1)
    FIELD(FLASH_CMD_CTRL_REG, CMD_EXEC_FLD, 0, 1)
REG32(FLASH_CMD_ADDR_REG, 0x94)
REG32(FLASH_RD_DATA_LOWER_REG, 0xa0)
REG32(FLASH_RD_DATA_UPPER_REG, 0xa4)
REG32(FLASH_WR_DATA_LOWER_REG, 0xa8)
REG32(FLASH_WR_DATA_UPPER_REG, 0xac)
REG32(POLLING_FLASH_STATUS_REG, 0xb0)
    FIELD(POLLING_FLASH_STATUS_REG, DEVICE_STATUS_RSVD_FLD2, 21, 11)
    FIELD(POLLING_FLASH_STATUS_REG, DEVICE_STATUS_NB_DUMMY, 16, 5)
    FIELD(POLLING_FLASH_STATUS_REG, DEVICE_STATUS_RSVD_FLD1, 9, 7)
    FIELD(POLLING_FLASH_STATUS_REG, DEVICE_STATUS_VALID_FLD, 8, 1)
    FIELD(POLLING_FLASH_STATUS_REG, DEVICE_STATUS_FLD, 0, 8)
REG32(PHY_CONFIGURATION_REG, 0xb4)
    FIELD(PHY_CONFIGURATION_REG, PHY_CONFIG_RESYNC_FLD, 31, 1)
    FIELD(PHY_CONFIGURATION_REG, PHY_CONFIG_RESET_FLD, 30, 1)
    FIELD(PHY_CONFIGURATION_REG, PHY_CONFIG_RX_DLL_BYPASS_FLD, 29, 1)
    FIELD(PHY_CONFIGURATION_REG, PHY_CONFIG_RESV2_FLD, 23, 6)
    FIELD(PHY_CONFIGURATION_REG, PHY_CONFIG_TX_DLL_DELAY_FLD, 16, 7)
    FIELD(PHY_CONFIGURATION_REG, PHY_CONFIG_RESV1_FLD, 7, 9)
    FIELD(PHY_CONFIGURATION_REG, PHY_CONFIG_RX_DLL_DELAY_FLD, 0, 7)
REG32(PHY_MASTER_CONTROL_REG, 0xb8)
    FIELD(PHY_MASTER_CONTROL_REG, PHY_MASTER_CONTROL_RESV3_FLD, 25, 7)
    FIELD(PHY_MASTER_CONTROL_REG, PHY_MASTER_LOCK_MODE_FLD, 24, 1)
    FIELD(PHY_MASTER_CONTROL_REG, PHY_MASTER_BYPASS_MODE_FLD, 23, 1)
    FIELD(PHY_MASTER_CONTROL_REG, PHY_MASTER_PHASE_DETECT_SELECTOR_FLD, 20, 3)
    FIELD(PHY_MASTER_CONTROL_REG, PHY_MASTER_CONTROL_RESV2_FLD, 19, 1)
    FIELD(PHY_MASTER_CONTROL_REG, PHY_MASTER_NB_INDICATIONS_FLD, 16, 3)
    FIELD(PHY_MASTER_CONTROL_REG, PHY_MASTER_CONTROL_RESV1_FLD, 7, 9)
    FIELD(PHY_MASTER_CONTROL_REG, PHY_MASTER_INITIAL_DELAY_FLD, 0, 7)
REG32(DLL_OBSERVABLE_LOWER_REG, 0xbc)
    FIELD(DLL_OBSERVABLE_LOWER_REG,
          DLL_OBSERVABLE_LOWER_DLL_LOCK_INC_FLD, 24, 8)
    FIELD(DLL_OBSERVABLE_LOWER_REG,
          DLL_OBSERVABLE_LOWER_DLL_LOCK_DEC_FLD, 16, 8)
    FIELD(DLL_OBSERVABLE_LOWER_REG,
          DLL_OBSERVABLE_LOWER_LOOPBACK_LOCK_FLD, 15, 1)
    FIELD(DLL_OBSERVABLE_LOWER_REG,
          DLL_OBSERVABLE_LOWER_LOCK_VALUE_FLD, 8, 7)
    FIELD(DLL_OBSERVABLE_LOWER_REG,
          DLL_OBSERVABLE_LOWER_UNLOCK_COUNTER_FLD, 3, 5)
    FIELD(DLL_OBSERVABLE_LOWER_REG,
          DLL_OBSERVABLE_LOWER_LOCK_MODE_FLD, 1, 2)
    FIELD(DLL_OBSERVABLE_LOWER_REG,
          DLL_OBSERVABLE_LOWER_DLL_LOCK_FLD, 0, 1)
REG32(DLL_OBSERVABLE_UPPER_REG, 0xc0)
    FIELD(DLL_OBSERVABLE_UPPER_REG,
          DLL_OBSERVABLE_UPPER_RESV2_FLD, 23, 9)
    FIELD(DLL_OBSERVABLE_UPPER_REG,
          DLL_OBSERVABLE_UPPER_TX_DECODER_OUTPUT_FLD, 16, 7)
    FIELD(DLL_OBSERVABLE_UPPER_REG,
          DLL_OBSERVABLE_UPPER_RESV1_FLD, 7, 9)
    FIELD(DLL_OBSERVABLE_UPPER_REG,
          DLL_OBSERVABLE__UPPER_RX_DECODER_OUTPUT_FLD, 0, 7)
REG32(OPCODE_EXT_LOWER_REG, 0xe0)
    FIELD(OPCODE_EXT_LOWER_REG, EXT_READ_OPCODE_FLD, 24, 8)
    FIELD(OPCODE_EXT_LOWER_REG, EXT_WRITE_OPCODE_FLD, 16, 8)
    FIELD(OPCODE_EXT_LOWER_REG, EXT_POLL_OPCODE_FLD, 8, 8)
    FIELD(OPCODE_EXT_LOWER_REG, EXT_STIG_OPCODE_FLD, 0, 8)
REG32(OPCODE_EXT_UPPER_REG, 0xe4)
    FIELD(OPCODE_EXT_UPPER_REG, WEL_OPCODE_FLD, 24, 8)
    FIELD(OPCODE_EXT_UPPER_REG, EXT_WEL_OPCODE_FLD, 16, 8)
    FIELD(OPCODE_EXT_UPPER_REG, OPCODE_EXT_UPPER_RESV1_FLD, 0, 16)
REG32(MODULE_ID_REG, 0xfc)
    FIELD(MODULE_ID_REG, FIX_PATCH_FLD, 24, 8)
    FIELD(MODULE_ID_REG, MODULE_ID_FLD, 8, 16)
    FIELD(MODULE_ID_REG, MODULE_ID_RESV_FLD, 2, 6)
    FIELD(MODULE_ID_REG, CONF_FLD, 0, 2)

#define RXFF_SZ 1024
#define TXFF_SZ 1024

#define MAX_RX_DEC_OUT 8

#define SZ_512MBIT (512 * 1024 * 1024)
#define SZ_1GBIT   (1024 * 1024 * 1024)
#define SZ_2GBIT   (2ULL * SZ_1GBIT)
#define SZ_4GBIT   (4ULL * SZ_1GBIT)

#define IS_IND_DMA_START(op) (op->done_bytes == 0)
/*
 * Bit field size of R_INDIRECT_WRITE_XFER_CTRL_REG_NUM_IND_OPS_DONE_FLD
 * is 2 bits, which can record max of 3 indac operations.
 */
#define IND_OPS_DONE_MAX 3

typedef enum {
    WREN = 0x6,
} FlashCMD;

static unsigned int ospi_stig_addr_len(XlnxVersalOspi *s)
{
    /* Num address bytes is NUM_ADDR_BYTES_FLD + 1 */
    return ARRAY_FIELD_EX32(s->regs,
                            FLASH_CMD_CTRL_REG, NUM_ADDR_BYTES_FLD) + 1;
}

static unsigned int ospi_stig_wr_data_len(XlnxVersalOspi *s)
{
    /* Num write data bytes is NUM_WR_DATA_BYTES_FLD + 1 */
    return ARRAY_FIELD_EX32(s->regs,
                            FLASH_CMD_CTRL_REG, NUM_WR_DATA_BYTES_FLD) + 1;
}

static unsigned int ospi_stig_rd_data_len(XlnxVersalOspi *s)
{
    /* Num read data bytes is NUM_RD_DATA_BYTES_FLD + 1 */
    return ARRAY_FIELD_EX32(s->regs,
                            FLASH_CMD_CTRL_REG, NUM_RD_DATA_BYTES_FLD) + 1;
}

/*
 * Status bits in R_IRQ_STATUS_REG are set when the event occurs and the
 * interrupt is enabled in the mask register ([1] Section 2.3.17)
 */
static void set_irq(XlnxVersalOspi *s, uint32_t set_mask)
{
    s->regs[R_IRQ_STATUS_REG] |= s->regs[R_IRQ_MASK_REG] & set_mask;
}

static void ospi_update_irq_line(XlnxVersalOspi *s)
{
    qemu_set_irq(s->irq, !!(s->regs[R_IRQ_STATUS_REG] &
                            s->regs[R_IRQ_MASK_REG]));
}

static uint8_t ospi_get_wr_opcode(XlnxVersalOspi *s)
{
    return ARRAY_FIELD_EX32(s->regs,
                            DEV_INSTR_WR_CONFIG_REG, WR_OPCODE_FLD);
}

static uint8_t ospi_get_rd_opcode(XlnxVersalOspi *s)
{
    return ARRAY_FIELD_EX32(s->regs,
                            DEV_INSTR_RD_CONFIG_REG, RD_OPCODE_NON_XIP_FLD);
}

static uint32_t ospi_get_num_addr_bytes(XlnxVersalOspi *s)
{
    /* Num address bytes is NUM_ADDR_BYTES_FLD + 1 */
    return ARRAY_FIELD_EX32(s->regs,
                            DEV_SIZE_CONFIG_REG, NUM_ADDR_BYTES_FLD) + 1;
}

static void ospi_stig_membank_req(XlnxVersalOspi *s)
{
    int idx = ARRAY_FIELD_EX32(s->regs,
                               FLASH_COMMAND_CTRL_MEM_REG, MEM_BANK_ADDR_FLD);

    ARRAY_FIELD_DP32(s->regs, FLASH_COMMAND_CTRL_MEM_REG,
                     MEM_BANK_READ_DATA_FLD, s->stig_membank[idx]);
}

static int ospi_stig_membank_rd_bytes(XlnxVersalOspi *s)
{
    int rd_data_fld = ARRAY_FIELD_EX32(s->regs, FLASH_COMMAND_CTRL_MEM_REG,
                                       NB_OF_STIG_READ_BYTES_FLD);
    static const int sizes[6] = { 16, 32, 64, 128, 256, 512 };
    return (rd_data_fld < 6) ? sizes[rd_data_fld] : 0;
}

static uint32_t ospi_get_page_sz(XlnxVersalOspi *s)
{
    return ARRAY_FIELD_EX32(s->regs,
                            DEV_SIZE_CONFIG_REG, BYTES_PER_DEVICE_PAGE_FLD);
}

static bool ospi_ind_rd_watermark_enabled(XlnxVersalOspi *s)
{
    return s->regs[R_INDIRECT_READ_XFER_WATERMARK_REG];
}

static void ind_op_advance(IndOp *op, unsigned int len)
{
    op->done_bytes += len;
    assert(op->done_bytes <= op->num_bytes);
    if (op->done_bytes == op->num_bytes) {
        op->completed = true;
    }
}

static uint32_t ind_op_next_byte(IndOp *op)
{
    return op->flash_addr + op->done_bytes;
}

static uint32_t ind_op_end_byte(IndOp *op)
{
    return op->flash_addr + op->num_bytes;
}

static void ospi_ind_op_next(IndOp *op)
{
    op[0] = op[1];
    op[1].completed = true;
}

static void ind_op_setup(IndOp *op, uint32_t flash_addr, uint32_t num_bytes)
{
    if (num_bytes & 0x3) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "OSPI indirect op num bytes not word aligned\n");
    }
    op->flash_addr = flash_addr;
    op->num_bytes = num_bytes;
    op->done_bytes = 0;
    op->completed = false;
}

static bool ospi_ind_op_completed(IndOp *op)
{
    return op->completed;
}

static bool ospi_ind_op_all_completed(XlnxVersalOspi *s)
{
    return s->rd_ind_op[0].completed && s->wr_ind_op[0].completed;
}

static void ospi_ind_op_cancel(IndOp *op)
{
    op[0].completed = true;
    op[1].completed = true;
}

static bool ospi_ind_op_add(IndOp *op, Fifo8 *fifo,
                            uint32_t flash_addr, uint32_t num_bytes)
{
    /* Check if first indirect op has been completed */
    if (op->completed) {
        fifo8_reset(fifo);
        ind_op_setup(op, flash_addr, num_bytes);
        return false;
    }

    /* Check if second indirect op has been completed */
    op++;
    if (op->completed) {
        ind_op_setup(op, flash_addr, num_bytes);
        return false;
    }
    return true;
}

static void ospi_ind_op_queue_up_rd(XlnxVersalOspi *s)
{
    uint32_t num_bytes = s->regs[R_INDIRECT_READ_XFER_NUM_BYTES_REG];
    uint32_t flash_addr = s->regs[R_INDIRECT_READ_XFER_START_REG];
    bool failed;

    failed = ospi_ind_op_add(s->rd_ind_op, &s->rx_sram, flash_addr, num_bytes);
    /* If two already queued set rd reject interrupt */
    if (failed) {
        set_irq(s, R_IRQ_STATUS_REG_INDIRECT_TRANSFER_REJECT_FLD_MASK);
    }
}

static void ospi_ind_op_queue_up_wr(XlnxVersalOspi *s)
{
    uint32_t num_bytes = s->regs[R_INDIRECT_WRITE_XFER_NUM_BYTES_REG];
    uint32_t flash_addr = s->regs[R_INDIRECT_WRITE_XFER_START_REG];
    bool failed;

    failed = ospi_ind_op_add(s->wr_ind_op, &s->tx_sram, flash_addr, num_bytes);
    /* If two already queued set rd reject interrupt */
    if (failed) {
        set_irq(s, R_IRQ_STATUS_REG_INDIRECT_TRANSFER_REJECT_FLD_MASK);
    }
}

static uint64_t flash_sz(XlnxVersalOspi *s, unsigned int cs)
{
    /* Flash sizes in MB */
    static const uint64_t sizes[4] = { SZ_512MBIT / 8, SZ_1GBIT / 8,
                                       SZ_2GBIT / 8, SZ_4GBIT / 8 };
    uint32_t v = s->regs[R_DEV_SIZE_CONFIG_REG];

    v >>= cs * R_DEV_SIZE_CONFIG_REG_MEM_SIZE_ON_CS0_FLD_LENGTH;
    return sizes[FIELD_EX32(v, DEV_SIZE_CONFIG_REG, MEM_SIZE_ON_CS0_FLD)];
}

static unsigned int ospi_get_block_sz(XlnxVersalOspi *s)
{
    unsigned int block_fld = ARRAY_FIELD_EX32(s->regs,
                                              DEV_SIZE_CONFIG_REG,
                                              BYTES_PER_SUBSECTOR_FLD);
    return 1 << block_fld;
}

static unsigned int flash_blocks(XlnxVersalOspi *s, unsigned int cs)
{
    unsigned int b_sz = ospi_get_block_sz(s);
    unsigned int f_sz = flash_sz(s, cs);

    return f_sz / b_sz;
}

static int ospi_ahb_decoder_cs(XlnxVersalOspi *s, hwaddr addr)
{
    uint64_t end_addr = 0;
    int cs;

    for (cs = 0; cs < s->num_cs; cs++) {
        end_addr += flash_sz(s, cs);
        if (addr < end_addr) {
            break;
        }
    }

    if (cs == s->num_cs) {
        /* Address is out of range */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "OSPI flash address does not fit in configuration\n");
        return -1;
    }
    return cs;
}

static void ospi_ahb_decoder_enable_cs(XlnxVersalOspi *s, hwaddr addr)
{
    int cs = ospi_ahb_decoder_cs(s, addr);

    if (cs >= 0) {
        for (int i = 0; i < s->num_cs; i++) {
            qemu_set_irq(s->cs_lines[i], cs != i);
        }
    }
}

static unsigned int single_cs(XlnxVersalOspi *s)
{
    unsigned int field = ARRAY_FIELD_EX32(s->regs,
                                          CONFIG_REG, PERIPH_CS_LINES_FLD);

    /*
     * Below one liner is a trick that finds the rightmost zero and makes sure
     * all other bits are turned to 1. It is a variant of the 'Isolate the
     * rightmost 0-bit' trick found below at the time of writing:
     *
     * https://emre.me/computer-science/bit-manipulation-tricks/
     *
     * 4'bXXX0 -> 4'b1110
     * 4'bXX01 -> 4'b1101
     * 4'bX011 -> 4'b1011
     * 4'b0111 -> 4'b0111
     * 4'b1111 -> 4'b1111
     */
    return (field | ~(field + 1)) & 0xf;
}

static void ospi_update_cs_lines(XlnxVersalOspi *s)
{
    unsigned int all_cs;
    int i;

    if (ARRAY_FIELD_EX32(s->regs, CONFIG_REG, PERIPH_SEL_DEC_FLD)) {
        all_cs = ARRAY_FIELD_EX32(s->regs, CONFIG_REG, PERIPH_CS_LINES_FLD);
    } else {
        all_cs = single_cs(s);
    }

    for (i = 0; i < s->num_cs; i++) {
        bool cs = (all_cs >> i) & 1;

        qemu_set_irq(s->cs_lines[i], cs);
    }
}

static void ospi_dac_cs(XlnxVersalOspi *s, hwaddr addr)
{
    if (ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENABLE_AHB_DECODER_FLD)) {
        ospi_ahb_decoder_enable_cs(s, addr);
    } else {
        ospi_update_cs_lines(s);
    }
}

static void ospi_disable_cs(XlnxVersalOspi *s)
{
    int i;

    for (i = 0; i < s->num_cs; i++) {
        qemu_set_irq(s->cs_lines[i], 1);
    }
}

static void ospi_flush_txfifo(XlnxVersalOspi *s)
{
    while (!fifo8_is_empty(&s->tx_fifo)) {
        uint32_t tx_rx = fifo8_pop(&s->tx_fifo);

        tx_rx = ssi_transfer(s->spi, tx_rx);
        fifo8_push(&s->rx_fifo, tx_rx);
    }
}

static void ospi_tx_fifo_push_address_raw(XlnxVersalOspi *s,
                                          uint32_t flash_addr,
                                          unsigned int addr_bytes)
{
    /* Push write address */
    if (addr_bytes == 4) {
        fifo8_push(&s->tx_fifo, flash_addr >> 24);
    }
    if (addr_bytes >= 3) {
        fifo8_push(&s->tx_fifo, flash_addr >> 16);
    }
    if (addr_bytes >= 2) {
        fifo8_push(&s->tx_fifo, flash_addr >> 8);
    }
    fifo8_push(&s->tx_fifo, flash_addr);
}

static void ospi_tx_fifo_push_address(XlnxVersalOspi *s, uint32_t flash_addr)
{
    /* Push write address */
    int addr_bytes = ospi_get_num_addr_bytes(s);

    ospi_tx_fifo_push_address_raw(s, flash_addr, addr_bytes);
}

static void ospi_tx_fifo_push_stig_addr(XlnxVersalOspi *s)
{
    uint32_t flash_addr = s->regs[R_FLASH_CMD_ADDR_REG];
    unsigned int addr_bytes = ospi_stig_addr_len(s);

    ospi_tx_fifo_push_address_raw(s, flash_addr, addr_bytes);
}

static void ospi_tx_fifo_push_rd_op_addr(XlnxVersalOspi *s, uint32_t flash_addr)
{
    uint8_t inst_code = ospi_get_rd_opcode(s);

    fifo8_reset(&s->tx_fifo);

    /* Push read opcode */
    fifo8_push(&s->tx_fifo, inst_code);

    /* Push read address */
    ospi_tx_fifo_push_address(s, flash_addr);
}

static void ospi_tx_fifo_push_stig_wr_data(XlnxVersalOspi *s)
{
    uint64_t data = s->regs[R_FLASH_WR_DATA_LOWER_REG];
    int wr_data_len = ospi_stig_wr_data_len(s);
    int i;

    data |= (uint64_t) s->regs[R_FLASH_WR_DATA_UPPER_REG] << 32;
    for (i = 0; i < wr_data_len; i++) {
        int shift = i * 8;
        fifo8_push(&s->tx_fifo, data >> shift);
    }
}

static void ospi_tx_fifo_push_stig_rd_data(XlnxVersalOspi *s)
{
    int rd_data_len;
    int i;

    if (ARRAY_FIELD_EX32(s->regs, FLASH_CMD_CTRL_REG, STIG_MEM_BANK_EN_FLD)) {
        rd_data_len = ospi_stig_membank_rd_bytes(s);
    } else {
        rd_data_len = ospi_stig_rd_data_len(s);
    }

    /* transmit second part (data) */
    for (i = 0; i < rd_data_len; ++i) {
        fifo8_push(&s->tx_fifo, 0);
    }
}

static void ospi_rx_fifo_pop_stig_rd_data(XlnxVersalOspi *s)
{
    int size = ospi_stig_rd_data_len(s);
    uint8_t bytes[8] = {};
    int i;

    size = MIN(fifo8_num_used(&s->rx_fifo), size);

    assert(size <= 8);

    for (i = 0; i < size; i++) {
        bytes[i] = fifo8_pop(&s->rx_fifo);
    }

    s->regs[R_FLASH_RD_DATA_LOWER_REG] = ldl_le_p(bytes);
    s->regs[R_FLASH_RD_DATA_UPPER_REG] = ldl_le_p(bytes + 4);
}

static void ospi_ind_read(XlnxVersalOspi *s, uint32_t flash_addr, uint32_t len)
{
    int i;

    /* Create first section of read cmd */
    ospi_tx_fifo_push_rd_op_addr(s, flash_addr);

    /* transmit first part */
    ospi_update_cs_lines(s);
    ospi_flush_txfifo(s);

    fifo8_reset(&s->rx_fifo);

    /* transmit second part (data) */
    for (i = 0; i < len; ++i) {
        fifo8_push(&s->tx_fifo, 0);
    }
    ospi_flush_txfifo(s);

    for (i = 0; i < len; ++i) {
        fifo8_push(&s->rx_sram, fifo8_pop(&s->rx_fifo));
    }

    /* done */
    ospi_disable_cs(s);
}

static unsigned int ospi_dma_burst_size(XlnxVersalOspi *s)
{
    return 1 << ARRAY_FIELD_EX32(s->regs,
                                 DMA_PERIPH_CONFIG_REG,
                                 NUM_BURST_REQ_BYTES_FLD);
}

static unsigned int ospi_dma_single_size(XlnxVersalOspi *s)
{
    return 1 << ARRAY_FIELD_EX32(s->regs,
                                 DMA_PERIPH_CONFIG_REG,
                                 NUM_SINGLE_REQ_BYTES_FLD);
}

static void ind_rd_inc_num_done(XlnxVersalOspi *s)
{
    unsigned int done = ARRAY_FIELD_EX32(s->regs,
                                         INDIRECT_READ_XFER_CTRL_REG,
                                         NUM_IND_OPS_DONE_FLD);
    if (done < IND_OPS_DONE_MAX) {
        done++;
    }
    done &= 0x3;
    ARRAY_FIELD_DP32(s->regs, INDIRECT_READ_XFER_CTRL_REG,
                     NUM_IND_OPS_DONE_FLD, done);
}

static void ospi_ind_rd_completed(XlnxVersalOspi *s)
{
    ARRAY_FIELD_DP32(s->regs, INDIRECT_READ_XFER_CTRL_REG,
                     IND_OPS_DONE_STATUS_FLD, 1);

    ind_rd_inc_num_done(s);
    ospi_ind_op_next(s->rd_ind_op);
    if (ospi_ind_op_all_completed(s)) {
        set_irq(s, R_IRQ_STATUS_REG_INDIRECT_OP_DONE_FLD_MASK);
    }
}

static void ospi_dma_read(XlnxVersalOspi *s)
{
    IndOp *op = s->rd_ind_op;
    uint32_t dma_len = op->num_bytes;
    uint32_t burst_sz = ospi_dma_burst_size(s);
    uint32_t single_sz = ospi_dma_single_size(s);
    uint32_t ind_trig_range;
    uint32_t remainder;
    XlnxCSUDMAClass *xcdc = XLNX_CSU_DMA_GET_CLASS(s->dma_src);

    ind_trig_range = (1 << ARRAY_FIELD_EX32(s->regs,
                                            INDIRECT_TRIGGER_ADDR_RANGE_REG,
                                            IND_RANGE_WIDTH_FLD));
    remainder = dma_len % burst_sz;
    remainder = remainder % single_sz;
    if (burst_sz > ind_trig_range || single_sz > ind_trig_range ||
        remainder != 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "OSPI DMA burst size / single size config error\n");
    }

    s->src_dma_inprog = true;
    if (xcdc->read(s->dma_src, 0, dma_len) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "OSPI DMA configuration error\n");
    }
    s->src_dma_inprog = false;
}

static void ospi_do_ind_read(XlnxVersalOspi *s)
{
    IndOp *op = s->rd_ind_op;
    uint32_t next_b;
    uint32_t end_b;
    uint32_t len;
    bool start_dma = IS_IND_DMA_START(op) && !s->src_dma_inprog;

    /* Continue to read flash until we run out of space in sram */
    while (!ospi_ind_op_completed(op) &&
           !fifo8_is_full(&s->rx_sram)) {
        /* Read requested number of bytes, max bytes limited to size of sram */
        next_b = ind_op_next_byte(op);
        end_b = next_b + fifo8_num_free(&s->rx_sram);
        end_b = MIN(end_b, ind_op_end_byte(op));

        len = end_b - next_b;
        ospi_ind_read(s, next_b, len);
        ind_op_advance(op, len);

        if (ospi_ind_rd_watermark_enabled(s)) {
            ARRAY_FIELD_DP32(s->regs, IRQ_STATUS_REG,
                             INDIRECT_XFER_LEVEL_BREACH_FLD, 1);
            set_irq(s,
                    R_IRQ_STATUS_REG_INDIRECT_XFER_LEVEL_BREACH_FLD_MASK);
        }

        if (!s->src_dma_inprog &&
            ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENB_DMA_IF_FLD)) {
            ospi_dma_read(s);
        }
    }

    /* Set sram full */
    if (fifo8_num_used(&s->rx_sram) == RXFF_SZ) {
        ARRAY_FIELD_DP32(s->regs,
                         INDIRECT_READ_XFER_CTRL_REG, SRAM_FULL_FLD, 1);
        set_irq(s, R_IRQ_STATUS_REG_INDRD_SRAM_FULL_FLD_MASK);
    }

    /* Signal completion if done, unless inside recursion via ospi_dma_read */
    if (!ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENB_DMA_IF_FLD) || start_dma) {
        if (ospi_ind_op_completed(op)) {
            ospi_ind_rd_completed(s);
        }
    }
}

/* Transmit write enable instruction */
static void ospi_transmit_wel(XlnxVersalOspi *s, bool ahb_decoder_cs,
                              hwaddr addr)
{
    fifo8_reset(&s->tx_fifo);
    fifo8_push(&s->tx_fifo, WREN);

    if (ahb_decoder_cs) {
        ospi_ahb_decoder_enable_cs(s, addr);
    } else {
        ospi_update_cs_lines(s);
    }

    ospi_flush_txfifo(s);
    ospi_disable_cs(s);

    fifo8_reset(&s->rx_fifo);
}

static void ospi_ind_write(XlnxVersalOspi *s, uint32_t flash_addr, uint32_t len)
{
    bool ahb_decoder_cs = false;
    uint8_t inst_code;
    int i;

    assert(fifo8_num_used(&s->tx_sram) >= len);

    if (!ARRAY_FIELD_EX32(s->regs, DEV_INSTR_WR_CONFIG_REG, WEL_DIS_FLD)) {
        ospi_transmit_wel(s, ahb_decoder_cs, 0);
    }

    /* reset fifos */
    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_fifo);

    /* Push write opcode */
    inst_code = ospi_get_wr_opcode(s);
    fifo8_push(&s->tx_fifo, inst_code);

    /* Push write address */
    ospi_tx_fifo_push_address(s, flash_addr);

    /* data */
    for (i = 0; i < len; i++) {
        fifo8_push(&s->tx_fifo, fifo8_pop(&s->tx_sram));
    }

    /* transmit */
    ospi_update_cs_lines(s);
    ospi_flush_txfifo(s);

    /* done */
    ospi_disable_cs(s);
    fifo8_reset(&s->rx_fifo);
}

static void ind_wr_inc_num_done(XlnxVersalOspi *s)
{
    unsigned int done = ARRAY_FIELD_EX32(s->regs, INDIRECT_WRITE_XFER_CTRL_REG,
                                         NUM_IND_OPS_DONE_FLD);
    if (done < IND_OPS_DONE_MAX) {
        done++;
    }
    done &= 0x3;
    ARRAY_FIELD_DP32(s->regs, INDIRECT_WRITE_XFER_CTRL_REG,
                     NUM_IND_OPS_DONE_FLD, done);
}

static void ospi_ind_wr_completed(XlnxVersalOspi *s)
{
    ARRAY_FIELD_DP32(s->regs, INDIRECT_WRITE_XFER_CTRL_REG,
                     IND_OPS_DONE_STATUS_FLD, 1);
    ind_wr_inc_num_done(s);
    ospi_ind_op_next(s->wr_ind_op);
    /* Set indirect op done interrupt if enabled */
    if (ospi_ind_op_all_completed(s)) {
        set_irq(s, R_IRQ_STATUS_REG_INDIRECT_OP_DONE_FLD_MASK);
    }
}

static void ospi_do_indirect_write(XlnxVersalOspi *s)
{
    uint32_t write_watermark = s->regs[R_INDIRECT_WRITE_XFER_WATERMARK_REG];
    uint32_t pagesz = ospi_get_page_sz(s);
    uint32_t page_mask = ~(pagesz - 1);
    IndOp *op = s->wr_ind_op;
    uint32_t next_b;
    uint32_t end_b;
    uint32_t len;

    /* Write out tx_fifo in maximum page sz chunks */
    while (!ospi_ind_op_completed(op) && fifo8_num_used(&s->tx_sram) > 0) {
        next_b = ind_op_next_byte(op);
        end_b = next_b +  MIN(fifo8_num_used(&s->tx_sram), pagesz);

        /* Dont cross page boundary */
        if ((end_b & page_mask) > next_b) {
            end_b &= page_mask;
        }

        len = end_b - next_b;
        len = MIN(len, op->num_bytes - op->done_bytes);
        ospi_ind_write(s, next_b, len);
        ind_op_advance(op, len);
    }

    /*
     * Always set indirect transfer level breached interrupt if enabled
     * (write watermark > 0) since the tx_sram always will be emptied
     */
    if (write_watermark > 0) {
        set_irq(s, R_IRQ_STATUS_REG_INDIRECT_XFER_LEVEL_BREACH_FLD_MASK);
    }

    /* Signal completions if done */
    if (ospi_ind_op_completed(op)) {
        ospi_ind_wr_completed(s);
    }
}

static void ospi_stig_fill_membank(XlnxVersalOspi *s)
{
    int num_rd_bytes = ospi_stig_membank_rd_bytes(s);
    int idx = num_rd_bytes - 8; /* first of last 8 */
    int i;

    for (i = 0; i < num_rd_bytes; i++) {
        s->stig_membank[i] = fifo8_pop(&s->rx_fifo);
    }

    g_assert((idx + 4) < ARRAY_SIZE(s->stig_membank));

    /* Fill in lower upper regs */
    s->regs[R_FLASH_RD_DATA_LOWER_REG] = ldl_le_p(&s->stig_membank[idx]);
    s->regs[R_FLASH_RD_DATA_UPPER_REG] = ldl_le_p(&s->stig_membank[idx + 4]);
}

static void ospi_stig_cmd_exec(XlnxVersalOspi *s)
{
    uint8_t inst_code;

    /* Reset fifos */
    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_fifo);

    /* Push write opcode */
    inst_code = ARRAY_FIELD_EX32(s->regs, FLASH_CMD_CTRL_REG, CMD_OPCODE_FLD);
    fifo8_push(&s->tx_fifo, inst_code);

    /* Push address if enabled */
    if (ARRAY_FIELD_EX32(s->regs, FLASH_CMD_CTRL_REG, ENB_COMD_ADDR_FLD)) {
        ospi_tx_fifo_push_stig_addr(s);
    }

    /* Enable cs */
    ospi_update_cs_lines(s);

    /* Data */
    if (ARRAY_FIELD_EX32(s->regs, FLASH_CMD_CTRL_REG, ENB_WRITE_DATA_FLD)) {
        ospi_tx_fifo_push_stig_wr_data(s);
    } else if (ARRAY_FIELD_EX32(s->regs,
                                FLASH_CMD_CTRL_REG, ENB_READ_DATA_FLD)) {
        /* transmit first part */
        ospi_flush_txfifo(s);
        fifo8_reset(&s->rx_fifo);
        ospi_tx_fifo_push_stig_rd_data(s);
    }

    /* Transmit */
    ospi_flush_txfifo(s);
    ospi_disable_cs(s);

    if (ARRAY_FIELD_EX32(s->regs, FLASH_CMD_CTRL_REG, ENB_READ_DATA_FLD)) {
        if (ARRAY_FIELD_EX32(s->regs,
                             FLASH_CMD_CTRL_REG, STIG_MEM_BANK_EN_FLD)) {
            ospi_stig_fill_membank(s);
        } else {
            ospi_rx_fifo_pop_stig_rd_data(s);
        }
    }
}

static uint32_t ospi_block_address(XlnxVersalOspi *s, unsigned int block)
{
    unsigned int block_sz = ospi_get_block_sz(s);
    unsigned int cs = 0;
    uint32_t addr = 0;

    while (cs < s->num_cs && block >= flash_blocks(s, cs)) {
        block -= flash_blocks(s, 0);
        addr += flash_sz(s, cs);
    }
    addr += block * block_sz;
    return addr;
}

static uint32_t ospi_get_wr_prot_addr_low(XlnxVersalOspi *s)
{
    unsigned int block = s->regs[R_LOWER_WR_PROT_REG];

    return ospi_block_address(s, block);
}

static uint32_t ospi_get_wr_prot_addr_upper(XlnxVersalOspi *s)
{
    unsigned int block = s->regs[R_UPPER_WR_PROT_REG];

    /* Get address of first block out of defined range */
    return ospi_block_address(s, block + 1);
}

static bool ospi_is_write_protected(XlnxVersalOspi *s, hwaddr addr)
{
    uint32_t wr_prot_addr_upper = ospi_get_wr_prot_addr_upper(s);
    uint32_t wr_prot_addr_low = ospi_get_wr_prot_addr_low(s);
    bool in_range = false;

    if (addr >= wr_prot_addr_low && addr < wr_prot_addr_upper) {
        in_range = true;
    }

    if (ARRAY_FIELD_EX32(s->regs, WR_PROT_CTRL_REG, INV_FLD)) {
        in_range = !in_range;
    }
    return in_range;
}

static uint64_t ospi_rx_sram_read(XlnxVersalOspi *s, unsigned int size)
{
    uint8_t bytes[8] = {};
    int i;

    if (size < 4 && fifo8_num_used(&s->rx_sram) >= 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "OSPI only last read of internal "
                      "sram is allowed to be < 32 bits\n");
    }

    size = MIN(fifo8_num_used(&s->rx_sram), size);

    assert(size <= 8);

    for (i = 0; i < size; i++) {
        bytes[i] = fifo8_pop(&s->rx_sram);
    }

    return ldq_le_p(bytes);
}

static void ospi_tx_sram_write(XlnxVersalOspi *s, uint64_t value,
                               unsigned int size)
{
    int i;
    for (i = 0; i < size && !fifo8_is_full(&s->tx_sram); i++) {
        fifo8_push(&s->tx_sram, value >> 8 * i);
    }
}

static uint64_t ospi_do_dac_read(void *opaque, hwaddr addr, unsigned int size)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(opaque);
    uint8_t bytes[8] = {};
    int i;

    /* Create first section of read cmd */
    ospi_tx_fifo_push_rd_op_addr(s, (uint32_t) addr);

    /* Enable cs and transmit first part */
    ospi_dac_cs(s, addr);
    ospi_flush_txfifo(s);

    fifo8_reset(&s->rx_fifo);

    /* transmit second part (data) */
    for (i = 0; i < size; ++i) {
        fifo8_push(&s->tx_fifo, 0);
    }
    ospi_flush_txfifo(s);

    /* fill in result */
    size = MIN(fifo8_num_used(&s->rx_fifo), size);

    assert(size <= 8);

    for (i = 0; i < size; i++) {
        bytes[i] = fifo8_pop(&s->rx_fifo);
    }

    /* done */
    ospi_disable_cs(s);

    return ldq_le_p(bytes);
}

static void ospi_do_dac_write(void *opaque,
                              hwaddr addr,
                              uint64_t value,
                              unsigned int size)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(opaque);
    bool ahb_decoder_cs = ARRAY_FIELD_EX32(s->regs, CONFIG_REG,
                                           ENABLE_AHB_DECODER_FLD);
    uint8_t inst_code;
    unsigned int i;

    if (!ARRAY_FIELD_EX32(s->regs, DEV_INSTR_WR_CONFIG_REG, WEL_DIS_FLD)) {
        ospi_transmit_wel(s, ahb_decoder_cs, addr);
    }

    /* reset fifos */
    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_fifo);

    /* Push write opcode */
    inst_code = ospi_get_wr_opcode(s);
    fifo8_push(&s->tx_fifo, inst_code);

    /* Push write address */
    ospi_tx_fifo_push_address(s, addr);

    /* data */
    for (i = 0; i < size; i++) {
        fifo8_push(&s->tx_fifo, value >> 8 * i);
    }

    /* Enable cs and transmit */
    ospi_dac_cs(s, addr);
    ospi_flush_txfifo(s);
    ospi_disable_cs(s);

    fifo8_reset(&s->rx_fifo);
}

static void flash_cmd_ctrl_mem_reg_post_write(RegisterInfo *reg,
                                              uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);
    if (ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENB_SPI_FLD)) {
        if (ARRAY_FIELD_EX32(s->regs,
                             FLASH_COMMAND_CTRL_MEM_REG,
                             TRIGGER_MEM_BANK_REQ_FLD)) {
            ospi_stig_membank_req(s);
            ARRAY_FIELD_DP32(s->regs, FLASH_COMMAND_CTRL_MEM_REG,
                             TRIGGER_MEM_BANK_REQ_FLD, 0);
        }
    }
}

static void flash_cmd_ctrl_reg_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);

    if (ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENB_SPI_FLD) &&
        ARRAY_FIELD_EX32(s->regs, FLASH_CMD_CTRL_REG, CMD_EXEC_FLD)) {
        ospi_stig_cmd_exec(s);
        set_irq(s, R_IRQ_STATUS_REG_STIG_REQ_INT_FLD_MASK);
        ARRAY_FIELD_DP32(s->regs, FLASH_CMD_CTRL_REG, CMD_EXEC_FLD, 0);
    }
}

static uint64_t ind_wr_dec_num_done(XlnxVersalOspi *s, uint64_t val)
{
    unsigned int done = ARRAY_FIELD_EX32(s->regs, INDIRECT_WRITE_XFER_CTRL_REG,
                                         NUM_IND_OPS_DONE_FLD);
    done--;
    done &= 0x3;
    val = FIELD_DP32(val, INDIRECT_WRITE_XFER_CTRL_REG,
                     NUM_IND_OPS_DONE_FLD, done);
    return val;
}

static bool ind_wr_clearing_op_done(XlnxVersalOspi *s, uint64_t new_val)
{
    bool set_in_reg = ARRAY_FIELD_EX32(s->regs, INDIRECT_WRITE_XFER_CTRL_REG,
                                       IND_OPS_DONE_STATUS_FLD);
    bool set_in_new_val = FIELD_EX32(new_val, INDIRECT_WRITE_XFER_CTRL_REG,
                                     IND_OPS_DONE_STATUS_FLD);
    /* return true if clearing bit */
    return set_in_reg && !set_in_new_val;
}

static uint64_t ind_wr_xfer_ctrl_reg_pre_write(RegisterInfo *reg,
                                               uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);

    if (ind_wr_clearing_op_done(s, val)) {
        val = ind_wr_dec_num_done(s, val);
    }
    return val;
}

static void ind_wr_xfer_ctrl_reg_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);

    if (s->ind_write_disabled) {
        return;
    }

    if (ARRAY_FIELD_EX32(s->regs, INDIRECT_WRITE_XFER_CTRL_REG, START_FLD)) {
        ospi_ind_op_queue_up_wr(s);
        ospi_do_indirect_write(s);
        ARRAY_FIELD_DP32(s->regs, INDIRECT_WRITE_XFER_CTRL_REG, START_FLD, 0);
    }

    if (ARRAY_FIELD_EX32(s->regs, INDIRECT_WRITE_XFER_CTRL_REG, CANCEL_FLD)) {
        ospi_ind_op_cancel(s->wr_ind_op);
        fifo8_reset(&s->tx_sram);
        ARRAY_FIELD_DP32(s->regs, INDIRECT_WRITE_XFER_CTRL_REG, CANCEL_FLD, 0);
    }
}

static uint64_t ind_wr_xfer_ctrl_reg_post_read(RegisterInfo *reg,
                                               uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);
    IndOp *op = s->wr_ind_op;

    /* Check if ind ops is ongoing */
    if (!ospi_ind_op_completed(&op[0])) {
        /* Check if two ind ops are queued */
        if (!ospi_ind_op_completed(&op[1])) {
            val = FIELD_DP32(val, INDIRECT_WRITE_XFER_CTRL_REG,
                             WR_QUEUED_FLD, 1);
        }
        val = FIELD_DP32(val, INDIRECT_WRITE_XFER_CTRL_REG, WR_STATUS_FLD, 1);
    }
    return val;
}

static uint64_t ind_rd_dec_num_done(XlnxVersalOspi *s, uint64_t val)
{
    unsigned int done = ARRAY_FIELD_EX32(s->regs, INDIRECT_READ_XFER_CTRL_REG,
                                         NUM_IND_OPS_DONE_FLD);
    done--;
    done &= 0x3;
    val = FIELD_DP32(val, INDIRECT_READ_XFER_CTRL_REG,
                     NUM_IND_OPS_DONE_FLD, done);
    return val;
}

static uint64_t ind_rd_xfer_ctrl_reg_pre_write(RegisterInfo *reg,
                                               uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);

    if (FIELD_EX32(val, INDIRECT_READ_XFER_CTRL_REG,
                   IND_OPS_DONE_STATUS_FLD)) {
        val = ind_rd_dec_num_done(s, val);
        val &= ~R_INDIRECT_READ_XFER_CTRL_REG_IND_OPS_DONE_STATUS_FLD_MASK;
    }
    return val;
}

static void ind_rd_xfer_ctrl_reg_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);

    if (ARRAY_FIELD_EX32(s->regs, INDIRECT_READ_XFER_CTRL_REG, START_FLD)) {
        ospi_ind_op_queue_up_rd(s);
        ospi_do_ind_read(s);
        ARRAY_FIELD_DP32(s->regs, INDIRECT_READ_XFER_CTRL_REG, START_FLD, 0);
    }

    if (ARRAY_FIELD_EX32(s->regs, INDIRECT_READ_XFER_CTRL_REG, CANCEL_FLD)) {
        ospi_ind_op_cancel(s->rd_ind_op);
        fifo8_reset(&s->rx_sram);
        ARRAY_FIELD_DP32(s->regs, INDIRECT_READ_XFER_CTRL_REG, CANCEL_FLD, 0);
    }
}

static uint64_t ind_rd_xfer_ctrl_reg_post_read(RegisterInfo *reg,
                                               uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);
    IndOp *op = s->rd_ind_op;

    /* Check if ind ops is ongoing */
    if (!ospi_ind_op_completed(&op[0])) {
        /* Check if two ind ops are queued */
        if (!ospi_ind_op_completed(&op[1])) {
            val = FIELD_DP32(val, INDIRECT_READ_XFER_CTRL_REG,
                             RD_QUEUED_FLD, 1);
        }
        val = FIELD_DP32(val, INDIRECT_READ_XFER_CTRL_REG, RD_STATUS_FLD, 1);
    }
    return val;
}

static uint64_t sram_fill_reg_post_read(RegisterInfo *reg, uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);
    val = ((fifo8_num_used(&s->tx_sram) & 0xFFFF) << 16) |
          (fifo8_num_used(&s->rx_sram) & 0xFFFF);
    return val;
}

static uint64_t dll_obs_upper_reg_post_read(RegisterInfo *reg, uint64_t val)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(reg->opaque);
    uint32_t rx_dec_out;

    rx_dec_out = FIELD_EX32(val, DLL_OBSERVABLE_UPPER_REG,
                            DLL_OBSERVABLE__UPPER_RX_DECODER_OUTPUT_FLD);

    if (rx_dec_out < MAX_RX_DEC_OUT) {
        ARRAY_FIELD_DP32(s->regs, DLL_OBSERVABLE_UPPER_REG,
                         DLL_OBSERVABLE__UPPER_RX_DECODER_OUTPUT_FLD,
                         rx_dec_out + 1);
    }

    return val;
}


static void xlnx_versal_ospi_reset(DeviceState *dev)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    fifo8_reset(&s->rx_fifo);
    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_sram);
    fifo8_reset(&s->tx_sram);

    s->rd_ind_op[0].completed = true;
    s->rd_ind_op[1].completed = true;
    s->wr_ind_op[0].completed = true;
    s->wr_ind_op[1].completed = true;
    ARRAY_FIELD_DP32(s->regs, DLL_OBSERVABLE_LOWER_REG,
                     DLL_OBSERVABLE_LOWER_DLL_LOCK_FLD, 1);
    ARRAY_FIELD_DP32(s->regs, DLL_OBSERVABLE_LOWER_REG,
                     DLL_OBSERVABLE_LOWER_LOOPBACK_LOCK_FLD, 1);
}

static RegisterAccessInfo ospi_regs_info[] = {
    {   .name = "CONFIG_REG",
        .addr = A_CONFIG_REG,
        .reset = 0x80780081,
        .ro = 0x9c000000,
    },{ .name = "DEV_INSTR_RD_CONFIG_REG",
        .addr = A_DEV_INSTR_RD_CONFIG_REG,
        .reset = 0x3,
        .ro = 0xe0ecc800,
    },{ .name = "DEV_INSTR_WR_CONFIG_REG",
        .addr = A_DEV_INSTR_WR_CONFIG_REG,
        .reset = 0x2,
        .ro = 0xe0fcce00,
    },{ .name = "DEV_DELAY_REG",
        .addr = A_DEV_DELAY_REG,
    },{ .name = "RD_DATA_CAPTURE_REG",
        .addr = A_RD_DATA_CAPTURE_REG,
        .reset = 0x1,
        .ro = 0xfff0fec0,
    },{ .name = "DEV_SIZE_CONFIG_REG",
        .addr = A_DEV_SIZE_CONFIG_REG,
        .reset = 0x101002,
        .ro = 0xe0000000,
    },{ .name = "SRAM_PARTITION_CFG_REG",
        .addr = A_SRAM_PARTITION_CFG_REG,
        .reset = 0x80,
        .ro = 0xffffff00,
    },{ .name = "IND_AHB_ADDR_TRIGGER_REG",
        .addr = A_IND_AHB_ADDR_TRIGGER_REG,
    },{ .name = "DMA_PERIPH_CONFIG_REG",
        .addr = A_DMA_PERIPH_CONFIG_REG,
        .ro = 0xfffff0f0,
    },{ .name = "REMAP_ADDR_REG",
        .addr = A_REMAP_ADDR_REG,
    },{ .name = "MODE_BIT_CONFIG_REG",
        .addr = A_MODE_BIT_CONFIG_REG,
        .reset = 0x200,
        .ro = 0xffff7800,
    },{ .name = "SRAM_FILL_REG",
        .addr = A_SRAM_FILL_REG,
        .ro = 0xffffffff,
        .post_read = sram_fill_reg_post_read,
    },{ .name = "TX_THRESH_REG",
        .addr = A_TX_THRESH_REG,
        .reset = 0x1,
        .ro = 0xffffffe0,
    },{ .name = "RX_THRESH_REG",
        .addr = A_RX_THRESH_REG,
        .reset = 0x1,
        .ro = 0xffffffe0,
    },{ .name = "WRITE_COMPLETION_CTRL_REG",
        .addr = A_WRITE_COMPLETION_CTRL_REG,
        .reset = 0x10005,
        .ro = 0x1800,
    },{ .name = "NO_OF_POLLS_BEF_EXP_REG",
        .addr = A_NO_OF_POLLS_BEF_EXP_REG,
        .reset = 0xffffffff,
    },{ .name = "IRQ_STATUS_REG",
        .addr = A_IRQ_STATUS_REG,
        .ro = 0xfff08000,
        .w1c = 0xf7fff,
    },{ .name = "IRQ_MASK_REG",
        .addr = A_IRQ_MASK_REG,
        .ro = 0xfff08000,
    },{ .name = "LOWER_WR_PROT_REG",
        .addr = A_LOWER_WR_PROT_REG,
    },{ .name = "UPPER_WR_PROT_REG",
        .addr = A_UPPER_WR_PROT_REG,
    },{ .name = "WR_PROT_CTRL_REG",
        .addr = A_WR_PROT_CTRL_REG,
        .ro = 0xfffffffc,
    },{ .name = "INDIRECT_READ_XFER_CTRL_REG",
        .addr = A_INDIRECT_READ_XFER_CTRL_REG,
        .ro = 0xffffffd4,
        .w1c = 0x08,
        .pre_write = ind_rd_xfer_ctrl_reg_pre_write,
        .post_write = ind_rd_xfer_ctrl_reg_post_write,
        .post_read = ind_rd_xfer_ctrl_reg_post_read,
    },{ .name = "INDIRECT_READ_XFER_WATERMARK_REG",
        .addr = A_INDIRECT_READ_XFER_WATERMARK_REG,
    },{ .name = "INDIRECT_READ_XFER_START_REG",
        .addr = A_INDIRECT_READ_XFER_START_REG,
    },{ .name = "INDIRECT_READ_XFER_NUM_BYTES_REG",
        .addr = A_INDIRECT_READ_XFER_NUM_BYTES_REG,
    },{ .name = "INDIRECT_WRITE_XFER_CTRL_REG",
        .addr = A_INDIRECT_WRITE_XFER_CTRL_REG,
        .ro = 0xffffffdc,
        .w1c = 0x20,
        .pre_write = ind_wr_xfer_ctrl_reg_pre_write,
        .post_write = ind_wr_xfer_ctrl_reg_post_write,
        .post_read = ind_wr_xfer_ctrl_reg_post_read,
    },{ .name = "INDIRECT_WRITE_XFER_WATERMARK_REG",
        .addr = A_INDIRECT_WRITE_XFER_WATERMARK_REG,
        .reset = 0xffffffff,
    },{ .name = "INDIRECT_WRITE_XFER_START_REG",
        .addr = A_INDIRECT_WRITE_XFER_START_REG,
    },{ .name = "INDIRECT_WRITE_XFER_NUM_BYTES_REG",
        .addr = A_INDIRECT_WRITE_XFER_NUM_BYTES_REG,
    },{ .name = "INDIRECT_TRIGGER_ADDR_RANGE_REG",
        .addr = A_INDIRECT_TRIGGER_ADDR_RANGE_REG,
        .reset = 0x4,
        .ro = 0xfffffff0,
    },{ .name = "FLASH_COMMAND_CTRL_MEM_REG",
        .addr = A_FLASH_COMMAND_CTRL_MEM_REG,
        .ro = 0xe008fffe,
        .post_write = flash_cmd_ctrl_mem_reg_post_write,
    },{ .name = "FLASH_CMD_CTRL_REG",
        .addr = A_FLASH_CMD_CTRL_REG,
        .ro = 0x7a,
        .post_write = flash_cmd_ctrl_reg_post_write,
    },{ .name = "FLASH_CMD_ADDR_REG",
        .addr = A_FLASH_CMD_ADDR_REG,
    },{ .name = "FLASH_RD_DATA_LOWER_REG",
        .addr = A_FLASH_RD_DATA_LOWER_REG,
        .ro = 0xffffffff,
    },{ .name = "FLASH_RD_DATA_UPPER_REG",
        .addr = A_FLASH_RD_DATA_UPPER_REG,
        .ro = 0xffffffff,
    },{ .name = "FLASH_WR_DATA_LOWER_REG",
        .addr = A_FLASH_WR_DATA_LOWER_REG,
    },{ .name = "FLASH_WR_DATA_UPPER_REG",
        .addr = A_FLASH_WR_DATA_UPPER_REG,
    },{ .name = "POLLING_FLASH_STATUS_REG",
        .addr = A_POLLING_FLASH_STATUS_REG,
        .ro = 0xfff0ffff,
    },{ .name = "PHY_CONFIGURATION_REG",
        .addr = A_PHY_CONFIGURATION_REG,
        .reset = 0x40000000,
        .ro = 0x1f80ff80,
    },{ .name = "PHY_MASTER_CONTROL_REG",
        .addr = A_PHY_MASTER_CONTROL_REG,
        .reset = 0x800000,
        .ro = 0xfe08ff80,
    },{ .name = "DLL_OBSERVABLE_LOWER_REG",
        .addr = A_DLL_OBSERVABLE_LOWER_REG,
        .ro = 0xffffffff,
    },{ .name = "DLL_OBSERVABLE_UPPER_REG",
        .addr = A_DLL_OBSERVABLE_UPPER_REG,
        .ro = 0xffffffff,
        .post_read = dll_obs_upper_reg_post_read,
    },{ .name = "OPCODE_EXT_LOWER_REG",
        .addr = A_OPCODE_EXT_LOWER_REG,
        .reset = 0x13edfa00,
    },{ .name = "OPCODE_EXT_UPPER_REG",
        .addr = A_OPCODE_EXT_UPPER_REG,
        .reset = 0x6f90000,
        .ro = 0xffff,
    },{ .name = "MODULE_ID_REG",
        .addr = A_MODULE_ID_REG,
        .reset = 0x300,
        .ro = 0xffffffff,
    }
};

/* Return dev-obj from reg-region created by register_init_block32 */
static XlnxVersalOspi *xilinx_ospi_of_mr(void *mr_accessor)
{
    RegisterInfoArray *reg_array = mr_accessor;
    Object *dev;

    dev = reg_array->mem.owner;
    assert(dev);

    return XILINX_VERSAL_OSPI(dev);
}

static void ospi_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned int size)
{
    XlnxVersalOspi *s = xilinx_ospi_of_mr(opaque);

    register_write_memory(opaque, addr, value, size);
    ospi_update_irq_line(s);
}

static const MemoryRegionOps ospi_ops = {
    .read = register_read_memory,
    .write = ospi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t ospi_indac_read(void *opaque, unsigned int size)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(opaque);
    uint64_t ret = ospi_rx_sram_read(s, size);

    if (!ospi_ind_op_completed(s->rd_ind_op)) {
        ospi_do_ind_read(s);
    }
    return ret;
}

static void ospi_indac_write(void *opaque, uint64_t value, unsigned int size)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(opaque);

    g_assert(!s->ind_write_disabled);

    if (!ospi_ind_op_completed(s->wr_ind_op)) {
        ospi_tx_sram_write(s, value, size);
        ospi_do_indirect_write(s);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
            "OSPI wr into indac area while no ongoing indac wr\n");
    }
}

static bool is_inside_indac_range(XlnxVersalOspi *s, hwaddr addr)
{
    uint32_t range_start;
    uint32_t range_end;

    if (ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENB_DMA_IF_FLD)) {
        return true;
    }

    range_start = s->regs[R_IND_AHB_ADDR_TRIGGER_REG];
    range_end = range_start +
                (1 << ARRAY_FIELD_EX32(s->regs,
                                       INDIRECT_TRIGGER_ADDR_RANGE_REG,
                                       IND_RANGE_WIDTH_FLD));

    addr += s->regs[R_IND_AHB_ADDR_TRIGGER_REG] & 0xF0000000;

    return addr >= range_start && addr < range_end;
}

static bool ospi_is_indac_active(XlnxVersalOspi *s)
{
    /*
     * When dac and indac cannot be active at the same time,
     * return true when dac is disabled.
     */
    return s->dac_with_indac || !s->dac_enable;
}

static uint64_t ospi_dac_read(void *opaque, hwaddr addr, unsigned int size)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(opaque);

    if (ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENB_SPI_FLD)) {
        if (ospi_is_indac_active(s) &&
            is_inside_indac_range(s, addr)) {
            return ospi_indac_read(s, size);
        }
        if (ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENB_DIR_ACC_CTLR_FLD)
            && s->dac_enable) {
            if (ARRAY_FIELD_EX32(s->regs,
                                 CONFIG_REG, ENB_AHB_ADDR_REMAP_FLD)) {
                addr += s->regs[R_REMAP_ADDR_REG];
            }
            return ospi_do_dac_read(opaque, addr, size);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "OSPI AHB rd while DAC disabled\n");
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "OSPI AHB rd while OSPI disabled\n");
    }

    return 0;
}

static void ospi_dac_write(void *opaque, hwaddr addr, uint64_t value,
                           unsigned int size)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(opaque);

    if (ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENB_SPI_FLD)) {
        if (ospi_is_indac_active(s) &&
            !s->ind_write_disabled &&
            is_inside_indac_range(s, addr)) {
            return ospi_indac_write(s, value, size);
        }
        if (ARRAY_FIELD_EX32(s->regs, CONFIG_REG, ENB_DIR_ACC_CTLR_FLD) &&
            s->dac_enable) {
            if (ARRAY_FIELD_EX32(s->regs,
                                 CONFIG_REG, ENB_AHB_ADDR_REMAP_FLD)) {
                addr += s->regs[R_REMAP_ADDR_REG];
            }
            /* Check if addr is write protected */
            if (ARRAY_FIELD_EX32(s->regs, WR_PROT_CTRL_REG, ENB_FLD) &&
                ospi_is_write_protected(s, addr)) {
                set_irq(s, R_IRQ_STATUS_REG_PROT_WR_ATTEMPT_FLD_MASK);
                ospi_update_irq_line(s);
                qemu_log_mask(LOG_GUEST_ERROR,
                              "OSPI writing into write protected area\n");
                return;
            }
            ospi_do_dac_write(opaque, addr, value, size);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "OSPI AHB wr while DAC disabled\n");
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "OSPI AHB wr while OSPI disabled\n");
    }
}

static const MemoryRegionOps ospi_dac_ops = {
    .read = ospi_dac_read,
    .write = ospi_dac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ospi_update_dac_status(void *opaque, int n, int level)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(opaque);

    s->dac_enable = level;
}

static void xlnx_versal_ospi_realize(DeviceState *dev, Error **errp)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    s->num_cs = 4;
    s->spi = ssi_create_bus(dev, "spi0");
    s->cs_lines = g_new0(qemu_irq, s->num_cs);
    for (int i = 0; i < s->num_cs; ++i) {
        sysbus_init_irq(sbd, &s->cs_lines[i]);
    }

    fifo8_create(&s->rx_fifo, RXFF_SZ);
    fifo8_create(&s->tx_fifo, TXFF_SZ);
    fifo8_create(&s->rx_sram, RXFF_SZ);
    fifo8_create(&s->tx_sram, TXFF_SZ);
}

static void xlnx_versal_ospi_init(Object *obj)
{
    XlnxVersalOspi *s = XILINX_VERSAL_OSPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XILINX_VERSAL_OSPI,
                       XILINX_VERSAL_OSPI_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), ospi_regs_info,
                              ARRAY_SIZE(ospi_regs_info),
                              s->regs_info, s->regs,
                              &ospi_ops,
                              XILINX_VERSAL_OSPI_ERR_DEBUG,
                              XILINX_VERSAL_OSPI_R_MAX * 4);
    memory_region_add_subregion(&s->iomem, 0x0, &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);

    memory_region_init_io(&s->iomem_dac, obj, &ospi_dac_ops, s,
                          TYPE_XILINX_VERSAL_OSPI "-dac", 0x20000000);
    sysbus_init_mmio(sbd, &s->iomem_dac);
    /*
     * The OSPI DMA reads flash data through the OSPI linear address space (the
     * iomem_dac region), because of this the reentrancy guard needs to be
     * disabled.
     */
    s->iomem_dac.disable_reentrancy_guard = true;

    sysbus_init_irq(sbd, &s->irq);

    object_property_add_link(obj, "dma-src", TYPE_XLNX_CSU_DMA,
                             (Object **)&s->dma_src,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG);

    qdev_init_gpio_in_named(dev, ospi_update_dac_status, "ospi-mux-sel", 1);
}

static const VMStateDescription vmstate_ind_op = {
    .name = "OSPIIndOp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(flash_addr, IndOp),
        VMSTATE_UINT32(num_bytes, IndOp),
        VMSTATE_UINT32(done_bytes, IndOp),
        VMSTATE_BOOL(completed, IndOp),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_xlnx_versal_ospi = {
    .name = TYPE_XILINX_VERSAL_OSPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_FIFO8(rx_fifo, XlnxVersalOspi),
        VMSTATE_FIFO8(tx_fifo, XlnxVersalOspi),
        VMSTATE_FIFO8(rx_sram, XlnxVersalOspi),
        VMSTATE_FIFO8(tx_sram, XlnxVersalOspi),
        VMSTATE_BOOL(ind_write_disabled, XlnxVersalOspi),
        VMSTATE_BOOL(dac_with_indac, XlnxVersalOspi),
        VMSTATE_BOOL(dac_enable, XlnxVersalOspi),
        VMSTATE_BOOL(src_dma_inprog, XlnxVersalOspi),
        VMSTATE_STRUCT_ARRAY(rd_ind_op, XlnxVersalOspi, 2, 1,
                             vmstate_ind_op, IndOp),
        VMSTATE_STRUCT_ARRAY(wr_ind_op, XlnxVersalOspi, 2, 1,
                             vmstate_ind_op, IndOp),
        VMSTATE_UINT32_ARRAY(regs, XlnxVersalOspi, XILINX_VERSAL_OSPI_R_MAX),
        VMSTATE_UINT8_ARRAY(stig_membank, XlnxVersalOspi, 512),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property xlnx_versal_ospi_properties[] = {
    DEFINE_PROP_BOOL("dac-with-indac", XlnxVersalOspi, dac_with_indac, false),
    DEFINE_PROP_BOOL("indac-write-disabled", XlnxVersalOspi,
                     ind_write_disabled, false),
};

static void xlnx_versal_ospi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, xlnx_versal_ospi_reset);
    dc->realize = xlnx_versal_ospi_realize;
    dc->vmsd = &vmstate_xlnx_versal_ospi;
    device_class_set_props(dc, xlnx_versal_ospi_properties);
}

static const TypeInfo xlnx_versal_ospi_info = {
    .name          = TYPE_XILINX_VERSAL_OSPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalOspi),
    .class_init    = xlnx_versal_ospi_class_init,
    .instance_init = xlnx_versal_ospi_init,
};

static void xlnx_versal_ospi_register_types(void)
{
    type_register_static(&xlnx_versal_ospi_info);
}

type_init(xlnx_versal_ospi_register_types)
