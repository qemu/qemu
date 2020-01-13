/*
 * QTest I2C driver
 *
 * Copyright (c) 2012 Andreas Färber
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "libqos/i2c.h"


#include "qemu/bswap.h"
#include "libqtest.h"

enum OMAPI2CRegisters {
    OMAP_I2C_REV  = 0x00,
    OMAP_I2C_STAT = 0x08,
    OMAP_I2C_CNT  = 0x18,
    OMAP_I2C_DATA = 0x1c,
    OMAP_I2C_CON  = 0x24,
    OMAP_I2C_SA   = 0x2c,
};

enum OMAPI2CSTATBits {
    OMAP_I2C_STAT_NACK = 1 << 1,
    OMAP_I2C_STAT_ARDY = 1 << 2,
    OMAP_I2C_STAT_RRDY = 1 << 3,
    OMAP_I2C_STAT_XRDY = 1 << 4,
    OMAP_I2C_STAT_ROVR = 1 << 11,
    OMAP_I2C_STAT_SBD  = 1 << 15,
};

enum OMAPI2CCONBits {
    OMAP_I2C_CON_STT    = 1 << 0,
    OMAP_I2C_CON_STP    = 1 << 1,
    OMAP_I2C_CON_TRX    = 1 << 9,
    OMAP_I2C_CON_MST    = 1 << 10,
    OMAP_I2C_CON_BE     = 1 << 14,
    OMAP_I2C_CON_I2C_EN = 1 << 15,
};


static void omap_i2c_set_slave_addr(OMAPI2C *s, uint8_t addr)
{
    uint16_t data = addr;

    qtest_writew(s->parent.qts, s->addr + OMAP_I2C_SA, data);
    data = qtest_readw(s->parent.qts, s->addr + OMAP_I2C_SA);
    g_assert_cmphex(data, ==, addr);
}

static void omap_i2c_send(I2CAdapter *i2c, uint8_t addr,
                          const uint8_t *buf, uint16_t len)
{
    OMAPI2C *s = container_of(i2c, OMAPI2C, parent);
    uint16_t data;

    omap_i2c_set_slave_addr(s, addr);

    data = len;
    qtest_writew(i2c->qts, s->addr + OMAP_I2C_CNT, data);

    data = OMAP_I2C_CON_I2C_EN |
           OMAP_I2C_CON_TRX |
           OMAP_I2C_CON_MST |
           OMAP_I2C_CON_STT |
           OMAP_I2C_CON_STP;
    qtest_writew(i2c->qts, s->addr + OMAP_I2C_CON, data);
    data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_CON);
    g_assert((data & OMAP_I2C_CON_STP) != 0);

    data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_STAT);
    g_assert((data & OMAP_I2C_STAT_NACK) == 0);

    while (len > 1) {
        data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_STAT);
        g_assert((data & OMAP_I2C_STAT_XRDY) != 0);

        data = buf[0] | ((uint16_t)buf[1] << 8);
        qtest_writew(i2c->qts, s->addr + OMAP_I2C_DATA, data);
        buf = (uint8_t *)buf + 2;
        len -= 2;
    }
    if (len == 1) {
        data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_STAT);
        g_assert((data & OMAP_I2C_STAT_XRDY) != 0);

        data = buf[0];
        qtest_writew(i2c->qts, s->addr + OMAP_I2C_DATA, data);
    }

    data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_CON);
    g_assert((data & OMAP_I2C_CON_STP) == 0);
}

static void omap_i2c_recv(I2CAdapter *i2c, uint8_t addr,
                          uint8_t *buf, uint16_t len)
{
    OMAPI2C *s = container_of(i2c, OMAPI2C, parent);
    uint16_t data, stat;
    uint16_t orig_len = len;

    omap_i2c_set_slave_addr(s, addr);

    data = len;
    qtest_writew(i2c->qts, s->addr + OMAP_I2C_CNT, data);

    data = OMAP_I2C_CON_I2C_EN |
           OMAP_I2C_CON_MST |
           OMAP_I2C_CON_STT |
           OMAP_I2C_CON_STP;
    qtest_writew(i2c->qts, s->addr + OMAP_I2C_CON, data);

    data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_STAT);
    g_assert((data & OMAP_I2C_STAT_NACK) == 0);

    while (len > 0) {
        data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_CON);
        if (len <= 4) {
            g_assert((data & OMAP_I2C_CON_STP) == 0);

            data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_CNT);
            g_assert_cmpuint(data, ==, orig_len);
        } else {
            g_assert((data & OMAP_I2C_CON_STP) != 0);

            data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_CNT);
            g_assert_cmpuint(data, ==, len - 4);
        }

        data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_STAT);
        g_assert((data & OMAP_I2C_STAT_RRDY) != 0);
        g_assert((data & OMAP_I2C_STAT_ROVR) == 0);

        data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_DATA);

        stat = qtest_readw(i2c->qts, s->addr + OMAP_I2C_STAT);

        if (unlikely(len == 1)) {
            g_assert((stat & OMAP_I2C_STAT_SBD) != 0);

            buf[0] = data & 0xff;
            buf++;
            len--;
        } else {
            buf[0] = data & 0xff;
            buf[1] = data >> 8;
            buf += 2;
            len -= 2;
        }
    }

    data = qtest_readw(i2c->qts, s->addr + OMAP_I2C_CON);
    g_assert((data & OMAP_I2C_CON_STP) == 0);
}

static void *omap_i2c_get_driver(void *obj, const char *interface)
{
    OMAPI2C *s = obj;
    if (!g_strcmp0(interface, "i2c-bus")) {
        return &s->parent;
    }
    fprintf(stderr, "%s not present in omap_i2c\n", interface);
    g_assert_not_reached();
}

static void omap_i2c_start_hw(QOSGraphObject *object)
{
    OMAPI2C *s = (OMAPI2C *) object;
    uint16_t data;

    /* verify the mmio address by looking for a known signature */
    data = qtest_readw(s->parent.qts, s->addr + OMAP_I2C_REV);
    g_assert_cmphex(data, ==, 0x34);
}

void omap_i2c_init(OMAPI2C *s, QTestState *qts, uint64_t addr)
{
    s->addr = addr;

    s->obj.get_driver = omap_i2c_get_driver;
    s->obj.start_hw = omap_i2c_start_hw;

    s->parent.send = omap_i2c_send;
    s->parent.recv = omap_i2c_recv;
    s->parent.qts = qts;
}

static void omap_i2c_register_nodes(void)
{
    qos_node_create_driver("omap_i2c", NULL);
    qos_node_produces("omap_i2c", "i2c-bus");
}

libqos_init(omap_i2c_register_nodes);
