/*
 * DesignWare I2C Module.
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/i2c/designware_i2c.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

#ifndef DESIGNWARE_I2C_ERR_DEBUG
#define DESIGNWARE_I2C_ERR_DEBUG 0
#endif

REG32(DW_IC_CON,                0x00) /* I2C control */
    FIELD(DW_IC_CON, STOP_DET_IF_MASTER_ACTIV, 10, 1)
    FIELD(DW_IC_CON, RX_FIFO_FULL_HLD_CTRL,     9, 1)
    FIELD(DW_IC_CON, TX_EMPTY_CTRL,             8, 1)
    FIELD(DW_IC_CON, STOP_IF_ADDRESSED,         7, 1)
    FIELD(DW_IC_CON, SLAVE_DISABLE,             6, 1)
    FIELD(DW_IC_CON, IC_RESTART_EN,             5, 1)
    FIELD(DW_IC_CON, 10BITADDR_MASTER,          4, 1)
    FIELD(DW_IC_CON, 10BITADDR_SLAVE,           3, 1)
    FIELD(DW_IC_CON, SPEED,                     1, 2)
    FIELD(DW_IC_CON, MASTER_MODE,               0, 1)
REG32(DW_IC_TAR,                0x04) /* I2C target address */
    FIELD(DW_IC_TAR, IC_10BITADDR_MASTER, 12,  1)
    FIELD(DW_IC_TAR, SPECIAL,             11,  1)
    FIELD(DW_IC_TAR, GC_OR_START,         10,  1)
    FIELD(DW_IC_TAR, ADDRESS,              0, 10)
REG32(DW_IC_SAR,                0x08) /* I2C slave address */
REG32(DW_IC_DATA_CMD,           0x10)
    FIELD(DW_IC_DATA_CMD, RESTART, 10, 1)
    FIELD(DW_IC_DATA_CMD, STOP,     9, 1)
    FIELD(DW_IC_DATA_CMD, CMD,      8, 1)
    FIELD(DW_IC_DATA_CMD, DAT,      0, 8)
REG32(DW_IC_SS_SCL_HCNT,        0x14) /* Standard speed i2c clock scl high count */
REG32(DW_IC_SS_SCL_LCNT,        0x18) /* Standard speed i2c clock scl low count */
REG32(DW_IC_FS_SCL_HCNT,        0x1c) /* Fast or fast plus i2c clock scl high count */
REG32(DW_IC_FS_SCL_LCNT,        0x20) /* Fast or fast plus i2c clock scl low count */
REG32(DW_IC_INTR_STAT,          0x2c)
REG32(DW_IC_INTR_MASK,          0x30) /* I2C Interrupt Mask */
REG32(DW_IC_RAW_INTR_STAT,      0x34) /* I2C raw interrupt status */
    /* DW_IC_INTR_STAT/INTR_MASK/RAW_INTR_STAT fields */
    SHARED_FIELD(DW_IC_INTR_RESTART_DET, 12, 1)
    SHARED_FIELD(DW_IC_INTR_GEN_CALL,    11, 1)
    SHARED_FIELD(DW_IC_INTR_START_DET,   10, 1)
    SHARED_FIELD(DW_IC_INTR_STOP_DET,    9, 1)
    SHARED_FIELD(DW_IC_INTR_ACTIVITY,    8, 1)
    SHARED_FIELD(DW_IC_INTR_RX_DONE,     7, 1)
    SHARED_FIELD(DW_IC_INTR_TX_ABRT,     6, 1)
    SHARED_FIELD(DW_IC_INTR_RD_REQ,      5, 1)
    SHARED_FIELD(DW_IC_INTR_TX_EMPTY,    4, 1) /* Hardware clear only. */
    SHARED_FIELD(DW_IC_INTR_TX_OVER,     3, 1)
    SHARED_FIELD(DW_IC_INTR_RX_FULL,     2, 1) /* Hardware clear only. */
    SHARED_FIELD(DW_IC_INTR_RX_OVER,     1, 1)
    SHARED_FIELD(DW_IC_INTR_RX_UNDER,    0, 1)

#define DW_IC_INTR_ANY_MASK                \
            (DW_IC_INTR_RESTART_DET_MASK | \
             DW_IC_INTR_GEN_CALL_MASK    | \
             DW_IC_INTR_START_DET_MASK   | \
             DW_IC_INTR_STOP_DET_MASK    | \
             DW_IC_INTR_ACTIVITY_MASK    | \
             DW_IC_INTR_RX_DONE_MASK     | \
             DW_IC_INTR_TX_ABRT_MASK     | \
             DW_IC_INTR_RD_REQ_MASK      | \
             DW_IC_INTR_TX_EMPTY_MASK    | \
             DW_IC_INTR_TX_OVER_MASK     | \
             DW_IC_INTR_RX_FULL_MASK     | \
             DW_IC_INTR_RX_OVER_MASK     | \
             DW_IC_INTR_RX_UNDER_MASK)

#define DW_IC_INTR_ANY_SW_CLEAR_MASK       \
            (DW_IC_INTR_ANY_MASK         & \
            ~(DW_IC_INTR_TX_EMPTY_MASK   | \
              DW_IC_INTR_RX_FULL_MASK))

REG32(DW_IC_RX_TL,              0x38) /* I2C receive FIFO threshold */
REG32(DW_IC_TX_TL,              0x3c) /* I2C transmit FIFO threshold */
REG32(DW_IC_CLR_INTR,           0x40)
REG32(DW_IC_CLR_RX_UNDER,       0x44)
REG32(DW_IC_CLR_RX_OVER,        0x48)
REG32(DW_IC_CLR_TX_OVER,        0x4c)
REG32(DW_IC_CLR_RD_REQ,         0x50)
REG32(DW_IC_CLR_TX_ABRT,        0x54)
REG32(DW_IC_CLR_RX_DONE,        0x58)
REG32(DW_IC_CLR_ACTIVITY,       0x5c)
REG32(DW_IC_CLR_STOP_DET,       0x60)
REG32(DW_IC_CLR_START_DET,      0x64)
REG32(DW_IC_CLR_GEN_CALL,       0x68)
REG32(DW_IC_ENABLE,             0x6c) /* I2C enable */
    FIELD(DW_IC_ENABLE, TX_CMD_BLOCK, 2, 1)
    FIELD(DW_IC_ENABLE, ABORT,        1, 1)
    FIELD(DW_IC_ENABLE, ENABLE,       0, 1)
REG32(DW_IC_STATUS,             0x70) /* I2C status */
    FIELD(DW_IC_STATUS, SLV_ACTIVITY, 6, 1)
    FIELD(DW_IC_STATUS, MST_ACTIVITY, 5, 1)
    FIELD(DW_IC_STATUS, RFF,          4, 1)
    FIELD(DW_IC_STATUS, RFNE,         3, 1)
    FIELD(DW_IC_STATUS, TFE,          2, 1)
    FIELD(DW_IC_STATUS, TFNF,         1, 1)
    FIELD(DW_IC_STATUS, ACTIVITY,     0, 1)
REG32(DW_IC_TXFLR,              0x74) /* I2C transmit fifo level */
REG32(DW_IC_RXFLR,              0x78) /* I2C receive fifo level */
REG32(DW_IC_SDA_HOLD,           0x7c) /* I2C SDA hold time length */
REG32(DW_IC_TX_ABRT_SOURCE,     0x80) /* The I2C transmit abort source */
    FIELD(DW_IC_TX_ABRT_SOURCE, USER_ABRT,       16, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, SLVRD_INTX,      15, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, SLV_ARBLOST,     14, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, SLVFLUSH_TXFIFO, 13, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, ARB_LOST,        12, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, MASTER_DIS,      11, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, 10B_RD_NORSTRT,  10, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, SBYTE_NORSTRT,   9, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, HS_NORSTRT,      8, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, SBYTE_ACKDET,    7, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, HS_ACKDET,       6, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, GCALL_READ,      5, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, GCALL_NOACK,     4, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, TXDATA_NOACK,    3, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, 10ADDR2_NOACK,   2, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, 10ADDR1_NOACK,   1, 1)
    FIELD(DW_IC_TX_ABRT_SOURCE, 7B_ADDR_NOACK,   0, 1)
REG32(DW_IC_SLV_DATA_NACK_ONLY, 0x84)
REG32(DW_IC_DMA_CR,             0x88)
REG32(DW_IC_DMA_TDLR,           0x8c)
REG32(DW_IC_DMA_RDLR,           0x90)
REG32(DW_IC_SDA_SETUP,          0x94) /* I2C SDA setup */
REG32(DW_IC_ACK_GENERAL_CALL,   0x98)
REG32(DW_IC_ENABLE_STATUS,      0x9c) /* I2C enable status */
    FIELD(DW_IC_ENABLE_STATUS, SLV_RX_DATA_LOST,        2, 1)
    FIELD(DW_IC_ENABLE_STATUS, SLV_DISABLED_WHILE_BUSY, 1, 1)
    FIELD(DW_IC_ENABLE_STATUS, IC_EN,                   0, 1)
REG32(DW_IC_FS_SPKLEN,          0xa0) /* I2C SS, FS or FM+ spike suppression limit */
REG32(DW_IC_CLR_RESTART_DET,    0xa8)
REG32(DW_IC_SMBUS_INTR_MASK,    0xcc) /* SMBus Interrupt Mask */
REG32(DW_IC_COMP_PARAM_1,       0xf4) /* Component parameter */
    FIELD(DW_IC_COMP_PARAM_1, TX_FIFO_SIZE,       16, 8)
    FIELD(DW_IC_COMP_PARAM_1, RX_FIFO_SIZE,        8, 8)
    FIELD(DW_IC_COMP_PARAM_1, HAS_ENCODED_PARAMS,  7, 1)
    FIELD(DW_IC_COMP_PARAM_1, HAS_DMA,             6, 1)
    FIELD(DW_IC_COMP_PARAM_1, INTR_IO,             5, 1)
    FIELD(DW_IC_COMP_PARAM_1, HC_COUNT_VAL,        4, 1)
    FIELD(DW_IC_COMP_PARAM_1, HIGH_SPEED_MODE,     2, 2)
    FIELD(DW_IC_COMP_PARAM_1, APB_DATA_WIDTH_32,   0, 2)
REG32(DW_IC_COMP_VERSION,       0xf8) /* I2C component version */
REG32(DW_IC_COMP_TYPE,          0xfc) /* I2C component type */

static void dw_i2c_update_irq(DesignWareI2CState *s)
{
    uint32_t intr = s->regs[R_DW_IC_RAW_INTR_STAT] & s->regs[R_DW_IC_INTR_MASK];

    qemu_set_irq(s->irq, !!(intr & DW_IC_INTR_ANY_MASK));
}

static uint64_t dw_ic_data_cmd_reg_post_read(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    g_assert(value == 0);

    if (s->status != DW_I2C_STATUS_RECEIVING) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Attempted to read from RX fifo when not in receive "
                      "state.\n", DEVICE(s)->canonical_path);
        if (s->status != DW_I2C_STATUS_IDLE) {
            SHARED_ARRAY_FIELD_DP32(s->regs, R_DW_IC_RAW_INTR_STAT,
                                    DW_IC_INTR_RX_UNDER, 1);
            dw_i2c_update_irq(s);
        }
        return 0;
    }

    g_assert(s->regs[R_DW_IC_RXFLR] == fifo8_num_used(&s->rx_fifo));

    if (fifo8_is_empty(&s->rx_fifo)) {
        SHARED_ARRAY_FIELD_DP32(s->regs, R_DW_IC_RAW_INTR_STAT, DW_IC_INTR_RX_UNDER, 1);
        dw_i2c_update_irq(s);
        return 0;
    }

    s->regs[R_DW_IC_RXFLR]--;
    if (s->regs[R_DW_IC_RXFLR] <= s->regs[R_DW_IC_RX_TL]) {
        SHARED_ARRAY_FIELD_DP32(s->regs, R_DW_IC_RAW_INTR_STAT, DW_IC_INTR_RX_FULL, 0);
        dw_i2c_update_irq(s);
    }

    return fifo8_pop(&s->rx_fifo);
}

static uint64_t dw_ic_clr_intr_reg_post_read(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    g_assert(value == 0);

    switch (reg->access->addr) {
    case A_DW_IC_CLR_INTR:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_ANY_SW_CLEAR_MASK;
        break;
    case A_DW_IC_CLR_RX_UNDER:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_RX_UNDER_MASK;
        break;
    case A_DW_IC_CLR_RX_OVER:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_RX_OVER_MASK;
        break;
    case A_DW_IC_CLR_TX_OVER:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_TX_OVER_MASK;
        break;
    case A_DW_IC_CLR_RD_REQ:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_RD_REQ_MASK;
        break;
    case A_DW_IC_CLR_TX_ABRT:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_TX_ABRT_MASK;
        break;
    case A_DW_IC_CLR_RX_DONE:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_RX_DONE_MASK;
        break;
    case A_DW_IC_CLR_ACTIVITY:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_ACTIVITY_MASK;
        break;
    case A_DW_IC_CLR_STOP_DET:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_STOP_DET_MASK;
        break;
    case A_DW_IC_CLR_START_DET:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_START_DET_MASK;
        break;
    case A_DW_IC_CLR_GEN_CALL:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_GEN_CALL_MASK;
        break;
    case A_DW_IC_CLR_RESTART_DET:
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_RESTART_DET_MASK;
        break;
    default:
        g_assert_not_reached();
    }

    dw_i2c_update_irq(s);

    return 0;
}

static uint64_t dw_ic_intr_stat_reg_post_read(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    g_assert(value == 0);

    return s->regs[R_DW_IC_RAW_INTR_STAT] & s->regs[R_DW_IC_INTR_MASK];
}

static uint64_t dw_ic_unsupported_reg_post_read(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    qemu_log_mask(LOG_UNIMP, "%s: unsupported read - %s\n",
                  DEVICE(s)->canonical_path, reg->access->name);

    return 0;
}

static uint64_t dw_ic_unsupported_reg_pre_write(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    qemu_log_mask(LOG_UNIMP, "%s: unsupported write - %s\n",
                  DEVICE(s)->canonical_path, reg->access->name);

    return 0;
}

static uint64_t dw_ic_con_reg_pre_write(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    if (s->regs[R_DW_IC_ENABLE] & R_DW_IC_ENABLE_ENABLE_MASK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid setting to ic_con %d when ic_enable[0]==1\n",
                      DEVICE(s)->canonical_path, (int)value);
        return s->regs[R_DW_IC_CON]; /* keep old value */
    }

    return value;
}

static void dw_i2c_reset_to_idle(DesignWareI2CState *s)
{
    s->regs[R_DW_IC_ENABLE_STATUS] &= ~R_DW_IC_ENABLE_STATUS_IC_EN_MASK;
    s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_TX_EMPTY_MASK;
    s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_RX_FULL_MASK;
    s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_RX_UNDER_MASK;
    s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_RX_OVER_MASK;
    s->regs[R_DW_IC_RXFLR] = 0;
    fifo8_reset(&s->rx_fifo);
    s->regs[R_DW_IC_STATUS] &= ~R_DW_IC_STATUS_ACTIVITY_MASK;
    s->status = DW_I2C_STATUS_IDLE;
    dw_i2c_update_irq(s);
}

static void dw_ic_tx_abort(DesignWareI2CState *s, uint32_t src)
{
    s->regs[R_DW_IC_TX_ABRT_SOURCE] |= src;
    s->regs[R_DW_IC_RAW_INTR_STAT] |= DW_IC_INTR_TX_ABRT_MASK;
    dw_i2c_reset_to_idle(s);
    dw_i2c_update_irq(s);
}

static void dw_ic_data_cmd_reg_post_write(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);
    int recv = !!(value & R_DW_IC_DATA_CMD_CMD_MASK);

    s->regs[R_DW_IC_DATA_CMD] = 0; /* Register has no storage */

    if (!(s->regs[R_DW_IC_ENABLE] & R_DW_IC_ENABLE_ENABLE_MASK)) {
        /*
         * Controller is not enabled. The register_reset() path also lands
         * here with value == 0, so silently ignore rather than reporting
         * a spurious guest error.
         */
        return;
    }

    if (s->status == DW_I2C_STATUS_IDLE ||
        s->regs[R_DW_IC_RAW_INTR_STAT] & DW_IC_INTR_TX_ABRT_MASK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Attempted to write to TX fifo when it is held in "
                      "reset\n", DEVICE(s)->canonical_path);
        return;
    }

    /* Send the address if it hasn't been sent yet. */
    if (s->status == DW_I2C_STATUS_SENDING_ADDRESS) {
        int rv = i2c_start_transfer(s->bus,
                     ARRAY_FIELD_EX32(s->regs, DW_IC_TAR, ADDRESS), recv);
        if (rv) {
            dw_ic_tx_abort(s, R_DW_IC_TX_ABRT_SOURCE_7B_ADDR_NOACK_MASK);
            return;
        }
        s->status = recv ? DW_I2C_STATUS_RECEIVING : DW_I2C_STATUS_SENDING;
    }

    /* Send data */
    if (!recv) {
        int rv = i2c_send(s->bus, FIELD_EX32(value, DW_IC_DATA_CMD, DAT));
        if (rv) {
            i2c_end_transfer(s->bus);
            dw_ic_tx_abort(s, R_DW_IC_TX_ABRT_SOURCE_TXDATA_NOACK_MASK);
            return;
        }
        dw_i2c_update_irq(s);
    }

    /* Restart command */
    if (value & R_DW_IC_DATA_CMD_RESTART_MASK &&
            s->regs[R_DW_IC_CON] & R_DW_IC_CON_IC_RESTART_EN_MASK) {
        s->regs[R_DW_IC_RAW_INTR_STAT] |= DW_IC_INTR_RESTART_DET_MASK |
                                          DW_IC_INTR_START_DET_MASK |
                                          DW_IC_INTR_ACTIVITY_MASK;
        s->regs[R_DW_IC_STATUS] |= R_DW_IC_STATUS_ACTIVITY_MASK;
        dw_i2c_update_irq(s);

        if (i2c_start_transfer(s->bus,
                    ARRAY_FIELD_EX32(s->regs, DW_IC_TAR, ADDRESS), recv)) {
            dw_ic_tx_abort(s, R_DW_IC_TX_ABRT_SOURCE_7B_ADDR_NOACK_MASK);
            return;
        }

        s->status = recv ? DW_I2C_STATUS_RECEIVING : DW_I2C_STATUS_SENDING;
    }

    /* Receive data */
    if (recv) {
        g_assert(s->regs[R_DW_IC_RXFLR] == fifo8_num_used(&s->rx_fifo));

        if (!fifo8_is_full(&s->rx_fifo)) {
            fifo8_push(&s->rx_fifo, i2c_recv(s->bus));
            s->regs[R_DW_IC_RXFLR]++;
        } else {
            s->regs[R_DW_IC_RAW_INTR_STAT] |= DW_IC_INTR_RX_OVER_MASK;
            dw_i2c_update_irq(s);
        }

        if (s->regs[R_DW_IC_RXFLR] > s->regs[R_DW_IC_RX_TL]) {
            s->regs[R_DW_IC_RAW_INTR_STAT] |= DW_IC_INTR_RX_FULL_MASK;
            dw_i2c_update_irq(s);
        }
        if (value & R_DW_IC_DATA_CMD_STOP_MASK) {
            i2c_nack(s->bus);
        }
    }

    /* Stop command */
    if (value & R_DW_IC_DATA_CMD_STOP_MASK) {
        s->regs[R_DW_IC_RAW_INTR_STAT] |= DW_IC_INTR_STOP_DET_MASK;
        s->regs[R_DW_IC_STATUS] &= ~R_DW_IC_STATUS_ACTIVITY_MASK;
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_TX_EMPTY_MASK;
        i2c_end_transfer(s->bus);
        dw_i2c_update_irq(s);
    }
}

static void dw_ic_intr_mask_reg_post_write(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    dw_i2c_update_irq(s);
}

static uint64_t dw_ic_enable_reg_pre_write(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    if (value & R_DW_IC_ENABLE_ENABLE_MASK &&
            !(s->regs[R_DW_IC_CON] & R_DW_IC_CON_SLAVE_DISABLE_MASK)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: Designware I2C slave mode is not supported.\n",
                      DEVICE(s)->canonical_path);
        return s->regs[R_DW_IC_ENABLE]; /* keep old value */
    }

    return value;
}

static void dw_ic_enable_reg_post_write(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    s->regs[R_DW_IC_ENABLE] = value & R_DW_IC_ENABLE_ENABLE_MASK;

    if (value & R_DW_IC_ENABLE_ABORT_MASK || value & R_DW_IC_ENABLE_TX_CMD_BLOCK_MASK) {
        dw_ic_tx_abort(s, R_DW_IC_TX_ABRT_SOURCE_USER_ABRT_MASK);
        return;
    }

    if (value & R_DW_IC_ENABLE_ENABLE_MASK) {
        s->regs[R_DW_IC_ENABLE_STATUS] |= R_DW_IC_ENABLE_STATUS_IC_EN_MASK;
        s->regs[R_DW_IC_STATUS] |= R_DW_IC_STATUS_ACTIVITY_MASK;
        s->regs[R_DW_IC_RAW_INTR_STAT] |= DW_IC_INTR_ACTIVITY_MASK |
                                          DW_IC_INTR_START_DET_MASK |
                                          DW_IC_INTR_TX_EMPTY_MASK;
        s->status = DW_I2C_STATUS_SENDING_ADDRESS;
        dw_i2c_update_irq(s);
    } else if ((value & R_DW_IC_ENABLE_ENABLE_MASK) == 0) {
        dw_i2c_reset_to_idle(s);
    }
}

static uint64_t dw_ic_rx_tl_reg_pre_write(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    /* Note that a value of 0 for ic_rx_tl indicates a threshold of 1. */
    if (value > DESIGNWARE_I2C_RX_FIFO_SIZE - 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid setting to ic_rx_tl %d\n",
                      DEVICE(s)->canonical_path, (int)value);
        return DESIGNWARE_I2C_RX_FIFO_SIZE - 1;
    }

    return value;
}

static void dw_ic_rx_tl_reg_post_write(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    if (s->regs[R_DW_IC_RXFLR] > s->regs[R_DW_IC_RX_TL] &&
            s->regs[R_DW_IC_ENABLE] & R_DW_IC_ENABLE_ENABLE_MASK) {
        s->regs[R_DW_IC_RAW_INTR_STAT] |= DW_IC_INTR_RX_FULL_MASK;
    } else {
        s->regs[R_DW_IC_RAW_INTR_STAT] &= ~DW_IC_INTR_RX_FULL_MASK;
    }
    dw_i2c_update_irq(s);
}

static uint64_t dw_ic_tx_tl_reg_pre_write(RegisterInfo *reg, uint64_t value)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(reg->opaque);

    /*
     * Note that a value of 0 for ic_tx_tl indicates a threshold of 1.
     * However, the tx threshold is not used in the model because commands are
     * always sent out as soon as they are written.
     */
    if (value > DESIGNWARE_I2C_TX_FIFO_SIZE - 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid setting to ic_tx_tl %d\n",
                      DEVICE(s)->canonical_path, (int)value);
        return DESIGNWARE_I2C_TX_FIFO_SIZE - 1;
    }

    return value;
}

static const RegisterAccessInfo designware_i2c_regs_info[] = {
    {   .name  = "DW_IC_CON", .addr = A_DW_IC_CON,
        .reset =       0x7d,
        .unimp = 0xfffffc00,
        .pre_write = dw_ic_con_reg_pre_write,
    },{ .name  = "DW_IC_TAR", .addr = A_DW_IC_TAR,
        .reset =     0x1055,
        .unimp = 0xfffff000,
    },{ .name  = "DW_IC_SAR", .addr = A_DW_IC_SAR,
        .reset =       0x55,
        .unimp = 0xfffffc00,
        .post_read = dw_ic_unsupported_reg_post_read,
        .pre_write = dw_ic_unsupported_reg_pre_write,
    },{ .name  = "DW_IC_DATA_CMD", .addr = A_DW_IC_DATA_CMD,
        .post_read = dw_ic_data_cmd_reg_post_read,
        .post_write = dw_ic_data_cmd_reg_post_write,
    },{ .name  = "DW_IC_SS_SCL_HCNT", .addr = A_DW_IC_SS_SCL_HCNT,
        .reset =      0x190,
        .unimp = 0xffff0000,
    },{ .name  = "DW_IC_SS_SCL_LCNT", .addr = A_DW_IC_SS_SCL_LCNT,
        .reset =      0x1d6,
        .unimp = 0xffff0000,
    },{ .name  = "DW_IC_FS_SCL_HCNT", .addr = A_DW_IC_FS_SCL_HCNT,
        .reset =       0x3c,
        .unimp = 0xffff0000,
    },{ .name  = "DW_IC_FS_SCL_LCNT", .addr = A_DW_IC_FS_SCL_LCNT,
        .reset =       0x82,
        .unimp = 0xffff0000,
    },{ .name  = "DW_IC_INTR_STAT", .addr = A_DW_IC_INTR_STAT,
        .ro    = 0xffffffff,
        .post_read = dw_ic_intr_stat_reg_post_read,
    },{ .name  = "DW_IC_INTR_MASK", .addr = A_DW_IC_INTR_MASK,
        .reset =      0x8ff,
        .unimp = 0xffff8000,
        .post_write = dw_ic_intr_mask_reg_post_write,
    },{ .name  = "DW_IC_RAW_INTR_STAT", .addr = A_DW_IC_RAW_INTR_STAT,
        .ro    = 0xffffffff,
    },{ .name  = "DW_IC_RX_TL", .addr = A_DW_IC_RX_TL,
        .pre_write = dw_ic_rx_tl_reg_pre_write,
        .post_write = dw_ic_rx_tl_reg_post_write,
    },{ .name  = "DW_IC_TX_TL", .addr = A_DW_IC_TX_TL,
        .pre_write = dw_ic_tx_tl_reg_pre_write,
    },{ .name  = "DW_IC_CLR_INTR", .addr = A_DW_IC_CLR_INTR,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_RX_UNDER", .addr = A_DW_IC_CLR_RX_UNDER,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_RX_OVER", .addr = A_DW_IC_CLR_RX_OVER,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_TX_OVER", .addr = A_DW_IC_CLR_TX_OVER,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_RD_REQ", .addr = A_DW_IC_CLR_RD_REQ,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_TX_ABRT", .addr = A_DW_IC_CLR_TX_ABRT,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_RX_DONE", .addr = A_DW_IC_CLR_RX_DONE,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_ACTIVITY", .addr = A_DW_IC_CLR_ACTIVITY,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_STOP_DET", .addr = A_DW_IC_CLR_STOP_DET,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_START_DET", .addr = A_DW_IC_CLR_START_DET,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_CLR_GEN_CALL", .addr = A_DW_IC_CLR_GEN_CALL,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_ENABLE", .addr = A_DW_IC_ENABLE,
        .unimp = 0xfffffff8,
        .pre_write = dw_ic_enable_reg_pre_write,
        .post_write = dw_ic_enable_reg_post_write,
    },{ .name  = "DW_IC_STATUS", .addr = A_DW_IC_STATUS,
        .reset =        0x6,
        .ro    = 0xffffffff,
    },{ .name  = "DW_IC_TXFLR", .addr = A_DW_IC_TXFLR,
        .ro    = 0xffffffff,
    },{ .name  = "DW_IC_RXFLR", .addr = A_DW_IC_RXFLR,
        .ro    = 0xffffffff,
    },{ .name  = "DW_IC_SDA_HOLD", .addr = A_DW_IC_SDA_HOLD,
        .reset =        0x1,
        .unimp = 0xff000000,
    },{ .name  = "DW_IC_TX_ABRT_SOURCE", .addr = A_DW_IC_TX_ABRT_SOURCE,
        .ro    = 0xffffffff,
    },{ .name  = "DW_IC_SLV_DATA_NACK_ONLY", .addr = A_DW_IC_SLV_DATA_NACK_ONLY,
        .post_read = dw_ic_unsupported_reg_post_read,
        .pre_write = dw_ic_unsupported_reg_pre_write,
    },{ .name  = "DW_IC_DMA_CR", .addr = A_DW_IC_DMA_CR,
        .post_read = dw_ic_unsupported_reg_post_read,
        .pre_write = dw_ic_unsupported_reg_pre_write,
    },{ .name  = "DW_IC_DMA_TDLR", .addr = A_DW_IC_DMA_TDLR,
        .post_read = dw_ic_unsupported_reg_post_read,
        .pre_write = dw_ic_unsupported_reg_pre_write,
    },{ .name  = "DW_IC_DMA_RDLR", .addr = A_DW_IC_DMA_RDLR,
        .post_read = dw_ic_unsupported_reg_post_read,
        .pre_write = dw_ic_unsupported_reg_pre_write,
    },{ .name  = "DW_IC_SDA_SETUP", .addr = A_DW_IC_SDA_SETUP,
        .reset =       0x64,
        .unimp = 0xffffff00,
    },{ .name  = "DW_IC_ACK_GENERAL_CALL", .addr = A_DW_IC_ACK_GENERAL_CALL,
        .post_read = dw_ic_unsupported_reg_post_read,
        .pre_write = dw_ic_unsupported_reg_pre_write,
    },{ .name  = "DW_IC_ENABLE_STATUS", .addr = A_DW_IC_ENABLE_STATUS,
        .ro    = 0xffffffff,
    },{ .name  = "DW_IC_FS_SPKLEN", .addr = A_DW_IC_FS_SPKLEN,
        .reset =        0x2,
        .ro    = 0xffffff00,
    },{ .name  = "DW_IC_CLR_RESTART_DET", .addr = A_DW_IC_CLR_RESTART_DET,
        .ro    = 0xffffffff,
        .post_read = dw_ic_clr_intr_reg_post_read,
    },{ .name  = "DW_IC_SMBUS_INTR_MASK", .addr = A_DW_IC_SMBUS_INTR_MASK,
        /* No SMBus interrupts are implemented, Linux updates the mask */
        .reset =      0x7ff,
        .unimp = 0xfffff800,
    },{ .name  = "DW_IC_COMP_PARAM_1", .addr = A_DW_IC_COMP_PARAM_1,
        .reset = /* HAS_DMA and HC_COUNT_VAL are disabled */
            ((2 << R_DW_IC_COMP_PARAM_1_APB_DATA_WIDTH_32_SHIFT) |
             R_DW_IC_COMP_PARAM_1_HIGH_SPEED_MODE_MASK           |
             R_DW_IC_COMP_PARAM_1_INTR_IO_MASK                   |
             R_DW_IC_COMP_PARAM_1_HAS_ENCODED_PARAMS_MASK        |
             ((DESIGNWARE_I2C_RX_FIFO_SIZE - 1)
                  << R_DW_IC_COMP_PARAM_1_RX_FIFO_SIZE_SHIFT)    |
             ((DESIGNWARE_I2C_TX_FIFO_SIZE - 1)
                  << R_DW_IC_COMP_PARAM_1_TX_FIFO_SIZE_SHIFT)),
        .ro    = 0xffffffff,
    },{ .name  = "DW_IC_COMP_VERSION", .addr = A_DW_IC_COMP_VERSION,
        .reset = 0x3132302a,
        .ro    = 0xffffffff,
    },{ .name  = "DW_IC_COMP_TYPE", .addr = A_DW_IC_COMP_TYPE,
        .reset = 0x44570140,
        .ro    = 0xffffffff,
    }
};

static const MemoryRegionOps designware_i2c_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void designware_i2c_enter_reset(Object *obj, ResetType type)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs); ++i) {
        register_reset(&s->regs_info[i]);
    }

    fifo8_reset(&s->rx_fifo);

    s->status = DW_I2C_STATUS_IDLE;
}

static void designware_i2c_hold_reset(Object *obj, ResetType type)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(obj);

    qemu_irq_lower(s->irq);
}

static const VMStateDescription vmstate_designware_i2c = {
    .name = TYPE_DESIGNWARE_I2C,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, DesignWareI2CState, DESIGNWARE_I2C_R_MAX),
        VMSTATE_FIFO8(rx_fifo, DesignWareI2CState),
        VMSTATE_UINT32(status, DesignWareI2CState),
        VMSTATE_END_OF_LIST(),
    },
};

static void designware_i2c_instance_init(Object *obj)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    fifo8_create(&s->rx_fifo, DESIGNWARE_I2C_RX_FIFO_SIZE);

    s->bus = i2c_init_bus(DEVICE(s), "i2c-bus");

    memory_region_init(&s->iomem, obj, TYPE_DESIGNWARE_I2C, 4 * KiB);
    reg_array = register_init_block32(DEVICE(obj), designware_i2c_regs_info,
                                      ARRAY_SIZE(designware_i2c_regs_info),
                                      s->regs_info, s->regs,
                                      &designware_i2c_ops,
                                      DESIGNWARE_I2C_ERR_DEBUG,
                                      DESIGNWARE_I2C_R_MAX * 4);
    memory_region_add_subregion(&s->iomem, 0, &reg_array->mem);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void designware_i2c_finalize(Object *obj)
{
    DesignWareI2CState *s = DESIGNWARE_I2C(obj);

    fifo8_destroy(&s->rx_fifo);
}

static void designware_i2c_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Designware I2C";
    dc->vmsd = &vmstate_designware_i2c;
    rc->phases.enter = designware_i2c_enter_reset;
    rc->phases.hold = designware_i2c_hold_reset;

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo designware_i2c_types[] = {
    {
        .name = TYPE_DESIGNWARE_I2C,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(DesignWareI2CState),
        .class_init = designware_i2c_class_init,
        .instance_init = designware_i2c_instance_init,
        .instance_finalize = designware_i2c_finalize,
    },
};
DEFINE_TYPES(designware_i2c_types);
