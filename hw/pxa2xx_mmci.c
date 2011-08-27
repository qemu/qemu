/*
 * Intel XScale PXA255/270 MultiMediaCard/SD/SDIO Controller emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPLv2.
 */

#include "hw.h"
#include "pxa.h"
#include "sd.h"
#include "qdev.h"

struct PXA2xxMMCIState {
    qemu_irq irq;
    qemu_irq rx_dma;
    qemu_irq tx_dma;

    SDState *card;

    uint32_t status;
    uint32_t clkrt;
    uint32_t spi;
    uint32_t cmdat;
    uint32_t resp_tout;
    uint32_t read_tout;
    int blklen;
    int numblk;
    uint32_t intmask;
    uint32_t intreq;
    int cmd;
    uint32_t arg;

    int active;
    int bytesleft;
    uint8_t tx_fifo[64];
    int tx_start;
    int tx_len;
    uint8_t rx_fifo[32];
    int rx_start;
    int rx_len;
    uint16_t resp_fifo[9];
    int resp_len;

    int cmdreq;
    int ac_width;
};

#define MMC_STRPCL	0x00	/* MMC Clock Start/Stop register */
#define MMC_STAT	0x04	/* MMC Status register */
#define MMC_CLKRT	0x08	/* MMC Clock Rate register */
#define MMC_SPI		0x0c	/* MMC SPI Mode register */
#define MMC_CMDAT	0x10	/* MMC Command/Data register */
#define MMC_RESTO	0x14	/* MMC Response Time-Out register */
#define MMC_RDTO	0x18	/* MMC Read Time-Out register */
#define MMC_BLKLEN	0x1c	/* MMC Block Length register */
#define MMC_NUMBLK	0x20	/* MMC Number of Blocks register */
#define MMC_PRTBUF	0x24	/* MMC Buffer Partly Full register */
#define MMC_I_MASK	0x28	/* MMC Interrupt Mask register */
#define MMC_I_REG	0x2c	/* MMC Interrupt Request register */
#define MMC_CMD		0x30	/* MMC Command register */
#define MMC_ARGH	0x34	/* MMC Argument High register */
#define MMC_ARGL	0x38	/* MMC Argument Low register */
#define MMC_RES		0x3c	/* MMC Response FIFO */
#define MMC_RXFIFO	0x40	/* MMC Receive FIFO */
#define MMC_TXFIFO	0x44	/* MMC Transmit FIFO */
#define MMC_RDWAIT	0x48	/* MMC RD_WAIT register */
#define MMC_BLKS_REM	0x4c	/* MMC Blocks Remaining register */

/* Bitfield masks */
#define STRPCL_STOP_CLK	(1 << 0)
#define STRPCL_STRT_CLK	(1 << 1)
#define STAT_TOUT_RES	(1 << 1)
#define STAT_CLK_EN	(1 << 8)
#define STAT_DATA_DONE	(1 << 11)
#define STAT_PRG_DONE	(1 << 12)
#define STAT_END_CMDRES	(1 << 13)
#define SPI_SPI_MODE	(1 << 0)
#define CMDAT_RES_TYPE	(3 << 0)
#define CMDAT_DATA_EN	(1 << 2)
#define CMDAT_WR_RD	(1 << 3)
#define CMDAT_DMA_EN	(1 << 7)
#define CMDAT_STOP_TRAN	(1 << 10)
#define INT_DATA_DONE	(1 << 0)
#define INT_PRG_DONE	(1 << 1)
#define INT_END_CMD	(1 << 2)
#define INT_STOP_CMD	(1 << 3)
#define INT_CLK_OFF	(1 << 4)
#define INT_RXFIFO_REQ	(1 << 5)
#define INT_TXFIFO_REQ	(1 << 6)
#define INT_TINT	(1 << 7)
#define INT_DAT_ERR	(1 << 8)
#define INT_RES_ERR	(1 << 9)
#define INT_RD_STALLED	(1 << 10)
#define INT_SDIO_INT	(1 << 11)
#define INT_SDIO_SACK	(1 << 12)
#define PRTBUF_PRT_BUF	(1 << 0)

/* Route internal interrupt lines to the global IC and DMA */
static void pxa2xx_mmci_int_update(PXA2xxMMCIState *s)
{
    uint32_t mask = s->intmask;
    if (s->cmdat & CMDAT_DMA_EN) {
        mask |= INT_RXFIFO_REQ | INT_TXFIFO_REQ;

        qemu_set_irq(s->rx_dma, !!(s->intreq & INT_RXFIFO_REQ));
        qemu_set_irq(s->tx_dma, !!(s->intreq & INT_TXFIFO_REQ));
    }

    qemu_set_irq(s->irq, !!(s->intreq & ~mask));
}

static void pxa2xx_mmci_fifo_update(PXA2xxMMCIState *s)
{
    if (!s->active)
        return;

    if (s->cmdat & CMDAT_WR_RD) {
        while (s->bytesleft && s->tx_len) {
            sd_write_data(s->card, s->tx_fifo[s->tx_start ++]);
            s->tx_start &= 0x1f;
            s->tx_len --;
            s->bytesleft --;
        }
        if (s->bytesleft)
            s->intreq |= INT_TXFIFO_REQ;
    } else
        while (s->bytesleft && s->rx_len < 32) {
            s->rx_fifo[(s->rx_start + (s->rx_len ++)) & 0x1f] =
                sd_read_data(s->card);
            s->bytesleft --;
            s->intreq |= INT_RXFIFO_REQ;
        }

    if (!s->bytesleft) {
        s->active = 0;
        s->intreq |= INT_DATA_DONE;
        s->status |= STAT_DATA_DONE;

        if (s->cmdat & CMDAT_WR_RD) {
            s->intreq |= INT_PRG_DONE;
            s->status |= STAT_PRG_DONE;
        }
    }

    pxa2xx_mmci_int_update(s);
}

static void pxa2xx_mmci_wakequeues(PXA2xxMMCIState *s)
{
    int rsplen, i;
    SDRequest request;
    uint8_t response[16];

    s->active = 1;
    s->rx_len = 0;
    s->tx_len = 0;
    s->cmdreq = 0;

    request.cmd = s->cmd;
    request.arg = s->arg;
    request.crc = 0;	/* FIXME */

    rsplen = sd_do_command(s->card, &request, response);
    s->intreq |= INT_END_CMD;

    memset(s->resp_fifo, 0, sizeof(s->resp_fifo));
    switch (s->cmdat & CMDAT_RES_TYPE) {
#define PXAMMCI_RESP(wd, value0, value1)	\
        s->resp_fifo[(wd) + 0] |= (value0);	\
        s->resp_fifo[(wd) + 1] |= (value1) << 8;
    case 0:	/* No response */
        goto complete;

    case 1:	/* R1, R4, R5 or R6 */
        if (rsplen < 4)
            goto timeout;
        goto complete;

    case 2:	/* R2 */
        if (rsplen < 16)
            goto timeout;
        goto complete;

    case 3:	/* R3 */
        if (rsplen < 4)
            goto timeout;
        goto complete;

    complete:
        for (i = 0; rsplen > 0; i ++, rsplen -= 2) {
            PXAMMCI_RESP(i, response[i * 2], response[i * 2 + 1]);
        }
        s->status |= STAT_END_CMDRES;

        if (!(s->cmdat & CMDAT_DATA_EN))
            s->active = 0;
        else
            s->bytesleft = s->numblk * s->blklen;

        s->resp_len = 0;
        break;

    timeout:
        s->active = 0;
        s->status |= STAT_TOUT_RES;
        break;
    }

    pxa2xx_mmci_fifo_update(s);
}

static uint32_t pxa2xx_mmci_read(void *opaque, target_phys_addr_t offset)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;
    uint32_t ret;

    switch (offset) {
    case MMC_STRPCL:
        return 0;
    case MMC_STAT:
        return s->status;
    case MMC_CLKRT:
        return s->clkrt;
    case MMC_SPI:
        return s->spi;
    case MMC_CMDAT:
        return s->cmdat;
    case MMC_RESTO:
        return s->resp_tout;
    case MMC_RDTO:
        return s->read_tout;
    case MMC_BLKLEN:
        return s->blklen;
    case MMC_NUMBLK:
        return s->numblk;
    case MMC_PRTBUF:
        return 0;
    case MMC_I_MASK:
        return s->intmask;
    case MMC_I_REG:
        return s->intreq;
    case MMC_CMD:
        return s->cmd | 0x40;
    case MMC_ARGH:
        return s->arg >> 16;
    case MMC_ARGL:
        return s->arg & 0xffff;
    case MMC_RES:
        if (s->resp_len < 9)
            return s->resp_fifo[s->resp_len ++];
        return 0;
    case MMC_RXFIFO:
        ret = 0;
        while (s->ac_width -- && s->rx_len) {
            ret |= s->rx_fifo[s->rx_start ++] << (s->ac_width << 3);
            s->rx_start &= 0x1f;
            s->rx_len --;
        }
        s->intreq &= ~INT_RXFIFO_REQ;
        pxa2xx_mmci_fifo_update(s);
        return ret;
    case MMC_RDWAIT:
        return 0;
    case MMC_BLKS_REM:
        return s->numblk;
    default:
        hw_error("%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }

    return 0;
}

static void pxa2xx_mmci_write(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;

    switch (offset) {
    case MMC_STRPCL:
        if (value & STRPCL_STRT_CLK) {
            s->status |= STAT_CLK_EN;
            s->intreq &= ~INT_CLK_OFF;

            if (s->cmdreq && !(s->cmdat & CMDAT_STOP_TRAN)) {
                s->status &= STAT_CLK_EN;
                pxa2xx_mmci_wakequeues(s);
            }
        }

        if (value & STRPCL_STOP_CLK) {
            s->status &= ~STAT_CLK_EN;
            s->intreq |= INT_CLK_OFF;
            s->active = 0;
        }

        pxa2xx_mmci_int_update(s);
        break;

    case MMC_CLKRT:
        s->clkrt = value & 7;
        break;

    case MMC_SPI:
        s->spi = value & 0xf;
        if (value & SPI_SPI_MODE)
            printf("%s: attempted to use card in SPI mode\n", __FUNCTION__);
        break;

    case MMC_CMDAT:
        s->cmdat = value & 0x3dff;
        s->active = 0;
        s->cmdreq = 1;
        if (!(value & CMDAT_STOP_TRAN)) {
            s->status &= STAT_CLK_EN;

            if (s->status & STAT_CLK_EN)
                pxa2xx_mmci_wakequeues(s);
        }

        pxa2xx_mmci_int_update(s);
        break;

    case MMC_RESTO:
        s->resp_tout = value & 0x7f;
        break;

    case MMC_RDTO:
        s->read_tout = value & 0xffff;
        break;

    case MMC_BLKLEN:
        s->blklen = value & 0xfff;
        break;

    case MMC_NUMBLK:
        s->numblk = value & 0xffff;
        break;

    case MMC_PRTBUF:
        if (value & PRTBUF_PRT_BUF) {
            s->tx_start ^= 32;
            s->tx_len = 0;
        }
        pxa2xx_mmci_fifo_update(s);
        break;

    case MMC_I_MASK:
        s->intmask = value & 0x1fff;
        pxa2xx_mmci_int_update(s);
        break;

    case MMC_CMD:
        s->cmd = value & 0x3f;
        break;

    case MMC_ARGH:
        s->arg &= 0x0000ffff;
        s->arg |= value << 16;
        break;

    case MMC_ARGL:
        s->arg &= 0xffff0000;
        s->arg |= value & 0x0000ffff;
        break;

    case MMC_TXFIFO:
        while (s->ac_width -- && s->tx_len < 0x20)
            s->tx_fifo[(s->tx_start + (s->tx_len ++)) & 0x1f] =
                    (value >> (s->ac_width << 3)) & 0xff;
        s->intreq &= ~INT_TXFIFO_REQ;
        pxa2xx_mmci_fifo_update(s);
        break;

    case MMC_RDWAIT:
    case MMC_BLKS_REM:
        break;

    default:
        hw_error("%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }
}

static uint32_t pxa2xx_mmci_readb(void *opaque, target_phys_addr_t offset)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;
    s->ac_width = 1;
    return pxa2xx_mmci_read(opaque, offset);
}

static uint32_t pxa2xx_mmci_readh(void *opaque, target_phys_addr_t offset)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;
    s->ac_width = 2;
    return pxa2xx_mmci_read(opaque, offset);
}

static uint32_t pxa2xx_mmci_readw(void *opaque, target_phys_addr_t offset)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;
    s->ac_width = 4;
    return pxa2xx_mmci_read(opaque, offset);
}

static CPUReadMemoryFunc * const pxa2xx_mmci_readfn[] = {
    pxa2xx_mmci_readb,
    pxa2xx_mmci_readh,
    pxa2xx_mmci_readw
};

static void pxa2xx_mmci_writeb(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;
    s->ac_width = 1;
    pxa2xx_mmci_write(opaque, offset, value);
}

static void pxa2xx_mmci_writeh(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;
    s->ac_width = 2;
    pxa2xx_mmci_write(opaque, offset, value);
}

static void pxa2xx_mmci_writew(void *opaque,
                target_phys_addr_t offset, uint32_t value)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;
    s->ac_width = 4;
    pxa2xx_mmci_write(opaque, offset, value);
}

static CPUWriteMemoryFunc * const pxa2xx_mmci_writefn[] = {
    pxa2xx_mmci_writeb,
    pxa2xx_mmci_writeh,
    pxa2xx_mmci_writew
};

static void pxa2xx_mmci_save(QEMUFile *f, void *opaque)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;
    int i;

    qemu_put_be32s(f, &s->status);
    qemu_put_be32s(f, &s->clkrt);
    qemu_put_be32s(f, &s->spi);
    qemu_put_be32s(f, &s->cmdat);
    qemu_put_be32s(f, &s->resp_tout);
    qemu_put_be32s(f, &s->read_tout);
    qemu_put_be32(f, s->blklen);
    qemu_put_be32(f, s->numblk);
    qemu_put_be32s(f, &s->intmask);
    qemu_put_be32s(f, &s->intreq);
    qemu_put_be32(f, s->cmd);
    qemu_put_be32s(f, &s->arg);
    qemu_put_be32(f, s->cmdreq);
    qemu_put_be32(f, s->active);
    qemu_put_be32(f, s->bytesleft);

    qemu_put_byte(f, s->tx_len);
    for (i = 0; i < s->tx_len; i ++)
        qemu_put_byte(f, s->tx_fifo[(s->tx_start + i) & 63]);

    qemu_put_byte(f, s->rx_len);
    for (i = 0; i < s->rx_len; i ++)
        qemu_put_byte(f, s->rx_fifo[(s->rx_start + i) & 31]);

    qemu_put_byte(f, s->resp_len);
    for (i = s->resp_len; i < 9; i ++)
        qemu_put_be16s(f, &s->resp_fifo[i]);
}

static int pxa2xx_mmci_load(QEMUFile *f, void *opaque, int version_id)
{
    PXA2xxMMCIState *s = (PXA2xxMMCIState *) opaque;
    int i;

    qemu_get_be32s(f, &s->status);
    qemu_get_be32s(f, &s->clkrt);
    qemu_get_be32s(f, &s->spi);
    qemu_get_be32s(f, &s->cmdat);
    qemu_get_be32s(f, &s->resp_tout);
    qemu_get_be32s(f, &s->read_tout);
    s->blklen = qemu_get_be32(f);
    s->numblk = qemu_get_be32(f);
    qemu_get_be32s(f, &s->intmask);
    qemu_get_be32s(f, &s->intreq);
    s->cmd = qemu_get_be32(f);
    qemu_get_be32s(f, &s->arg);
    s->cmdreq = qemu_get_be32(f);
    s->active = qemu_get_be32(f);
    s->bytesleft = qemu_get_be32(f);

    s->tx_len = qemu_get_byte(f);
    s->tx_start = 0;
    if (s->tx_len >= sizeof(s->tx_fifo) || s->tx_len < 0)
        return -EINVAL;
    for (i = 0; i < s->tx_len; i ++)
        s->tx_fifo[i] = qemu_get_byte(f);

    s->rx_len = qemu_get_byte(f);
    s->rx_start = 0;
    if (s->rx_len >= sizeof(s->rx_fifo) || s->rx_len < 0)
        return -EINVAL;
    for (i = 0; i < s->rx_len; i ++)
        s->rx_fifo[i] = qemu_get_byte(f);

    s->resp_len = qemu_get_byte(f);
    if (s->resp_len > 9 || s->resp_len < 0)
        return -EINVAL;
    for (i = s->resp_len; i < 9; i ++)
         qemu_get_be16s(f, &s->resp_fifo[i]);

    return 0;
}

PXA2xxMMCIState *pxa2xx_mmci_init(target_phys_addr_t base,
                BlockDriverState *bd, qemu_irq irq,
                qemu_irq rx_dma, qemu_irq tx_dma)
{
    int iomemtype;
    PXA2xxMMCIState *s;

    s = (PXA2xxMMCIState *) g_malloc0(sizeof(PXA2xxMMCIState));
    s->irq = irq;
    s->rx_dma = rx_dma;
    s->tx_dma = tx_dma;

    iomemtype = cpu_register_io_memory(pxa2xx_mmci_readfn,
                    pxa2xx_mmci_writefn, s, DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base, 0x00100000, iomemtype);

    /* Instantiate the actual storage */
    s->card = sd_init(bd, 0);

    register_savevm(NULL, "pxa2xx_mmci", 0, 0,
                    pxa2xx_mmci_save, pxa2xx_mmci_load, s);

    return s;
}

void pxa2xx_mmci_handlers(PXA2xxMMCIState *s, qemu_irq readonly,
                qemu_irq coverswitch)
{
    sd_set_cb(s->card, readonly, coverswitch);
}
