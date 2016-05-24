/*
 * QTest i.MX I2C driver
 *
 * Copyright (c) 2013 Jean-Christophe Dubois
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "libqos/i2c.h"


#include "libqtest.h"

#include "hw/i2c/imx_i2c.h"

enum IMXI2CDirection {
    IMX_I2C_READ,
    IMX_I2C_WRITE,
};

typedef struct IMXI2C {
    I2CAdapter parent;

    uint64_t addr;
} IMXI2C;


static void imx_i2c_set_slave_addr(IMXI2C *s, uint8_t addr,
                                   enum IMXI2CDirection direction)
{
    writeb(s->addr + I2DR_ADDR, (addr << 1) |
           (direction == IMX_I2C_READ ? 1 : 0));
}

static void imx_i2c_send(I2CAdapter *i2c, uint8_t addr,
                         const uint8_t *buf, uint16_t len)
{
    IMXI2C *s = (IMXI2C *)i2c;
    uint8_t data;
    uint8_t status;
    uint16_t size = 0;

    if (!len) {
        return;
    }

    /* set the bus for write */
    data = I2CR_IEN |
           I2CR_IIEN |
           I2CR_MSTA |
           I2CR_MTX |
           I2CR_TXAK;

    writeb(s->addr + I2CR_ADDR, data);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) != 0);

    /* set the slave address */
    imx_i2c_set_slave_addr(s, addr, IMX_I2C_WRITE);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) != 0);
    g_assert((status & I2SR_RXAK) == 0);

    /* ack the interrupt */
    writeb(s->addr + I2SR_ADDR, 0);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) == 0);

    while (size < len) {
        /* check we are still busy */
        status = readb(s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IBB) != 0);

        /* write the data */
        writeb(s->addr + I2DR_ADDR, buf[size]);
        status = readb(s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IIF) != 0);
        g_assert((status & I2SR_RXAK) == 0);

        /* ack the interrupt */
        writeb(s->addr + I2SR_ADDR, 0);
        status = readb(s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IIF) == 0);

        size++;
    }

    /* release the bus */
    data &= ~(I2CR_MSTA | I2CR_MTX);
    writeb(s->addr + I2CR_ADDR, data);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) == 0);
}

static void imx_i2c_recv(I2CAdapter *i2c, uint8_t addr,
                         uint8_t *buf, uint16_t len)
{
    IMXI2C *s = (IMXI2C *)i2c;
    uint8_t data;
    uint8_t status;
    uint16_t size = 0;

    if (!len) {
        return;
    }

    /* set the bus for write */
    data = I2CR_IEN |
           I2CR_IIEN |
           I2CR_MSTA |
           I2CR_MTX |
           I2CR_TXAK;

    writeb(s->addr + I2CR_ADDR, data);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) != 0);

    /* set the slave address */
    imx_i2c_set_slave_addr(s, addr, IMX_I2C_READ);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) != 0);
    g_assert((status & I2SR_RXAK) == 0);

    /* ack the interrupt */
    writeb(s->addr + I2SR_ADDR, 0);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) == 0);

    /* set the bus for read */
    data &= ~I2CR_MTX;
    /* if only one byte don't ack */
    if (len != 1) {
        data &= ~I2CR_TXAK;
    }
    writeb(s->addr + I2CR_ADDR, data);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) != 0);

    /* dummy read */
    readb(s->addr + I2DR_ADDR);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) != 0);

    /* ack the interrupt */
    writeb(s->addr + I2SR_ADDR, 0);
    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) == 0);

    while (size < len) {
        /* check we are still busy */
        status = readb(s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IBB) != 0);

        if (size == (len - 1)) {
            /* stop the read transaction */
            data &= ~(I2CR_MSTA | I2CR_MTX);
        } else {
            /* ack the data read */
            data |= I2CR_TXAK;
        }
        writeb(s->addr + I2CR_ADDR, data);

        /* read the data */
        buf[size] = readb(s->addr + I2DR_ADDR);

        if (size != (len - 1)) {
            status = readb(s->addr + I2SR_ADDR);
            g_assert((status & I2SR_IIF) != 0);

            /* ack the interrupt */
            writeb(s->addr + I2SR_ADDR, 0);
        }

        status = readb(s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IIF) == 0);

        size++;
    }

    status = readb(s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) == 0);
}

I2CAdapter *imx_i2c_create(uint64_t addr)
{
    IMXI2C *s = g_malloc0(sizeof(*s));
    I2CAdapter *i2c = (I2CAdapter *)s;

    s->addr = addr;

    i2c->send = imx_i2c_send;
    i2c->recv = imx_i2c_recv;

    return i2c;
}
