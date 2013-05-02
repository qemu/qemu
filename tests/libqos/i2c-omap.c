/*
 * QTest I2C driver
 *
 * Copyright (c) 2012 Andreas Färber
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "libqos/i2c.h"

#include <glib.h>
#include <string.h>

#include "qemu/osdep.h"
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

typedef struct OMAPI2C {
    I2CAdapter parent;

    uint64_t addr;
} OMAPI2C;


static void omap_i2c_set_slave_addr(OMAPI2C *s, uint8_t addr)
{
    uint16_t data = addr;

    writew(s->addr + OMAP_I2C_SA, data);
    data = readw(s->addr + OMAP_I2C_SA);
    g_assert_cmphex(data, ==, addr);
}

static void omap_i2c_send(I2CAdapter *i2c, uint8_t addr,
                          const uint8_t *buf, uint16_t len)
{
    OMAPI2C *s = (OMAPI2C *)i2c;
    uint16_t data;

    omap_i2c_set_slave_addr(s, addr);

    data = len;
    writew(s->addr + OMAP_I2C_CNT, data);

    data = OMAP_I2C_CON_I2C_EN |
           OMAP_I2C_CON_TRX |
           OMAP_I2C_CON_MST |
           OMAP_I2C_CON_STT |
           OMAP_I2C_CON_STP;
    writew(s->addr + OMAP_I2C_CON, data);
    data = readw(s->addr + OMAP_I2C_CON);
    g_assert((data & OMAP_I2C_CON_STP) != 0);

    data = readw(s->addr + OMAP_I2C_STAT);
    g_assert((data & OMAP_I2C_STAT_NACK) == 0);

    while (len > 1) {
        data = readw(s->addr + OMAP_I2C_STAT);
        g_assert((data & OMAP_I2C_STAT_XRDY) != 0);

        data = buf[0] | ((uint16_t)buf[1] << 8);
        writew(s->addr + OMAP_I2C_DATA, data);
        buf = (uint8_t *)buf + 2;
        len -= 2;
    }
    if (len == 1) {
        data = readw(s->addr + OMAP_I2C_STAT);
        g_assert((data & OMAP_I2C_STAT_XRDY) != 0);

        data = buf[0];
        writew(s->addr + OMAP_I2C_DATA, data);
    }

    data = readw(s->addr + OMAP_I2C_CON);
    g_assert((data & OMAP_I2C_CON_STP) == 0);
}

static void omap_i2c_recv(I2CAdapter *i2c, uint8_t addr,
                          uint8_t *buf, uint16_t len)
{
    OMAPI2C *s = (OMAPI2C *)i2c;
    uint16_t data, stat;

    omap_i2c_set_slave_addr(s, addr);

    data = len;
    writew(s->addr + OMAP_I2C_CNT, data);

    data = OMAP_I2C_CON_I2C_EN |
           OMAP_I2C_CON_MST |
           OMAP_I2C_CON_STT |
           OMAP_I2C_CON_STP;
    writew(s->addr + OMAP_I2C_CON, data);
    data = readw(s->addr + OMAP_I2C_CON);
    g_assert((data & OMAP_I2C_CON_STP) == 0);

    data = readw(s->addr + OMAP_I2C_STAT);
    g_assert((data & OMAP_I2C_STAT_NACK) == 0);

    data = readw(s->addr + OMAP_I2C_CNT);
    g_assert_cmpuint(data, ==, len);

    while (len > 0) {
        data = readw(s->addr + OMAP_I2C_STAT);
        g_assert((data & OMAP_I2C_STAT_RRDY) != 0);
        g_assert((data & OMAP_I2C_STAT_ROVR) == 0);

        data = readw(s->addr + OMAP_I2C_DATA);

        stat = readw(s->addr + OMAP_I2C_STAT);

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

    data = readw(s->addr + OMAP_I2C_CON);
    g_assert((data & OMAP_I2C_CON_STP) == 0);
}

I2CAdapter *omap_i2c_create(uint64_t addr)
{
    OMAPI2C *s = g_malloc0(sizeof(*s));
    I2CAdapter *i2c = (I2CAdapter *)s;
    uint16_t data;

    s->addr = addr;

    i2c->send = omap_i2c_send;
    i2c->recv = omap_i2c_recv;

    /* verify the mmio address by looking for a known signature */
    data = readw(addr + OMAP_I2C_REV);
    g_assert_cmphex(data, ==, 0x34);

    return i2c;
}
