/*
 * TI OMAP on-chip I2C controller.  Only "new I2C" mode supported.
 *
 * Copyright (C) 2007 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "hw.h"
#include "i2c.h"
#include "omap.h"

struct omap_i2c_s {
    qemu_irq irq;
    qemu_irq drq[2];
    i2c_bus *bus;

    uint8_t revision;
    uint8_t mask;
    uint16_t stat;
    uint16_t dma;
    uint16_t count;
    int count_cur;
    uint32_t fifo;
    int rxlen;
    int txlen;
    uint16_t control;
    uint16_t addr[2];
    uint8_t divider;
    uint8_t times[2];
    uint16_t test;
};

#define OMAP2_INTR_REV	0x34
#define OMAP2_GC_REV	0x34

static void omap_i2c_interrupts_update(struct omap_i2c_s *s)
{
    qemu_set_irq(s->irq, s->stat & s->mask);
    if ((s->dma >> 15) & 1)					/* RDMA_EN */
        qemu_set_irq(s->drq[0], (s->stat >> 3) & 1);		/* RRDY */
    if ((s->dma >> 7) & 1)					/* XDMA_EN */
        qemu_set_irq(s->drq[1], (s->stat >> 4) & 1);		/* XRDY */
}

static void omap_i2c_fifo_run(struct omap_i2c_s *s)
{
    int ack = 1;

    if (!i2c_bus_busy(s->bus))
        return;

    if ((s->control >> 2) & 1) {				/* RM */
        if ((s->control >> 1) & 1) {				/* STP */
            i2c_end_transfer(s->bus);
            s->control &= ~(1 << 1);				/* STP */
            s->count_cur = s->count;
            s->txlen = 0;
        } else if ((s->control >> 9) & 1) {			/* TRX */
            while (ack && s->txlen)
                ack = (i2c_send(s->bus,
                                        (s->fifo >> ((-- s->txlen) << 3)) &
                                        0xff) >= 0);
            s->stat |= 1 << 4;					/* XRDY */
        } else {
            while (s->rxlen < 4)
                s->fifo |= i2c_recv(s->bus) << ((s->rxlen ++) << 3);
            s->stat |= 1 << 3;					/* RRDY */
        }
    } else {
        if ((s->control >> 9) & 1) {				/* TRX */
            while (ack && s->count_cur && s->txlen) {
                ack = (i2c_send(s->bus,
                                        (s->fifo >> ((-- s->txlen) << 3)) &
                                        0xff) >= 0);
                s->count_cur --;
            }
            if (ack && s->count_cur)
                s->stat |= 1 << 4;				/* XRDY */
            else
                s->stat &= ~(1 << 4);				/* XRDY */
            if (!s->count_cur) {
                s->stat |= 1 << 2;				/* ARDY */
                s->control &= ~(1 << 10);			/* MST */
            }
        } else {
            while (s->count_cur && s->rxlen < 4) {
                s->fifo |= i2c_recv(s->bus) << ((s->rxlen ++) << 3);
                s->count_cur --;
            }
            if (s->rxlen)
                s->stat |= 1 << 3;				/* RRDY */
            else
                s->stat &= ~(1 << 3);				/* RRDY */
        }
        if (!s->count_cur) {
            if ((s->control >> 1) & 1) {			/* STP */
                i2c_end_transfer(s->bus);
                s->control &= ~(1 << 1);			/* STP */
                s->count_cur = s->count;
                s->txlen = 0;
            } else {
                s->stat |= 1 << 2;				/* ARDY */
                s->control &= ~(1 << 10);			/* MST */
            }
        }
    }

    s->stat |= (!ack) << 1;					/* NACK */
    if (!ack)
        s->control &= ~(1 << 1);				/* STP */
}

void omap_i2c_reset(struct omap_i2c_s *s)
{
    s->mask = 0;
    s->stat = 0;
    s->dma = 0;
    s->count = 0;
    s->count_cur = 0;
    s->fifo = 0;
    s->rxlen = 0;
    s->txlen = 0;
    s->control = 0;
    s->addr[0] = 0;
    s->addr[1] = 0;
    s->divider = 0;
    s->times[0] = 0;
    s->times[1] = 0;
    s->test = 0;
}

static uint32_t omap_i2c_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_i2c_s *s = (struct omap_i2c_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t ret;

    switch (offset) {
    case 0x00:	/* I2C_REV */
        return s->revision;					/* REV */

    case 0x04:	/* I2C_IE */
        return s->mask;

    case 0x08:	/* I2C_STAT */
        return s->stat | (i2c_bus_busy(s->bus) << 12);

    case 0x0c:	/* I2C_IV */
        if (s->revision >= OMAP2_INTR_REV)
            break;
        ret = ffs(s->stat & s->mask);
        if (ret)
            s->stat ^= 1 << (ret - 1);
        omap_i2c_interrupts_update(s);
        return ret;

    case 0x10:	/* I2C_SYSS */
        return (s->control >> 15) & 1;				/* I2C_EN */

    case 0x14:	/* I2C_BUF */
        return s->dma;

    case 0x18:	/* I2C_CNT */
        return s->count_cur;					/* DCOUNT */

    case 0x1c:	/* I2C_DATA */
        ret = 0;
        if (s->control & (1 << 14)) {				/* BE */
            ret |= ((s->fifo >> 0) & 0xff) << 8;
            ret |= ((s->fifo >> 8) & 0xff) << 0;
        } else {
            ret |= ((s->fifo >> 8) & 0xff) << 8;
            ret |= ((s->fifo >> 0) & 0xff) << 0;
        }
        if (s->rxlen == 1) {
            s->stat |= 1 << 15;					/* SBD */
            s->rxlen = 0;
        } else if (s->rxlen > 1) {
            if (s->rxlen > 2)
                s->fifo >>= 16;
            s->rxlen -= 2;
        } else
            /* XXX: remote access (qualifier) error - what's that?  */;
        if (!s->rxlen) {
            s->stat &= ~(1 << 3);				/* RRDY */
            if (((s->control >> 10) & 1) &&			/* MST */
                            ((~s->control >> 9) & 1)) {		/* TRX */
                s->stat |= 1 << 2;				/* ARDY */
                s->control &= ~(1 << 10);			/* MST */
            }
        }
        s->stat &= ~(1 << 11);					/* ROVR */
        omap_i2c_fifo_run(s);
        omap_i2c_interrupts_update(s);
        return ret;

    case 0x20:	/* I2C_SYSC */
        return 0;

    case 0x24:	/* I2C_CON */
        return s->control;

    case 0x28:	/* I2C_OA */
        return s->addr[0];

    case 0x2c:	/* I2C_SA */
        return s->addr[1];

    case 0x30:	/* I2C_PSC */
        return s->divider;

    case 0x34:	/* I2C_SCLL */
        return s->times[0];

    case 0x38:	/* I2C_SCLH */
        return s->times[1];

    case 0x3c:	/* I2C_SYSTEST */
        if (s->test & (1 << 15)) {				/* ST_EN */
            s->test ^= 0xa;
            return s->test;
        } else
            return s->test & ~0x300f;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_i2c_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_i2c_s *s = (struct omap_i2c_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    int nack;

    switch (offset) {
    case 0x00:	/* I2C_REV */
    case 0x0c:	/* I2C_IV */
    case 0x10:	/* I2C_SYSS */
        OMAP_RO_REG(addr);
        return;

    case 0x04:	/* I2C_IE */
        s->mask = value & (s->revision < OMAP2_GC_REV ? 0x1f : 0x3f);
        break;

    case 0x08:	/* I2C_STAT */
        if (s->revision < OMAP2_INTR_REV) {
            OMAP_RO_REG(addr);
            return;
        }

        /* RRDY and XRDY are reset by hardware. (in all versions???) */
        s->stat &= ~(value & 0x27);
        omap_i2c_interrupts_update(s);
        break;

    case 0x14:	/* I2C_BUF */
        s->dma = value & 0x8080;
        if (value & (1 << 15))					/* RDMA_EN */
            s->mask &= ~(1 << 3);				/* RRDY_IE */
        if (value & (1 << 7))					/* XDMA_EN */
            s->mask &= ~(1 << 4);				/* XRDY_IE */
        break;

    case 0x18:	/* I2C_CNT */
        s->count = value;					/* DCOUNT */
        break;

    case 0x1c:	/* I2C_DATA */
        if (s->txlen > 2) {
            /* XXX: remote access (qualifier) error - what's that?  */
            break;
        }
        s->fifo <<= 16;
        s->txlen += 2;
        if (s->control & (1 << 14)) {				/* BE */
            s->fifo |= ((value >> 8) & 0xff) << 8;
            s->fifo |= ((value >> 0) & 0xff) << 0;
        } else {
            s->fifo |= ((value >> 0) & 0xff) << 8;
            s->fifo |= ((value >> 8) & 0xff) << 0;
        }
        s->stat &= ~(1 << 10);					/* XUDF */
        if (s->txlen > 2)
            s->stat &= ~(1 << 4);				/* XRDY */
        omap_i2c_fifo_run(s);
        omap_i2c_interrupts_update(s);
        break;

    case 0x20:	/* I2C_SYSC */
        if (s->revision < OMAP2_INTR_REV) {
            OMAP_BAD_REG(addr);
            return;
        }

        if (value & 2)
            omap_i2c_reset(s);
        break;

    case 0x24:	/* I2C_CON */
        s->control = value & 0xcf87;
        if (~value & (1 << 15)) {				/* I2C_EN */
            if (s->revision < OMAP2_INTR_REV)
                omap_i2c_reset(s);
            break;
        }
        if ((value & (1 << 15)) && !(value & (1 << 10))) {	/* MST */
            fprintf(stderr, "%s: I^2C slave mode not supported\n",
                            __FUNCTION__);
            break;
        }
        if ((value & (1 << 15)) && value & (1 << 8)) {		/* XA */
            fprintf(stderr, "%s: 10-bit addressing mode not supported\n",
                            __FUNCTION__);
            break;
        }
        if ((value & (1 << 15)) && value & (1 << 0)) {		/* STT */
            nack = !!i2c_start_transfer(s->bus, s->addr[1],	/* SA */
                            (~value >> 9) & 1);			/* TRX */
            s->stat |= nack << 1;				/* NACK */
            s->control &= ~(1 << 0);				/* STT */
            s->fifo = 0;
            if (nack)
                s->control &= ~(1 << 1);			/* STP */
            else {
                s->count_cur = s->count;
                omap_i2c_fifo_run(s);
            }
            omap_i2c_interrupts_update(s);
        }
        break;

    case 0x28:	/* I2C_OA */
        s->addr[0] = value & 0x3ff;
        break;

    case 0x2c:	/* I2C_SA */
        s->addr[1] = value & 0x3ff;
        break;

    case 0x30:	/* I2C_PSC */
        s->divider = value;
        break;

    case 0x34:	/* I2C_SCLL */
        s->times[0] = value;
        break;

    case 0x38:	/* I2C_SCLH */
        s->times[1] = value;
        break;

    case 0x3c:	/* I2C_SYSTEST */
        s->test = value & 0xf80f;
        if (value & (1 << 11))					/* SBB */
            if (s->revision >= OMAP2_INTR_REV) {
                s->stat |= 0x3f;
                omap_i2c_interrupts_update(s);
            }
        if (value & (1 << 15))					/* ST_EN */
            fprintf(stderr, "%s: System Test not supported\n", __FUNCTION__);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static void omap_i2c_writeb(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_i2c_s *s = (struct omap_i2c_s *) opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    switch (offset) {
    case 0x1c:	/* I2C_DATA */
        if (s->txlen > 2) {
            /* XXX: remote access (qualifier) error - what's that?  */
            break;
        }
        s->fifo <<= 8;
        s->txlen += 1;
        s->fifo |= value & 0xff;
        s->stat &= ~(1 << 10);					/* XUDF */
        if (s->txlen > 2)
            s->stat &= ~(1 << 4);				/* XRDY */
        omap_i2c_fifo_run(s);
        omap_i2c_interrupts_update(s);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static CPUReadMemoryFunc *omap_i2c_readfn[] = {
    omap_badwidth_read16,
    omap_i2c_read,
    omap_badwidth_read16,
};

static CPUWriteMemoryFunc *omap_i2c_writefn[] = {
    omap_i2c_writeb,	/* Only the last fifo write can be 8 bit.  */
    omap_i2c_write,
    omap_badwidth_write16,
};

struct omap_i2c_s *omap_i2c_init(target_phys_addr_t base,
                qemu_irq irq, qemu_irq *dma, omap_clk clk)
{
    int iomemtype;
    struct omap_i2c_s *s = (struct omap_i2c_s *)
            qemu_mallocz(sizeof(struct omap_i2c_s));

    /* TODO: set a value greater or equal to real hardware */
    s->revision = 0x11;
    s->irq = irq;
    s->drq[0] = dma[0];
    s->drq[1] = dma[1];
    s->bus = i2c_init_bus(NULL, "i2c");
    omap_i2c_reset(s);

    iomemtype = cpu_register_io_memory(0, omap_i2c_readfn,
                    omap_i2c_writefn, s);
    cpu_register_physical_memory(base, 0x800, iomemtype);

    return s;
}

struct omap_i2c_s *omap2_i2c_init(struct omap_target_agent_s *ta,
                qemu_irq irq, qemu_irq *dma, omap_clk fclk, omap_clk iclk)
{
    int iomemtype;
    struct omap_i2c_s *s = (struct omap_i2c_s *)
            qemu_mallocz(sizeof(struct omap_i2c_s));

    s->revision = 0x34;
    s->irq = irq;
    s->drq[0] = dma[0];
    s->drq[1] = dma[1];
    s->bus = i2c_init_bus(NULL, "i2c");
    omap_i2c_reset(s);

    iomemtype = l4_register_io_memory(0, omap_i2c_readfn,
                    omap_i2c_writefn, s);
    omap_l4_attach(ta, 0, iomemtype);

    return s;
}

i2c_bus *omap_i2c_bus(struct omap_i2c_s *s)
{
    return s->bus;
}
