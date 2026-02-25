/*
 * DesignWare I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Copyright (C) 2025 Google, LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/i3c/i3c.h"
#include "hw/i3c/dw-i3c.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "hw/core/irq.h"

REG32(DEVICE_CTRL,                  0x00)
    FIELD(DEVICE_CTRL, I3C_BROADCAST_ADDR_INC,    0, 1)
    FIELD(DEVICE_CTRL, I2C_SLAVE_PRESENT,         7, 1)
    FIELD(DEVICE_CTRL, HOT_JOIN_ACK_NACK_CTRL,    8, 1)
    FIELD(DEVICE_CTRL, IDLE_CNT_MULTIPLIER,       24, 2)
    FIELD(DEVICE_CTRL, SLV_ADAPT_TO_I2C_I3C_MODE, 27, 1)
    FIELD(DEVICE_CTRL, DMA_HANDSHAKE_EN,          28, 1)
    FIELD(DEVICE_CTRL, I3C_ABORT,                 29, 1)
    FIELD(DEVICE_CTRL, I3C_RESUME,                30, 1)
    FIELD(DEVICE_CTRL, I3C_EN,                    31, 1)
REG32(DEVICE_ADDR,                  0x04)
    FIELD(DEVICE_ADDR, STATIC_ADDR,         0, 7)
    FIELD(DEVICE_ADDR, STATIC_ADDR_VALID,   15, 1)
    FIELD(DEVICE_ADDR, DYNAMIC_ADDR,        16, 7)
    FIELD(DEVICE_ADDR, DYNAMIC_ADDR_VALID,  31, 1)
REG32(HW_CAPABILITY,                0x08)
    FIELD(HW_CAPABILITY, DEVICE_ROLE_CONFIG,  0, 3)
    FIELD(HW_CAPABILITY, HDR_DDR, 3, 1)
    FIELD(HW_CAPABILITY, HDR_TS,  4, 1)
REG32(COMMAND_QUEUE_PORT,           0x0c)
    FIELD(COMMAND_QUEUE_PORT, CMD_ATTR, 0, 3)
    /* Transfer command structure */
    FIELD(COMMAND_QUEUE_PORT, TID, 3, 4)
    FIELD(COMMAND_QUEUE_PORT, CMD, 7, 8)
    FIELD(COMMAND_QUEUE_PORT, CP, 15, 1)
    FIELD(COMMAND_QUEUE_PORT, DEV_INDEX, 16, 5)
    FIELD(COMMAND_QUEUE_PORT, SPEED, 21, 3)
    FIELD(COMMAND_QUEUE_PORT, ROC, 26, 1)
    FIELD(COMMAND_QUEUE_PORT, SDAP, 27, 1)
    FIELD(COMMAND_QUEUE_PORT, RNW, 28, 1)
    FIELD(COMMAND_QUEUE_PORT, TOC, 30, 1)
    FIELD(COMMAND_QUEUE_PORT, PEC, 31, 1)
    /* Transfer argument data structure */
    FIELD(COMMAND_QUEUE_PORT, DB, 8, 8)
    FIELD(COMMAND_QUEUE_PORT, DL, 16, 16)
    /* Short data argument data structure */
    FIELD(COMMAND_QUEUE_PORT, BYTE_STRB, 3, 3)
    FIELD(COMMAND_QUEUE_PORT, BYTE0, 8, 8)
    FIELD(COMMAND_QUEUE_PORT, BYTE1, 16, 8)
    FIELD(COMMAND_QUEUE_PORT, BYTE2, 24, 8)
    /* Address assignment command structure */
    /*
     * bits 3..21 and 26..31 are the same as the transfer command structure, or
     * marked as reserved.
     */
    FIELD(COMMAND_QUEUE_PORT, DEV_COUNT, 21, 3)
REG32(RESPONSE_QUEUE_PORT,          0x10)
    FIELD(RESPONSE_QUEUE_PORT, DL, 0, 16)
    FIELD(RESPONSE_QUEUE_PORT, CCCT, 16, 8)
    FIELD(RESPONSE_QUEUE_PORT, TID, 24, 3)
    FIELD(RESPONSE_QUEUE_PORT, ERR_STATUS, 28, 4)
REG32(RX_TX_DATA_PORT,              0x14)
REG32(IBI_QUEUE_STATUS,             0x18)
    FIELD(IBI_QUEUE_STATUS, IBI_DATA_LEN,   0, 8)
    FIELD(IBI_QUEUE_STATUS, IBI_ID,         8, 8)
    FIELD(IBI_QUEUE_STATUS, LAST_STATUS,    24, 1)
    FIELD(IBI_QUEUE_STATUS, ERROR,          30, 1)
    FIELD(IBI_QUEUE_STATUS, IBI_STATUS,     31, 1)
REG32(IBI_QUEUE_DATA,               0x18)
REG32(QUEUE_THLD_CTRL,              0x1c)
    FIELD(QUEUE_THLD_CTRL, CMD_BUF_EMPTY_THLD,  0, 8);
    FIELD(QUEUE_THLD_CTRL, RESP_BUF_THLD, 8, 8);
    FIELD(QUEUE_THLD_CTRL, IBI_DATA_THLD, 16, 5);
    FIELD(QUEUE_THLD_CTRL, IBI_STATUS_THLD,     24, 8);
REG32(DATA_BUFFER_THLD_CTRL,        0x20)
    FIELD(DATA_BUFFER_THLD_CTRL, TX_BUF_THLD,   0, 3)
    FIELD(DATA_BUFFER_THLD_CTRL, RX_BUF_THLD,   8, 3)
    FIELD(DATA_BUFFER_THLD_CTRL, TX_START_THLD, 16, 3)
    FIELD(DATA_BUFFER_THLD_CTRL, RX_START_THLD, 24, 3)
REG32(IBI_QUEUE_CTRL,               0x24)
    FIELD(IBI_QUEUE_CTRL, NOTIFY_REJECTED_HOT_JOIN,   0, 1)
    FIELD(IBI_QUEUE_CTRL, NOTIFY_REJECTED_MASTER_REQ, 1, 1)
    FIELD(IBI_QUEUE_CTRL, NOTIFY_REJECTED_SLAVE_IRQ,  3, 1)
REG32(IBI_MR_REQ_REJECT,            0x2c)
REG32(IBI_SIR_REQ_REJECT,           0x30)
REG32(RESET_CTRL,                   0x34)
    FIELD(RESET_CTRL, CORE_RESET,       0, 1)
    FIELD(RESET_CTRL, CMD_QUEUE_RESET,  1, 1)
    FIELD(RESET_CTRL, RESP_QUEUE_RESET, 2, 1)
    FIELD(RESET_CTRL, TX_BUF_RESET,     3, 1)
    FIELD(RESET_CTRL, RX_BUF_RESET,     4, 1)
    FIELD(RESET_CTRL, IBI_QUEUE_RESET,  5, 1)
REG32(SLV_EVENT_CTRL,               0x38)
    FIELD(SLV_EVENT_CTRL, SLV_INTERRUPT,      0, 1)
    FIELD(SLV_EVENT_CTRL, MASTER_INTERRUPT,   1, 1)
    FIELD(SLV_EVENT_CTRL, HOT_JOIN_INTERRUPT, 3, 1)
    FIELD(SLV_EVENT_CTRL, ACTIVITY_STATE,     4, 2)
    FIELD(SLV_EVENT_CTRL, MRL_UPDATED,        6, 1)
    FIELD(SLV_EVENT_CTRL, MWL_UPDATED,        7, 1)
REG32(INTR_STATUS,                  0x3c)
    FIELD(INTR_STATUS, TX_THLD,           0, 1)
    FIELD(INTR_STATUS, RX_THLD,           1, 1)
    FIELD(INTR_STATUS, IBI_THLD,          2, 1)
    FIELD(INTR_STATUS, CMD_QUEUE_RDY,     3, 1)
    FIELD(INTR_STATUS, RESP_RDY,          4, 1)
    FIELD(INTR_STATUS, TRANSFER_ABORT,    5, 1)
    FIELD(INTR_STATUS, CCC_UPDATED,       6, 1)
    FIELD(INTR_STATUS, DYN_ADDR_ASSGN,    8, 1)
    FIELD(INTR_STATUS, TRANSFER_ERR,      9, 1)
    FIELD(INTR_STATUS, DEFSLV,            10, 1)
    FIELD(INTR_STATUS, READ_REQ_RECV,     11, 1)
    FIELD(INTR_STATUS, IBI_UPDATED,       12, 1)
    FIELD(INTR_STATUS, BUSOWNER_UPDATED,  13, 1)
REG32(INTR_STATUS_EN,               0x40)
    FIELD(INTR_STATUS_EN, TX_THLD,          0, 1)
    FIELD(INTR_STATUS_EN, RX_THLD,          1, 1)
    FIELD(INTR_STATUS_EN, IBI_THLD,         2, 1)
    FIELD(INTR_STATUS_EN, CMD_QUEUE_RDY,    3, 1)
    FIELD(INTR_STATUS_EN, RESP_RDY,         4, 1)
    FIELD(INTR_STATUS_EN, TRANSFER_ABORT,   5, 1)
    FIELD(INTR_STATUS_EN, CCC_UPDATED,      6, 1)
    FIELD(INTR_STATUS_EN, DYN_ADDR_ASSGN,   8, 1)
    FIELD(INTR_STATUS_EN, TRANSFER_ERR,     9, 1)
    FIELD(INTR_STATUS_EN, DEFSLV,           10, 1)
    FIELD(INTR_STATUS_EN, READ_REQ_RECV,    11, 1)
    FIELD(INTR_STATUS_EN, IBI_UPDATED,      12, 1)
    FIELD(INTR_STATUS_EN, BUSOWNER_UPDATED, 13, 1)
REG32(INTR_SIGNAL_EN,               0x44)
    FIELD(INTR_SIGNAL_EN, TX_THLD,          0, 1)
    FIELD(INTR_SIGNAL_EN, RX_THLD,          1, 1)
    FIELD(INTR_SIGNAL_EN, IBI_THLD,         2, 1)
    FIELD(INTR_SIGNAL_EN, CMD_QUEUE_RDY,    3, 1)
    FIELD(INTR_SIGNAL_EN, RESP_RDY,         4, 1)
    FIELD(INTR_SIGNAL_EN, TRANSFER_ABORT,   5, 1)
    FIELD(INTR_SIGNAL_EN, CCC_UPDATED,      6, 1)
    FIELD(INTR_SIGNAL_EN, DYN_ADDR_ASSGN,   8, 1)
    FIELD(INTR_SIGNAL_EN, TRANSFER_ERR,     9, 1)
    FIELD(INTR_SIGNAL_EN, DEFSLV,           10, 1)
    FIELD(INTR_SIGNAL_EN, READ_REQ_RECV,    11, 1)
    FIELD(INTR_SIGNAL_EN, IBI_UPDATED,      12, 1)
    FIELD(INTR_SIGNAL_EN, BUSOWNER_UPDATED, 13, 1)
REG32(INTR_FORCE,                   0x48)
    FIELD(INTR_FORCE, TX_THLD,          0, 1)
    FIELD(INTR_FORCE, RX_THLD,          1, 1)
    FIELD(INTR_FORCE, IBI_THLD,         2, 1)
    FIELD(INTR_FORCE, CMD_QUEUE_RDY,    3, 1)
    FIELD(INTR_FORCE, RESP_RDY,         4, 1)
    FIELD(INTR_FORCE, TRANSFER_ABORT,   5, 1)
    FIELD(INTR_FORCE, CCC_UPDATED,      6, 1)
    FIELD(INTR_FORCE, DYN_ADDR_ASSGN,   8, 1)
    FIELD(INTR_FORCE, TRANSFER_ERR,     9, 1)
    FIELD(INTR_FORCE, DEFSLV,           10, 1)
    FIELD(INTR_FORCE, READ_REQ_RECV,    11, 1)
    FIELD(INTR_FORCE, IBI_UPDATED,      12, 1)
    FIELD(INTR_FORCE, BUSOWNER_UPDATED, 13, 1)
REG32(QUEUE_STATUS_LEVEL,           0x4c)
    FIELD(QUEUE_STATUS_LEVEL, CMD_QUEUE_EMPTY_LOC,  0, 8)
    FIELD(QUEUE_STATUS_LEVEL, RESP_BUF_BLR,         8, 8)
    FIELD(QUEUE_STATUS_LEVEL, IBI_BUF_BLR,          16, 8)
    FIELD(QUEUE_STATUS_LEVEL, IBI_STATUS_CNT,       24, 5)
REG32(DATA_BUFFER_STATUS_LEVEL,     0x50)
    FIELD(DATA_BUFFER_STATUS_LEVEL, TX_BUF_EMPTY_LOC, 0, 8)
    FIELD(DATA_BUFFER_STATUS_LEVEL, RX_BUF_BLR,       16, 8)
REG32(PRESENT_STATE,                0x54)
    FIELD(PRESENT_STATE, SCL_LINE_SIGNAL_LEVEL, 0, 1)
    FIELD(PRESENT_STATE, SDA_LINE_SIGNAL_LEVEL, 1, 1)
    FIELD(PRESENT_STATE, CURRENT_MASTER,        2, 1)
    FIELD(PRESENT_STATE, CM_TFR_STATUS,         8, 6)
    FIELD(PRESENT_STATE, CM_TFR_ST_STATUS,      16, 6)
    FIELD(PRESENT_STATE, CMD_TID,               24, 4)
REG32(CCC_DEVICE_STATUS,            0x58)
    FIELD(CCC_DEVICE_STATUS, PENDING_INTR,      0, 4)
    FIELD(CCC_DEVICE_STATUS, PROTOCOL_ERR,      5, 1)
    FIELD(CCC_DEVICE_STATUS, ACTIVITY_MODE,     6, 2)
    FIELD(CCC_DEVICE_STATUS, UNDER_ERR,         8, 1)
    FIELD(CCC_DEVICE_STATUS, SLV_BUSY,          9, 1)
    FIELD(CCC_DEVICE_STATUS, OVERFLOW_ERR,      10, 1)
    FIELD(CCC_DEVICE_STATUS, DATA_NOT_READY,    11, 1)
    FIELD(CCC_DEVICE_STATUS, BUFFER_NOT_AVAIL,  12, 1)
REG32(DEVICE_ADDR_TABLE_POINTER,    0x5c)
    FIELD(DEVICE_ADDR_TABLE_POINTER, DEPTH, 16, 16)
    FIELD(DEVICE_ADDR_TABLE_POINTER, ADDR,  0,  16)
REG32(DEV_CHAR_TABLE_POINTER,       0x60)
    FIELD(DEV_CHAR_TABLE_POINTER, P_DEV_CHAR_TABLE_START_ADDR,  0, 12)
    FIELD(DEV_CHAR_TABLE_POINTER, DEV_CHAR_TABLE_DEPTH,         12, 7)
    FIELD(DEV_CHAR_TABLE_POINTER, PRESENT_DEV_CHAR_TABLE_INDEX, 19, 3)
REG32(VENDOR_SPECIFIC_REG_POINTER,  0x6c)
    FIELD(VENDOR_SPECIFIC_REG_POINTER, P_VENDOR_REG_START_ADDR, 0, 16)
REG32(SLV_MIPI_PID_VALUE,           0x70)
REG32(SLV_PID_VALUE,                0x74)
    FIELD(SLV_PID_VALUE, SLV_PID_DCR, 0, 12)
    FIELD(SLV_PID_VALUE, SLV_INST_ID, 12, 4)
    FIELD(SLV_PID_VALUE, SLV_PART_ID, 16, 16)
REG32(SLV_CHAR_CTRL,                0x78)
    FIELD(SLV_CHAR_CTRL, BCR,     0, 8)
    FIELD(SLV_CHAR_CTRL, DCR,     8, 8)
    FIELD(SLV_CHAR_CTRL, HDR_CAP, 16, 8)
REG32(SLV_MAX_LEN,                  0x7c)
    FIELD(SLV_MAX_LEN, MWL, 0, 16)
    FIELD(SLV_MAX_LEN, MRL, 16, 16)
REG32(MAX_READ_TURNAROUND,          0x80)
REG32(MAX_DATA_SPEED,               0x84)
REG32(SLV_DEBUG_STATUS,             0x88)
REG32(SLV_INTR_REQ,                 0x8c)
    FIELD(SLV_INTR_REQ, SIR,          0, 1)
    FIELD(SLV_INTR_REQ, SIR_CTRL,     1, 2)
    FIELD(SLV_INTR_REQ, MIR,          3, 1)
    FIELD(SLV_INTR_REQ, TS,           4, 1)
    FIELD(SLV_INTR_REQ, IBI_STS,      8, 2)
REG32(SLV_TSX_SYMBL_TIMING,         0x90)
    FIELD(SLV_TSX_SYMBL_TIMING, SLV_TSX_SYMBL_CNT, 0, 6)
REG32(DEVICE_CTRL_EXTENDED,         0xb0)
    FIELD(DEVICE_CTRL_EXTENDED, MODE, 0, 2)
    FIELD(DEVICE_CTRL_EXTENDED, REQMST_ACK_CTRL, 3, 1)
REG32(SCL_I3C_OD_TIMING,            0xb4)
    FIELD(SCL_I3C_OD_TIMING, I3C_OD_LCNT, 0, 8)
    FIELD(SCL_I3C_OD_TIMING, I3C_OD_HCNT, 16, 8)
REG32(SCL_I3C_PP_TIMING,            0xb8)
    FIELD(SCL_I3C_PP_TIMING, I3C_PP_LCNT, 0, 8)
    FIELD(SCL_I3C_PP_TIMING, I3C_PP_HCNT, 16, 8)
REG32(SCL_I2C_FM_TIMING,            0xbc)
REG32(SCL_I2C_FMP_TIMING,           0xc0)
    FIELD(SCL_I2C_FMP_TIMING, I2C_FMP_LCNT, 0, 16)
    FIELD(SCL_I2C_FMP_TIMING, I2C_FMP_HCNT, 16, 8)
REG32(SCL_EXT_LCNT_TIMING,          0xc8)
REG32(SCL_EXT_TERMN_LCNT_TIMING,    0xcc)
REG32(BUS_FREE_TIMING,              0xd4)
REG32(BUS_IDLE_TIMING,              0xd8)
    FIELD(BUS_IDLE_TIMING, BUS_IDLE_TIME, 0, 20)
REG32(I3C_VER_ID,                   0xe0)
REG32(I3C_VER_TYPE,                 0xe4)
REG32(EXTENDED_CAPABILITY,          0xe8)
REG32(SLAVE_CONFIG,                 0xec)
/* Device characteristic table fields */
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC1, 0x200)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, 0x200)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, DYNAMIC_ADDR, 0, 8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, DCR, 8, 8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, BCR, 16, 8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, STATIC_ADDR, 24, 8)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC2, 0x204)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC2, MSB_PID, 0, 16)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC3, 0x208)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC3, DCR, 0, 8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC3, BCR, 8, 8)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC4, 0x20c)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC4, DEV_DYNAMIC_ADDR, 0, 8)
/* Dev addr table fields */
REG32(DEVICE_ADDR_TABLE_LOC1, 0x280)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_STATIC_ADDR, 0, 7)
    FIELD(DEVICE_ADDR_TABLE_LOC1, IBI_PEC_EN, 11, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, IBI_WITH_DATA, 12, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, SIR_REJECT, 13, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, MR_REJECT, 14, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_DYNAMIC_ADDR, 16, 8)
    FIELD(DEVICE_ADDR_TABLE_LOC1, IBI_ADDR_MASK, 24, 2)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_NACK_RETRY_CNT, 29, 2)
    FIELD(DEVICE_ADDR_TABLE_LOC1, LEGACY_I2C_DEVICE, 31, 1)

static const uint32_t dw_i3c_resets[DW_I3C_NR_REGS] = {
    /* Target mode is not supported, don't advertise it for now. */
    [R_HW_CAPABILITY]               = 0x000e00b9,
    [R_QUEUE_THLD_CTRL]             = 0x01000101,
    [R_DATA_BUFFER_THLD_CTRL]       = 0x01010100,
    [R_SLV_EVENT_CTRL]              = 0x0000000b,
    [R_QUEUE_STATUS_LEVEL]          = 0x00000002,
    [R_DATA_BUFFER_STATUS_LEVEL]    = 0x00000010,
    [R_PRESENT_STATE]               = 0x00000003,
    [R_I3C_VER_ID]                  = 0x3130302a,
    [R_I3C_VER_TYPE]                = 0x6c633033,
    [R_DEVICE_ADDR_TABLE_POINTER]   = 0x00080280,
    [R_DEV_CHAR_TABLE_POINTER]      = 0x00020200,
    [R_SLV_CHAR_CTRL]               = 0x00010000,
    [A_VENDOR_SPECIFIC_REG_POINTER] = 0x000000b0,
    [R_SLV_MAX_LEN]                 = 0x00ff00ff,
    [R_SLV_TSX_SYMBL_TIMING]        = 0x0000003f,
    [R_SCL_I3C_OD_TIMING]           = 0x000a0010,
    [R_SCL_I3C_PP_TIMING]           = 0x000a000a,
    [R_SCL_I2C_FM_TIMING]           = 0x00100010,
    [R_SCL_I2C_FMP_TIMING]          = 0x00100010,
    [R_SCL_EXT_LCNT_TIMING]         = 0x20202020,
    [R_SCL_EXT_TERMN_LCNT_TIMING]   = 0x00300000,
    [R_BUS_FREE_TIMING]             = 0x00200020,
    [R_BUS_IDLE_TIMING]             = 0x00000020,
    [R_EXTENDED_CAPABILITY]         = 0x00000239,
    [R_SLAVE_CONFIG]                = 0x00000023,
};

static const uint32_t dw_i3c_ro[DW_I3C_NR_REGS] = {
    [R_DEVICE_CTRL]                 = 0x04fffe00,
    [R_DEVICE_ADDR]                 = 0x7f807f80,
    [R_HW_CAPABILITY]               = 0xffffffff,
    [R_IBI_QUEUE_STATUS]            = 0xffffffff,
    [R_DATA_BUFFER_THLD_CTRL]       = 0xf8f8f8f8,
    [R_IBI_QUEUE_CTRL]              = 0xfffffff0,
    [R_RESET_CTRL]                  = 0xffffffc0,
    [R_SLV_EVENT_CTRL]              = 0xffffff3f,
    [R_INTR_STATUS]                 = 0xffff809f,
    [R_INTR_STATUS_EN]              = 0xffff8080,
    [R_INTR_SIGNAL_EN]              = 0xffff8080,
    [R_INTR_FORCE]                  = 0xffff8000,
    [R_QUEUE_STATUS_LEVEL]          = 0xffffffff,
    [R_DATA_BUFFER_STATUS_LEVEL]    = 0xffffffff,
    [R_PRESENT_STATE]               = 0xffffffff,
    [R_CCC_DEVICE_STATUS]           = 0xffffffff,
    [R_I3C_VER_ID]                  = 0xffffffff,
    [R_I3C_VER_TYPE]                = 0xffffffff,
    [R_DEVICE_ADDR_TABLE_POINTER]   = 0xffffffff,
    [R_DEV_CHAR_TABLE_POINTER]      = 0xffcbffff,
    [R_SLV_PID_VALUE]               = 0xffff0fff,
    [R_SLV_CHAR_CTRL]               = 0xffffffff,
    [A_VENDOR_SPECIFIC_REG_POINTER] = 0xffffffff,
    [R_SLV_MAX_LEN]                 = 0xffffffff,
    [R_MAX_READ_TURNAROUND]         = 0xffffffff,
    [R_MAX_DATA_SPEED]              = 0xffffffff,
    [R_SLV_INTR_REQ]                = 0xfffffff0,
    [R_SLV_TSX_SYMBL_TIMING]        = 0xffffffc0,
    [R_DEVICE_CTRL_EXTENDED]        = 0xfffffff8,
    [R_SCL_I3C_OD_TIMING]           = 0xff00ff00,
    [R_SCL_I3C_PP_TIMING]           = 0xff00ff00,
    [R_SCL_I2C_FMP_TIMING]          = 0xff000000,
    [R_SCL_EXT_TERMN_LCNT_TIMING]   = 0x0000fff0,
    [R_BUS_IDLE_TIMING]             = 0xfff00000,
    [R_EXTENDED_CAPABILITY]         = 0xffffffff,
    [R_SLAVE_CONFIG]                = 0xffffffff,
};

static inline bool dw_i3c_has_hdr_ts(DWI3C *s)
{
    return ARRAY_FIELD_EX32(s->regs, HW_CAPABILITY, HDR_TS);
}

static inline bool dw_i3c_has_hdr_ddr(DWI3C *s)
{
    return ARRAY_FIELD_EX32(s->regs, HW_CAPABILITY, HDR_DDR);
}

static inline bool dw_i3c_can_transmit(DWI3C *s)
{
    /*
     * We can only transmit if we're enabled and the resume bit is cleared.
     * The resume bit is set on a transaction error, and software must clear it.
     */
    return ARRAY_FIELD_EX32(s->regs, DEVICE_CTRL, I3C_EN) &&
           !ARRAY_FIELD_EX32(s->regs, DEVICE_CTRL, I3C_RESUME);
}

static inline uint8_t dw_i3c_fifo_threshold_from_reg(uint8_t regval)
{
    return regval = regval ? (2 << regval) : 1;
}

static void dw_i3c_update_irq(DWI3C *s)
{
    bool level = !!(s->regs[R_INTR_SIGNAL_EN] & s->regs[R_INTR_STATUS]);
    qemu_set_irq(s->irq, level);
}

static void dw_i3c_end_transfer(DWI3C *s, bool is_i2c)
{
    if (is_i2c) {
        legacy_i2c_end_transfer(s->bus);
    } else {
        i3c_end_transfer(s->bus);
    }
}

static int dw_i3c_send_start(DWI3C *s, uint8_t addr, bool is_recv, bool is_i2c)
{
    int ret;

    if (is_i2c) {
        ret = legacy_i2c_start_transfer(s->bus, addr, is_recv);
    } else {
        ret = i3c_start_transfer(s->bus, addr, is_recv);
    }
    if (ret) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: NACKed on TX with addr 0x%.2x\n",
                      path, addr);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DW_I3C_TRANSFER_STATE_HALT);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_STATUS,
                         DW_I3C_TRANSFER_STATUS_HALT);
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TRANSFER_ERR, 1);
        ARRAY_FIELD_DP32(s->regs, DEVICE_CTRL, I3C_RESUME, 1);
    }

    return ret;
}

static int dw_i3c_send(DWI3C *s, const uint8_t *data, uint32_t num_to_send,
                       uint32_t *num_sent, bool is_i2c)
{
    int ret;
    uint32_t i;

    *num_sent = 0;
    if (is_i2c) {
        /* Legacy I2C must be byte-by-byte. */
        for (i = 0; i < num_to_send; i++) {
            ret = legacy_i2c_send(s->bus, data[i]);
            if (ret) {
                break;
            }
            (*num_sent)++;
        }
    } else {
        ret = i3c_send(s->bus, data, num_to_send, num_sent);
    }
    if (ret) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: NACKed sending byte 0x%.2x\n",
                      path, data[*num_sent]);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DW_I3C_TRANSFER_STATE_HALT);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_STATUS,
                         DW_I3C_TRANSFER_STATUS_HALT);
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TRANSFER_ERR, 1);
        ARRAY_FIELD_DP32(s->regs, DEVICE_CTRL, I3C_RESUME, 1);
    }

    trace_dw_i3c_send(s->cfg.id, *num_sent);

    return ret;
}

static int dw_i3c_send_byte(DWI3C *s, uint8_t byte, bool is_i2c)
{
    /*
     * Ignored, the caller will know if we sent 0 or 1 bytes depending on if
     * we were ACKed/NACKed.
     */
    uint32_t num_sent;
    return dw_i3c_send(s, &byte, 1, &num_sent, is_i2c);
}

static int dw_i3c_recv_data(DWI3C *s, bool is_i2c, uint8_t *data,
                            uint16_t num_to_read, uint32_t *num_read)
{
    int ret;

    if (is_i2c) {
        for (uint16_t i = 0; i < num_to_read; i++) {
            data[i] = legacy_i2c_recv(s->bus);
        }
        /* I2C devices can neither NACK a read, nor end transfers early. */
        *num_read = num_to_read;
        trace_dw_i3c_recv_data(s->cfg.id, *num_read);
        return 0;
    }
    /* I3C devices can NACK if the controller sends an unsupported CCC. */
    ret = i3c_recv(s->bus, data, num_to_read, num_read);
    if (ret) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: NACKed receiving byte\n",
                      object_get_canonical_path(OBJECT(s)));
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DW_I3C_TRANSFER_STATE_HALT);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_STATUS,
                         DW_I3C_TRANSFER_STATUS_HALT);
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TRANSFER_ERR, 1);
        ARRAY_FIELD_DP32(s->regs, DEVICE_CTRL, I3C_RESUME, 1);
    }

    trace_dw_i3c_recv_data(s->cfg.id, *num_read);

    return ret;
}

static inline bool dw_i3c_target_is_i2c(DWI3C *s, uint16_t offset)
{
    /* / sizeof(uint32_t) because we're indexing into our 32-bit reg array. */
    uint16_t dev_index = (ARRAY_FIELD_EX32(s->regs, DEVICE_ADDR_TABLE_POINTER,
                                          ADDR) / sizeof(uint32_t)) + offset;
    return FIELD_EX32(s->regs[dev_index], DEVICE_ADDR_TABLE_LOC1,
                   LEGACY_I2C_DEVICE);
}

static uint8_t dw_i3c_target_addr(DWI3C *s, uint16_t offset)
{
    if (offset > s->cfg.num_addressable_devices) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Device addr table offset %d out of "
                      "bounds\n", path, offset);
        /* If we're out of bounds, return an address of 0. */
        return 0;
    }

    /* / sizeof(uint32_t) because we're indexing into our 32-bit reg array. */
    uint16_t dev_index = (ARRAY_FIELD_EX32(s->regs, DEVICE_ADDR_TABLE_POINTER,
                                          ADDR) / sizeof(uint32_t)) + offset;
    /* I2C devices use a static address. */
    if (dw_i3c_target_is_i2c(s, offset)) {
        return FIELD_EX32(s->regs[dev_index], DEVICE_ADDR_TABLE_LOC1,
                          DEV_STATIC_ADDR);
    }
    return FIELD_EX32(s->regs[dev_index], DEVICE_ADDR_TABLE_LOC1,
                      DEV_DYNAMIC_ADDR);
}

static uint32_t dw_i3c_intr_status_r(DWI3C *s)
{
    /* Only return the status whose corresponding EN bits are set. */
    return s->regs[R_INTR_STATUS] & s->regs[R_INTR_STATUS_EN];
}

static void dw_i3c_intr_status_w(DWI3C *s, uint32_t val)
{
    /* INTR_STATUS[13:5] is w1c, other bits are RO. */
    val &= 0x3fe0;
    s->regs[R_INTR_STATUS] &= ~val;

    dw_i3c_update_irq(s);
}

static void dw_i3c_intr_status_en_w(DWI3C *s, uint32_t val)
{
    s->regs[R_INTR_STATUS_EN] = val;
    dw_i3c_update_irq(s);
}

static void dw_i3c_intr_signal_en_w(DWI3C *s, uint32_t val)
{
    s->regs[R_INTR_SIGNAL_EN] = val;
    dw_i3c_update_irq(s);
}

static void dw_i3c_intr_force_w(DWI3C *s, uint32_t val)
{
    /* INTR_FORCE is WO, just set the corresponding INTR_STATUS bits. */
    s->regs[R_INTR_STATUS] = val;
    dw_i3c_update_irq(s);
}

static uint32_t dw_i3c_pop_rx(DWI3C *s)
{
    if (fifo32_is_empty(&s->rx_queue)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to read RX FIFO when empty\n",
                      path);
        return 0;
    }

    uint32_t val = fifo32_pop(&s->rx_queue);
    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, RX_BUF_BLR,
                     fifo32_num_used(&s->rx_queue));

    /* Threshold is 2^RX_BUF_THLD. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                         RX_BUF_THLD);
    threshold = dw_i3c_fifo_threshold_from_reg(threshold);
    if (fifo32_num_used(&s->rx_queue) < threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RX_THLD, 0);
        dw_i3c_update_irq(s);
    }

    trace_dw_i3c_pop_rx(s->cfg.id, val);
    return val;
}

static uint32_t dw_i3c_resp_queue_port_r(DWI3C *s)
{
    if (fifo32_is_empty(&s->resp_queue)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to read response FIFO when "
                      "empty\n", path);
        return 0;
    }

    uint32_t val = fifo32_pop(&s->resp_queue);
    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, RESP_BUF_BLR,
                     fifo32_num_used(&s->resp_queue));

    /* Threshold is the register value + 1. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                         RESP_BUF_THLD) + 1;
    if (fifo32_num_used(&s->resp_queue) < threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RESP_RDY, 0);
        dw_i3c_update_irq(s);
    }

    return val;
}

static uint64_t dw_i3c_read(void *opaque, hwaddr offset, unsigned size)
{
    DWI3C *s = DW_I3C(opaque);
    uint32_t addr = offset >> 2;
    uint64_t value;

    switch (addr) {
    /* RAZ */
    case R_COMMAND_QUEUE_PORT:
    case R_RESET_CTRL:
    case R_INTR_FORCE:
        value = 0;
        break;
    case R_INTR_STATUS:
        value = dw_i3c_intr_status_r(s);
        break;
    case R_RX_TX_DATA_PORT:
        value = dw_i3c_pop_rx(s);
        break;
    case R_RESPONSE_QUEUE_PORT:
        value = dw_i3c_resp_queue_port_r(s);
        break;
    default:
        value = s->regs[addr];
        break;
    }

    trace_dw_i3c_read(s->cfg.id, offset, value);

    return value;
}

static void dw_i3c_resp_queue_push(DWI3C *s, uint8_t err, uint8_t tid,
                                   uint8_t ccc_type, uint16_t data_len)
{
    uint32_t val = 0;
    val = FIELD_DP32(val, RESPONSE_QUEUE_PORT, ERR_STATUS, err);
    val = FIELD_DP32(val, RESPONSE_QUEUE_PORT, TID, tid);
    val = FIELD_DP32(val, RESPONSE_QUEUE_PORT, CCCT, ccc_type);
    val = FIELD_DP32(val, RESPONSE_QUEUE_PORT, DL, data_len);
    if (!fifo32_is_full(&s->resp_queue)) {
        trace_dw_i3c_resp_queue_push(s->cfg.id, val);
        fifo32_push(&s->resp_queue, val);
    }

    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, RESP_BUF_BLR,
                     fifo32_num_used(&s->resp_queue));
    /* Threshold is the register value + 1. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                         RESP_BUF_THLD) + 1;
    if (fifo32_num_used(&s->resp_queue) >= threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RESP_RDY, 1);
        dw_i3c_update_irq(s);
    }
}

static void dw_i3c_push_tx(DWI3C *s, uint32_t val)
{
    if (fifo32_is_full(&s->tx_queue)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to push to TX FIFO when "
                      "full\n", object_get_canonical_path(OBJECT(s)));
        return;
    }

    trace_dw_i3c_push_tx(s->cfg.id, val);
    fifo32_push(&s->tx_queue, val);
    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, TX_BUF_EMPTY_LOC,
                     fifo32_num_free(&s->tx_queue));

    /* Threshold is 2^TX_BUF_THLD. */
    uint8_t empty_threshold = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                               TX_BUF_THLD);
    empty_threshold =
        dw_i3c_fifo_threshold_from_reg(empty_threshold);
    if (fifo32_num_free(&s->tx_queue) < empty_threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TX_THLD, 0);
        dw_i3c_update_irq(s);
    }
}

static uint32_t dw_i3c_pop_tx(DWI3C *s)
{
    if (fifo32_is_empty(&s->tx_queue)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to pop from TX FIFO when "
                      "empty\n", path);
        return 0;
    }

    uint32_t val = fifo32_pop(&s->tx_queue);
    trace_dw_i3c_pop_tx(s->cfg.id, val);
    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, TX_BUF_EMPTY_LOC,
                     fifo32_num_free(&s->tx_queue));

    /* Threshold is 2^TX_BUF_THLD. */
    uint8_t empty_threshold = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                               TX_BUF_THLD);
    empty_threshold =
        dw_i3c_fifo_threshold_from_reg(empty_threshold);
    if (fifo32_num_free(&s->tx_queue) >= empty_threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TX_THLD, 1);
        dw_i3c_update_irq(s);
    }
    return val;
}

static void dw_i3c_push_rx(DWI3C *s, uint32_t val)
{
    if (fifo32_is_full(&s->rx_queue)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to push to RX FIFO when "
                      "full\n", path);
        return;
    }
    trace_dw_i3c_push_rx(s->cfg.id, val);
    fifo32_push(&s->rx_queue, val);

    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, RX_BUF_BLR,
                     fifo32_num_used(&s->rx_queue));
    /* Threshold is 2^RX_BUF_THLD. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                         RX_BUF_THLD);
    threshold = dw_i3c_fifo_threshold_from_reg(threshold);
    if (fifo32_num_used(&s->rx_queue) >= threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RX_THLD, 1);
        dw_i3c_update_irq(s);
    }
}

static void dw_i3c_short_transfer(DWI3C *s, DWI3CTransferCmd cmd,
                                  DWI3CShortArg arg)
{
    uint8_t err = DW_I3C_RESP_QUEUE_ERR_NONE;
    uint8_t addr = dw_i3c_target_addr(s, cmd.dev_index);
    bool is_i2c = dw_i3c_target_is_i2c(s, cmd.dev_index);
    uint8_t data[4]; /* Max we can send on a short transfer is 4 bytes. */
    uint8_t len = 0;
    uint32_t bytes_sent; /* Ignored on short transfers. */

    /* Can't do reads on a short transfer. */
    if (cmd.rnw) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Cannot do a read on a short "
                      "transfer\n", path);
        return;
    }

    if (dw_i3c_send_start(s, addr, /*is_recv=*/false, is_i2c)) {
        err = DW_I3C_RESP_QUEUE_ERR_I2C_NACK;
        goto transfer_done;
    }

    /* Are we sending a command? */
    if (cmd.cp) {
        data[len] = cmd.cmd;
        len++;
        /*
         * byte0 is the defining byte for a command, and is only sent if a
         * command is present and if the command has a defining byte present.
         * (byte_strb & 0x01) is always treated as set by the controller, and is
         * ignored.
         */
        if (cmd.dbp) {
            data[len] += arg.byte0;
            len++;
        }
    }

    /* Send the bytes passed in the argument. */
    if (arg.byte_strb & 0x02) {
        data[len] = arg.byte1;
        len++;
    }
    if (arg.byte_strb & 0x04) {
        data[len] = arg.byte2;
        len++;
    }

    if (dw_i3c_send(s, data, len, &bytes_sent, is_i2c)) {
        err = DW_I3C_RESP_QUEUE_ERR_I2C_NACK;
    } else {
        /* Only go to an idle state on a successful transfer. */
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DW_I3C_TRANSFER_STATE_IDLE);
    }

transfer_done:
    if (cmd.toc) {
        dw_i3c_end_transfer(s, is_i2c);
    }
    if (cmd.roc) {
        /*
         * ccc_type is always 0 in controller mode, data_len is 0 in short
         * transfers.
         */
        dw_i3c_resp_queue_push(s, err, cmd.tid, /*ccc_type=*/0,
                                          /*data_len=*/0);
    }
}

/* Returns number of bytes transmitted. */
static uint16_t dw_i3c_tx(DWI3C *s, uint16_t num, bool is_i2c)
{
    uint16_t bytes_sent = 0;
    union {
        uint8_t b[sizeof(uint32_t)];
        uint32_t val;
    } val32;

    while (bytes_sent < num) {
        val32.val = dw_i3c_pop_tx(s);
        for (uint8_t i = 0; i < sizeof(val32.val); i++) {
            if (dw_i3c_send_byte(s, val32.b[i], is_i2c)) {
                return bytes_sent;
            }
            bytes_sent++;

            /* We're not sending the full 32-bits, break early. */
            if (bytes_sent >= num) {
                break;
            }
        }
    }

    return bytes_sent;
}

/* Returns number of bytes received. */
static uint16_t dw_i3c_rx(DWI3C *s, uint16_t num, bool is_i2c)
{
    /*
     * Allocate a temporary buffer to read data from the target.
     * Zero it and word-align it as well in case we're reading unaligned data.
     */
    g_autofree uint8_t *data = g_new0(uint8_t, num + (4 - (num & 0x03)));
    uint32_t *data32 = (uint32_t *)data;
    /*
     * 32-bits since the I3C API wants a 32-bit number, even though the
     * controller can only do 16-bit transfers.
     */
    uint32_t num_read = 0;

    /* Can NACK if the target receives an unsupported CCC. */
    if (dw_i3c_recv_data(s, is_i2c, data, num, &num_read)) {
        return 0;
    }

    for (uint16_t i = 0; i < num_read / 4; i++) {
        dw_i3c_push_rx(s, *data32);
        data32++;
    }
    /*
     * If we're pushing data that isn't 32-bit aligned, push what's left.
     * It's software's responsibility to know what bits are valid in the partial
     * data.
     */
    if (num_read & 0x03) {
        dw_i3c_push_rx(s, *data32);
    }

    return num_read;
}

static int dw_i3c_transfer_ccc(DWI3C *s, DWI3CTransferCmd cmd,
                               DWI3CTransferArg arg)
{
    /* CCC start is always a write. CCCs cannot be done on I2C devices. */
    if (dw_i3c_send_start(s, I3C_BROADCAST, /*is_recv=*/false,
                                     /*is_i2c=*/false)) {
        return DW_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
    }
    trace_dw_i3c_transfer_ccc(s->cfg.id, cmd.cmd);
    if (dw_i3c_send_byte(s, cmd.cmd, /*is_i2c=*/false)) {
        return DW_I3C_RESP_QUEUE_ERR_I2C_NACK;
    }

    /* On a direct CCC, we do a restart and then send the target's address. */
    if (CCC_IS_DIRECT(cmd.cmd)) {
        bool is_recv = cmd.rnw;
        uint8_t addr = dw_i3c_target_addr(s, cmd.dev_index);
        if (dw_i3c_send_start(s, addr, is_recv, /*is_i2c=*/false)) {
            return DW_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
        }
    }

    return DW_I3C_RESP_QUEUE_ERR_NONE;
}

static void dw_i3c_transfer(DWI3C *s, DWI3CTransferCmd cmd,
                            DWI3CTransferArg arg)
{
    bool is_recv = cmd.rnw;
    uint8_t err = DW_I3C_RESP_QUEUE_ERR_NONE;
    uint8_t addr = dw_i3c_target_addr(s, cmd.dev_index);
    bool is_i2c = dw_i3c_target_is_i2c(s, cmd.dev_index);
    uint16_t bytes_transferred = 0;

    if (cmd.cp) {
        /* We're sending a CCC. */
        err = dw_i3c_transfer_ccc(s, cmd, arg);
        if (err != DW_I3C_RESP_QUEUE_ERR_NONE) {
            goto transfer_done;
        }
    } else {
        if (ARRAY_FIELD_EX32(s->regs, DEVICE_CTRL, I3C_BROADCAST_ADDR_INC) &&
            is_i2c == false) {
            if (dw_i3c_send_start(s, I3C_BROADCAST,
                                             /*is_recv=*/false, is_i2c)) {
                err = DW_I3C_RESP_QUEUE_ERR_I2C_NACK;
                goto transfer_done;
            }
        }
        /* Otherwise we're doing a private transfer. */
        if (dw_i3c_send_start(s, addr, is_recv, is_i2c)) {
            err = DW_I3C_RESP_QUEUE_ERR_I2C_NACK;
            goto transfer_done;
        }
    }

    if (is_recv) {
        bytes_transferred = dw_i3c_rx(s, arg.data_len, is_i2c);
    } else {
        bytes_transferred = dw_i3c_tx(s, arg.data_len, is_i2c);
    }

    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                     DW_I3C_TRANSFER_STATE_IDLE);

transfer_done:
    if (cmd.toc) {
        dw_i3c_end_transfer(s, is_i2c);
    }
    if (cmd.roc) {
        /*
         * data_len is the number of bytes that still need to be TX'd, or the
         * number of bytes RX'd.
         */
        uint16_t data_len = is_recv ? bytes_transferred : arg.data_len -
                                                          bytes_transferred;
        /* CCCT is always 0 in controller mode. */
        dw_i3c_resp_queue_push(s, err, cmd.tid, /*ccc_type=*/0,
                                          data_len);
    }

    dw_i3c_update_irq(s);
}

static void dw_i3c_transfer_cmd(DWI3C *s, DWI3CTransferCmd cmd,
                                DWI3CCmdQueueData arg)
{
    uint8_t arg_attr = FIELD_EX32(arg.word, COMMAND_QUEUE_PORT, CMD_ATTR);

    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CMD_TID, cmd.tid);

    /* User is trying to do HDR transfers, see if we can do them. */
    if (cmd.speed == 0x06 && !dw_i3c_has_hdr_ddr(s)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: HDR DDR is not supported\n", path);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DW_I3C_TRANSFER_STATE_HALT);
        return;
    }
    if (cmd.speed == 0x05 && !dw_i3c_has_hdr_ts(s)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: HDR TS is not supported\n", path);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DW_I3C_TRANSFER_STATE_HALT);
        return;
    }

    if (arg_attr == DW_I3C_CMD_ATTR_TRANSFER_ARG) {
        dw_i3c_transfer(s, cmd, arg.transfer_arg);
    } else if (arg_attr == DW_I3C_CMD_ATTR_SHORT_DATA_ARG) {
        dw_i3c_short_transfer(s, cmd, arg.short_arg);
    } else {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown command queue cmd_attr 0x%x"
                      "\n", path, arg_attr);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DW_I3C_TRANSFER_STATE_HALT);
    }
}

static void dw_i3c_update_char_table(DWI3C *s, uint8_t offset, uint64_t pid,
                                     uint8_t bcr, uint8_t dcr, uint8_t addr)
{
    if (offset > s->cfg.num_addressable_devices) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Device char table offset %d out of "
                      "bounds\n", path, offset);
        /* If we're out of bounds, do nothing. */
        return;
    }

    /*
     * Each device offset is 128 bits apart in the table, since each device gets
     * 4 * 32-bits of entries in the table.
     * / sizeof(uint32_t) because we're indexing into our 32-bit reg array.
     */
    uint16_t dev_index = (ARRAY_FIELD_EX32(s->regs, DEV_CHAR_TABLE_POINTER,
                                          P_DEV_CHAR_TABLE_START_ADDR) /
                                          sizeof(uint32_t)) +
                                          (offset * sizeof(uint32_t));
    s->regs[dev_index] = pid & 0xffffffff;
    pid >>= 32;
    s->regs[dev_index + 1] = FIELD_DP32(s->regs[dev_index + 1],
                                        DEVICE_CHARACTERISTIC_TABLE_LOC2,
                                        MSB_PID, pid);
    s->regs[dev_index + 2] = FIELD_DP32(s->regs[dev_index + 2],
                                        DEVICE_CHARACTERISTIC_TABLE_LOC3, DCR,
                                        dcr);
    s->regs[dev_index + 2] = FIELD_DP32(s->regs[dev_index + 2],
                                        DEVICE_CHARACTERISTIC_TABLE_LOC3, BCR,
                                        bcr);
    s->regs[dev_index + 3] = FIELD_DP32(s->regs[dev_index + 3],
                                        DEVICE_CHARACTERISTIC_TABLE_LOC4,
                                        DEV_DYNAMIC_ADDR, addr);

    /* Increment PRESENT_DEV_CHAR_TABLE_INDEX. */
    uint8_t idx = ARRAY_FIELD_EX32(s->regs, DEV_CHAR_TABLE_POINTER,
                     PRESENT_DEV_CHAR_TABLE_INDEX);
    /* Increment and rollover. */
    idx++;
    if (idx >= ARRAY_FIELD_EX32(s->regs, DEV_CHAR_TABLE_POINTER,
                               DEV_CHAR_TABLE_DEPTH) / 4) {
        idx = 0;
    }
    ARRAY_FIELD_DP32(s->regs, DEV_CHAR_TABLE_POINTER,
                     PRESENT_DEV_CHAR_TABLE_INDEX, idx);
}

static void dw_i3c_addr_assign_cmd(DWI3C *s, DWI3CAddrAssignCmd cmd)
{
    uint8_t i = 0;
    uint8_t err = DW_I3C_RESP_QUEUE_ERR_NONE;

    /* Tell everyone to ENTDAA. If these error, no one is on the bus. */
    if (dw_i3c_send_start(s, I3C_BROADCAST, /*is_recv=*/false,
                                     /*is_i2c=*/false)) {
        err = DW_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
        goto transfer_done;
    }
    if (dw_i3c_send_byte(s, cmd.cmd, /*is_i2c=*/false)) {
        err = DW_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
        goto transfer_done;
    }

    /* Go through each device in the table and assign it an address. */
    for (i = 0; i < cmd.dev_count; i++) {
        uint8_t addr = dw_i3c_target_addr(s, cmd.dev_index + i);
        union {
            uint64_t pid:48;
            uint8_t bcr;
            uint8_t dcr;
            uint32_t w[2];
            uint8_t b[8];
        } target_info;

        /* If this fails, there was no one left to ENTDAA. */
        if (dw_i3c_send_start(s, I3C_BROADCAST, /*is_recv=*/false,
                                         /*is_i2c=*/false)) {
            err = DW_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
            break;
        }

        /*
         * In ENTDAA, we read 8 bytes from the target, which will be the
         * target's PID, BCR, and DCR. After that, we send it the dynamic
         * address.
         * Don't bother checking the number of bytes received, it must send 8
         * bytes during ENTDAA.
         */
        uint32_t num_read;
        if (dw_i3c_recv_data(s, /*is_i2c=*/false, target_info.b,
                                        I3C_ENTDAA_SIZE, &num_read)) {
            g_autofree char *path = object_get_canonical_path(OBJECT(s));
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Target NACKed ENTDAA CCC\n",
                          path);
            err = DW_I3C_RESP_QUEUE_ERR_DAA_NACK;
            goto transfer_done;
        }
        if (dw_i3c_send_byte(s, addr, /*is_i2c=*/false)) {
            g_autofree char *path = object_get_canonical_path(OBJECT(s));
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Target NACKed addr 0x%.2x "
                          "during ENTDAA\n", path, addr);
            err = DW_I3C_RESP_QUEUE_ERR_DAA_NACK;
            break;
        }
        dw_i3c_update_char_table(s, cmd.dev_index + i,
                                            target_info.pid, target_info.bcr,
                                            target_info.dcr, addr);

        /* Push the PID, BCR, and DCR to the RX queue. */
        dw_i3c_push_rx(s, target_info.w[0]);
        dw_i3c_push_rx(s, target_info.w[1]);
    }

transfer_done:
    /* Do we send a STOP? */
    if (cmd.toc) {
        dw_i3c_end_transfer(s, /*is_i2c=*/false);
    }
    /*
     * For addr assign commands, the length field is the number of devices
     * left to assign. CCCT is always 0 in controller mode.
     */
    if (cmd.roc) {
        dw_i3c_resp_queue_push(s, err, cmd.tid, /*ccc_type=*/0,
                                         cmd.dev_count - i);
    }
}

static uint32_t dw_i3c_cmd_queue_pop(DWI3C *s)
{
    if (fifo32_is_empty(&s->cmd_queue)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to dequeue command queue "
                      "when it was empty\n", path);
        return 0;
    }
    uint32_t val = fifo32_pop(&s->cmd_queue);

    uint8_t empty_threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                               CMD_BUF_EMPTY_THLD);
    uint8_t cmd_queue_empty_loc = ARRAY_FIELD_EX32(s->regs,
                                                   QUEUE_STATUS_LEVEL,
                                                   CMD_QUEUE_EMPTY_LOC);
    cmd_queue_empty_loc++;
    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, CMD_QUEUE_EMPTY_LOC,
                     cmd_queue_empty_loc);
    if (cmd_queue_empty_loc >= empty_threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, CMD_QUEUE_RDY, 1);
        dw_i3c_update_irq(s);
    }

    return val;
}

static void dw_i3c_cmd_queue_execute(DWI3C *s)
{
    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                     DW_I3C_TRANSFER_STATE_IDLE);
    if (!dw_i3c_can_transmit(s)) {
        return;
    }

    /*
     * We only start executing when a command is passed into the FIFO.
     * We expect there to be a multiple of 2 items in the queue. The first item
     * should be an argument to a command, and the command should be the second
     * item.
     */
    if (fifo32_num_used(&s->cmd_queue) & 1) {
        return;
    }

    while (!fifo32_is_empty(&s->cmd_queue)) {
        DWI3CCmdQueueData arg;
        arg.word = dw_i3c_cmd_queue_pop(s);
        DWI3CCmdQueueData cmd;
        cmd.word = dw_i3c_cmd_queue_pop(s);
        trace_dw_i3c_cmd_queue_execute(s->cfg.id, cmd.word, arg.word);

        uint8_t cmd_attr = FIELD_EX32(cmd.word, COMMAND_QUEUE_PORT, CMD_ATTR);
        switch (cmd_attr) {
        case DW_I3C_CMD_ATTR_TRANSFER_CMD:
            dw_i3c_transfer_cmd(s, cmd.transfer_cmd, arg);
            break;
        case DW_I3C_CMD_ATTR_ADDR_ASSIGN_CMD:
            /* Arg is discarded for addr assign commands. */
            dw_i3c_addr_assign_cmd(s, cmd.addr_assign_cmd);
            break;
        case DW_I3C_CMD_ATTR_TRANSFER_ARG:
        case DW_I3C_CMD_ATTR_SHORT_DATA_ARG:
            {
                g_autofree char *path = object_get_canonical_path(OBJECT(s));
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Command queue received "
                              "argument packet when it expected a command "
                              "packet\n", path);
            }
            break;
        default:
            /*
             * The caller's check before queueing an item should prevent this
             * from happening.
             */
            g_assert_not_reached();
            break;
        }
    }
}

static void dw_i3c_cmd_queue_push(DWI3C *s, uint32_t val)
{
    if (fifo32_is_full(&s->cmd_queue)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Command queue received packet when "
                      "already full\n", path);
        return;
    }
    trace_dw_i3c_cmd_queue_push(s->cfg.id, val);
    fifo32_push(&s->cmd_queue, val);

    uint8_t empty_threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                               CMD_BUF_EMPTY_THLD);
    uint8_t cmd_queue_empty_loc = ARRAY_FIELD_EX32(s->regs,
                                                   QUEUE_STATUS_LEVEL,
                                                   CMD_QUEUE_EMPTY_LOC);
    if (cmd_queue_empty_loc) {
        cmd_queue_empty_loc--;
        ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, CMD_QUEUE_EMPTY_LOC,
                         cmd_queue_empty_loc);
    }
    if (cmd_queue_empty_loc < empty_threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, CMD_QUEUE_RDY, 0);
        dw_i3c_update_irq(s);
    }
}

static void dw_i3c_cmd_queue_port_w(DWI3C *s, uint32_t val)
{
    uint8_t cmd_attr = FIELD_EX32(val, COMMAND_QUEUE_PORT, CMD_ATTR);

    switch (cmd_attr) {
    /* If a command is received we can start executing it. */
    case DW_I3C_CMD_ATTR_TRANSFER_CMD:
    case DW_I3C_CMD_ATTR_ADDR_ASSIGN_CMD:
        dw_i3c_cmd_queue_push(s, val);
        dw_i3c_cmd_queue_execute(s);
        break;
    /* If we get an argument just push it. */
    case DW_I3C_CMD_ATTR_TRANSFER_ARG:
    case DW_I3C_CMD_ATTR_SHORT_DATA_ARG:
        dw_i3c_cmd_queue_push(s, val);
        break;
    default:
        {
            g_autofree char *path = object_get_canonical_path(OBJECT(s));
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Command queue received packet "
                          "with unknown cmd attr 0x%x\n", path, cmd_attr);
        }
        break;
    }
}

static void dw_i3c_write(void *opaque, hwaddr offset, uint64_t value,
                         unsigned size)
{
    DWI3C *s = DW_I3C(opaque);
    uint32_t addr = offset >> 2;
    uint32_t val32 = (uint32_t)value;

    trace_dw_i3c_write(s->cfg.id, offset, value);

    val32 &= ~dw_i3c_ro[addr];
    switch (addr) {
    case R_HW_CAPABILITY:
    case R_RESPONSE_QUEUE_PORT:
    case R_IBI_QUEUE_DATA:
    case R_QUEUE_STATUS_LEVEL:
    case R_PRESENT_STATE:
    case R_CCC_DEVICE_STATUS:
    case R_DEVICE_ADDR_TABLE_POINTER:
    case R_VENDOR_SPECIFIC_REG_POINTER:
    case R_SLV_CHAR_CTRL:
    case R_SLV_MAX_LEN:
    case R_MAX_READ_TURNAROUND:
    case R_I3C_VER_ID:
    case R_I3C_VER_TYPE:
    case R_EXTENDED_CAPABILITY:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to readonly register[0x%02" HWADDR_PRIx
                      "] = 0x%08" PRIx64 "\n",
                      __func__, offset, value);
        break;
    case R_RX_TX_DATA_PORT:
        dw_i3c_push_tx(s, val32);
        break;
    case R_COMMAND_QUEUE_PORT:
        dw_i3c_cmd_queue_port_w(s, val32);
        break;
    case R_RESET_CTRL:
        break;
    case R_INTR_STATUS:
        dw_i3c_intr_status_w(s, val32);
        break;
    case R_INTR_STATUS_EN:
        dw_i3c_intr_status_en_w(s, val32);
        break;
    case R_INTR_SIGNAL_EN:
        dw_i3c_intr_signal_en_w(s, val32);
        break;
    case R_INTR_FORCE:
        dw_i3c_intr_force_w(s, val32);
        break;
    default:
        s->regs[addr] = val32;
        break;
    }
}

const VMStateDescription vmstate_dw_i3c = {
    .name = TYPE_DW_I3C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){
        VMSTATE_UINT32_ARRAY(regs, DWI3C, DW_I3C_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static const MemoryRegionOps dw_i3c_ops = {
    .read = dw_i3c_read,
    .write = dw_i3c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void dw_i3c_reset_enter(Object *obj, ResetType type)
{
    DWI3C *s = DW_I3C(obj);

    memcpy(s->regs, dw_i3c_resets, sizeof(s->regs));
    /*
     * The user config for these may differ from our resets array, set them
     * manually.
     */
    ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR_TABLE_POINTER, ADDR,
                     s->cfg.dev_addr_table_pointer);
    ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR_TABLE_POINTER, DEPTH,
                     s->cfg.dev_addr_table_depth);
    ARRAY_FIELD_DP32(s->regs, DEV_CHAR_TABLE_POINTER,
                     P_DEV_CHAR_TABLE_START_ADDR,
                     s->cfg.dev_char_table_pointer);
    ARRAY_FIELD_DP32(s->regs, DEV_CHAR_TABLE_POINTER, DEV_CHAR_TABLE_DEPTH,
                     s->cfg.dev_char_table_depth);
}

static void dw_i3c_realize(DeviceState *dev, Error **errp)
{
    DWI3C *s = DW_I3C(dev);
    g_autofree char *name = g_strdup_printf(TYPE_DW_I3C ".%d", s->cfg.id);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    memory_region_init_io(&s->mr, OBJECT(s), &dw_i3c_ops, s, name,
                          DW_I3C_NR_REGS << 2);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);

    fifo32_create(&s->cmd_queue, s->cfg.cmd_resp_queue_capacity_bytes);
    fifo32_create(&s->resp_queue, s->cfg.cmd_resp_queue_capacity_bytes);
    fifo32_create(&s->tx_queue, s->cfg.tx_rx_queue_capacity_bytes);
    fifo32_create(&s->rx_queue, s->cfg.tx_rx_queue_capacity_bytes);

    s->bus = i3c_init_bus(DEVICE(s), name);
}

static const Property dw_i3c_properties[] = {
    DEFINE_PROP_UINT8("device-id", DWI3C, cfg.id, 0),
    DEFINE_PROP_UINT8("command-response-queue-capacity-bytes", DWI3C,
                      cfg.cmd_resp_queue_capacity_bytes, 0x10),
    DEFINE_PROP_UINT16("tx-rx-queue-capacity-bytes", DWI3C,
                      cfg.tx_rx_queue_capacity_bytes, 0x40),
    DEFINE_PROP_UINT8("num-addressable-devices", DWI3C,
                      cfg.num_addressable_devices, 8),
    DEFINE_PROP_UINT16("dev-addr-table-pointer", DWI3C,
                       cfg.dev_addr_table_pointer, 0x280),
    DEFINE_PROP_UINT16("dev-addr-table-depth", DWI3C,
                       cfg.dev_addr_table_depth, 0x08),
    DEFINE_PROP_UINT16("dev-char-table-pointer", DWI3C,
                       cfg.dev_char_table_pointer, 0x200),
    DEFINE_PROP_UINT16("dev-char-table-depth", DWI3C,
                       cfg.dev_char_table_depth, 0x20),
};

static void dw_i3c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = dw_i3c_reset_enter;

    dc->desc = "DesignWare I3C Controller";
    dc->realize = dw_i3c_realize;
    dc->vmsd = &vmstate_dw_i3c;
    device_class_set_props(dc, dw_i3c_properties);
}

static const TypeInfo dw_i3c_types[] = {
    {
        .name = TYPE_DW_I3C,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(DWI3C),
        .class_init = dw_i3c_class_init,
    },
};

DEFINE_TYPES(dw_i3c_types)

