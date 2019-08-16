/*
 * TI OMAP processor's Multichannel SPI emulation.
 *
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Original code for OMAP2 by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) any later version of the License.
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/arm/omap.h"

/* Multichannel SPI */
struct omap_mcspi_s {
    MemoryRegion iomem;
    qemu_irq irq;
    int chnum;

    uint32_t sysconfig;
    uint32_t systest;
    uint32_t irqst;
    uint32_t irqen;
    uint32_t wken;
    uint32_t control;

    struct omap_mcspi_ch_s {
        qemu_irq txdrq;
        qemu_irq rxdrq;
        uint32_t (*txrx)(void *opaque, uint32_t, int);
        void *opaque;

        uint32_t tx;
        uint32_t rx;

        uint32_t config;
        uint32_t status;
        uint32_t control;
    } ch[4];
};

static inline void omap_mcspi_interrupt_update(struct omap_mcspi_s *s)
{
    qemu_set_irq(s->irq, s->irqst & s->irqen);
}

static inline void omap_mcspi_dmarequest_update(struct omap_mcspi_ch_s *ch)
{
    qemu_set_irq(ch->txdrq,
                    (ch->control & 1) &&		/* EN */
                    (ch->config & (1 << 14)) &&		/* DMAW */
                    (ch->status & (1 << 1)) &&		/* TXS */
                    ((ch->config >> 12) & 3) != 1);	/* TRM */
    qemu_set_irq(ch->rxdrq,
                    (ch->control & 1) &&		/* EN */
                    (ch->config & (1 << 15)) &&		/* DMAW */
                    (ch->status & (1 << 0)) &&		/* RXS */
                    ((ch->config >> 12) & 3) != 2);	/* TRM */
}

static void omap_mcspi_transfer_run(struct omap_mcspi_s *s, int chnum)
{
    struct omap_mcspi_ch_s *ch = s->ch + chnum;

    if (!(ch->control & 1))				/* EN */
        return;
    if ((ch->status & (1 << 0)) &&			/* RXS */
                    ((ch->config >> 12) & 3) != 2 &&	/* TRM */
                    !(ch->config & (1 << 19)))		/* TURBO */
        goto intr_update;
    if ((ch->status & (1 << 1)) &&			/* TXS */
                    ((ch->config >> 12) & 3) != 1)	/* TRM */
        goto intr_update;

    if (!(s->control & 1) ||				/* SINGLE */
                    (ch->config & (1 << 20))) {		/* FORCE */
        if (ch->txrx)
            ch->rx = ch->txrx(ch->opaque, ch->tx,	/* WL */
                            1 + (0x1f & (ch->config >> 7)));
    }

    ch->tx = 0;
    ch->status |= 1 << 2;				/* EOT */
    ch->status |= 1 << 1;				/* TXS */
    if (((ch->config >> 12) & 3) != 2)			/* TRM */
        ch->status |= 1 << 0;				/* RXS */

intr_update:
    if ((ch->status & (1 << 0)) &&			/* RXS */
                    ((ch->config >> 12) & 3) != 2 &&	/* TRM */
                    !(ch->config & (1 << 19)))		/* TURBO */
        s->irqst |= 1 << (2 + 4 * chnum);		/* RX_FULL */
    if ((ch->status & (1 << 1)) &&			/* TXS */
                    ((ch->config >> 12) & 3) != 1)	/* TRM */
        s->irqst |= 1 << (0 + 4 * chnum);		/* TX_EMPTY */
    omap_mcspi_interrupt_update(s);
    omap_mcspi_dmarequest_update(ch);
}

void omap_mcspi_reset(struct omap_mcspi_s *s)
{
    int ch;

    s->sysconfig = 0;
    s->systest = 0;
    s->irqst = 0;
    s->irqen = 0;
    s->wken = 0;
    s->control = 4;

    for (ch = 0; ch < 4; ch ++) {
        s->ch[ch].config = 0x060000;
        s->ch[ch].status = 2;				/* TXS */
        s->ch[ch].control = 0;

        omap_mcspi_dmarequest_update(s->ch + ch);
    }

    omap_mcspi_interrupt_update(s);
}

static uint64_t omap_mcspi_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    struct omap_mcspi_s *s = (struct omap_mcspi_s *) opaque;
    int ch = 0;
    uint32_t ret;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (addr) {
    case 0x00:	/* MCSPI_REVISION */
        return 0x91;

    case 0x10:	/* MCSPI_SYSCONFIG */
        return s->sysconfig;

    case 0x14:	/* MCSPI_SYSSTATUS */
        return 1;					/* RESETDONE */

    case 0x18:	/* MCSPI_IRQSTATUS */
        return s->irqst;

    case 0x1c:	/* MCSPI_IRQENABLE */
        return s->irqen;

    case 0x20:	/* MCSPI_WAKEUPENABLE */
        return s->wken;

    case 0x24:	/* MCSPI_SYST */
        return s->systest;

    case 0x28:	/* MCSPI_MODULCTRL */
        return s->control;

    case 0x68: ch ++;
        /* fall through */
    case 0x54: ch ++;
        /* fall through */
    case 0x40: ch ++;
        /* fall through */
    case 0x2c:	/* MCSPI_CHCONF */
        return s->ch[ch].config;

    case 0x6c: ch ++;
        /* fall through */
    case 0x58: ch ++;
        /* fall through */
    case 0x44: ch ++;
        /* fall through */
    case 0x30:	/* MCSPI_CHSTAT */
        return s->ch[ch].status;

    case 0x70: ch ++;
        /* fall through */
    case 0x5c: ch ++;
        /* fall through */
    case 0x48: ch ++;
        /* fall through */
    case 0x34:	/* MCSPI_CHCTRL */
        return s->ch[ch].control;

    case 0x74: ch ++;
        /* fall through */
    case 0x60: ch ++;
        /* fall through */
    case 0x4c: ch ++;
        /* fall through */
    case 0x38:	/* MCSPI_TX */
        return s->ch[ch].tx;

    case 0x78: ch ++;
        /* fall through */
    case 0x64: ch ++;
        /* fall through */
    case 0x50: ch ++;
        /* fall through */
    case 0x3c:	/* MCSPI_RX */
        s->ch[ch].status &= ~(1 << 0);			/* RXS */
        ret = s->ch[ch].rx;
        omap_mcspi_transfer_run(s, ch);
        return ret;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_mcspi_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    struct omap_mcspi_s *s = (struct omap_mcspi_s *) opaque;
    int ch = 0;

    if (size != 4) {
        omap_badwidth_write32(opaque, addr, value);
        return;
    }

    switch (addr) {
    case 0x00:	/* MCSPI_REVISION */
    case 0x14:	/* MCSPI_SYSSTATUS */
    case 0x30:	/* MCSPI_CHSTAT0 */
    case 0x3c:	/* MCSPI_RX0 */
    case 0x44:	/* MCSPI_CHSTAT1 */
    case 0x50:	/* MCSPI_RX1 */
    case 0x58:	/* MCSPI_CHSTAT2 */
    case 0x64:	/* MCSPI_RX2 */
    case 0x6c:	/* MCSPI_CHSTAT3 */
    case 0x78:	/* MCSPI_RX3 */
        OMAP_RO_REG(addr);
        return;

    case 0x10:	/* MCSPI_SYSCONFIG */
        if (value & (1 << 1))				/* SOFTRESET */
            omap_mcspi_reset(s);
        s->sysconfig = value & 0x31d;
        break;

    case 0x18:	/* MCSPI_IRQSTATUS */
        if (!((s->control & (1 << 3)) && (s->systest & (1 << 11)))) {
            s->irqst &= ~value;
            omap_mcspi_interrupt_update(s);
        }
        break;

    case 0x1c:	/* MCSPI_IRQENABLE */
        s->irqen = value & 0x1777f;
        omap_mcspi_interrupt_update(s);
        break;

    case 0x20:	/* MCSPI_WAKEUPENABLE */
        s->wken = value & 1;
        break;

    case 0x24:	/* MCSPI_SYST */
        if (s->control & (1 << 3))			/* SYSTEM_TEST */
            if (value & (1 << 11)) {			/* SSB */
                s->irqst |= 0x1777f;
                omap_mcspi_interrupt_update(s);
            }
        s->systest = value & 0xfff;
        break;

    case 0x28:	/* MCSPI_MODULCTRL */
        if (value & (1 << 3))				/* SYSTEM_TEST */
            if (s->systest & (1 << 11)) {		/* SSB */
                s->irqst |= 0x1777f;
                omap_mcspi_interrupt_update(s);
            }
        s->control = value & 0xf;
        break;

    case 0x68: ch ++;
        /* fall through */
    case 0x54: ch ++;
        /* fall through */
    case 0x40: ch ++;
        /* fall through */
    case 0x2c:	/* MCSPI_CHCONF */
        if ((value ^ s->ch[ch].config) & (3 << 14))	/* DMAR | DMAW */
            omap_mcspi_dmarequest_update(s->ch + ch);
        if (((value >> 12) & 3) == 3) { /* TRM */
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid TRM value (3)\n",
                          __func__);
        }
        if (((value >> 7) & 0x1f) < 3) { /* WL */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid WL value (%" PRIx64 ")\n",
                          __func__, (value >> 7) & 0x1f);
        }
        s->ch[ch].config = value & 0x7fffff;
        break;

    case 0x70: ch ++;
        /* fall through */
    case 0x5c: ch ++;
        /* fall through */
    case 0x48: ch ++;
        /* fall through */
    case 0x34:	/* MCSPI_CHCTRL */
        if (value & ~s->ch[ch].control & 1) {		/* EN */
            s->ch[ch].control |= 1;
            omap_mcspi_transfer_run(s, ch);
        } else
            s->ch[ch].control = value & 1;
        break;

    case 0x74: ch ++;
        /* fall through */
    case 0x60: ch ++;
        /* fall through */
    case 0x4c: ch ++;
        /* fall through */
    case 0x38:	/* MCSPI_TX */
        s->ch[ch].tx = value;
        s->ch[ch].status &= ~(1 << 1);			/* TXS */
        omap_mcspi_transfer_run(s, ch);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_mcspi_ops = {
    .read = omap_mcspi_read,
    .write = omap_mcspi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

struct omap_mcspi_s *omap_mcspi_init(struct omap_target_agent_s *ta, int chnum,
                qemu_irq irq, qemu_irq *drq, omap_clk fclk, omap_clk iclk)
{
    struct omap_mcspi_s *s = g_new0(struct omap_mcspi_s, 1);
    struct omap_mcspi_ch_s *ch = s->ch;

    s->irq = irq;
    s->chnum = chnum;
    while (chnum --) {
        ch->txdrq = *drq ++;
        ch->rxdrq = *drq ++;
        ch ++;
    }
    omap_mcspi_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_mcspi_ops, s, "omap.mcspi",
                          omap_l4_region_size(ta, 0));
    omap_l4_attach(ta, 0, &s->iomem);

    return s;
}

void omap_mcspi_attach(struct omap_mcspi_s *s,
                uint32_t (*txrx)(void *opaque, uint32_t, int), void *opaque,
                int chipselect)
{
    if (chipselect < 0 || chipselect >= s->chnum)
        hw_error("%s: Bad chipselect %i\n", __func__, chipselect);

    s->ch[chipselect].txrx = txrx;
    s->ch[chipselect].opaque = opaque;
}
