/*
 * QEMU PowerPC PowerNV Processor I2C model
 *
 * Copyright (c) 2019-2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "sysemu/reset.h"

#include "hw/irq.h"
#include "hw/qdev-properties.h"

#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_i2c.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/fdt.h"

#include <libfdt.h>

/* I2C FIFO register */
#define I2C_FIFO_REG                    0x4
#define I2C_FIFO                        PPC_BITMASK(0, 7)

/* I2C command register */
#define I2C_CMD_REG                     0x5
#define I2C_CMD_WITH_START              PPC_BIT(0)
#define I2C_CMD_WITH_ADDR               PPC_BIT(1)
#define I2C_CMD_READ_CONT               PPC_BIT(2)
#define I2C_CMD_WITH_STOP               PPC_BIT(3)
#define I2C_CMD_INTR_STEERING           PPC_BITMASK(6, 7) /* P9 */
#define   I2C_CMD_INTR_STEER_HOST       1
#define   I2C_CMD_INTR_STEER_OCC        2
#define I2C_CMD_DEV_ADDR                PPC_BITMASK(8, 14)
#define I2C_CMD_READ_NOT_WRITE          PPC_BIT(15)
#define I2C_CMD_LEN_BYTES               PPC_BITMASK(16, 31)
#define I2C_MAX_TFR_LEN                 0xfff0ull

/* I2C mode register */
#define I2C_MODE_REG                    0x6
#define I2C_MODE_BIT_RATE_DIV           PPC_BITMASK(0, 15)
#define I2C_MODE_PORT_NUM               PPC_BITMASK(16, 21)
#define I2C_MODE_ENHANCED               PPC_BIT(28)
#define I2C_MODE_DIAGNOSTIC             PPC_BIT(29)
#define I2C_MODE_PACING_ALLOW           PPC_BIT(30)
#define I2C_MODE_WRAP                   PPC_BIT(31)

/* I2C watermark register */
#define I2C_WATERMARK_REG               0x7
#define I2C_WATERMARK_HIGH              PPC_BITMASK(16, 19)
#define I2C_WATERMARK_LOW               PPC_BITMASK(24, 27)

/*
 * I2C interrupt mask and condition registers
 *
 * NB: The function of 0x9 and 0xa changes depending on whether you're reading
 *     or writing to them. When read they return the interrupt condition bits
 *     and on writes they update the interrupt mask register.
 *
 *  The bit definitions are the same for all the interrupt registers.
 */
#define I2C_INTR_MASK_REG               0x8

#define I2C_INTR_RAW_COND_REG           0x9 /* read */
#define I2C_INTR_MASK_OR_REG            0x9 /* write*/

#define I2C_INTR_COND_REG               0xa /* read */
#define I2C_INTR_MASK_AND_REG           0xa /* write */

#define I2C_INTR_ALL                    PPC_BITMASK(16, 31)
#define I2C_INTR_INVALID_CMD            PPC_BIT(16)
#define I2C_INTR_LBUS_PARITY_ERR        PPC_BIT(17)
#define I2C_INTR_BKEND_OVERRUN_ERR      PPC_BIT(18)
#define I2C_INTR_BKEND_ACCESS_ERR       PPC_BIT(19)
#define I2C_INTR_ARBT_LOST_ERR          PPC_BIT(20)
#define I2C_INTR_NACK_RCVD_ERR          PPC_BIT(21)
#define I2C_INTR_DATA_REQ               PPC_BIT(22)
#define I2C_INTR_CMD_COMP               PPC_BIT(23)
#define I2C_INTR_STOP_ERR               PPC_BIT(24)
#define I2C_INTR_I2C_BUSY               PPC_BIT(25)
#define I2C_INTR_NOT_I2C_BUSY           PPC_BIT(26)
#define I2C_INTR_SCL_EQ_1               PPC_BIT(28)
#define I2C_INTR_SCL_EQ_0               PPC_BIT(29)
#define I2C_INTR_SDA_EQ_1               PPC_BIT(30)
#define I2C_INTR_SDA_EQ_0               PPC_BIT(31)

/* I2C status register */
#define I2C_RESET_I2C_REG               0xb /* write */
#define I2C_RESET_ERRORS                0xc
#define I2C_STAT_REG                    0xb /* read */
#define I2C_STAT_INVALID_CMD            PPC_BIT(0)
#define I2C_STAT_LBUS_PARITY_ERR        PPC_BIT(1)
#define I2C_STAT_BKEND_OVERRUN_ERR      PPC_BIT(2)
#define I2C_STAT_BKEND_ACCESS_ERR       PPC_BIT(3)
#define I2C_STAT_ARBT_LOST_ERR          PPC_BIT(4)
#define I2C_STAT_NACK_RCVD_ERR          PPC_BIT(5)
#define I2C_STAT_DATA_REQ               PPC_BIT(6)
#define I2C_STAT_CMD_COMP               PPC_BIT(7)
#define I2C_STAT_STOP_ERR               PPC_BIT(8)
#define I2C_STAT_UPPER_THRS             PPC_BITMASK(9, 15)
#define I2C_STAT_ANY_I2C_INTR           PPC_BIT(16)
#define I2C_STAT_PORT_HISTORY_BUSY      PPC_BIT(19)
#define I2C_STAT_SCL_INPUT_LEVEL        PPC_BIT(20)
#define I2C_STAT_SDA_INPUT_LEVEL        PPC_BIT(21)
#define I2C_STAT_PORT_BUSY              PPC_BIT(22)
#define I2C_STAT_INTERFACE_BUSY         PPC_BIT(23)
#define I2C_STAT_FIFO_ENTRY_COUNT       PPC_BITMASK(24, 31)

#define I2C_STAT_ANY_ERR (I2C_STAT_INVALID_CMD | I2C_STAT_LBUS_PARITY_ERR | \
                          I2C_STAT_BKEND_OVERRUN_ERR | \
                          I2C_STAT_BKEND_ACCESS_ERR | I2C_STAT_ARBT_LOST_ERR | \
                          I2C_STAT_NACK_RCVD_ERR | I2C_STAT_STOP_ERR)


#define I2C_INTR_ACTIVE \
        ((I2C_STAT_ANY_ERR >> 16) | I2C_INTR_CMD_COMP | I2C_INTR_DATA_REQ)

/* Pseudo-status used for timeouts */
#define I2C_STAT_PSEUDO_TIMEOUT         PPC_BIT(63)

/* I2C extended status register */
#define I2C_EXTD_STAT_REG               0xc
#define I2C_EXTD_STAT_FIFO_SIZE         PPC_BITMASK(0, 7)
#define I2C_EXTD_STAT_MSM_CURSTATE      PPC_BITMASK(11, 15)
#define I2C_EXTD_STAT_SCL_IN_SYNC       PPC_BIT(16)
#define I2C_EXTD_STAT_SDA_IN_SYNC       PPC_BIT(17)
#define I2C_EXTD_STAT_S_SCL             PPC_BIT(18)
#define I2C_EXTD_STAT_S_SDA             PPC_BIT(19)
#define I2C_EXTD_STAT_M_SCL             PPC_BIT(20)
#define I2C_EXTD_STAT_M_SDA             PPC_BIT(21)
#define I2C_EXTD_STAT_HIGH_WATER        PPC_BIT(22)
#define I2C_EXTD_STAT_LOW_WATER         PPC_BIT(23)
#define I2C_EXTD_STAT_I2C_BUSY          PPC_BIT(24)
#define I2C_EXTD_STAT_SELF_BUSY         PPC_BIT(25)
#define I2C_EXTD_STAT_I2C_VERSION       PPC_BITMASK(27, 31)

/* I2C residual front end/back end length */
#define I2C_RESIDUAL_LEN_REG            0xd
#define I2C_RESIDUAL_FRONT_END          PPC_BITMASK(0, 15)
#define I2C_RESIDUAL_BACK_END           PPC_BITMASK(16, 31)

/* Port busy register */
#define I2C_PORT_BUSY_REG               0xe
#define I2C_SET_S_SCL_REG               0xd
#define I2C_RESET_S_SCL_REG             0xf
#define I2C_SET_S_SDA_REG               0x10
#define I2C_RESET_S_SDA_REG             0x11

#define PNV_I2C_FIFO_SIZE 8
#define PNV_I2C_MAX_BUSSES 64

static I2CBus *pnv_i2c_get_bus(PnvI2C *i2c)
{
    uint8_t port = GETFIELD(I2C_MODE_PORT_NUM, i2c->regs[I2C_MODE_REG]);

    if (port >= i2c->num_busses) {
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: invalid bus number %d/%d\n", port,
                      i2c->num_busses);
        return NULL;
    }
    return i2c->busses[port];
}

static void pnv_i2c_update_irq(PnvI2C *i2c)
{
    I2CBus *bus = pnv_i2c_get_bus(i2c);
    bool recv = !!(i2c->regs[I2C_CMD_REG] & I2C_CMD_READ_NOT_WRITE);
    uint16_t front_end = GETFIELD(I2C_RESIDUAL_FRONT_END,
                                  i2c->regs[I2C_RESIDUAL_LEN_REG]);
    uint16_t back_end = GETFIELD(I2C_RESIDUAL_BACK_END,
                                 i2c->regs[I2C_RESIDUAL_LEN_REG]);
    uint8_t fifo_count = GETFIELD(I2C_STAT_FIFO_ENTRY_COUNT,
                                   i2c->regs[I2C_STAT_REG]);
    uint8_t fifo_free = PNV_I2C_FIFO_SIZE - fifo_count;

    if (!bus) {
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: invalid port\n");
        return;
    }

    if (i2c_bus_busy(bus)) {
        i2c->regs[I2C_STAT_REG] &= ~I2C_STAT_DATA_REQ;

        if (recv) {
            if (fifo_count >=
                GETFIELD(I2C_WATERMARK_HIGH, i2c->regs[I2C_WATERMARK_REG])) {
                i2c->regs[I2C_EXTD_STAT_REG] |= I2C_EXTD_STAT_HIGH_WATER;
            } else {
                i2c->regs[I2C_EXTD_STAT_REG] &= ~I2C_EXTD_STAT_HIGH_WATER;
            }

            if (((i2c->regs[I2C_EXTD_STAT_REG] & I2C_EXTD_STAT_HIGH_WATER) &&
                 fifo_count != 0) || front_end == 0) {
                i2c->regs[I2C_STAT_REG] |= I2C_STAT_DATA_REQ;
            }
        } else {
            if (fifo_count <=
                GETFIELD(I2C_WATERMARK_LOW, i2c->regs[I2C_WATERMARK_REG])) {
                i2c->regs[I2C_EXTD_STAT_REG] |= I2C_EXTD_STAT_LOW_WATER;
            } else {
                i2c->regs[I2C_EXTD_STAT_REG] &= ~I2C_EXTD_STAT_LOW_WATER;
            }

            if (back_end > 0 &&
                (fifo_free >= back_end ||
                 (i2c->regs[I2C_EXTD_STAT_REG] & I2C_EXTD_STAT_LOW_WATER))) {
                i2c->regs[I2C_STAT_REG] |= I2C_STAT_DATA_REQ;
            }
        }

        if (back_end == 0 && front_end == 0) {
            i2c->regs[I2C_STAT_REG] &= ~I2C_STAT_DATA_REQ;
            i2c->regs[I2C_STAT_REG] |= I2C_STAT_CMD_COMP;

            if (i2c->regs[I2C_CMD_REG] & I2C_CMD_WITH_STOP) {
                i2c_end_transfer(bus);
                i2c->regs[I2C_EXTD_STAT_REG] &=
                    ~(I2C_EXTD_STAT_I2C_BUSY | I2C_EXTD_STAT_SELF_BUSY);
            }
        } else {
            i2c->regs[I2C_STAT_REG] &= ~I2C_STAT_CMD_COMP;
        }
    }

    /*
     * Status and interrupt registers have nearly the same layout.
     */
    i2c->regs[I2C_INTR_RAW_COND_REG] = i2c->regs[I2C_STAT_REG] >> 16;
    i2c->regs[I2C_INTR_COND_REG] =
        i2c->regs[I2C_INTR_RAW_COND_REG] & i2c->regs[I2C_INTR_MASK_REG];

    qemu_set_irq(i2c->psi_irq, i2c->regs[I2C_INTR_COND_REG] != 0);
}

static void pnv_i2c_fifo_update_count(PnvI2C *i2c)
{
    uint64_t stat = i2c->regs[I2C_STAT_REG];

    i2c->regs[I2C_STAT_REG] = SETFIELD(I2C_STAT_FIFO_ENTRY_COUNT, stat,
                                       fifo8_num_used(&i2c->fifo));
}

static void pnv_i2c_frontend_update(PnvI2C *i2c)
{
    uint64_t residual_end = i2c->regs[I2C_RESIDUAL_LEN_REG];
    uint16_t front_end = GETFIELD(I2C_RESIDUAL_FRONT_END, residual_end);

    i2c->regs[I2C_RESIDUAL_LEN_REG] =
        SETFIELD(I2C_RESIDUAL_FRONT_END, residual_end, front_end - 1);
}

static void pnv_i2c_fifo_flush(PnvI2C *i2c)
{
    I2CBus *bus = pnv_i2c_get_bus(i2c);
    uint8_t data;
    int ret;

    if (!bus) {
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: invalid port\n");
        return;
    }
    if (!i2c_bus_busy(bus)) {
        return;
    }

    if (i2c->regs[I2C_CMD_REG] & I2C_CMD_READ_NOT_WRITE) {
        if (fifo8_is_full(&i2c->fifo)) {
            return;
        }

        data = i2c_recv(bus);
        fifo8_push(&i2c->fifo, data);
    } else {
        if (fifo8_is_empty(&i2c->fifo)) {
            return;
        }

        data = fifo8_pop(&i2c->fifo);
        ret = i2c_send(bus, data);
        if (ret) {
            i2c->regs[I2C_STAT_REG] |= I2C_STAT_NACK_RCVD_ERR;
            i2c_end_transfer(bus);
        }
    }

    pnv_i2c_fifo_update_count(i2c);
    pnv_i2c_frontend_update(i2c);
}

static void pnv_i2c_handle_cmd(PnvI2C *i2c, uint64_t val)
{
    I2CBus *bus = pnv_i2c_get_bus(i2c);
    uint8_t addr = GETFIELD(I2C_CMD_DEV_ADDR, val);
    int recv = !!(val & I2C_CMD_READ_NOT_WRITE);
    uint32_t len_bytes = GETFIELD(I2C_CMD_LEN_BYTES, val);

    if (!(val & I2C_CMD_WITH_START) && !(val & I2C_CMD_WITH_ADDR) &&
        !(val & I2C_CMD_WITH_STOP) && !len_bytes) {
        i2c->regs[I2C_STAT_REG] |= I2C_STAT_INVALID_CMD;
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: invalid command 0x%"PRIx64"\n",
                      val);
        return;
    }

    if (!(i2c->regs[I2C_STAT_REG] & I2C_STAT_CMD_COMP)) {
        i2c->regs[I2C_STAT_REG] |= I2C_STAT_INVALID_CMD;
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: command in progress\n");
        return;
    }

    if (!bus) {
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: invalid port\n");
        return;
    }

    i2c->regs[I2C_RESIDUAL_LEN_REG] =
        SETFIELD(I2C_RESIDUAL_FRONT_END, 0ull, len_bytes) |
        SETFIELD(I2C_RESIDUAL_BACK_END, 0ull, len_bytes);

    if (val & I2C_CMD_WITH_START) {
        if (i2c_start_transfer(bus, addr, recv)) {
            i2c->regs[I2C_STAT_REG] |= I2C_STAT_NACK_RCVD_ERR;
        } else {
            i2c->regs[I2C_EXTD_STAT_REG] |=
                (I2C_EXTD_STAT_I2C_BUSY | I2C_EXTD_STAT_SELF_BUSY);
            pnv_i2c_fifo_flush(i2c);
        }
    }
}

static void pnv_i2c_backend_update(PnvI2C *i2c)
{
    uint64_t residual_end = i2c->regs[I2C_RESIDUAL_LEN_REG];
    uint16_t back_end = GETFIELD(I2C_RESIDUAL_BACK_END, residual_end);

    if (!back_end) {
        i2c->regs[I2C_STAT_REG] |= I2C_STAT_BKEND_ACCESS_ERR;
        return;
    }

    i2c->regs[I2C_RESIDUAL_LEN_REG] =
        SETFIELD(I2C_RESIDUAL_BACK_END, residual_end, back_end - 1);
}

static void pnv_i2c_fifo_in(PnvI2C *i2c)
{
    uint8_t data = GETFIELD(I2C_FIFO, i2c->regs[I2C_FIFO_REG]);
    I2CBus *bus = pnv_i2c_get_bus(i2c);

    if (!bus) {
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: invalid port\n");
        return;
    }

    if (!i2c_bus_busy(bus)) {
        i2c->regs[I2C_STAT_REG] |= I2C_STAT_INVALID_CMD;
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: no command in progress\n");
        return;
    }

    if (i2c->regs[I2C_CMD_REG] & I2C_CMD_READ_NOT_WRITE) {
        i2c->regs[I2C_STAT_REG] |= I2C_STAT_INVALID_CMD;
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: read command in progress\n");
        return;
    }

    if (fifo8_is_full(&i2c->fifo)) {
        if (!(i2c->regs[I2C_MODE_REG] & I2C_MODE_PACING_ALLOW)) {
            i2c->regs[I2C_STAT_REG] |= I2C_STAT_BKEND_OVERRUN_ERR;
        }
        return;
    }

    fifo8_push(&i2c->fifo, data);
    pnv_i2c_fifo_update_count(i2c);
    pnv_i2c_backend_update(i2c);
    pnv_i2c_fifo_flush(i2c);
}

static void pnv_i2c_fifo_out(PnvI2C *i2c)
{
    uint8_t data;
    I2CBus *bus = pnv_i2c_get_bus(i2c);

    if (!bus) {
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: invalid port\n");
        return;
    }

    if (!i2c_bus_busy(bus)) {
        i2c->regs[I2C_STAT_REG] |= I2C_STAT_INVALID_CMD;
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: no command in progress\n");
        return;
    }

    if (!(i2c->regs[I2C_CMD_REG] & I2C_CMD_READ_NOT_WRITE)) {
        i2c->regs[I2C_STAT_REG] |= I2C_STAT_INVALID_CMD;
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: write command in progress\n");
        return;
    }

    if (fifo8_is_empty(&i2c->fifo)) {
        if (!(i2c->regs[I2C_MODE_REG] & I2C_MODE_PACING_ALLOW)) {
            i2c->regs[I2C_STAT_REG] |= I2C_STAT_BKEND_OVERRUN_ERR;
        }
        return;
    }

    data = fifo8_pop(&i2c->fifo);

    i2c->regs[I2C_FIFO_REG] = SETFIELD(I2C_FIFO, 0ull, data);
    pnv_i2c_fifo_update_count(i2c);
    pnv_i2c_backend_update(i2c);
}

static uint64_t pnv_i2c_xscom_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    PnvI2C *i2c = PNV_I2C(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = -1;
    int i;

    switch (offset) {
    case I2C_STAT_REG:
        val = i2c->regs[offset];
        break;

    case I2C_FIFO_REG:
        pnv_i2c_fifo_out(i2c);
        val = i2c->regs[offset];
        break;

    case I2C_PORT_BUSY_REG: /* compute busy bit for each port  */
        val = 0;
        for (i = 0; i < i2c->num_busses; i++) {
            val |= (uint64_t)i2c_bus_busy(i2c->busses[i]) << i;
        }
        break;

    case I2C_CMD_REG:
    case I2C_MODE_REG:
    case I2C_WATERMARK_REG:
    case I2C_INTR_MASK_REG:
    case I2C_INTR_RAW_COND_REG:
    case I2C_INTR_COND_REG:
    case I2C_EXTD_STAT_REG:
    case I2C_RESIDUAL_LEN_REG:
        val = i2c->regs[offset];
        break;
    default:
        i2c->regs[I2C_STAT_REG] |= I2C_STAT_INVALID_CMD;
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: read at register: 0x%"
                      HWADDR_PRIx "\n", addr >> 3);
    }

    pnv_i2c_update_irq(i2c);

    return val;
}

static void pnv_i2c_reset(void *dev)
{
    PnvI2C *i2c = PNV_I2C(dev);

    memset(i2c->regs, 0, sizeof(i2c->regs));

    i2c->regs[I2C_STAT_REG] =
        SETFIELD(I2C_STAT_UPPER_THRS, 0ull, i2c->num_busses - 1) |
        I2C_STAT_CMD_COMP | I2C_STAT_SCL_INPUT_LEVEL |
        I2C_STAT_SDA_INPUT_LEVEL;
    i2c->regs[I2C_EXTD_STAT_REG] =
        SETFIELD(I2C_EXTD_STAT_FIFO_SIZE, 0ull, PNV_I2C_FIFO_SIZE) |
        SETFIELD(I2C_EXTD_STAT_I2C_VERSION, 0ull, 23); /* last version */

    fifo8_reset(&i2c->fifo);
}

static void pnv_i2c_xscom_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    PnvI2C *i2c = PNV_I2C(opaque);
    uint32_t offset = addr >> 3;

    switch (offset) {
    case I2C_MODE_REG:
        {
            i2c->regs[offset] = val;
            I2CBus *bus = pnv_i2c_get_bus(i2c);
            if (!bus) {
                qemu_log_mask(LOG_GUEST_ERROR, "I2C: invalid port\n");
                return;
            }
            if (i2c_bus_busy(bus)) {
                i2c->regs[I2C_STAT_REG] |= I2C_STAT_INVALID_CMD;
                qemu_log_mask(LOG_GUEST_ERROR, "I2C: command in progress\n");
            }
        }
        break;

    case I2C_CMD_REG:
        i2c->regs[offset] = val;
        pnv_i2c_handle_cmd(i2c, val);
        break;

    case I2C_FIFO_REG:
        i2c->regs[offset] = val;
        pnv_i2c_fifo_in(i2c);
        break;

    case I2C_WATERMARK_REG:
        i2c->regs[offset] = val;
        break;

    case I2C_RESET_I2C_REG:
        pnv_i2c_reset(i2c);
        break;

    case I2C_RESET_ERRORS:
        i2c->regs[I2C_STAT_REG] &= ~I2C_STAT_ANY_ERR;
        i2c->regs[I2C_RESIDUAL_LEN_REG] = 0;
        i2c->regs[I2C_EXTD_STAT_REG] &=
            (I2C_EXTD_STAT_FIFO_SIZE | I2C_EXTD_STAT_I2C_VERSION);
        fifo8_reset(&i2c->fifo);
        break;

    case I2C_INTR_MASK_REG:
        i2c->regs[offset] = val;
        break;

    case I2C_INTR_MASK_OR_REG:
        i2c->regs[I2C_INTR_MASK_REG] |= val;
        break;

    case I2C_INTR_MASK_AND_REG:
        i2c->regs[I2C_INTR_MASK_REG] &= val;
        break;

    case I2C_PORT_BUSY_REG:
    case I2C_SET_S_SCL_REG:
    case I2C_RESET_S_SCL_REG:
    case I2C_SET_S_SDA_REG:
    case I2C_RESET_S_SDA_REG:
        i2c->regs[offset] = val;
        break;
    default:
        i2c->regs[I2C_STAT_REG] |= I2C_STAT_INVALID_CMD;
        qemu_log_mask(LOG_GUEST_ERROR, "I2C: write at register: 0x%"
                      HWADDR_PRIx " val=0x%"PRIx64"\n", addr >> 3, val);
    }

    pnv_i2c_update_irq(i2c);
}

static const MemoryRegionOps pnv_i2c_xscom_ops = {
    .read = pnv_i2c_xscom_read,
    .write = pnv_i2c_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static int pnv_i2c_bus_dt_xscom(PnvI2C *i2c, void *fdt,
                                int offset, int index)
{
    int i2c_bus_offset;
    const char i2c_compat[] =
        "ibm,opal-i2c\0ibm,power8-i2c-port\0ibm,power9-i2c-port";
    g_autofree char *i2c_port_name = NULL;
    g_autofree char *name = g_strdup_printf("i2c-bus@%x", index);

    i2c_bus_offset = fdt_add_subnode(fdt, offset, name);
    _FDT(i2c_bus_offset);

    _FDT((fdt_setprop_cell(fdt, i2c_bus_offset, "reg", index)));
    _FDT((fdt_setprop_cell(fdt, i2c_bus_offset, "#address-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, i2c_bus_offset, "#size-cells", 0)));
    _FDT(fdt_setprop(fdt, i2c_bus_offset, "compatible", i2c_compat,
                     sizeof(i2c_compat)));
    _FDT((fdt_setprop_cell(fdt, i2c_bus_offset, "bus-frequency", 400000)));

    i2c_port_name = g_strdup_printf("p8_%08x_e%dp%d", i2c->chip->chip_id,
                                    i2c->engine, index);
    _FDT(fdt_setprop_string(fdt, i2c_bus_offset, "ibm,port-name",
                            i2c_port_name));
    return 0;
}

#define XSCOM_BUS_FREQUENCY 466500000
#define I2C_CLOCK_FREQUENCY (XSCOM_BUS_FREQUENCY / 4)

static int pnv_i2c_dt_xscom(PnvXScomInterface *dev, void *fdt,
                            int offset)
{
    PnvI2C *i2c = PNV_I2C(dev);
    int i2c_offset;
    const char i2c_compat[] = "ibm,power8-i2cm\0ibm,power9-i2cm";
    uint32_t i2c_pcba = PNV9_XSCOM_I2CM_BASE +
        (i2c->engine - 1) * PNV9_XSCOM_I2CM_SIZE;
    uint32_t reg[2] = {
        cpu_to_be32(i2c_pcba),
        cpu_to_be32(PNV9_XSCOM_I2CM_SIZE)
    };
    int i;
    g_autofree char *name = g_strdup_printf("i2cm@%x", i2c_pcba);

    i2c_offset = fdt_add_subnode(fdt, offset, name);
    _FDT(i2c_offset);

    _FDT(fdt_setprop(fdt, i2c_offset, "reg", reg, sizeof(reg)));

    _FDT((fdt_setprop_cell(fdt, i2c_offset, "#address-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, i2c_offset, "#size-cells", 0)));
    _FDT(fdt_setprop(fdt, i2c_offset, "compatible", i2c_compat,
                     sizeof(i2c_compat)));
    _FDT((fdt_setprop_cell(fdt, i2c_offset, "chip-engine#", i2c->engine)));
    _FDT((fdt_setprop_cell(fdt, i2c_offset, "clock-frequency",
                           I2C_CLOCK_FREQUENCY)));

    for (i = 0; i < i2c->num_busses; i++) {
        pnv_i2c_bus_dt_xscom(i2c, fdt, i2c_offset, i);
    }
    return 0;
}

static void pnv_i2c_realize(DeviceState *dev, Error **errp)
{
    PnvI2C *i2c = PNV_I2C(dev);
    int i;

    assert(i2c->chip);

    if (i2c->num_busses > PNV_I2C_MAX_BUSSES) {
        error_setg(errp, "Invalid number of busses: %u", i2c->num_busses);
        return;
    }

    pnv_xscom_region_init(&i2c->xscom_regs, OBJECT(i2c), &pnv_i2c_xscom_ops,
                          i2c, "xscom-i2c", PNV9_XSCOM_I2CM_SIZE);

    i2c->busses = g_new(I2CBus *, i2c->num_busses);
    for (i = 0; i < i2c->num_busses; i++) {
        char name[32];

        snprintf(name, sizeof(name), TYPE_PNV_I2C ".%d", i);
        i2c->busses[i] = i2c_init_bus(dev, name);
    }

    fifo8_create(&i2c->fifo, PNV_I2C_FIFO_SIZE);

    qemu_register_reset(pnv_i2c_reset, dev);

    qdev_init_gpio_out(DEVICE(dev), &i2c->psi_irq, 1);
}

static Property pnv_i2c_properties[] = {
    DEFINE_PROP_LINK("chip", PnvI2C, chip, TYPE_PNV_CHIP, PnvChip *),
    DEFINE_PROP_UINT32("engine", PnvI2C, engine, 1),
    DEFINE_PROP_UINT32("num-busses", PnvI2C, num_busses, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xscomc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xscomc->dt_xscom = pnv_i2c_dt_xscom;

    dc->desc = "PowerNV I2C";
    dc->realize = pnv_i2c_realize;
    device_class_set_props(dc, pnv_i2c_properties);
}

static const TypeInfo pnv_i2c_info = {
    .name          = TYPE_PNV_I2C,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvI2C),
    .class_init    = pnv_i2c_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_i2c_register_types(void)
{
    type_register_static(&pnv_i2c_info);
}

type_init(pnv_i2c_register_types);
