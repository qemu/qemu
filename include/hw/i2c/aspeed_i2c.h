/*
 *  ASPEED AST2400 I2C Controller
 *
 *  Copyright (C) 2016 IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef ASPEED_I2C_H
#define ASPEED_I2C_H

#include "hw/i2c/i2c.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "qom/object.h"

#define TYPE_ASPEED_I2C "aspeed.i2c"
#define TYPE_ASPEED_2400_I2C TYPE_ASPEED_I2C "-ast2400"
#define TYPE_ASPEED_2500_I2C TYPE_ASPEED_I2C "-ast2500"
#define TYPE_ASPEED_2600_I2C TYPE_ASPEED_I2C "-ast2600"
#define TYPE_ASPEED_1030_I2C TYPE_ASPEED_I2C "-ast1030"
OBJECT_DECLARE_TYPE(AspeedI2CState, AspeedI2CClass, ASPEED_I2C)

#define ASPEED_I2C_NR_BUSSES 16
#define ASPEED_I2C_MAX_POOL_SIZE 0x800
#define ASPEED_I2C_OLD_NUM_REG 11
#define ASPEED_I2C_NEW_NUM_REG 22

#define A_I2CD_M_STOP_CMD       BIT(5)
#define A_I2CD_M_RX_CMD         BIT(3)
#define A_I2CD_M_TX_CMD         BIT(1)
#define A_I2CD_M_START_CMD      BIT(0)

#define A_I2CD_MASTER_EN        BIT(0)

/* Tx State Machine */
#define   I2CD_TX_STATE_MASK                  0xf
#define     I2CD_IDLE                         0x0
#define     I2CD_MACTIVE                      0x8
#define     I2CD_MSTART                       0x9
#define     I2CD_MSTARTR                      0xa
#define     I2CD_MSTOP                        0xb
#define     I2CD_MTXD                         0xc
#define     I2CD_MRXACK                       0xd
#define     I2CD_MRXD                         0xe
#define     I2CD_MTXACK                       0xf
#define     I2CD_SWAIT                        0x1
#define     I2CD_SRXD                         0x4
#define     I2CD_STXACK                       0x5
#define     I2CD_STXD                         0x6
#define     I2CD_SRXACK                       0x7
#define     I2CD_RECOVER                      0x3

/* I2C Global Register */
REG32(I2C_CTRL_STATUS, 0x0) /* Device Interrupt Status */
REG32(I2C_CTRL_ASSIGN, 0x8) /* Device Interrupt Target Assignment */
REG32(I2C_CTRL_GLOBAL, 0xC) /* Global Control Register */
    FIELD(I2C_CTRL_GLOBAL, REG_MODE, 2, 1)
    FIELD(I2C_CTRL_GLOBAL, SRAM_EN, 0, 1)
REG32(I2C_CTRL_NEW_CLK_DIVIDER, 0x10) /* New mode clock divider */

/* I2C Old Mode Device (Bus) Register */
REG32(I2CD_FUN_CTRL, 0x0) /* I2CD Function Control  */
    FIELD(I2CD_FUN_CTRL, POOL_PAGE_SEL, 20, 3) /* AST2400 */
    SHARED_FIELD(M_SDA_LOCK_EN, 16, 1)
    SHARED_FIELD(MULTI_MASTER_DIS, 15, 1)
    SHARED_FIELD(M_SCL_DRIVE_EN, 14, 1)
    SHARED_FIELD(MSB_STS, 9, 1)
    SHARED_FIELD(SDA_DRIVE_IT_EN, 8, 1)
    SHARED_FIELD(M_SDA_DRIVE_IT_EN, 7, 1)
    SHARED_FIELD(M_HIGH_SPEED_EN, 6, 1)
    SHARED_FIELD(DEF_ADDR_EN, 5, 1)
    SHARED_FIELD(DEF_ALERT_EN, 4, 1)
    SHARED_FIELD(DEF_ARP_EN, 3, 1)
    SHARED_FIELD(DEF_GCALL_EN, 2, 1)
    SHARED_FIELD(SLAVE_EN, 1, 1)
    SHARED_FIELD(MASTER_EN, 0, 1)
REG32(I2CD_AC_TIMING1, 0x04) /* Clock and AC Timing Control #1 */
REG32(I2CD_AC_TIMING2, 0x08) /* Clock and AC Timing Control #2 */
REG32(I2CD_INTR_CTRL, 0x0C)  /* I2CD Interrupt Control */
REG32(I2CD_INTR_STS, 0x10)   /* I2CD Interrupt Status */
    SHARED_FIELD(SLAVE_ADDR_MATCH, 31, 1)    /* 0: addr1 1: addr2 */
    SHARED_FIELD(SLAVE_ADDR_RX_PENDING, 29, 1)
    SHARED_FIELD(SLAVE_INACTIVE_TIMEOUT, 15, 1)
    SHARED_FIELD(SDA_DL_TIMEOUT, 14, 1)
    SHARED_FIELD(BUS_RECOVER_DONE, 13, 1)
    SHARED_FIELD(SMBUS_ALERT, 12, 1)                    /* Bus [0-3] only */
    FIELD(I2CD_INTR_STS, SMBUS_ARP_ADDR, 11, 1)         /* Removed */
    FIELD(I2CD_INTR_STS, SMBUS_DEV_ALERT_ADDR, 10, 1)   /* Removed */
    FIELD(I2CD_INTR_STS, SMBUS_DEF_ADDR, 9, 1)          /* Removed */
    FIELD(I2CD_INTR_STS, GCALL_ADDR, 8, 1)              /* Removed */
    FIELD(I2CD_INTR_STS, SLAVE_ADDR_RX_MATCH, 7, 1)     /* use RX_DONE */
    SHARED_FIELD(SCL_TIMEOUT, 6, 1)
    SHARED_FIELD(ABNORMAL, 5, 1)
    SHARED_FIELD(NORMAL_STOP, 4, 1)
    SHARED_FIELD(ARBIT_LOSS, 3, 1)
    SHARED_FIELD(RX_DONE, 2, 1)
    SHARED_FIELD(TX_NAK, 1, 1)
    SHARED_FIELD(TX_ACK, 0, 1)
REG32(I2CD_CMD, 0x14) /* I2CD Command/Status */
    SHARED_FIELD(SDA_OE, 28, 1)
    SHARED_FIELD(SDA_O, 27, 1)
    SHARED_FIELD(SCL_OE, 26, 1)
    SHARED_FIELD(SCL_O, 25, 1)
    SHARED_FIELD(TX_TIMING, 23, 2)
    SHARED_FIELD(TX_STATE, 19, 4)
    SHARED_FIELD(SCL_LINE_STS, 18, 1)
    SHARED_FIELD(SDA_LINE_STS, 17, 1)
    SHARED_FIELD(BUS_BUSY_STS, 16, 1)
    SHARED_FIELD(SDA_OE_OUT_DIR, 15, 1)
    SHARED_FIELD(SDA_O_OUT_DIR, 14, 1)
    SHARED_FIELD(SCL_OE_OUT_DIR, 13, 1)
    SHARED_FIELD(SCL_O_OUT_DIR, 12, 1)
    SHARED_FIELD(BUS_RECOVER_CMD_EN, 11, 1)
    SHARED_FIELD(S_ALT_EN, 10, 1)
    /* Command Bits */
    SHARED_FIELD(RX_DMA_EN, 9, 1)
    SHARED_FIELD(TX_DMA_EN, 8, 1)
    SHARED_FIELD(RX_BUFF_EN, 7, 1)
    SHARED_FIELD(TX_BUFF_EN, 6, 1)
    SHARED_FIELD(M_STOP_CMD, 5, 1)
    SHARED_FIELD(M_S_RX_CMD_LAST, 4, 1)
    SHARED_FIELD(M_RX_CMD, 3, 1)
    SHARED_FIELD(S_TX_CMD, 2, 1)
    SHARED_FIELD(M_TX_CMD, 1, 1)
    SHARED_FIELD(M_START_CMD, 0, 1)
REG32(I2CD_DEV_ADDR, 0x18) /* Slave Device Address */
    SHARED_FIELD(SLAVE_DEV_ADDR1, 0, 7)
REG32(I2CD_POOL_CTRL, 0x1C) /* Pool Buffer Control */
    SHARED_FIELD(RX_COUNT, 24, 6)
    SHARED_FIELD(RX_SIZE, 16, 5)
    SHARED_FIELD(TX_COUNT, 8, 5)
    FIELD(I2CD_POOL_CTRL, OFFSET, 2, 6) /* AST2400 */
REG32(I2CD_BYTE_BUF, 0x20) /* Transmit/Receive Byte Buffer */
    SHARED_FIELD(RX_BUF, 8, 8)
    SHARED_FIELD(TX_BUF, 0, 8)
REG32(I2CD_DMA_ADDR, 0x24) /* DMA Buffer Address */
REG32(I2CD_DMA_LEN, 0x28) /* DMA Transfer Length < 4KB */

/* I2C New Mode Device (Bus) Register */
REG32(I2CC_FUN_CTRL, 0x0)
    FIELD(I2CC_FUN_CTRL, RB_EARLY_DONE_EN, 22, 1)
    FIELD(I2CC_FUN_CTRL, DMA_DIS_AUTO_RECOVER, 21, 1)
    FIELD(I2CC_FUN_CTRL, S_SAVE_ADDR, 20, 1)
    FIELD(I2CC_FUN_CTRL, M_PKT_RETRY_CNT, 18, 2)
    /* 17:0 shared with I2CD_FUN_CTRL[17:0] */
REG32(I2CC_AC_TIMING, 0x04)
REG32(I2CC_MS_TXRX_BYTE_BUF, 0x08)
    /* 31:16 shared with I2CD_CMD[31:16] */
    /* 15:0  shared with I2CD_BYTE_BUF[15:0] */
REG32(I2CC_POOL_CTRL, 0x0c)
    /* 31:0 shared with I2CD_POOL_CTRL[31:0] */
REG32(I2CM_INTR_CTRL, 0x10)
REG32(I2CM_INTR_STS, 0x14)
    FIELD(I2CM_INTR_STS, PKT_STATE, 28, 4)
    FIELD(I2CM_INTR_STS, PKT_CMD_TIMEOUT, 18, 1)
    FIELD(I2CM_INTR_STS, PKT_CMD_FAIL, 17, 1)
    FIELD(I2CM_INTR_STS, PKT_CMD_DONE, 16, 1)
    FIELD(I2CM_INTR_STS, BUS_RECOVER_FAIL, 15, 1)
    /* 14:0 shared with I2CD_INTR_STS[14:0] */
REG32(I2CM_CMD, 0x18)
    FIELD(I2CM_CMD, W1_CTRL, 31, 1)
    FIELD(I2CM_CMD, PKT_DEV_ADDR, 24, 7)
    FIELD(I2CM_CMD, HS_MASTER_MODE_LSB, 17, 3)
    FIELD(I2CM_CMD, PKT_OP_EN, 16, 1)
    /* 15:0 shared with I2CD_CMD[15:0] */
REG32(I2CM_DMA_LEN, 0x1c)
    FIELD(I2CM_DMA_LEN, RX_BUF_LEN_W1T, 31, 1)
    FIELD(I2CM_DMA_LEN, RX_BUF_LEN, 16, 11)
    FIELD(I2CM_DMA_LEN, TX_BUF_LEN_W1T, 15, 1)
    FIELD(I2CM_DMA_LEN, TX_BUF_LEN, 0, 11)
REG32(I2CS_INTR_CTRL, 0x20)
    FIELD(I2CS_INTR_CTRL, PKT_CMD_FAIL, 17, 1)
    FIELD(I2CS_INTR_CTRL, PKT_CMD_DONE, 16, 1)
REG32(I2CS_INTR_STS, 0x24)
    /* 31:29 shared with I2CD_INTR_STS[31:29] */
    FIELD(I2CS_INTR_STS, SLAVE_PARKING_STS, 24, 2)
    FIELD(I2CS_INTR_STS, SLAVE_ADDR3_NAK, 22, 1)
    FIELD(I2CS_INTR_STS, SLAVE_ADDR2_NAK, 21, 1)
    FIELD(I2CS_INTR_STS, SLAVE_ADDR1_NAK, 20, 1)
    FIELD(I2CS_INTR_STS, SLAVE_ADDR_INDICATOR, 18, 2)
    FIELD(I2CS_INTR_STS, PKT_CMD_FAIL, 17, 1)
    FIELD(I2CS_INTR_STS, PKT_CMD_DONE, 16, 1)
    /* 14:0 shared with I2CD_INTR_STS[14:0] */
    FIELD(I2CS_INTR_STS, SLAVE_ADDR_RX_MATCH, 7, 1)
REG32(I2CS_CMD, 0x28)
    FIELD(I2CS_CMD, W1_CTRL, 31, 1)
    FIELD(I2CS_CMD, PKT_MODE_ACTIVE_ADDR, 17, 2)
    FIELD(I2CS_CMD, PKT_MODE_EN, 16, 1)
    FIELD(I2CS_CMD, AUTO_NAK_INACTIVE_ADDR, 15, 1)
    FIELD(I2CS_CMD, AUTO_NAK_ACTIVE_ADDR, 14, 1)
    /* 13:0 shared with I2CD_CMD[13:0] */
REG32(I2CS_DMA_LEN, 0x2c)
    FIELD(I2CS_DMA_LEN, RX_BUF_LEN_W1T, 31, 1)
    FIELD(I2CS_DMA_LEN, RX_BUF_LEN, 16, 11)
    FIELD(I2CS_DMA_LEN, TX_BUF_LEN_W1T, 15, 1)
    FIELD(I2CS_DMA_LEN, TX_BUF_LEN, 0, 11)
REG32(I2CM_DMA_TX_ADDR, 0x30)
    FIELD(I2CM_DMA_TX_ADDR, ADDR, 0, 31)
REG32(I2CM_DMA_RX_ADDR, 0x34)
    FIELD(I2CM_DMA_RX_ADDR, ADDR, 0, 31)
REG32(I2CS_DMA_TX_ADDR, 0x38)
    FIELD(I2CS_DMA_TX_ADDR, ADDR, 0, 31)
REG32(I2CS_DMA_RX_ADDR, 0x3c)
    FIELD(I2CS_DMA_RX_ADDR, ADDR, 0, 31)
REG32(I2CS_DEV_ADDR, 0x40)
REG32(I2CM_DMA_LEN_STS, 0x48)
    FIELD(I2CM_DMA_LEN_STS, RX_LEN, 16, 13)
    FIELD(I2CM_DMA_LEN_STS, TX_LEN, 0, 13)
REG32(I2CS_DMA_LEN_STS, 0x4c)
    FIELD(I2CS_DMA_LEN_STS, RX_LEN, 16, 13)
    FIELD(I2CS_DMA_LEN_STS, TX_LEN, 0, 13)
REG32(I2CC_DMA_ADDR, 0x50)
REG32(I2CC_DMA_LEN, 0x54)

struct AspeedI2CState;

#define TYPE_ASPEED_I2C_BUS "aspeed.i2c.bus"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedI2CBus, ASPEED_I2C_BUS)
struct AspeedI2CBus {
    SysBusDevice parent_obj;

    struct AspeedI2CState *controller;

    /* slave mode */
    I2CSlave *slave;

    MemoryRegion mr;

    I2CBus *bus;
    uint8_t id;
    qemu_irq irq;

    uint32_t regs[ASPEED_I2C_NEW_NUM_REG];
};

struct AspeedI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t intr_status;
    uint32_t ctrl_global;
    uint32_t new_clk_divider;
    MemoryRegion pool_iomem;
    uint8_t pool[ASPEED_I2C_MAX_POOL_SIZE];

    AspeedI2CBus busses[ASPEED_I2C_NR_BUSSES];
    MemoryRegion *dram_mr;
    AddressSpace dram_as;
};

#define TYPE_ASPEED_I2C_BUS_SLAVE "aspeed.i2c.slave"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedI2CBusSlave, ASPEED_I2C_BUS_SLAVE)
struct AspeedI2CBusSlave {
    I2CSlave i2c;
};

struct AspeedI2CClass {
    SysBusDeviceClass parent_class;

    uint8_t num_busses;
    uint8_t reg_size;
    uint8_t gap;
    qemu_irq (*bus_get_irq)(AspeedI2CBus *);

    uint64_t pool_size;
    hwaddr pool_base;
    uint8_t *(*bus_pool_base)(AspeedI2CBus *);
    bool check_sram;
    bool has_dma;

};

static inline bool aspeed_i2c_is_new_mode(AspeedI2CState *s)
{
    return FIELD_EX32(s->ctrl_global, I2C_CTRL_GLOBAL, REG_MODE);
}

static inline bool aspeed_i2c_bus_pkt_mode_en(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return ARRAY_FIELD_EX32(bus->regs, I2CM_CMD, PKT_OP_EN);
    }
    return false;
}

static inline uint32_t aspeed_i2c_bus_ctrl_offset(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return R_I2CC_FUN_CTRL;
    }
    return R_I2CD_FUN_CTRL;
}

static inline uint32_t aspeed_i2c_bus_cmd_offset(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return R_I2CM_CMD;
    }
    return R_I2CD_CMD;
}

static inline uint32_t aspeed_i2c_bus_dev_addr_offset(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return R_I2CS_DEV_ADDR;
    }
    return R_I2CD_DEV_ADDR;
}

static inline uint32_t aspeed_i2c_bus_intr_ctrl_offset(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return R_I2CM_INTR_CTRL;
    }
    return R_I2CD_INTR_CTRL;
}

static inline uint32_t aspeed_i2c_bus_intr_sts_offset(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return R_I2CM_INTR_STS;
    }
    return R_I2CD_INTR_STS;
}

static inline uint32_t aspeed_i2c_bus_pool_ctrl_offset(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return R_I2CC_POOL_CTRL;
    }
    return R_I2CD_POOL_CTRL;
}

static inline uint32_t aspeed_i2c_bus_byte_buf_offset(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return R_I2CC_MS_TXRX_BYTE_BUF;
    }
    return R_I2CD_BYTE_BUF;
}

static inline uint32_t aspeed_i2c_bus_dma_len_offset(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return R_I2CC_DMA_LEN;
    }
    return R_I2CD_DMA_LEN;
}

static inline uint32_t aspeed_i2c_bus_dma_addr_offset(AspeedI2CBus *bus)
{
    if (aspeed_i2c_is_new_mode(bus->controller)) {
        return R_I2CC_DMA_ADDR;
    }
    return R_I2CD_DMA_ADDR;
}

static inline bool aspeed_i2c_bus_is_master(AspeedI2CBus *bus)
{
    return SHARED_ARRAY_FIELD_EX32(bus->regs, aspeed_i2c_bus_ctrl_offset(bus),
                                   MASTER_EN);
}

static inline bool aspeed_i2c_bus_is_enabled(AspeedI2CBus *bus)
{
    uint32_t ctrl_reg = aspeed_i2c_bus_ctrl_offset(bus);
    return SHARED_ARRAY_FIELD_EX32(bus->regs, ctrl_reg, MASTER_EN) ||
           SHARED_ARRAY_FIELD_EX32(bus->regs, ctrl_reg, SLAVE_EN);
}

I2CBus *aspeed_i2c_get_bus(AspeedI2CState *s, int busnr);

#endif /* ASPEED_I2C_H */
