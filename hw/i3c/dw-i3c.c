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
    default:
        value = s->regs[addr];
        break;
    }

    trace_dw_i3c_read(s->id, offset, value);

    return value;
}

static void dw_i3c_write(void *opaque, hwaddr offset, uint64_t value,
                         unsigned size)
{
    DWI3C *s = DW_I3C(opaque);
    uint32_t addr = offset >> 2;

    trace_dw_i3c_write(s->id, offset, value);

    value &= ~dw_i3c_ro[addr];
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
        break;
    case R_RESET_CTRL:
        break;
    default:
        s->regs[addr] = value;
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
}

static void dw_i3c_realize(DeviceState *dev, Error **errp)
{
    DWI3C *s = DW_I3C(dev);
    g_autofree char *name = g_strdup_printf(TYPE_DW_I3C ".%d", s->id);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    memory_region_init_io(&s->mr, OBJECT(s), &dw_i3c_ops, s, name,
                          DW_I3C_NR_REGS << 2);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);
}

static const Property dw_i3c_properties[] = {
    DEFINE_PROP_UINT8("device-id", DWI3C, id, 0),
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

