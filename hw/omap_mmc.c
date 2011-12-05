/*
 * OMAP on-chip MMC/SD host emulation.
 *
 * Copyright (C) 2006-2007 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw.h"
#include "omap.h"
#include "sd.h"

struct omap_mmc_s {
    qemu_irq irq;
    qemu_irq *dma;
    qemu_irq coverswitch;
    MemoryRegion iomem;
    omap_clk clk;
    SDState *card;
    uint16_t last_cmd;
    uint16_t sdio;
    uint16_t rsp[8];
    uint32_t arg;
    int lines;
    int dw;
    int mode;
    int enable;
    int be;
    int rev;
    uint16_t status;
    uint16_t mask;
    uint8_t cto;
    uint16_t dto;
    int clkdiv;
    uint16_t fifo[32];
    int fifo_start;
    int fifo_len;
    uint16_t blen;
    uint16_t blen_counter;
    uint16_t nblk;
    uint16_t nblk_counter;
    int tx_dma;
    int rx_dma;
    int af_level;
    int ae_level;

    int ddir;
    int transfer;

    int cdet_wakeup;
    int cdet_enable;
    int cdet_state;
    qemu_irq cdet;
};

static void omap_mmc_interrupts_update(struct omap_mmc_s *s)
{
    qemu_set_irq(s->irq, !!(s->status & s->mask));
}

static void omap_mmc_fifolevel_update(struct omap_mmc_s *host)
{
    if (!host->transfer && !host->fifo_len) {
        host->status &= 0xf3ff;
        return;
    }

    if (host->fifo_len > host->af_level && host->ddir) {
        if (host->rx_dma) {
            host->status &= 0xfbff;
            qemu_irq_raise(host->dma[1]);
        } else
            host->status |= 0x0400;
    } else {
        host->status &= 0xfbff;
        qemu_irq_lower(host->dma[1]);
    }

    if (host->fifo_len < host->ae_level && !host->ddir) {
        if (host->tx_dma) {
            host->status &= 0xf7ff;
            qemu_irq_raise(host->dma[0]);
        } else
            host->status |= 0x0800;
    } else {
        qemu_irq_lower(host->dma[0]);
        host->status &= 0xf7ff;
    }
}

typedef enum {
    sd_nore = 0,	/* no response */
    sd_r1,		/* normal response command */
    sd_r2,		/* CID, CSD registers */
    sd_r3,		/* OCR register */
    sd_r6 = 6,		/* Published RCA response */
    sd_r1b = -1,
} sd_rsp_type_t;

static void omap_mmc_command(struct omap_mmc_s *host, int cmd, int dir,
                sd_cmd_type_t type, int busy, sd_rsp_type_t resptype, int init)
{
    uint32_t rspstatus, mask;
    int rsplen, timeout;
    SDRequest request;
    uint8_t response[16];

    if (init && cmd == 0) {
        host->status |= 0x0001;
        return;
    }

    if (resptype == sd_r1 && busy)
        resptype = sd_r1b;

    if (type == sd_adtc) {
        host->fifo_start = 0;
        host->fifo_len = 0;
        host->transfer = 1;
        host->ddir = dir;
    } else
        host->transfer = 0;
    timeout = 0;
    mask = 0;
    rspstatus = 0;

    request.cmd = cmd;
    request.arg = host->arg;
    request.crc = 0; /* FIXME */

    rsplen = sd_do_command(host->card, &request, response);

    /* TODO: validate CRCs */
    switch (resptype) {
    case sd_nore:
        rsplen = 0;
        break;

    case sd_r1:
    case sd_r1b:
        if (rsplen < 4) {
            timeout = 1;
            break;
        }
        rsplen = 4;

        mask = OUT_OF_RANGE | ADDRESS_ERROR | BLOCK_LEN_ERROR |
                ERASE_SEQ_ERROR | ERASE_PARAM | WP_VIOLATION |
                LOCK_UNLOCK_FAILED | COM_CRC_ERROR | ILLEGAL_COMMAND |
                CARD_ECC_FAILED | CC_ERROR | SD_ERROR |
                CID_CSD_OVERWRITE;
        if (host->sdio & (1 << 13))
            mask |= AKE_SEQ_ERROR;
        rspstatus = (response[0] << 24) | (response[1] << 16) |
                (response[2] << 8) | (response[3] << 0);
        break;

    case sd_r2:
        if (rsplen < 16) {
            timeout = 1;
            break;
        }
        rsplen = 16;
        break;

    case sd_r3:
        if (rsplen < 4) {
            timeout = 1;
            break;
        }
        rsplen = 4;

        rspstatus = (response[0] << 24) | (response[1] << 16) |
                (response[2] << 8) | (response[3] << 0);
        if (rspstatus & 0x80000000)
            host->status &= 0xe000;
        else
            host->status |= 0x1000;
        break;

    case sd_r6:
        if (rsplen < 4) {
            timeout = 1;
            break;
        }
        rsplen = 4;

        mask = 0xe000 | AKE_SEQ_ERROR;
        rspstatus = (response[2] << 8) | (response[3] << 0);
    }

    if (rspstatus & mask)
        host->status |= 0x4000;
    else
        host->status &= 0xb000;

    if (rsplen)
        for (rsplen = 0; rsplen < 8; rsplen ++)
            host->rsp[~rsplen & 7] = response[(rsplen << 1) | 1] |
                    (response[(rsplen << 1) | 0] << 8);

    if (timeout)
        host->status |= 0x0080;
    else if (cmd == 12)
        host->status |= 0x0005;	/* Makes it more real */
    else
        host->status |= 0x0001;
}

static void omap_mmc_transfer(struct omap_mmc_s *host)
{
    uint8_t value;

    if (!host->transfer)
        return;

    while (1) {
        if (host->ddir) {
            if (host->fifo_len > host->af_level)
                break;

            value = sd_read_data(host->card);
            host->fifo[(host->fifo_start + host->fifo_len) & 31] = value;
            if (-- host->blen_counter) {
                value = sd_read_data(host->card);
                host->fifo[(host->fifo_start + host->fifo_len) & 31] |=
                        value << 8;
                host->blen_counter --;
            }

            host->fifo_len ++;
        } else {
            if (!host->fifo_len)
                break;

            value = host->fifo[host->fifo_start] & 0xff;
            sd_write_data(host->card, value);
            if (-- host->blen_counter) {
                value = host->fifo[host->fifo_start] >> 8;
                sd_write_data(host->card, value);
                host->blen_counter --;
            }

            host->fifo_start ++;
            host->fifo_len --;
            host->fifo_start &= 31;
        }

        if (host->blen_counter == 0) {
            host->nblk_counter --;
            host->blen_counter = host->blen;

            if (host->nblk_counter == 0) {
                host->nblk_counter = host->nblk;
                host->transfer = 0;
                host->status |= 0x0008;
                break;
            }
        }
    }
}

static void omap_mmc_update(void *opaque)
{
    struct omap_mmc_s *s = opaque;
    omap_mmc_transfer(s);
    omap_mmc_fifolevel_update(s);
    omap_mmc_interrupts_update(s);
}

void omap_mmc_reset(struct omap_mmc_s *host)
{
    host->last_cmd = 0;
    memset(host->rsp, 0, sizeof(host->rsp));
    host->arg = 0;
    host->dw = 0;
    host->mode = 0;
    host->enable = 0;
    host->status = 0;
    host->mask = 0;
    host->cto = 0;
    host->dto = 0;
    host->fifo_len = 0;
    host->blen = 0;
    host->blen_counter = 0;
    host->nblk = 0;
    host->nblk_counter = 0;
    host->tx_dma = 0;
    host->rx_dma = 0;
    host->ae_level = 0x00;
    host->af_level = 0x1f;
    host->transfer = 0;
    host->cdet_wakeup = 0;
    host->cdet_enable = 0;
    qemu_set_irq(host->coverswitch, host->cdet_state);
    host->clkdiv = 0;
}

static uint64_t omap_mmc_read(void *opaque, target_phys_addr_t offset,
                              unsigned size)
{
    uint16_t i;
    struct omap_mmc_s *s = (struct omap_mmc_s *) opaque;

    if (size != 2) {
        return omap_badwidth_read16(opaque, offset);
    }

    switch (offset) {
    case 0x00:	/* MMC_CMD */
        return s->last_cmd;

    case 0x04:	/* MMC_ARGL */
        return s->arg & 0x0000ffff;

    case 0x08:	/* MMC_ARGH */
        return s->arg >> 16;

    case 0x0c:	/* MMC_CON */
        return (s->dw << 15) | (s->mode << 12) | (s->enable << 11) | 
                (s->be << 10) | s->clkdiv;

    case 0x10:	/* MMC_STAT */
        return s->status;

    case 0x14:	/* MMC_IE */
        return s->mask;

    case 0x18:	/* MMC_CTO */
        return s->cto;

    case 0x1c:	/* MMC_DTO */
        return s->dto;

    case 0x20:	/* MMC_DATA */
        /* TODO: support 8-bit access */
        i = s->fifo[s->fifo_start];
        if (s->fifo_len == 0) {
            printf("MMC: FIFO underrun\n");
            return i;
        }
        s->fifo_start ++;
        s->fifo_len --;
        s->fifo_start &= 31;
        omap_mmc_transfer(s);
        omap_mmc_fifolevel_update(s);
        omap_mmc_interrupts_update(s);
        return i;

    case 0x24:	/* MMC_BLEN */
        return s->blen_counter;

    case 0x28:	/* MMC_NBLK */
        return s->nblk_counter;

    case 0x2c:	/* MMC_BUF */
        return (s->rx_dma << 15) | (s->af_level << 8) |
            (s->tx_dma << 7) | s->ae_level;

    case 0x30:	/* MMC_SPI */
        return 0x0000;
    case 0x34:	/* MMC_SDIO */
        return (s->cdet_wakeup << 2) | (s->cdet_enable) | s->sdio;
    case 0x38:	/* MMC_SYST */
        return 0x0000;

    case 0x3c:	/* MMC_REV */
        return s->rev;

    case 0x40:	/* MMC_RSP0 */
    case 0x44:	/* MMC_RSP1 */
    case 0x48:	/* MMC_RSP2 */
    case 0x4c:	/* MMC_RSP3 */
    case 0x50:	/* MMC_RSP4 */
    case 0x54:	/* MMC_RSP5 */
    case 0x58:	/* MMC_RSP6 */
    case 0x5c:	/* MMC_RSP7 */
        return s->rsp[(offset - 0x40) >> 2];

    /* OMAP2-specific */
    case 0x60:	/* MMC_IOSR */
    case 0x64:	/* MMC_SYSC */
        return 0;
    case 0x68:	/* MMC_SYSS */
        return 1;						/* RSTD */
    }

    OMAP_BAD_REG(offset);
    return 0;
}

static void omap_mmc_write(void *opaque, target_phys_addr_t offset,
                           uint64_t value, unsigned size)
{
    int i;
    struct omap_mmc_s *s = (struct omap_mmc_s *) opaque;

    if (size != 2) {
        return omap_badwidth_write16(opaque, offset, value);
    }

    switch (offset) {
    case 0x00:	/* MMC_CMD */
        if (!s->enable)
            break;

        s->last_cmd = value;
        for (i = 0; i < 8; i ++)
            s->rsp[i] = 0x0000;
        omap_mmc_command(s, value & 63, (value >> 15) & 1,
                (sd_cmd_type_t) ((value >> 12) & 3),
                (value >> 11) & 1,
                (sd_rsp_type_t) ((value >> 8) & 7),
                (value >> 7) & 1);
        omap_mmc_update(s);
        break;

    case 0x04:	/* MMC_ARGL */
        s->arg &= 0xffff0000;
        s->arg |= 0x0000ffff & value;
        break;

    case 0x08:	/* MMC_ARGH */
        s->arg &= 0x0000ffff;
        s->arg |= value << 16;
        break;

    case 0x0c:	/* MMC_CON */
        s->dw = (value >> 15) & 1;
        s->mode = (value >> 12) & 3;
        s->enable = (value >> 11) & 1;
        s->be = (value >> 10) & 1;
        s->clkdiv = (value >> 0) & (s->rev >= 2 ? 0x3ff : 0xff);
        if (s->mode != 0)
            printf("SD mode %i unimplemented!\n", s->mode);
        if (s->be != 0)
            printf("SD FIFO byte sex unimplemented!\n");
        if (s->dw != 0 && s->lines < 4)
            printf("4-bit SD bus enabled\n");
        if (!s->enable)
            omap_mmc_reset(s);
        break;

    case 0x10:	/* MMC_STAT */
        s->status &= ~value;
        omap_mmc_interrupts_update(s);
        break;

    case 0x14:	/* MMC_IE */
        s->mask = value & 0x7fff;
        omap_mmc_interrupts_update(s);
        break;

    case 0x18:	/* MMC_CTO */
        s->cto = value & 0xff;
        if (s->cto > 0xfd && s->rev <= 1)
            printf("MMC: CTO of 0xff and 0xfe cannot be used!\n");
        break;

    case 0x1c:	/* MMC_DTO */
        s->dto = value & 0xffff;
        break;

    case 0x20:	/* MMC_DATA */
        /* TODO: support 8-bit access */
        if (s->fifo_len == 32)
            break;
        s->fifo[(s->fifo_start + s->fifo_len) & 31] = value;
        s->fifo_len ++;
        omap_mmc_transfer(s);
        omap_mmc_fifolevel_update(s);
        omap_mmc_interrupts_update(s);
        break;

    case 0x24:	/* MMC_BLEN */
        s->blen = (value & 0x07ff) + 1;
        s->blen_counter = s->blen;
        break;

    case 0x28:	/* MMC_NBLK */
        s->nblk = (value & 0x07ff) + 1;
        s->nblk_counter = s->nblk;
        s->blen_counter = s->blen;
        break;

    case 0x2c:	/* MMC_BUF */
        s->rx_dma = (value >> 15) & 1;
        s->af_level = (value >> 8) & 0x1f;
        s->tx_dma = (value >> 7) & 1;
        s->ae_level = value & 0x1f;

        if (s->rx_dma)
            s->status &= 0xfbff;
        if (s->tx_dma)
            s->status &= 0xf7ff;
        omap_mmc_fifolevel_update(s);
        omap_mmc_interrupts_update(s);
        break;

    /* SPI, SDIO and TEST modes unimplemented */
    case 0x30:	/* MMC_SPI (OMAP1 only) */
        break;
    case 0x34:	/* MMC_SDIO */
        s->sdio = value & (s->rev >= 2 ? 0xfbf3 : 0x2020);
        s->cdet_wakeup = (value >> 9) & 1;
        s->cdet_enable = (value >> 2) & 1;
        break;
    case 0x38:	/* MMC_SYST */
        break;

    case 0x3c:	/* MMC_REV */
    case 0x40:	/* MMC_RSP0 */
    case 0x44:	/* MMC_RSP1 */
    case 0x48:	/* MMC_RSP2 */
    case 0x4c:	/* MMC_RSP3 */
    case 0x50:	/* MMC_RSP4 */
    case 0x54:	/* MMC_RSP5 */
    case 0x58:	/* MMC_RSP6 */
    case 0x5c:	/* MMC_RSP7 */
        OMAP_RO_REG(offset);
        break;

    /* OMAP2-specific */
    case 0x60:	/* MMC_IOSR */
        if (value & 0xf)
            printf("MMC: SDIO bits used!\n");
        break;
    case 0x64:	/* MMC_SYSC */
        if (value & (1 << 2))					/* SRTS */
            omap_mmc_reset(s);
        break;
    case 0x68:	/* MMC_SYSS */
        OMAP_RO_REG(offset);
        break;

    default:
        OMAP_BAD_REG(offset);
    }
}

static const MemoryRegionOps omap_mmc_ops = {
    .read = omap_mmc_read,
    .write = omap_mmc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_mmc_cover_cb(void *opaque, int line, int level)
{
    struct omap_mmc_s *host = (struct omap_mmc_s *) opaque;

    if (!host->cdet_state && level) {
        host->status |= 0x0002;
        omap_mmc_interrupts_update(host);
        if (host->cdet_wakeup) {
            /* TODO: Assert wake-up */
        }
    }

    if (host->cdet_state != level) {
        qemu_set_irq(host->coverswitch, level);
        host->cdet_state = level;
    }
}

struct omap_mmc_s *omap_mmc_init(target_phys_addr_t base,
                MemoryRegion *sysmem,
                BlockDriverState *bd,
                qemu_irq irq, qemu_irq dma[], omap_clk clk)
{
    struct omap_mmc_s *s = (struct omap_mmc_s *)
            g_malloc0(sizeof(struct omap_mmc_s));

    s->irq = irq;
    s->dma = dma;
    s->clk = clk;
    s->lines = 1;	/* TODO: needs to be settable per-board */
    s->rev = 1;

    omap_mmc_reset(s);

    memory_region_init_io(&s->iomem, &omap_mmc_ops, s, "omap.mmc", 0x800);
    memory_region_add_subregion(sysmem, base, &s->iomem);

    /* Instantiate the storage */
    s->card = sd_init(bd, 0);

    return s;
}

struct omap_mmc_s *omap2_mmc_init(struct omap_target_agent_s *ta,
                BlockDriverState *bd, qemu_irq irq, qemu_irq dma[],
                omap_clk fclk, omap_clk iclk)
{
    struct omap_mmc_s *s = (struct omap_mmc_s *)
            g_malloc0(sizeof(struct omap_mmc_s));

    s->irq = irq;
    s->dma = dma;
    s->clk = fclk;
    s->lines = 4;
    s->rev = 2;

    omap_mmc_reset(s);

    memory_region_init_io(&s->iomem, &omap_mmc_ops, s, "omap.mmc",
                          omap_l4_region_size(ta, 0));
    omap_l4_attach(ta, 0, &s->iomem);

    /* Instantiate the storage */
    s->card = sd_init(bd, 0);

    s->cdet = qemu_allocate_irqs(omap_mmc_cover_cb, s, 1)[0];
    sd_set_cb(s->card, NULL, s->cdet);

    return s;
}

void omap_mmc_handlers(struct omap_mmc_s *s, qemu_irq ro, qemu_irq cover)
{
    if (s->cdet) {
        sd_set_cb(s->card, ro, s->cdet);
        s->coverswitch = cover;
        qemu_set_irq(cover, s->cdet_state);
    } else
        sd_set_cb(s->card, ro, cover);
}

void omap_mmc_enable(struct omap_mmc_s *s, int enable)
{
    sd_enable(s->card, enable);
}
