/*
 * ARM Aspeed I2C controller
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/i2c/aspeed_i2c.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "trace.h"

/* I2C Global Register */
REG32(I2C_CTRL_STATUS, 0x0) /* Device Interrupt Status */
REG32(I2C_CTRL_ASSIGN, 0x8) /* Device Interrupt Target Assignment */
REG32(I2C_CTRL_GLOBAL, 0xC) /* Global Control Register */
    FIELD(I2C_CTRL_GLOBAL, SRAM_EN, 0, 1)

/* I2C Device (Bus) Register */
REG32(I2CD_FUN_CTRL, 0x0) /* I2CD Function Control  */
    FIELD(I2CD_FUN_CTRL, POOL_PAGE_SEL, 20, 3) /* AST2400 */
    FIELD(I2CD_FUN_CTRL, M_SDA_LOCK_EN, 16, 1)
    FIELD(I2CD_FUN_CTRL, MULTI_MASTER_DIS, 15, 1)
    FIELD(I2CD_FUN_CTRL, M_SCL_DRIVE_EN, 14, 1)
    FIELD(I2CD_FUN_CTRL, MSB_STS, 9, 1)
    FIELD(I2CD_FUN_CTRL, SDA_DRIVE_IT_EN, 8, 1)
    FIELD(I2CD_FUN_CTRL, M_SDA_DRIVE_IT_EN, 7, 1)
    FIELD(I2CD_FUN_CTRL, M_HIGH_SPEED_EN, 6, 1)
    FIELD(I2CD_FUN_CTRL, DEF_ADDR_EN, 5, 1)
    FIELD(I2CD_FUN_CTRL, DEF_ALERT_EN, 4, 1)
    FIELD(I2CD_FUN_CTRL, DEF_ARP_EN, 3, 1)
    FIELD(I2CD_FUN_CTRL, DEF_GCALL_EN, 2, 1)
    FIELD(I2CD_FUN_CTRL, SLAVE_EN, 1, 1)
    FIELD(I2CD_FUN_CTRL, MASTER_EN, 0, 1)
REG32(I2CD_AC_TIMING1, 0x04) /* Clock and AC Timing Control #1 */
REG32(I2CD_AC_TIMING2, 0x08) /* Clock and AC Timing Control #2 */
REG32(I2CD_INTR_CTRL, 0x0C)  /* I2CD Interrupt Control */
REG32(I2CD_INTR_STS, 0x10)   /* I2CD Interrupt Status */
    FIELD(I2CD_INTR_STS, SLAVE_ADDR_MATCH, 31, 1)    /* 0: addr1 1: addr2 */
    FIELD(I2CD_INTR_STS, SLAVE_ADDR_RX_PENDING, 29, 1)
    FIELD(I2CD_INTR_STS, SLAVE_INACTIVE_TIMEOUT, 15, 1)
    FIELD(I2CD_INTR_STS, SDA_DL_TIMEOUT, 14, 1)
    FIELD(I2CD_INTR_STS, BUS_RECOVER_DONE, 13, 1)
    FIELD(I2CD_INTR_STS, SMBUS_ALERT, 12, 1)            /* Bus [0-3] only */
    FIELD(I2CD_INTR_STS, SMBUS_ARP_ADDR, 11, 1)         /* Removed */
    FIELD(I2CD_INTR_STS, SMBUS_DEV_ALERT_ADDR, 10, 1)   /* Removed */
    FIELD(I2CD_INTR_STS, SMBUS_DEF_ADDR, 9, 1)          /* Removed */
    FIELD(I2CD_INTR_STS, GCALL_ADDR, 8, 1)              /* Removed */
    FIELD(I2CD_INTR_STS, SLAVE_ADDR_RX_MATCH, 7, 1)     /* use RX_DONE */
    FIELD(I2CD_INTR_STS, SCL_TIMEOUT, 6, 1)
    FIELD(I2CD_INTR_STS, ABNORMAL, 5, 1)
    FIELD(I2CD_INTR_STS, NORMAL_STOP, 4, 1)
    FIELD(I2CD_INTR_STS, ARBIT_LOSS, 3, 1)
    FIELD(I2CD_INTR_STS, RX_DONE, 2, 1)
    FIELD(I2CD_INTR_STS, TX_NAK, 1, 1)
    FIELD(I2CD_INTR_STS, TX_ACK, 0, 1)
REG32(I2CD_CMD, 0x14) /* I2CD Command/Status */
    FIELD(I2CD_CMD, SDA_OE, 28, 1)
    FIELD(I2CD_CMD, SDA_O, 27, 1)
    FIELD(I2CD_CMD, SCL_OE, 26, 1)
    FIELD(I2CD_CMD, SCL_O, 25, 1)
    FIELD(I2CD_CMD, TX_TIMING, 23, 2)
    FIELD(I2CD_CMD, TX_STATE, 19, 4)
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
    FIELD(I2CD_CMD, SCL_LINE_STS, 18, 1)
    FIELD(I2CD_CMD, SDA_LINE_STS, 17, 1)
    FIELD(I2CD_CMD, BUS_BUSY_STS, 16, 1)
    FIELD(I2CD_CMD, SDA_OE_OUT_DIR, 15, 1)
    FIELD(I2CD_CMD, SDA_O_OUT_DIR, 14, 1)
    FIELD(I2CD_CMD, SCL_OE_OUT_DIR, 13, 1)
    FIELD(I2CD_CMD, SCL_O_OUT_DIR, 12, 1)
    FIELD(I2CD_CMD, BUS_RECOVER_CMD_EN, 11, 1)
    FIELD(I2CD_CMD, S_ALT_EN, 10, 1)
    /* Command Bits */
    FIELD(I2CD_CMD, RX_DMA_EN, 9, 1)
    FIELD(I2CD_CMD, TX_DMA_EN, 8, 1)
    FIELD(I2CD_CMD, RX_BUFF_EN, 7, 1)
    FIELD(I2CD_CMD, TX_BUFF_EN, 6, 1)
    FIELD(I2CD_CMD, M_STOP_CMD, 5, 1)
    FIELD(I2CD_CMD, M_S_RX_CMD_LAST, 4, 1)
    FIELD(I2CD_CMD, M_RX_CMD, 3, 1)
    FIELD(I2CD_CMD, S_TX_CMD, 2, 1)
    FIELD(I2CD_CMD, M_TX_CMD, 1, 1)
    FIELD(I2CD_CMD, M_START_CMD, 0, 1)
REG32(I2CD_DEV_ADDR, 0x18) /* Slave Device Address */
REG32(I2CD_POOL_CTRL, 0x1C) /* Pool Buffer Control */
    FIELD(I2CD_POOL_CTRL, RX_COUNT, 24, 5)
    FIELD(I2CD_POOL_CTRL, RX_SIZE, 16, 5)
    FIELD(I2CD_POOL_CTRL, TX_COUNT, 9, 5)
    FIELD(I2CD_POOL_CTRL, OFFSET, 2, 6) /* AST2400 */
REG32(I2CD_BYTE_BUF, 0x20) /* Transmit/Receive Byte Buffer */
    FIELD(I2CD_BYTE_BUF, RX_BUF, 8, 8)
    FIELD(I2CD_BYTE_BUF, TX_BUF, 0, 8)
REG32(I2CD_DMA_ADDR, 0x24) /* DMA Buffer Address */
REG32(I2CD_DMA_LEN, 0x28) /* DMA Transfer Length < 4KB */

static inline bool aspeed_i2c_bus_is_master(AspeedI2CBus *bus)
{
    return FIELD_EX32(bus->ctrl, I2CD_FUN_CTRL, MASTER_EN);
}

static inline bool aspeed_i2c_bus_is_enabled(AspeedI2CBus *bus)
{
    return FIELD_EX32(bus->ctrl, I2CD_FUN_CTRL, MASTER_EN) ||
           FIELD_EX32(bus->ctrl, I2CD_FUN_CTRL, SLAVE_EN);
}

static inline void aspeed_i2c_bus_raise_interrupt(AspeedI2CBus *bus)
{
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(bus->controller);

    trace_aspeed_i2c_bus_raise_interrupt(bus->intr_status,
        FIELD_EX32(bus->intr_status, I2CD_INTR_STS, TX_NAK) ? "nak|" : "",
        FIELD_EX32(bus->intr_status, I2CD_INTR_STS, TX_ACK) ? "ack|" : "",
        FIELD_EX32(bus->intr_status, I2CD_INTR_STS, RX_DONE) ? "done|" : "",
        FIELD_EX32(bus->intr_status, I2CD_INTR_STS, NORMAL_STOP) ? "normal|"
                                                                 : "",
        FIELD_EX32(bus->intr_status, I2CD_INTR_STS, ABNORMAL) ? "abnormal"
                                                              : "");

    bus->intr_status &= bus->intr_ctrl;
    if (bus->intr_status) {
        bus->controller->intr_status |= 1 << bus->id;
        qemu_irq_raise(aic->bus_get_irq(bus));
    }
}

static uint64_t aspeed_i2c_bus_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    AspeedI2CBus *bus = opaque;
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(bus->controller);
    uint64_t value = -1;

    switch (offset) {
    case A_I2CD_FUN_CTRL:
        value = bus->ctrl;
        break;
    case A_I2CD_AC_TIMING1:
        value = bus->timing[0];
        break;
    case A_I2CD_AC_TIMING2:
        value = bus->timing[1];
        break;
    case A_I2CD_INTR_CTRL:
        value = bus->intr_ctrl;
        break;
    case A_I2CD_INTR_STS:
        value = bus->intr_status;
        break;
    case A_I2CD_POOL_CTRL:
        value = bus->pool_ctrl;
        break;
    case A_I2CD_BYTE_BUF:
        value = bus->buf;
        break;
    case A_I2CD_CMD:
        value = bus->cmd | (i2c_bus_busy(bus->bus) << 16);
        break;
    case A_I2CD_DMA_ADDR:
        if (!aic->has_dma) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: No DMA support\n",  __func__);
            break;
        }
        value = bus->dma_addr;
        break;
    case A_I2CD_DMA_LEN:
        if (!aic->has_dma) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: No DMA support\n",  __func__);
            break;
        }
        value = bus->dma_len;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, offset);
        value = -1;
        break;
    }

    trace_aspeed_i2c_bus_read(bus->id, offset, size, value);
    return value;
}

static void aspeed_i2c_set_state(AspeedI2CBus *bus, uint8_t state)
{
    bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, TX_STATE, state);
}

static uint8_t aspeed_i2c_get_state(AspeedI2CBus *bus)
{
    return FIELD_EX32(bus->cmd, I2CD_CMD, TX_STATE);
}

static int aspeed_i2c_dma_read(AspeedI2CBus *bus, uint8_t *data)
{
    MemTxResult result;
    AspeedI2CState *s = bus->controller;

    result = address_space_read(&s->dram_as, bus->dma_addr,
                                MEMTXATTRS_UNSPECIFIED, data, 1);
    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DRAM read failed @%08x\n",
                      __func__, bus->dma_addr);
        return -1;
    }

    bus->dma_addr++;
    bus->dma_len--;
    return 0;
}

static int aspeed_i2c_bus_send(AspeedI2CBus *bus, uint8_t pool_start)
{
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(bus->controller);
    int ret = -1;
    int i;
    int pool_tx_count = FIELD_EX32(bus->pool_ctrl, I2CD_POOL_CTRL, TX_COUNT);

    if (FIELD_EX32(bus->cmd, I2CD_CMD, TX_BUFF_EN)) {
        for (i = pool_start; i < pool_tx_count; i++) {
            uint8_t *pool_base = aic->bus_pool_base(bus);

            trace_aspeed_i2c_bus_send("BUF", i + 1, pool_tx_count,
                                      pool_base[i]);
            ret = i2c_send(bus->bus, pool_base[i]);
            if (ret) {
                break;
            }
        }
        bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, TX_BUFF_EN, 0);
    } else if (FIELD_EX32(bus->cmd, I2CD_CMD, TX_DMA_EN)) {
        while (bus->dma_len) {
            uint8_t data;
            aspeed_i2c_dma_read(bus, &data);
            trace_aspeed_i2c_bus_send("DMA", bus->dma_len, bus->dma_len, data);
            ret = i2c_send(bus->bus, data);
            if (ret) {
                break;
            }
        }
        bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, TX_DMA_EN, 0);
    } else {
        trace_aspeed_i2c_bus_send("BYTE", pool_start, 1, bus->buf);
        ret = i2c_send(bus->bus, bus->buf);
    }

    return ret;
}

static void aspeed_i2c_bus_recv(AspeedI2CBus *bus)
{
    AspeedI2CState *s = bus->controller;
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(s);
    uint8_t data;
    int i;
    int pool_rx_count = FIELD_EX32(bus->pool_ctrl, I2CD_POOL_CTRL, RX_COUNT);

    if (FIELD_EX32(bus->cmd, I2CD_CMD, RX_BUFF_EN)) {
        uint8_t *pool_base = aic->bus_pool_base(bus);

        for (i = 0; i < pool_rx_count; i++) {
            pool_base[i] = i2c_recv(bus->bus);
            trace_aspeed_i2c_bus_recv("BUF", i + 1, pool_rx_count,
                                      pool_base[i]);
        }

        /* Update RX count */
        bus->pool_ctrl = FIELD_DP32(bus->pool_ctrl, I2CD_POOL_CTRL, RX_COUNT,
                                    i & 0xff);
        bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, RX_BUFF_EN, 0);
    } else if (FIELD_EX32(bus->cmd, I2CD_CMD, RX_DMA_EN)) {
        uint8_t data;

        while (bus->dma_len) {
            MemTxResult result;

            data = i2c_recv(bus->bus);
            trace_aspeed_i2c_bus_recv("DMA", bus->dma_len, bus->dma_len, data);
            result = address_space_write(&s->dram_as, bus->dma_addr,
                                         MEMTXATTRS_UNSPECIFIED, &data, 1);
            if (result != MEMTX_OK) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: DRAM write failed @%08x\n",
                              __func__, bus->dma_addr);
                return;
            }
            bus->dma_addr++;
            bus->dma_len--;
        }
        bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, RX_DMA_EN, 0);
    } else {
        data = i2c_recv(bus->bus);
        trace_aspeed_i2c_bus_recv("BYTE", 1, 1, bus->buf);
        bus->buf = FIELD_DP32(bus->buf, I2CD_BYTE_BUF, RX_BUF, data);
    }
}

static void aspeed_i2c_handle_rx_cmd(AspeedI2CBus *bus)
{
    aspeed_i2c_set_state(bus, I2CD_MRXD);
    aspeed_i2c_bus_recv(bus);
    bus->intr_status = FIELD_DP32(bus->intr_status, I2CD_INTR_STS, RX_DONE, 1);
    if (FIELD_EX32(bus->cmd, I2CD_CMD, M_S_RX_CMD_LAST)) {
        i2c_nack(bus->bus);
    }
    bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, M_RX_CMD, 0);
    bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, M_S_RX_CMD_LAST, 0);
    aspeed_i2c_set_state(bus, I2CD_MACTIVE);
}

static uint8_t aspeed_i2c_get_addr(AspeedI2CBus *bus)
{
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(bus->controller);

    if (FIELD_EX32(bus->cmd, I2CD_CMD, TX_BUFF_EN)) {
        uint8_t *pool_base = aic->bus_pool_base(bus);

        return pool_base[0];
    } else if (FIELD_EX32(bus->cmd, I2CD_CMD, TX_DMA_EN)) {
        uint8_t data;

        aspeed_i2c_dma_read(bus, &data);
        return data;
    } else {
        return bus->buf;
    }
}

static bool aspeed_i2c_check_sram(AspeedI2CBus *bus)
{
    AspeedI2CState *s = bus->controller;
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(s);
    bool dma_en = FIELD_EX32(bus->cmd, I2CD_CMD, RX_DMA_EN)  ||
                  FIELD_EX32(bus->cmd, I2CD_CMD, TX_DMA_EN)  ||
                  FIELD_EX32(bus->cmd, I2CD_CMD, RX_BUFF_EN) ||
                  FIELD_EX32(bus->cmd, I2CD_CMD, TX_BUFF_EN);
    if (!aic->check_sram) {
        return true;
    }

    /*
     * AST2500: SRAM must be enabled before using the Buffer Pool or
     * DMA mode.
     */
    if (!FIELD_EX32(s->ctrl_global, I2C_CTRL_GLOBAL, SRAM_EN) && dma_en) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: SRAM is not enabled\n", __func__);
        return false;
    }

    return true;
}

static void aspeed_i2c_bus_cmd_dump(AspeedI2CBus *bus)
{
    g_autofree char *cmd_flags = NULL;
    uint32_t count;
    if (FIELD_EX32(bus->cmd, I2CD_CMD, RX_BUFF_EN)) {
        count = FIELD_EX32(bus->pool_ctrl, I2CD_POOL_CTRL, TX_COUNT);
    } else if (FIELD_EX32(bus->cmd, I2CD_CMD, RX_DMA_EN)) {
        count = bus->dma_len;
    } else { /* BYTE mode */
        count = 1;
    }

    cmd_flags = g_strdup_printf("%s%s%s%s%s%s%s%s%s",
             FIELD_EX32(bus->cmd, I2CD_CMD, M_START_CMD) ? "start|" : "",
             FIELD_EX32(bus->cmd, I2CD_CMD, RX_DMA_EN) ? "rxdma|" : "",
             FIELD_EX32(bus->cmd, I2CD_CMD, TX_DMA_EN) ? "txdma|" : "",
             FIELD_EX32(bus->cmd, I2CD_CMD, RX_BUFF_EN) ? "rxbuf|" : "",
             FIELD_EX32(bus->cmd, I2CD_CMD, TX_BUFF_EN) ? "txbuf|" : "",
             FIELD_EX32(bus->cmd, I2CD_CMD, M_TX_CMD) ? "tx|" : "",
             FIELD_EX32(bus->cmd, I2CD_CMD, M_RX_CMD) ? "rx|" : "",
             FIELD_EX32(bus->cmd, I2CD_CMD, M_S_RX_CMD_LAST) ? "last|" : "",
             FIELD_EX32(bus->cmd, I2CD_CMD, M_STOP_CMD) ? "stop" : "");

    trace_aspeed_i2c_bus_cmd(bus->cmd, cmd_flags, count, bus->intr_status);
}

/*
 * The state machine needs some refinement. It is only used to track
 * invalid STOP commands for the moment.
 */
static void aspeed_i2c_bus_handle_cmd(AspeedI2CBus *bus, uint64_t value)
{
    uint8_t pool_start = 0;

    bus->cmd &= ~0xFFFF;
    bus->cmd |= value & 0xFFFF;

    if (!aspeed_i2c_check_sram(bus)) {
        return;
    }

    if (trace_event_get_state_backends(TRACE_ASPEED_I2C_BUS_CMD)) {
        aspeed_i2c_bus_cmd_dump(bus);
    }

    if (FIELD_EX32(bus->cmd, I2CD_CMD, M_START_CMD)) {
        uint8_t state = aspeed_i2c_get_state(bus) & I2CD_MACTIVE ?
            I2CD_MSTARTR : I2CD_MSTART;
        uint8_t addr;

        aspeed_i2c_set_state(bus, state);

        addr = aspeed_i2c_get_addr(bus);

        if (i2c_start_transfer(bus->bus, extract32(addr, 1, 7),
                               extract32(addr, 0, 1))) {
            bus->intr_status = FIELD_DP32(bus->intr_status, I2CD_INTR_STS,
                                          TX_NAK, 1);
        } else {
            bus->intr_status = FIELD_DP32(bus->intr_status, I2CD_INTR_STS,
                                          TX_ACK, 1);
        }

        bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, M_START_CMD, 0);

        /*
         * The START command is also a TX command, as the slave
         * address is sent on the bus. Drop the TX flag if nothing
         * else needs to be sent in this sequence.
         */
        if (FIELD_EX32(bus->cmd, I2CD_CMD, TX_BUFF_EN)) {
            if (FIELD_EX32(bus->pool_ctrl, I2CD_POOL_CTRL, TX_COUNT) == 1) {
                bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, M_TX_CMD, 0);
            } else {
                /*
                 * Increase the start index in the TX pool buffer to
                 * skip the address byte.
                 */
                pool_start++;
            }
        } else if (FIELD_EX32(bus->cmd, I2CD_CMD, TX_DMA_EN)) {
            if (bus->dma_len == 0) {
                bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, M_TX_CMD, 0);
            }
        } else {
            bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, M_TX_CMD, 0);
        }

        /* No slave found */
        if (!i2c_bus_busy(bus->bus)) {
            return;
        }
        aspeed_i2c_set_state(bus, I2CD_MACTIVE);
    }

    if (FIELD_EX32(bus->cmd, I2CD_CMD, M_TX_CMD)) {
        aspeed_i2c_set_state(bus, I2CD_MTXD);
        if (aspeed_i2c_bus_send(bus, pool_start)) {
            bus->intr_status = FIELD_DP32(bus->intr_status, I2CD_INTR_STS,
                                          TX_NAK, 1);
            i2c_end_transfer(bus->bus);
        } else {
            bus->intr_status = FIELD_DP32(bus->intr_status, I2CD_INTR_STS,
                                          TX_ACK, 1);
        }
        bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, M_TX_CMD, 0);
        aspeed_i2c_set_state(bus, I2CD_MACTIVE);
    }

    if ((FIELD_EX32(bus->cmd, I2CD_CMD, M_RX_CMD) ||
         FIELD_EX32(bus->cmd, I2CD_CMD, M_S_RX_CMD_LAST)) &&
        !FIELD_EX32(bus->intr_status, I2CD_INTR_STS, RX_DONE)) {
        aspeed_i2c_handle_rx_cmd(bus);
    }

    if (FIELD_EX32(bus->cmd, I2CD_CMD, M_STOP_CMD)) {
        if (!(aspeed_i2c_get_state(bus) & I2CD_MACTIVE)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: abnormal stop\n", __func__);
            bus->intr_status = FIELD_DP32(bus->intr_status, I2CD_INTR_STS,
                                          ABNORMAL, 1);
        } else {
            aspeed_i2c_set_state(bus, I2CD_MSTOP);
            i2c_end_transfer(bus->bus);
            bus->intr_status = FIELD_DP32(bus->intr_status, I2CD_INTR_STS,
                                          NORMAL_STOP, 1);
        }
        bus->cmd = FIELD_DP32(bus->cmd, I2CD_CMD, M_STOP_CMD, 0);
        aspeed_i2c_set_state(bus, I2CD_IDLE);
    }
}

static void aspeed_i2c_bus_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    AspeedI2CBus *bus = opaque;
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(bus->controller);
    bool handle_rx;

    trace_aspeed_i2c_bus_write(bus->id, offset, size, value);

    switch (offset) {
    case A_I2CD_FUN_CTRL:
        if (FIELD_EX32(value, I2CD_FUN_CTRL, SLAVE_EN)) {
            qemu_log_mask(LOG_UNIMP, "%s: slave mode not implemented\n",
                          __func__);
            break;
        }
        bus->ctrl = value & 0x0071C3FF;
        break;
    case A_I2CD_AC_TIMING1:
        bus->timing[0] = value & 0xFFFFF0F;
        break;
    case A_I2CD_AC_TIMING2:
        bus->timing[1] = value & 0x7;
        break;
    case A_I2CD_INTR_CTRL:
        bus->intr_ctrl = value & 0x7FFF;
        break;
    case A_I2CD_INTR_STS:
        handle_rx = FIELD_EX32(bus->intr_status, I2CD_INTR_STS, RX_DONE) &&
                    FIELD_EX32(value, I2CD_INTR_STS, RX_DONE);
        bus->intr_status &= ~(value & 0x7FFF);
        if (!bus->intr_status) {
            bus->controller->intr_status &= ~(1 << bus->id);
            qemu_irq_lower(aic->bus_get_irq(bus));
        }
        if (handle_rx && (FIELD_EX32(bus->cmd, I2CD_CMD, M_RX_CMD) ||
                         FIELD_EX32(bus->cmd, I2CD_CMD, M_S_RX_CMD_LAST))) {
            aspeed_i2c_handle_rx_cmd(bus);
            aspeed_i2c_bus_raise_interrupt(bus);
        }
        break;
    case A_I2CD_DEV_ADDR:
        qemu_log_mask(LOG_UNIMP, "%s: slave mode not implemented\n",
                      __func__);
        break;
    case A_I2CD_POOL_CTRL:
        bus->pool_ctrl &= ~0xffffff;
        bus->pool_ctrl |= (value & 0xffffff);
        break;

    case A_I2CD_BYTE_BUF:
        bus->buf = FIELD_DP32(bus->buf, I2CD_BYTE_BUF, TX_BUF, value);
        break;
    case A_I2CD_CMD:
        if (!aspeed_i2c_bus_is_enabled(bus)) {
            break;
        }

        if (!aspeed_i2c_bus_is_master(bus)) {
            qemu_log_mask(LOG_UNIMP, "%s: slave mode not implemented\n",
                          __func__);
            break;
        }

        if (!aic->has_dma &&
            (FIELD_EX32(value, I2CD_CMD, RX_DMA_EN) ||
             FIELD_EX32(value, I2CD_CMD, TX_DMA_EN))) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: No DMA support\n",  __func__);
            break;
        }

        aspeed_i2c_bus_handle_cmd(bus, value);
        aspeed_i2c_bus_raise_interrupt(bus);
        break;
    case A_I2CD_DMA_ADDR:
        if (!aic->has_dma) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: No DMA support\n",  __func__);
            break;
        }

        bus->dma_addr = value & 0x3ffffffc;
        break;

    case A_I2CD_DMA_LEN:
        if (!aic->has_dma) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: No DMA support\n",  __func__);
            break;
        }

        bus->dma_len = value & 0xfff;
        if (!bus->dma_len) {
            qemu_log_mask(LOG_UNIMP, "%s: invalid DMA length\n",  __func__);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }
}

static uint64_t aspeed_i2c_ctrl_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    AspeedI2CState *s = opaque;

    switch (offset) {
    case A_I2C_CTRL_STATUS:
        return s->intr_status;
    case A_I2C_CTRL_GLOBAL:
        return s->ctrl_global;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    return -1;
}

static void aspeed_i2c_ctrl_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    AspeedI2CState *s = opaque;

    switch (offset) {
    case A_I2C_CTRL_GLOBAL:
        s->ctrl_global = value;
        break;
    case A_I2C_CTRL_STATUS:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps aspeed_i2c_bus_ops = {
    .read = aspeed_i2c_bus_read,
    .write = aspeed_i2c_bus_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps aspeed_i2c_ctrl_ops = {
    .read = aspeed_i2c_ctrl_read,
    .write = aspeed_i2c_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t aspeed_i2c_pool_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    AspeedI2CState *s = opaque;
    uint64_t ret = 0;
    int i;

    for (i = 0; i < size; i++) {
        ret |= (uint64_t) s->pool[offset + i] << (8 * i);
    }

    return ret;
}

static void aspeed_i2c_pool_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    AspeedI2CState *s = opaque;
    int i;

    for (i = 0; i < size; i++) {
        s->pool[offset + i] = (value >> (8 * i)) & 0xFF;
    }
}

static const MemoryRegionOps aspeed_i2c_pool_ops = {
    .read = aspeed_i2c_pool_read,
    .write = aspeed_i2c_pool_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const VMStateDescription aspeed_i2c_bus_vmstate = {
    .name = TYPE_ASPEED_I2C,
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(id, AspeedI2CBus),
        VMSTATE_UINT32(ctrl, AspeedI2CBus),
        VMSTATE_UINT32_ARRAY(timing, AspeedI2CBus, 2),
        VMSTATE_UINT32(intr_ctrl, AspeedI2CBus),
        VMSTATE_UINT32(intr_status, AspeedI2CBus),
        VMSTATE_UINT32(cmd, AspeedI2CBus),
        VMSTATE_UINT32(buf, AspeedI2CBus),
        VMSTATE_UINT32(pool_ctrl, AspeedI2CBus),
        VMSTATE_UINT32(dma_addr, AspeedI2CBus),
        VMSTATE_UINT32(dma_len, AspeedI2CBus),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription aspeed_i2c_vmstate = {
    .name = TYPE_ASPEED_I2C,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(intr_status, AspeedI2CState),
        VMSTATE_STRUCT_ARRAY(busses, AspeedI2CState,
                             ASPEED_I2C_NR_BUSSES, 1, aspeed_i2c_bus_vmstate,
                             AspeedI2CBus),
        VMSTATE_UINT8_ARRAY(pool, AspeedI2CState, ASPEED_I2C_MAX_POOL_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void aspeed_i2c_reset(DeviceState *dev)
{
    AspeedI2CState *s = ASPEED_I2C(dev);

    s->intr_status = 0;
}

static void aspeed_i2c_instance_init(Object *obj)
{
    AspeedI2CState *s = ASPEED_I2C(obj);
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(s);
    int i;

    for (i = 0; i < aic->num_busses; i++) {
        object_initialize_child(obj, "bus[*]", &s->busses[i],
                                TYPE_ASPEED_I2C_BUS);
    }
}

/*
 * Address Definitions (AST2400 and AST2500)
 *
 *   0x000 ... 0x03F: Global Register
 *   0x040 ... 0x07F: Device 1
 *   0x080 ... 0x0BF: Device 2
 *   0x0C0 ... 0x0FF: Device 3
 *   0x100 ... 0x13F: Device 4
 *   0x140 ... 0x17F: Device 5
 *   0x180 ... 0x1BF: Device 6
 *   0x1C0 ... 0x1FF: Device 7
 *   0x200 ... 0x2FF: Buffer Pool  (unused in linux driver)
 *   0x300 ... 0x33F: Device 8
 *   0x340 ... 0x37F: Device 9
 *   0x380 ... 0x3BF: Device 10
 *   0x3C0 ... 0x3FF: Device 11
 *   0x400 ... 0x43F: Device 12
 *   0x440 ... 0x47F: Device 13
 *   0x480 ... 0x4BF: Device 14
 *   0x800 ... 0xFFF: Buffer Pool  (unused in linux driver)
 */
static void aspeed_i2c_realize(DeviceState *dev, Error **errp)
{
    int i;
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedI2CState *s = ASPEED_I2C(dev);
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(s);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_i2c_ctrl_ops, s,
                          "aspeed.i2c", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    for (i = 0; i < aic->num_busses; i++) {
        Object *bus = OBJECT(&s->busses[i]);
        int offset = i < aic->gap ? 1 : 5;

        if (!object_property_set_link(bus, "controller", OBJECT(s), errp)) {
            return;
        }

        if (!object_property_set_uint(bus, "bus-id", i, errp)) {
            return;
        }

        if (!sysbus_realize(SYS_BUS_DEVICE(bus), errp)) {
            return;
        }

        memory_region_add_subregion(&s->iomem, aic->reg_size * (i + offset),
                                    &s->busses[i].mr);
    }

    memory_region_init_io(&s->pool_iomem, OBJECT(s), &aspeed_i2c_pool_ops, s,
                          "aspeed.i2c-pool", aic->pool_size);
    memory_region_add_subregion(&s->iomem, aic->pool_base, &s->pool_iomem);

    if (aic->has_dma) {
        if (!s->dram_mr) {
            error_setg(errp, TYPE_ASPEED_I2C ": 'dram' link not set");
            return;
        }

        address_space_init(&s->dram_as, s->dram_mr,
                           TYPE_ASPEED_I2C "-dma-dram");
    }
}

static Property aspeed_i2c_properties[] = {
    DEFINE_PROP_LINK("dram", AspeedI2CState, dram_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &aspeed_i2c_vmstate;
    dc->reset = aspeed_i2c_reset;
    device_class_set_props(dc, aspeed_i2c_properties);
    dc->realize = aspeed_i2c_realize;
    dc->desc = "Aspeed I2C Controller";
}

static const TypeInfo aspeed_i2c_info = {
    .name          = TYPE_ASPEED_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = aspeed_i2c_instance_init,
    .instance_size = sizeof(AspeedI2CState),
    .class_init    = aspeed_i2c_class_init,
    .class_size = sizeof(AspeedI2CClass),
    .abstract   = true,
};

static void aspeed_i2c_bus_reset(DeviceState *dev)
{
    AspeedI2CBus *s = ASPEED_I2C_BUS(dev);

    s->intr_ctrl = 0;
    s->intr_status = 0;
    s->cmd = 0;
    s->buf = 0;
    s->dma_addr = 0;
    s->dma_len = 0;
    i2c_end_transfer(s->bus);
}

static void aspeed_i2c_bus_realize(DeviceState *dev, Error **errp)
{
    AspeedI2CBus *s = ASPEED_I2C_BUS(dev);
    AspeedI2CClass *aic;
    g_autofree char *name = g_strdup_printf(TYPE_ASPEED_I2C_BUS ".%d", s->id);

    if (!s->controller) {
        error_setg(errp, TYPE_ASPEED_I2C_BUS ": 'controller' link not set");
        return;
    }

    aic = ASPEED_I2C_GET_CLASS(s->controller);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    s->bus = i2c_init_bus(dev, name);

    memory_region_init_io(&s->mr, OBJECT(s), &aspeed_i2c_bus_ops,
                          s, name, aic->reg_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);
}

static Property aspeed_i2c_bus_properties[] = {
    DEFINE_PROP_UINT8("bus-id", AspeedI2CBus, id, 0),
    DEFINE_PROP_LINK("controller", AspeedI2CBus, controller, TYPE_ASPEED_I2C,
                     AspeedI2CState *),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_i2c_bus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Aspeed I2C Bus";
    dc->realize = aspeed_i2c_bus_realize;
    dc->reset = aspeed_i2c_bus_reset;
    device_class_set_props(dc, aspeed_i2c_bus_properties);
}

static const TypeInfo aspeed_i2c_bus_info = {
    .name           = TYPE_ASPEED_I2C_BUS,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(AspeedI2CBus),
    .class_init     = aspeed_i2c_bus_class_init,
};

static qemu_irq aspeed_2400_i2c_bus_get_irq(AspeedI2CBus *bus)
{
    return bus->controller->irq;
}

static uint8_t *aspeed_2400_i2c_bus_pool_base(AspeedI2CBus *bus)
{
    uint8_t *pool_page =
        &bus->controller->pool[FIELD_EX32(bus->ctrl, I2CD_FUN_CTRL,
                                          POOL_PAGE_SEL) * 0x100];

    return &pool_page[FIELD_EX32(bus->pool_ctrl, I2CD_POOL_CTRL, OFFSET)];
}

static void aspeed_2400_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedI2CClass *aic = ASPEED_I2C_CLASS(klass);

    dc->desc = "ASPEED 2400 I2C Controller";

    aic->num_busses = 14;
    aic->reg_size = 0x40;
    aic->gap = 7;
    aic->bus_get_irq = aspeed_2400_i2c_bus_get_irq;
    aic->pool_size = 0x800;
    aic->pool_base = 0x800;
    aic->bus_pool_base = aspeed_2400_i2c_bus_pool_base;
}

static const TypeInfo aspeed_2400_i2c_info = {
    .name = TYPE_ASPEED_2400_I2C,
    .parent = TYPE_ASPEED_I2C,
    .class_init = aspeed_2400_i2c_class_init,
};

static qemu_irq aspeed_2500_i2c_bus_get_irq(AspeedI2CBus *bus)
{
    return bus->controller->irq;
}

static uint8_t *aspeed_2500_i2c_bus_pool_base(AspeedI2CBus *bus)
{
    return &bus->controller->pool[bus->id * 0x10];
}

static void aspeed_2500_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedI2CClass *aic = ASPEED_I2C_CLASS(klass);

    dc->desc = "ASPEED 2500 I2C Controller";

    aic->num_busses = 14;
    aic->reg_size = 0x40;
    aic->gap = 7;
    aic->bus_get_irq = aspeed_2500_i2c_bus_get_irq;
    aic->pool_size = 0x100;
    aic->pool_base = 0x200;
    aic->bus_pool_base = aspeed_2500_i2c_bus_pool_base;
    aic->check_sram = true;
    aic->has_dma = true;
}

static const TypeInfo aspeed_2500_i2c_info = {
    .name = TYPE_ASPEED_2500_I2C,
    .parent = TYPE_ASPEED_I2C,
    .class_init = aspeed_2500_i2c_class_init,
};

static qemu_irq aspeed_2600_i2c_bus_get_irq(AspeedI2CBus *bus)
{
    return bus->irq;
}

static uint8_t *aspeed_2600_i2c_bus_pool_base(AspeedI2CBus *bus)
{
   return &bus->controller->pool[bus->id * 0x20];
}

static void aspeed_2600_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedI2CClass *aic = ASPEED_I2C_CLASS(klass);

    dc->desc = "ASPEED 2600 I2C Controller";

    aic->num_busses = 16;
    aic->reg_size = 0x80;
    aic->gap = -1; /* no gap */
    aic->bus_get_irq = aspeed_2600_i2c_bus_get_irq;
    aic->pool_size = 0x200;
    aic->pool_base = 0xC00;
    aic->bus_pool_base = aspeed_2600_i2c_bus_pool_base;
    aic->has_dma = true;
}

static const TypeInfo aspeed_2600_i2c_info = {
    .name = TYPE_ASPEED_2600_I2C,
    .parent = TYPE_ASPEED_I2C,
    .class_init = aspeed_2600_i2c_class_init,
};

static void aspeed_i2c_register_types(void)
{
    type_register_static(&aspeed_i2c_bus_info);
    type_register_static(&aspeed_i2c_info);
    type_register_static(&aspeed_2400_i2c_info);
    type_register_static(&aspeed_2500_i2c_info);
    type_register_static(&aspeed_2600_i2c_info);
}

type_init(aspeed_i2c_register_types)


I2CBus *aspeed_i2c_get_bus(AspeedI2CState *s, int busnr)
{
    AspeedI2CClass *aic = ASPEED_I2C_GET_CLASS(s);
    I2CBus *bus = NULL;

    if (busnr >= 0 && busnr < aic->num_busses) {
        bus = s->busses[busnr].bus;
    }

    return bus;
}
