/*
 * DesignWare I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Copyright (C) 2025 Google, LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DW_I3C_H
#define DW_I3C_H

#include "qemu/fifo32.h"
#include "hw/i3c/i3c.h"
#include "hw/core/sysbus.h"

#define TYPE_DW_I3C "dw.i3c"
OBJECT_DECLARE_SIMPLE_TYPE(DWI3C, DW_I3C)

/*
 * Sufficiently large enough to handle configurations with large device address
 * tables.
 */
#define DW_I3C_NR_REGS (0x1000 >> 2)

/* From datasheet. */
#define DW_I3C_CMD_ATTR_TRANSFER_CMD 0
#define DW_I3C_CMD_ATTR_TRANSFER_ARG 1
#define DW_I3C_CMD_ATTR_SHORT_DATA_ARG 2
#define DW_I3C_CMD_ATTR_ADDR_ASSIGN_CMD 3

/* Enum values from datasheet. */
typedef enum DWI3CRespQueueErr {
    DW_I3C_RESP_QUEUE_ERR_NONE = 0,
    DW_I3C_RESP_QUEUE_ERR_CRC = 1,
    DW_I3C_RESP_QUEUE_ERR_PARITY = 2,
    DW_I3C_RESP_QUEUE_ERR_FRAME = 3,
    DW_I3C_RESP_QUEUE_ERR_BROADCAST_NACK = 4,
    DW_I3C_RESP_QUEUE_ERR_DAA_NACK = 5,
    DW_I3C_RESP_QUEUE_ERR_OVERFLOW = 6,
    DW_I3C_RESP_QUEUE_ERR_ABORTED = 8,
    DW_I3C_RESP_QUEUE_ERR_I2C_NACK = 9,
} DWI3CRespQueueErr;

typedef enum DWI3CTransferState {
    DW_I3C_TRANSFER_STATE_IDLE = 0x00,
    DW_I3C_TRANSFER_STATE_START = 0x01,
    DW_I3C_TRANSFER_STATE_RESTART = 0x02,
    DW_I3C_TRANSFER_STATE_STOP = 0x03,
    DW_I3C_TRANSFER_STATE_START_HOLD = 0x04,
    DW_I3C_TRANSFER_STATE_BROADCAST_W = 0x05,
    DW_I3C_TRANSFER_STATE_BROADCAST_R = 0x06,
    DW_I3C_TRANSFER_STATE_DAA = 0x07,
    DW_I3C_TRANSFER_STATE_DAA_GEN = 0x08,
    DW_I3C_TRANSFER_STATE_CCC_BYTE = 0x0b,
    DW_I3C_TRANSFER_STATE_HDR_CMD = 0x0c,
    DW_I3C_TRANSFER_STATE_WRITE = 0x0d,
    DW_I3C_TRANSFER_STATE_READ = 0x0e,
    DW_I3C_TRANSFER_STATE_IBI_READ = 0x0f,
    DW_I3C_TRANSFER_STATE_IBI_DIS = 0x10,
    DW_I3C_TRANSFER_STATE_HDR_DDR_CRC = 0x11,
    DW_I3C_TRANSFER_STATE_CLK_STRETCH = 0x12,
    DW_I3C_TRANSFER_STATE_HALT = 0x13,
} DWI3CTransferState;

typedef enum DWI3CTransferStatus {
    DW_I3C_TRANSFER_STATUS_IDLE = 0x00,
    DW_I3C_TRANSFER_STATUS_BROACAST_CCC = 0x01,
    DW_I3C_TRANSFER_STATUS_DIRECT_CCC_W = 0x02,
    DW_I3C_TRANSFER_STATUS_DIRECT_CCC_R = 0x03,
    DW_I3C_TRANSFER_STATUS_ENTDAA = 0x04,
    DW_I3C_TRANSFER_STATUS_SETDASA = 0x05,
    DW_I3C_TRANSFER_STATUS_I3C_SDR_W = 0x06,
    DW_I3C_TRANSFER_STATUS_I3C_SDR_R = 0x07,
    DW_I3C_TRANSFER_STATUS_I2C_SDR_W = 0x08,
    DW_I3C_TRANSFER_STATUS_I2C_SDR_R = 0x09,
    DW_I3C_TRANSFER_STATUS_HDR_TS_W = 0x0a,
    DW_I3C_TRANSFER_STATUS_HDR_TS_R = 0x0b,
    DW_I3C_TRANSFER_STATUS_HDR_DDR_W = 0x0c,
    DW_I3C_TRANSFER_STATUS_HDR_DDR_R = 0x0d,
    DW_I3C_TRANSFER_STATUS_IBI = 0x0e,
    DW_I3C_TRANSFER_STATUS_HALT = 0x0f,
} DWI3CTransferStatus;

/*
 * Transfer commands and arguments are 32-bit wide values that the user passes
 * into the command queue. We interpret each 32-bit word based on the cmd_attr
 * field.
 */
typedef struct DWI3CTransferCmd {
    uint8_t cmd_attr:3;
    uint8_t tid:4; /* Transaction ID */
    uint16_t cmd:8;
    uint8_t cp:1; /* Command present */
    uint8_t dev_index:5;
    uint8_t speed:3;
    uint8_t resv0:1;
    uint8_t dbp:1; /* Defining byte present */
    uint8_t roc:1; /* Response on completion */
    uint8_t sdap:1; /* Short data argument present */
    uint8_t rnw:1; /* Read not write */
    uint8_t resv1:1;
    uint8_t toc:1; /* Termination (I3C STOP) on completion */
    uint8_t pec:1; /* Parity error check enabled */
} DWI3CTransferCmd;

typedef struct DWI3CTransferArg {
    uint8_t cmd_attr:3;
    uint8_t resv:5;
    uint8_t db; /* Defining byte */
    uint16_t data_len;
} DWI3CTransferArg;

typedef struct DWI3CShortArg {
    uint8_t cmd_attr:3;
    uint8_t byte_strb:3;
    uint8_t resv:2;
    uint8_t byte0;
    uint8_t byte1;
    uint8_t byte2;
} DWI3CShortArg;

typedef struct DWI3CAddrAssignCmd {
    uint8_t cmd_attr:3;
    uint8_t tid:4; /* Transaction ID */
    uint16_t cmd:8;
    uint8_t resv0:1;
    uint8_t dev_index:5;
    uint16_t dev_count:5;
    uint8_t roc:1; /* Response on completion */
    uint8_t resv1:3;
    uint8_t toc:1; /* Termination (I3C STOP) on completion */
    uint8_t resv2:1;
} DWI3CAddrAssignCmd;

typedef union DWI3CCmdQueueData {
    uint32_t word;
    DWI3CTransferCmd transfer_cmd;
    DWI3CTransferArg transfer_arg;
    DWI3CShortArg short_arg;
    DWI3CAddrAssignCmd addr_assign_cmd;
} DWI3CCmdQueueData;

struct DWI3C {
    SysBusDevice parent_obj;

    MemoryRegion mr;
    qemu_irq irq;
    I3CBus *bus;

    Fifo32 cmd_queue;
    Fifo32 resp_queue;
    Fifo32 tx_queue;
    Fifo32 rx_queue;

    struct {
        uint8_t id;
        uint8_t cmd_resp_queue_capacity_bytes;
        uint16_t tx_rx_queue_capacity_bytes;
        uint8_t num_addressable_devices;
        uint16_t dev_addr_table_pointer;
        uint16_t dev_addr_table_depth;
        uint16_t dev_char_table_pointer;
        uint16_t dev_char_table_depth;
    } cfg;
    uint32_t regs[DW_I3C_NR_REGS];
};

/* Extern for other controllers that use DesignWare I3C. */
extern const VMStateDescription vmstate_dw_i3c;

#endif /* DW_I3C_H */
