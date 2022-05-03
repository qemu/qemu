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
#include "i2c.h"


#include "../libqtest.h"

#include "hw/i2c/imx_i2c.h"

enum IMXI2CDirection {
    IMX_I2C_READ,
    IMX_I2C_WRITE,
};

static void imx_i2c_set_slave_addr(IMXI2C *s, uint8_t addr,
                                   enum IMXI2CDirection direction)
{
    qtest_writeb(s->parent.qts, s->addr + I2DR_ADDR,
                 (addr << 1) | (direction == IMX_I2C_READ ? 1 : 0));
}

static void imx_i2c_send(I2CAdapter *i2c, uint8_t addr,
                         const uint8_t *buf, uint16_t len)
{
    IMXI2C *s = container_of(i2c, IMXI2C, parent);
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

    qtest_writeb(i2c->qts, s->addr + I2CR_ADDR, data);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) != 0);

    /* set the slave address */
    imx_i2c_set_slave_addr(s, addr, IMX_I2C_WRITE);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) != 0);
    g_assert((status & I2SR_RXAK) == 0);

    /* ack the interrupt */
    qtest_writeb(i2c->qts, s->addr + I2SR_ADDR, 0);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) == 0);

    while (size < len) {
        /* check we are still busy */
        status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IBB) != 0);

        /* write the data */
        qtest_writeb(i2c->qts, s->addr + I2DR_ADDR, buf[size]);
        status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IIF) != 0);
        g_assert((status & I2SR_RXAK) == 0);

        /* ack the interrupt */
        qtest_writeb(i2c->qts, s->addr + I2SR_ADDR, 0);
        status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IIF) == 0);

        size++;
    }

    /* release the bus */
    data &= ~(I2CR_MSTA | I2CR_MTX);
    qtest_writeb(i2c->qts, s->addr + I2CR_ADDR, data);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) == 0);
}

static void imx_i2c_recv(I2CAdapter *i2c, uint8_t addr,
                         uint8_t *buf, uint16_t len)
{
    IMXI2C *s = container_of(i2c, IMXI2C, parent);
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

    qtest_writeb(i2c->qts, s->addr + I2CR_ADDR, data);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) != 0);

    /* set the slave address */
    imx_i2c_set_slave_addr(s, addr, IMX_I2C_READ);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) != 0);
    g_assert((status & I2SR_RXAK) == 0);

    /* ack the interrupt */
    qtest_writeb(i2c->qts, s->addr + I2SR_ADDR, 0);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) == 0);

    /* set the bus for read */
    data &= ~I2CR_MTX;
    /* if only one byte don't ack */
    if (len != 1) {
        data &= ~I2CR_TXAK;
    }
    qtest_writeb(i2c->qts, s->addr + I2CR_ADDR, data);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) != 0);

    /* dummy read */
    qtest_readb(i2c->qts, s->addr + I2DR_ADDR);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) != 0);

    /* ack the interrupt */
    qtest_writeb(i2c->qts, s->addr + I2SR_ADDR, 0);
    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IIF) == 0);

    while (size < len) {
        /* check we are still busy */
        status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IBB) != 0);

        if (size == (len - 1)) {
            /* stop the read transaction */
            data &= ~(I2CR_MSTA | I2CR_MTX);
        } else {
            /* ack the data read */
            data |= I2CR_TXAK;
        }
        qtest_writeb(i2c->qts, s->addr + I2CR_ADDR, data);

        /* read the data */
        buf[size] = qtest_readb(i2c->qts, s->addr + I2DR_ADDR);

        if (size != (len - 1)) {
            status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
            g_assert((status & I2SR_IIF) != 0);

            /* ack the interrupt */
            qtest_writeb(i2c->qts, s->addr + I2SR_ADDR, 0);
        }

        status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
        g_assert((status & I2SR_IIF) == 0);

        size++;
    }

    status = qtest_readb(i2c->qts, s->addr + I2SR_ADDR);
    g_assert((status & I2SR_IBB) == 0);
}

static void *imx_i2c_get_driver(void *obj, const char *interface)
{
    IMXI2C *s = obj;
    if (!g_strcmp0(interface, "i2c-bus")) {
        return &s->parent;
    }
    fprintf(stderr, "%s not present in imx-i2c\n", interface);
    g_assert_not_reached();
}

void imx_i2c_init(IMXI2C *s, QTestState *qts, uint64_t addr)
{
    s->addr = addr;

    s->obj.get_driver = imx_i2c_get_driver;

    s->parent.send = imx_i2c_send;
    s->parent.recv = imx_i2c_recv;
    s->parent.qts = qts;
}

static void imx_i2c_register_nodes(void)
{
    qos_node_create_driver("imx.i2c", NULL);
    qos_node_produces("imx.i2c", "i2c-bus");
}

libqos_init(imx_i2c_register_nodes);
