/*
 * TI OMAP processors emulation.
 *
 * Copyright (C) 2007-2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "sysemu/blockdev.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/arm/boot.h"
#include "hw/arm/omap.h"
#include "sysemu/sysemu.h"
#include "qemu/timer.h"
#include "chardev/char-fe.h"
#include "hw/block/flash.h"
#include "hw/arm/soc_dma.h"
#include "hw/sysbus.h"
#include "audio/audio.h"

/* Enhanced Audio Controller (CODEC only) */
struct omap_eac_s {
    qemu_irq irq;
    MemoryRegion iomem;

    uint16_t sysconfig;
    uint8_t config[4];
    uint8_t control;
    uint8_t address;
    uint16_t data;
    uint8_t vtol;
    uint8_t vtsl;
    uint16_t mixer;
    uint16_t gain[4];
    uint8_t att;
    uint16_t max[7];

    struct {
        qemu_irq txdrq;
        qemu_irq rxdrq;
        uint32_t (*txrx)(void *opaque, uint32_t, int);
        void *opaque;

#define EAC_BUF_LEN 1024
        uint32_t rxbuf[EAC_BUF_LEN];
        int rxoff;
        int rxlen;
        int rxavail;
        uint32_t txbuf[EAC_BUF_LEN];
        int txlen;
        int txavail;

        int enable;
        int rate;

        uint16_t config[4];

        /* These need to be moved to the actual codec */
        QEMUSoundCard card;
        SWVoiceIn *in_voice;
        SWVoiceOut *out_voice;
        int hw_enable;
    } codec;

    struct {
        uint8_t control;
        uint16_t config;
    } modem, bt;
};

static inline void omap_eac_interrupt_update(struct omap_eac_s *s)
{
    qemu_set_irq(s->irq, (s->codec.config[1] >> 14) & 1);	/* AURDI */
}

static inline void omap_eac_in_dmarequest_update(struct omap_eac_s *s)
{
    qemu_set_irq(s->codec.rxdrq, (s->codec.rxavail || s->codec.rxlen) &&
                    ((s->codec.config[1] >> 12) & 1));		/* DMAREN */
}

static inline void omap_eac_out_dmarequest_update(struct omap_eac_s *s)
{
    qemu_set_irq(s->codec.txdrq, s->codec.txlen < s->codec.txavail &&
                    ((s->codec.config[1] >> 11) & 1));		/* DMAWEN */
}

static inline void omap_eac_in_refill(struct omap_eac_s *s)
{
    int left = MIN(EAC_BUF_LEN - s->codec.rxlen, s->codec.rxavail) << 2;
    int start = ((s->codec.rxoff + s->codec.rxlen) & (EAC_BUF_LEN - 1)) << 2;
    int leftwrap = MIN(left, (EAC_BUF_LEN << 2) - start);
    int recv = 1;
    uint8_t *buf = (uint8_t *) s->codec.rxbuf + start;

    left -= leftwrap;
    start = 0;
    while (leftwrap && (recv = AUD_read(s->codec.in_voice, buf + start,
                                    leftwrap)) > 0) {	/* Be defensive */
        start += recv;
        leftwrap -= recv;
    }
    if (recv <= 0)
        s->codec.rxavail = 0;
    else
        s->codec.rxavail -= start >> 2;
    s->codec.rxlen += start >> 2;

    if (recv > 0 && left > 0) {
        start = 0;
        while (left && (recv = AUD_read(s->codec.in_voice,
                                        (uint8_t *) s->codec.rxbuf + start,
                                        left)) > 0) {	/* Be defensive */
            start += recv;
            left -= recv;
        }
        if (recv <= 0)
            s->codec.rxavail = 0;
        else
            s->codec.rxavail -= start >> 2;
        s->codec.rxlen += start >> 2;
    }
}

static inline void omap_eac_out_empty(struct omap_eac_s *s)
{
    int left = s->codec.txlen << 2;
    int start = 0;
    int sent = 1;

    while (left && (sent = AUD_write(s->codec.out_voice,
                                    (uint8_t *) s->codec.txbuf + start,
                                    left)) > 0) {	/* Be defensive */
        start += sent;
        left -= sent;
    }

    if (!sent) {
        s->codec.txavail = 0;
        omap_eac_out_dmarequest_update(s);
    }

    if (start)
        s->codec.txlen = 0;
}

static void omap_eac_in_cb(void *opaque, int avail_b)
{
    struct omap_eac_s *s = opaque;

    s->codec.rxavail = avail_b >> 2;
    omap_eac_in_refill(s);
    /* TODO: possibly discard current buffer if overrun */
    omap_eac_in_dmarequest_update(s);
}

static void omap_eac_out_cb(void *opaque, int free_b)
{
    struct omap_eac_s *s = opaque;

    s->codec.txavail = free_b >> 2;
    if (s->codec.txlen)
        omap_eac_out_empty(s);
    else
        omap_eac_out_dmarequest_update(s);
}

static void omap_eac_enable_update(struct omap_eac_s *s)
{
    s->codec.enable = !(s->codec.config[1] & 1) &&		/* EACPWD */
            (s->codec.config[1] & 2) &&				/* AUDEN */
            s->codec.hw_enable;
}

static const int omap_eac_fsint[4] = {
    8000,
    11025,
    22050,
    44100,
};

static const int omap_eac_fsint2[8] = {
    8000,
    11025,
    22050,
    44100,
    48000,
    0, 0, 0,
};

static const int omap_eac_fsint3[16] = {
    8000,
    11025,
    16000,
    22050,
    24000,
    32000,
    44100,
    48000,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static void omap_eac_rate_update(struct omap_eac_s *s)
{
    int fsint[3];

    fsint[2] = (s->codec.config[3] >> 9) & 0xf;
    fsint[1] = (s->codec.config[2] >> 0) & 0x7;
    fsint[0] = (s->codec.config[0] >> 6) & 0x3;
    if (fsint[2] < 0xf)
        s->codec.rate = omap_eac_fsint3[fsint[2]];
    else if (fsint[1] < 0x7)
        s->codec.rate = omap_eac_fsint2[fsint[1]];
    else
        s->codec.rate = omap_eac_fsint[fsint[0]];
}

static void omap_eac_volume_update(struct omap_eac_s *s)
{
    /* TODO */
}

static void omap_eac_format_update(struct omap_eac_s *s)
{
    struct audsettings fmt;

    /* The hardware buffers at most one sample */
    if (s->codec.rxlen)
        s->codec.rxlen = 1;

    if (s->codec.in_voice) {
        AUD_set_active_in(s->codec.in_voice, 0);
        AUD_close_in(&s->codec.card, s->codec.in_voice);
        s->codec.in_voice = NULL;
    }
    if (s->codec.out_voice) {
        omap_eac_out_empty(s);
        AUD_set_active_out(s->codec.out_voice, 0);
        AUD_close_out(&s->codec.card, s->codec.out_voice);
        s->codec.out_voice = NULL;
        s->codec.txavail = 0;
    }
    /* Discard what couldn't be written */
    s->codec.txlen = 0;

    omap_eac_enable_update(s);
    if (!s->codec.enable)
        return;

    omap_eac_rate_update(s);
    fmt.endianness = ((s->codec.config[0] >> 8) & 1);		/* LI_BI */
    fmt.nchannels = ((s->codec.config[0] >> 10) & 1) ? 2 : 1;	/* MN_ST */
    fmt.freq = s->codec.rate;
    /* TODO: signedness possibly depends on the CODEC hardware - or
     * does I2S specify it?  */
    /* All register writes are 16 bits so we store 16-bit samples
     * in the buffers regardless of AGCFR[B8_16] value.  */
    fmt.fmt = AUDIO_FORMAT_U16;

    s->codec.in_voice = AUD_open_in(&s->codec.card, s->codec.in_voice,
                    "eac.codec.in", s, omap_eac_in_cb, &fmt);
    s->codec.out_voice = AUD_open_out(&s->codec.card, s->codec.out_voice,
                    "eac.codec.out", s, omap_eac_out_cb, &fmt);

    omap_eac_volume_update(s);

    AUD_set_active_in(s->codec.in_voice, 1);
    AUD_set_active_out(s->codec.out_voice, 1);
}

static void omap_eac_reset(struct omap_eac_s *s)
{
    s->sysconfig = 0;
    s->config[0] = 0x0c;
    s->config[1] = 0x09;
    s->config[2] = 0xab;
    s->config[3] = 0x03;
    s->control = 0x00;
    s->address = 0x00;
    s->data = 0x0000;
    s->vtol = 0x00;
    s->vtsl = 0x00;
    s->mixer = 0x0000;
    s->gain[0] = 0xe7e7;
    s->gain[1] = 0x6767;
    s->gain[2] = 0x6767;
    s->gain[3] = 0x6767;
    s->att = 0xce;
    s->max[0] = 0;
    s->max[1] = 0;
    s->max[2] = 0;
    s->max[3] = 0;
    s->max[4] = 0;
    s->max[5] = 0;
    s->max[6] = 0;

    s->modem.control = 0x00;
    s->modem.config = 0x0000;
    s->bt.control = 0x00;
    s->bt.config = 0x0000;
    s->codec.config[0] = 0x0649;
    s->codec.config[1] = 0x0000;
    s->codec.config[2] = 0x0007;
    s->codec.config[3] = 0x1ffc;
    s->codec.rxoff = 0;
    s->codec.rxlen = 0;
    s->codec.txlen = 0;
    s->codec.rxavail = 0;
    s->codec.txavail = 0;

    omap_eac_format_update(s);
    omap_eac_interrupt_update(s);
}

static uint64_t omap_eac_read(void *opaque, hwaddr addr, unsigned size)
{
    struct omap_eac_s *s = opaque;
    uint32_t ret;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (addr) {
    case 0x000:	/* CPCFR1 */
        return s->config[0];
    case 0x004:	/* CPCFR2 */
        return s->config[1];
    case 0x008:	/* CPCFR3 */
        return s->config[2];
    case 0x00c:	/* CPCFR4 */
        return s->config[3];

    case 0x010:	/* CPTCTL */
        return s->control | ((s->codec.rxavail + s->codec.rxlen > 0) << 7) |
                ((s->codec.txlen < s->codec.txavail) << 5);

    case 0x014:	/* CPTTADR */
        return s->address;
    case 0x018:	/* CPTDATL */
        return s->data & 0xff;
    case 0x01c:	/* CPTDATH */
        return s->data >> 8;
    case 0x020:	/* CPTVSLL */
        return s->vtol;
    case 0x024:	/* CPTVSLH */
        return s->vtsl | (3 << 5);	/* CRDY1 | CRDY2 */
    case 0x040:	/* MPCTR */
        return s->modem.control;
    case 0x044:	/* MPMCCFR */
        return s->modem.config;
    case 0x060:	/* BPCTR */
        return s->bt.control;
    case 0x064:	/* BPMCCFR */
        return s->bt.config;
    case 0x080:	/* AMSCFR */
        return s->mixer;
    case 0x084:	/* AMVCTR */
        return s->gain[0];
    case 0x088:	/* AM1VCTR */
        return s->gain[1];
    case 0x08c:	/* AM2VCTR */
        return s->gain[2];
    case 0x090:	/* AM3VCTR */
        return s->gain[3];
    case 0x094:	/* ASTCTR */
        return s->att;
    case 0x098:	/* APD1LCR */
        return s->max[0];
    case 0x09c:	/* APD1RCR */
        return s->max[1];
    case 0x0a0:	/* APD2LCR */
        return s->max[2];
    case 0x0a4:	/* APD2RCR */
        return s->max[3];
    case 0x0a8:	/* APD3LCR */
        return s->max[4];
    case 0x0ac:	/* APD3RCR */
        return s->max[5];
    case 0x0b0:	/* APD4R */
        return s->max[6];
    case 0x0b4:	/* ADWR */
        /* This should be write-only?  Docs list it as read-only.  */
        return 0x0000;
    case 0x0b8:	/* ADRDR */
        if (likely(s->codec.rxlen > 1)) {
            ret = s->codec.rxbuf[s->codec.rxoff ++];
            s->codec.rxlen --;
            s->codec.rxoff &= EAC_BUF_LEN - 1;
            return ret;
        } else if (s->codec.rxlen) {
            ret = s->codec.rxbuf[s->codec.rxoff ++];
            s->codec.rxlen --;
            s->codec.rxoff &= EAC_BUF_LEN - 1;
            if (s->codec.rxavail)
                omap_eac_in_refill(s);
            omap_eac_in_dmarequest_update(s);
            return ret;
        }
        return 0x0000;
    case 0x0bc:	/* AGCFR */
        return s->codec.config[0];
    case 0x0c0:	/* AGCTR */
        return s->codec.config[1] | ((s->codec.config[1] & 2) << 14);
    case 0x0c4:	/* AGCFR2 */
        return s->codec.config[2];
    case 0x0c8:	/* AGCFR3 */
        return s->codec.config[3];
    case 0x0cc:	/* MBPDMACTR */
    case 0x0d0:	/* MPDDMARR */
    case 0x0d8:	/* MPUDMARR */
    case 0x0e4:	/* BPDDMARR */
    case 0x0ec:	/* BPUDMARR */
        return 0x0000;

    case 0x100:	/* VERSION_NUMBER */
        return 0x0010;

    case 0x104:	/* SYSCONFIG */
        return s->sysconfig;

    case 0x108:	/* SYSSTATUS */
        return 1 | 0xe;					/* RESETDONE | stuff */
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_eac_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    struct omap_eac_s *s = opaque;

    if (size != 2) {
        omap_badwidth_write16(opaque, addr, value);
        return;
    }

    switch (addr) {
    case 0x098:	/* APD1LCR */
    case 0x09c:	/* APD1RCR */
    case 0x0a0:	/* APD2LCR */
    case 0x0a4:	/* APD2RCR */
    case 0x0a8:	/* APD3LCR */
    case 0x0ac:	/* APD3RCR */
    case 0x0b0:	/* APD4R */
    case 0x0b8:	/* ADRDR */
    case 0x0d0:	/* MPDDMARR */
    case 0x0d8:	/* MPUDMARR */
    case 0x0e4:	/* BPDDMARR */
    case 0x0ec:	/* BPUDMARR */
    case 0x100:	/* VERSION_NUMBER */
    case 0x108:	/* SYSSTATUS */
        OMAP_RO_REG(addr);
        return;

    case 0x000:	/* CPCFR1 */
        s->config[0] = value & 0xff;
        omap_eac_format_update(s);
        break;
    case 0x004:	/* CPCFR2 */
        s->config[1] = value & 0xff;
        omap_eac_format_update(s);
        break;
    case 0x008:	/* CPCFR3 */
        s->config[2] = value & 0xff;
        omap_eac_format_update(s);
        break;
    case 0x00c:	/* CPCFR4 */
        s->config[3] = value & 0xff;
        omap_eac_format_update(s);
        break;

    case 0x010:	/* CPTCTL */
        /* Assuming TXF and TXE bits are read-only... */
        s->control = value & 0x5f;
        omap_eac_interrupt_update(s);
        break;

    case 0x014:	/* CPTTADR */
        s->address = value & 0xff;
        break;
    case 0x018:	/* CPTDATL */
        s->data &= 0xff00;
        s->data |= value & 0xff;
        break;
    case 0x01c:	/* CPTDATH */
        s->data &= 0x00ff;
        s->data |= value << 8;
        break;
    case 0x020:	/* CPTVSLL */
        s->vtol = value & 0xf8;
        break;
    case 0x024:	/* CPTVSLH */
        s->vtsl = value & 0x9f;
        break;
    case 0x040:	/* MPCTR */
        s->modem.control = value & 0x8f;
        break;
    case 0x044:	/* MPMCCFR */
        s->modem.config = value & 0x7fff;
        break;
    case 0x060:	/* BPCTR */
        s->bt.control = value & 0x8f;
        break;
    case 0x064:	/* BPMCCFR */
        s->bt.config = value & 0x7fff;
        break;
    case 0x080:	/* AMSCFR */
        s->mixer = value & 0x0fff;
        break;
    case 0x084:	/* AMVCTR */
        s->gain[0] = value & 0xffff;
        break;
    case 0x088:	/* AM1VCTR */
        s->gain[1] = value & 0xff7f;
        break;
    case 0x08c:	/* AM2VCTR */
        s->gain[2] = value & 0xff7f;
        break;
    case 0x090:	/* AM3VCTR */
        s->gain[3] = value & 0xff7f;
        break;
    case 0x094:	/* ASTCTR */
        s->att = value & 0xff;
        break;

    case 0x0b4:	/* ADWR */
        s->codec.txbuf[s->codec.txlen ++] = value;
        if (unlikely(s->codec.txlen == EAC_BUF_LEN ||
                                s->codec.txlen == s->codec.txavail)) {
            if (s->codec.txavail)
                omap_eac_out_empty(s);
            /* Discard what couldn't be written */
            s->codec.txlen = 0;
        }
        break;

    case 0x0bc:	/* AGCFR */
        s->codec.config[0] = value & 0x07ff;
        omap_eac_format_update(s);
        break;
    case 0x0c0:	/* AGCTR */
        s->codec.config[1] = value & 0x780f;
        omap_eac_format_update(s);
        break;
    case 0x0c4:	/* AGCFR2 */
        s->codec.config[2] = value & 0x003f;
        omap_eac_format_update(s);
        break;
    case 0x0c8:	/* AGCFR3 */
        s->codec.config[3] = value & 0xffff;
        omap_eac_format_update(s);
        break;
    case 0x0cc:	/* MBPDMACTR */
    case 0x0d4:	/* MPDDMAWR */
    case 0x0e0:	/* MPUDMAWR */
    case 0x0e8:	/* BPDDMAWR */
    case 0x0f0:	/* BPUDMAWR */
        break;

    case 0x104:	/* SYSCONFIG */
        if (value & (1 << 1))				/* SOFTRESET */
            omap_eac_reset(s);
        s->sysconfig = value & 0x31d;
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_eac_ops = {
    .read = omap_eac_read,
    .write = omap_eac_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static struct omap_eac_s *omap_eac_init(struct omap_target_agent_s *ta,
                qemu_irq irq, qemu_irq *drq, omap_clk fclk, omap_clk iclk)
{
    struct omap_eac_s *s = g_new0(struct omap_eac_s, 1);

    s->irq = irq;
    s->codec.rxdrq = *drq ++;
    s->codec.txdrq = *drq;
    omap_eac_reset(s);

    AUD_register_card("OMAP EAC", &s->codec.card);

    memory_region_init_io(&s->iomem, NULL, &omap_eac_ops, s, "omap.eac",
                          omap_l4_region_size(ta, 0));
    omap_l4_attach(ta, 0, &s->iomem);

    return s;
}

/* STI/XTI (emulation interface) console - reverse engineered only */
struct omap_sti_s {
    qemu_irq irq;
    MemoryRegion iomem;
    MemoryRegion iomem_fifo;
    CharBackend chr;

    uint32_t sysconfig;
    uint32_t systest;
    uint32_t irqst;
    uint32_t irqen;
    uint32_t clkcontrol;
    uint32_t serial_config;
};

#define STI_TRACE_CONSOLE_CHANNEL	239
#define STI_TRACE_CONTROL_CHANNEL	253

static inline void omap_sti_interrupt_update(struct omap_sti_s *s)
{
    qemu_set_irq(s->irq, s->irqst & s->irqen);
}

static void omap_sti_reset(struct omap_sti_s *s)
{
    s->sysconfig = 0;
    s->irqst = 0;
    s->irqen = 0;
    s->clkcontrol = 0;
    s->serial_config = 0;

    omap_sti_interrupt_update(s);
}

static uint64_t omap_sti_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    struct omap_sti_s *s = opaque;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (addr) {
    case 0x00:	/* STI_REVISION */
        return 0x10;

    case 0x10:	/* STI_SYSCONFIG */
        return s->sysconfig;

    case 0x14:	/* STI_SYSSTATUS / STI_RX_STATUS / XTI_SYSSTATUS */
        return 0x00;

    case 0x18:	/* STI_IRQSTATUS */
        return s->irqst;

    case 0x1c:	/* STI_IRQSETEN / STI_IRQCLREN */
        return s->irqen;

    case 0x24:	/* STI_ER / STI_DR / XTI_TRACESELECT */
    case 0x28:	/* STI_RX_DR / XTI_RXDATA */
        /* TODO */
        return 0;

    case 0x2c:	/* STI_CLK_CTRL / XTI_SCLKCRTL */
        return s->clkcontrol;

    case 0x30:	/* STI_SERIAL_CFG / XTI_SCONFIG */
        return s->serial_config;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_sti_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    struct omap_sti_s *s = opaque;

    if (size != 4) {
        omap_badwidth_write32(opaque, addr, value);
        return;
    }

    switch (addr) {
    case 0x00:	/* STI_REVISION */
    case 0x14:	/* STI_SYSSTATUS / STI_RX_STATUS / XTI_SYSSTATUS */
        OMAP_RO_REG(addr);
        return;

    case 0x10:	/* STI_SYSCONFIG */
        if (value & (1 << 1))				/* SOFTRESET */
            omap_sti_reset(s);
        s->sysconfig = value & 0xfe;
        break;

    case 0x18:	/* STI_IRQSTATUS */
        s->irqst &= ~value;
        omap_sti_interrupt_update(s);
        break;

    case 0x1c:	/* STI_IRQSETEN / STI_IRQCLREN */
        s->irqen = value & 0xffff;
        omap_sti_interrupt_update(s);
        break;

    case 0x2c:	/* STI_CLK_CTRL / XTI_SCLKCRTL */
        s->clkcontrol = value & 0xff;
        break;

    case 0x30:	/* STI_SERIAL_CFG / XTI_SCONFIG */
        s->serial_config = value & 0xff;
        break;

    case 0x24:	/* STI_ER / STI_DR / XTI_TRACESELECT */
    case 0x28:	/* STI_RX_DR / XTI_RXDATA */
        /* TODO */
        return;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_sti_ops = {
    .read = omap_sti_read,
    .write = omap_sti_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t omap_sti_fifo_read(void *opaque, hwaddr addr, unsigned size)
{
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_sti_fifo_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    struct omap_sti_s *s = opaque;
    int ch = addr >> 6;
    uint8_t byte = value;

    if (size != 1) {
        omap_badwidth_write8(opaque, addr, size);
        return;
    }

    if (ch == STI_TRACE_CONTROL_CHANNEL) {
        /* Flush channel <i>value</i>.  */
        /* XXX this blocks entire thread. Rewrite to use
         * qemu_chr_fe_write and background I/O callbacks */
        qemu_chr_fe_write_all(&s->chr, (const uint8_t *) "\r", 1);
    } else if (ch == STI_TRACE_CONSOLE_CHANNEL || 1) {
        if (value == 0xc0 || value == 0xc3) {
            /* Open channel <i>ch</i>.  */
        } else if (value == 0x00) {
            qemu_chr_fe_write_all(&s->chr, (const uint8_t *) "\n", 1);
        } else {
            qemu_chr_fe_write_all(&s->chr, &byte, 1);
        }
    }
}

static const MemoryRegionOps omap_sti_fifo_ops = {
    .read = omap_sti_fifo_read,
    .write = omap_sti_fifo_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static struct omap_sti_s *omap_sti_init(struct omap_target_agent_s *ta,
                MemoryRegion *sysmem,
                hwaddr channel_base, qemu_irq irq, omap_clk clk,
                Chardev *chr)
{
    struct omap_sti_s *s = g_new0(struct omap_sti_s, 1);

    s->irq = irq;
    omap_sti_reset(s);

    qemu_chr_fe_init(&s->chr, chr ?: qemu_chr_new("null", "null", NULL),
                     &error_abort);

    memory_region_init_io(&s->iomem, NULL, &omap_sti_ops, s, "omap.sti",
                          omap_l4_region_size(ta, 0));
    omap_l4_attach(ta, 0, &s->iomem);

    memory_region_init_io(&s->iomem_fifo, NULL, &omap_sti_fifo_ops, s,
                          "omap.sti.fifo", 0x10000);
    memory_region_add_subregion(sysmem, channel_base, &s->iomem_fifo);

    return s;
}

/* L4 Interconnect */
#define L4TA(n)		(n)
#define L4TAO(n)	((n) + 39)

static const struct omap_l4_region_s omap_l4_region[125] = {
    [  1] = { 0x40800,  0x800, 32          }, /* Initiator agent */
    [  2] = { 0x41000, 0x1000, 32          }, /* Link agent */
    [  0] = { 0x40000,  0x800, 32          }, /* Address and protection */
    [  3] = { 0x00000, 0x1000, 32 | 16 | 8 }, /* System Control and Pinout */
    [  4] = { 0x01000, 0x1000, 32 | 16 | 8 }, /* L4TAO1 */
    [  5] = { 0x04000, 0x1000, 32 | 16     }, /* 32K Timer */
    [  6] = { 0x05000, 0x1000, 32 | 16 | 8 }, /* L4TAO2 */
    [  7] = { 0x08000,  0x800, 32          }, /* PRCM Region A */
    [  8] = { 0x08800,  0x800, 32          }, /* PRCM Region B */
    [  9] = { 0x09000, 0x1000, 32 | 16 | 8 }, /* L4TAO */
    [ 10] = { 0x12000, 0x1000, 32 | 16 | 8 }, /* Test (BCM) */
    [ 11] = { 0x13000, 0x1000, 32 | 16 | 8 }, /* L4TA1 */
    [ 12] = { 0x14000, 0x1000, 32          }, /* Test/emulation (TAP) */
    [ 13] = { 0x15000, 0x1000, 32 | 16 | 8 }, /* L4TA2 */
    [ 14] = { 0x18000, 0x1000, 32 | 16 | 8 }, /* GPIO1 */
    [ 16] = { 0x1a000, 0x1000, 32 | 16 | 8 }, /* GPIO2 */
    [ 18] = { 0x1c000, 0x1000, 32 | 16 | 8 }, /* GPIO3 */
    [ 19] = { 0x1e000, 0x1000, 32 | 16 | 8 }, /* GPIO4 */
    [ 15] = { 0x19000, 0x1000, 32 | 16 | 8 }, /* Quad GPIO TOP */
    [ 17] = { 0x1b000, 0x1000, 32 | 16 | 8 }, /* L4TA3 */
    [ 20] = { 0x20000, 0x1000, 32 | 16 | 8 }, /* WD Timer 1 (Secure) */
    [ 22] = { 0x22000, 0x1000, 32 | 16 | 8 }, /* WD Timer 2 (OMAP) */
    [ 21] = { 0x21000, 0x1000, 32 | 16 | 8 }, /* Dual WD timer TOP */
    [ 23] = { 0x23000, 0x1000, 32 | 16 | 8 }, /* L4TA4 */
    [ 24] = { 0x28000, 0x1000, 32 | 16 | 8 }, /* GP Timer 1 */
    [ 25] = { 0x29000, 0x1000, 32 | 16 | 8 }, /* L4TA7 */
    [ 26] = { 0x48000, 0x2000, 32 | 16 | 8 }, /* Emulation (ARM11ETB) */
    [ 27] = { 0x4a000, 0x1000, 32 | 16 | 8 }, /* L4TA9 */
    [ 28] = { 0x50000,  0x400, 32 | 16 | 8 }, /* Display top */
    [ 29] = { 0x50400,  0x400, 32 | 16 | 8 }, /* Display control */
    [ 30] = { 0x50800,  0x400, 32 | 16 | 8 }, /* Display RFBI */
    [ 31] = { 0x50c00,  0x400, 32 | 16 | 8 }, /* Display encoder */
    [ 32] = { 0x51000, 0x1000, 32 | 16 | 8 }, /* L4TA10 */
    [ 33] = { 0x52000,  0x400, 32 | 16 | 8 }, /* Camera top */
    [ 34] = { 0x52400,  0x400, 32 | 16 | 8 }, /* Camera core */
    [ 35] = { 0x52800,  0x400, 32 | 16 | 8 }, /* Camera DMA */
    [ 36] = { 0x52c00,  0x400, 32 | 16 | 8 }, /* Camera MMU */
    [ 37] = { 0x53000, 0x1000, 32 | 16 | 8 }, /* L4TA11 */
    [ 38] = { 0x56000, 0x1000, 32 | 16 | 8 }, /* sDMA */
    [ 39] = { 0x57000, 0x1000, 32 | 16 | 8 }, /* L4TA12 */
    [ 40] = { 0x58000, 0x1000, 32 | 16 | 8 }, /* SSI top */
    [ 41] = { 0x59000, 0x1000, 32 | 16 | 8 }, /* SSI GDD */
    [ 42] = { 0x5a000, 0x1000, 32 | 16 | 8 }, /* SSI Port1 */
    [ 43] = { 0x5b000, 0x1000, 32 | 16 | 8 }, /* SSI Port2 */
    [ 44] = { 0x5c000, 0x1000, 32 | 16 | 8 }, /* L4TA13 */
    [ 45] = { 0x5e000, 0x1000, 32 | 16 | 8 }, /* USB OTG */
    [ 46] = { 0x5f000, 0x1000, 32 | 16 | 8 }, /* L4TAO4 */
    [ 47] = { 0x60000, 0x1000, 32 | 16 | 8 }, /* Emulation (WIN_TRACER1SDRC) */
    [ 48] = { 0x61000, 0x1000, 32 | 16 | 8 }, /* L4TA14 */
    [ 49] = { 0x62000, 0x1000, 32 | 16 | 8 }, /* Emulation (WIN_TRACER2GPMC) */
    [ 50] = { 0x63000, 0x1000, 32 | 16 | 8 }, /* L4TA15 */
    [ 51] = { 0x64000, 0x1000, 32 | 16 | 8 }, /* Emulation (WIN_TRACER3OCM) */
    [ 52] = { 0x65000, 0x1000, 32 | 16 | 8 }, /* L4TA16 */
    [ 53] = { 0x66000,  0x300, 32 | 16 | 8 }, /* Emulation (WIN_TRACER4L4) */
    [ 54] = { 0x67000, 0x1000, 32 | 16 | 8 }, /* L4TA17 */
    [ 55] = { 0x68000, 0x1000, 32 | 16 | 8 }, /* Emulation (XTI) */
    [ 56] = { 0x69000, 0x1000, 32 | 16 | 8 }, /* L4TA18 */
    [ 57] = { 0x6a000, 0x1000,      16 | 8 }, /* UART1 */
    [ 58] = { 0x6b000, 0x1000, 32 | 16 | 8 }, /* L4TA19 */
    [ 59] = { 0x6c000, 0x1000,      16 | 8 }, /* UART2 */
    [ 60] = { 0x6d000, 0x1000, 32 | 16 | 8 }, /* L4TA20 */
    [ 61] = { 0x6e000, 0x1000,      16 | 8 }, /* UART3 */
    [ 62] = { 0x6f000, 0x1000, 32 | 16 | 8 }, /* L4TA21 */
    [ 63] = { 0x70000, 0x1000,      16     }, /* I2C1 */
    [ 64] = { 0x71000, 0x1000, 32 | 16 | 8 }, /* L4TAO5 */
    [ 65] = { 0x72000, 0x1000,      16     }, /* I2C2 */
    [ 66] = { 0x73000, 0x1000, 32 | 16 | 8 }, /* L4TAO6 */
    [ 67] = { 0x74000, 0x1000,      16     }, /* McBSP1 */
    [ 68] = { 0x75000, 0x1000, 32 | 16 | 8 }, /* L4TAO7 */
    [ 69] = { 0x76000, 0x1000,      16     }, /* McBSP2 */
    [ 70] = { 0x77000, 0x1000, 32 | 16 | 8 }, /* L4TAO8 */
    [ 71] = { 0x24000, 0x1000, 32 | 16 | 8 }, /* WD Timer 3 (DSP) */
    [ 72] = { 0x25000, 0x1000, 32 | 16 | 8 }, /* L4TA5 */
    [ 73] = { 0x26000, 0x1000, 32 | 16 | 8 }, /* WD Timer 4 (IVA) */
    [ 74] = { 0x27000, 0x1000, 32 | 16 | 8 }, /* L4TA6 */
    [ 75] = { 0x2a000, 0x1000, 32 | 16 | 8 }, /* GP Timer 2 */
    [ 76] = { 0x2b000, 0x1000, 32 | 16 | 8 }, /* L4TA8 */
    [ 77] = { 0x78000, 0x1000, 32 | 16 | 8 }, /* GP Timer 3 */
    [ 78] = { 0x79000, 0x1000, 32 | 16 | 8 }, /* L4TA22 */
    [ 79] = { 0x7a000, 0x1000, 32 | 16 | 8 }, /* GP Timer 4 */
    [ 80] = { 0x7b000, 0x1000, 32 | 16 | 8 }, /* L4TA23 */
    [ 81] = { 0x7c000, 0x1000, 32 | 16 | 8 }, /* GP Timer 5 */
    [ 82] = { 0x7d000, 0x1000, 32 | 16 | 8 }, /* L4TA24 */
    [ 83] = { 0x7e000, 0x1000, 32 | 16 | 8 }, /* GP Timer 6 */
    [ 84] = { 0x7f000, 0x1000, 32 | 16 | 8 }, /* L4TA25 */
    [ 85] = { 0x80000, 0x1000, 32 | 16 | 8 }, /* GP Timer 7 */
    [ 86] = { 0x81000, 0x1000, 32 | 16 | 8 }, /* L4TA26 */
    [ 87] = { 0x82000, 0x1000, 32 | 16 | 8 }, /* GP Timer 8 */
    [ 88] = { 0x83000, 0x1000, 32 | 16 | 8 }, /* L4TA27 */
    [ 89] = { 0x84000, 0x1000, 32 | 16 | 8 }, /* GP Timer 9 */
    [ 90] = { 0x85000, 0x1000, 32 | 16 | 8 }, /* L4TA28 */
    [ 91] = { 0x86000, 0x1000, 32 | 16 | 8 }, /* GP Timer 10 */
    [ 92] = { 0x87000, 0x1000, 32 | 16 | 8 }, /* L4TA29 */
    [ 93] = { 0x88000, 0x1000, 32 | 16 | 8 }, /* GP Timer 11 */
    [ 94] = { 0x89000, 0x1000, 32 | 16 | 8 }, /* L4TA30 */
    [ 95] = { 0x8a000, 0x1000, 32 | 16 | 8 }, /* GP Timer 12 */
    [ 96] = { 0x8b000, 0x1000, 32 | 16 | 8 }, /* L4TA31 */
    [ 97] = { 0x90000, 0x1000,      16     }, /* EAC */
    [ 98] = { 0x91000, 0x1000, 32 | 16 | 8 }, /* L4TA32 */
    [ 99] = { 0x92000, 0x1000,      16     }, /* FAC */
    [100] = { 0x93000, 0x1000, 32 | 16 | 8 }, /* L4TA33 */
    [101] = { 0x94000, 0x1000, 32 | 16 | 8 }, /* IPC (MAILBOX) */
    [102] = { 0x95000, 0x1000, 32 | 16 | 8 }, /* L4TA34 */
    [103] = { 0x98000, 0x1000, 32 | 16 | 8 }, /* SPI1 */
    [104] = { 0x99000, 0x1000, 32 | 16 | 8 }, /* L4TA35 */
    [105] = { 0x9a000, 0x1000, 32 | 16 | 8 }, /* SPI2 */
    [106] = { 0x9b000, 0x1000, 32 | 16 | 8 }, /* L4TA36 */
    [107] = { 0x9c000, 0x1000,      16 | 8 }, /* MMC SDIO */
    [108] = { 0x9d000, 0x1000, 32 | 16 | 8 }, /* L4TAO9 */
    [109] = { 0x9e000, 0x1000, 32 | 16 | 8 }, /* MS_PRO */
    [110] = { 0x9f000, 0x1000, 32 | 16 | 8 }, /* L4TAO10 */
    [111] = { 0xa0000, 0x1000, 32          }, /* RNG */
    [112] = { 0xa1000, 0x1000, 32 | 16 | 8 }, /* L4TAO11 */
    [113] = { 0xa2000, 0x1000, 32          }, /* DES3DES */
    [114] = { 0xa3000, 0x1000, 32 | 16 | 8 }, /* L4TAO12 */
    [115] = { 0xa4000, 0x1000, 32          }, /* SHA1MD5 */
    [116] = { 0xa5000, 0x1000, 32 | 16 | 8 }, /* L4TAO13 */
    [117] = { 0xa6000, 0x1000, 32          }, /* AES */
    [118] = { 0xa7000, 0x1000, 32 | 16 | 8 }, /* L4TA37 */
    [119] = { 0xa8000, 0x2000, 32          }, /* PKA */
    [120] = { 0xaa000, 0x1000, 32 | 16 | 8 }, /* L4TA38 */
    [121] = { 0xb0000, 0x1000, 32          }, /* MG */
    [122] = { 0xb1000, 0x1000, 32 | 16 | 8 },
    [123] = { 0xb2000, 0x1000, 32          }, /* HDQ/1-Wire */
    [124] = { 0xb3000, 0x1000, 32 | 16 | 8 }, /* L4TA39 */
};

static const struct omap_l4_agent_info_s omap_l4_agent_info[54] = {
    { 0,           0, 3, 2 }, /* L4IA initiatior agent */
    { L4TAO(1),    3, 2, 1 }, /* Control and pinout module */
    { L4TAO(2),    5, 2, 1 }, /* 32K timer */
    { L4TAO(3),    7, 3, 2 }, /* PRCM */
    { L4TA(1),    10, 2, 1 }, /* BCM */
    { L4TA(2),    12, 2, 1 }, /* Test JTAG */
    { L4TA(3),    14, 6, 3 }, /* Quad GPIO */
    { L4TA(4),    20, 4, 3 }, /* WD timer 1/2 */
    { L4TA(7),    24, 2, 1 }, /* GP timer 1 */
    { L4TA(9),    26, 2, 1 }, /* ATM11 ETB */
    { L4TA(10),   28, 5, 4 }, /* Display subsystem */
    { L4TA(11),   33, 5, 4 }, /* Camera subsystem */
    { L4TA(12),   38, 2, 1 }, /* sDMA */
    { L4TA(13),   40, 5, 4 }, /* SSI */
    { L4TAO(4),   45, 2, 1 }, /* USB */
    { L4TA(14),   47, 2, 1 }, /* Win Tracer1 */
    { L4TA(15),   49, 2, 1 }, /* Win Tracer2 */
    { L4TA(16),   51, 2, 1 }, /* Win Tracer3 */
    { L4TA(17),   53, 2, 1 }, /* Win Tracer4 */
    { L4TA(18),   55, 2, 1 }, /* XTI */
    { L4TA(19),   57, 2, 1 }, /* UART1 */
    { L4TA(20),   59, 2, 1 }, /* UART2 */
    { L4TA(21),   61, 2, 1 }, /* UART3 */
    { L4TAO(5),   63, 2, 1 }, /* I2C1 */
    { L4TAO(6),   65, 2, 1 }, /* I2C2 */
    { L4TAO(7),   67, 2, 1 }, /* McBSP1 */
    { L4TAO(8),   69, 2, 1 }, /* McBSP2 */
    { L4TA(5),    71, 2, 1 }, /* WD Timer 3 (DSP) */
    { L4TA(6),    73, 2, 1 }, /* WD Timer 4 (IVA) */
    { L4TA(8),    75, 2, 1 }, /* GP Timer 2 */
    { L4TA(22),   77, 2, 1 }, /* GP Timer 3 */
    { L4TA(23),   79, 2, 1 }, /* GP Timer 4 */
    { L4TA(24),   81, 2, 1 }, /* GP Timer 5 */
    { L4TA(25),   83, 2, 1 }, /* GP Timer 6 */
    { L4TA(26),   85, 2, 1 }, /* GP Timer 7 */
    { L4TA(27),   87, 2, 1 }, /* GP Timer 8 */
    { L4TA(28),   89, 2, 1 }, /* GP Timer 9 */
    { L4TA(29),   91, 2, 1 }, /* GP Timer 10 */
    { L4TA(30),   93, 2, 1 }, /* GP Timer 11 */
    { L4TA(31),   95, 2, 1 }, /* GP Timer 12 */
    { L4TA(32),   97, 2, 1 }, /* EAC */
    { L4TA(33),   99, 2, 1 }, /* FAC */
    { L4TA(34),  101, 2, 1 }, /* IPC */
    { L4TA(35),  103, 2, 1 }, /* SPI1 */
    { L4TA(36),  105, 2, 1 }, /* SPI2 */
    { L4TAO(9),  107, 2, 1 }, /* MMC SDIO */
    { L4TAO(10), 109, 2, 1 },
    { L4TAO(11), 111, 2, 1 }, /* RNG */
    { L4TAO(12), 113, 2, 1 }, /* DES3DES */
    { L4TAO(13), 115, 2, 1 }, /* SHA1MD5 */
    { L4TA(37),  117, 2, 1 }, /* AES */
    { L4TA(38),  119, 2, 1 }, /* PKA */
    { -1,        121, 2, 1 },
    { L4TA(39),  123, 2, 1 }, /* HDQ/1-Wire */
};

#define omap_l4ta(bus, cs)	\
    omap_l4ta_get(bus, omap_l4_region, omap_l4_agent_info, L4TA(cs))
#define omap_l4tao(bus, cs)	\
    omap_l4ta_get(bus, omap_l4_region, omap_l4_agent_info, L4TAO(cs))

/* Power, Reset, and Clock Management */
struct omap_prcm_s {
    qemu_irq irq[3];
    struct omap_mpu_state_s *mpu;
    MemoryRegion iomem0;
    MemoryRegion iomem1;

    uint32_t irqst[3];
    uint32_t irqen[3];

    uint32_t sysconfig;
    uint32_t voltctrl;
    uint32_t scratch[20];

    uint32_t clksrc[1];
    uint32_t clkout[1];
    uint32_t clkemul[1];
    uint32_t clkpol[1];
    uint32_t clksel[8];
    uint32_t clken[12];
    uint32_t clkctrl[4];
    uint32_t clkidle[7];
    uint32_t setuptime[2];

    uint32_t wkup[3];
    uint32_t wken[3];
    uint32_t wkst[3];
    uint32_t rst[4];
    uint32_t rstctrl[1];
    uint32_t power[4];
    uint32_t rsttime_wkup;

    uint32_t ev;
    uint32_t evtime[2];

    int dpll_lock, apll_lock[2];
};

static void omap_prcm_int_update(struct omap_prcm_s *s, int dom)
{
    qemu_set_irq(s->irq[dom], s->irqst[dom] & s->irqen[dom]);
    /* XXX or is the mask applied before PRCM_IRQSTATUS_* ? */
}

static uint64_t omap_prcm_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct omap_prcm_s *s = opaque;
    uint32_t ret;

    if (size != 4) {
        return omap_badwidth_read32(opaque, addr);
    }

    switch (addr) {
    case 0x000:	/* PRCM_REVISION */
        return 0x10;

    case 0x010:	/* PRCM_SYSCONFIG */
        return s->sysconfig;

    case 0x018:	/* PRCM_IRQSTATUS_MPU */
        return s->irqst[0];

    case 0x01c:	/* PRCM_IRQENABLE_MPU */
        return s->irqen[0];

    case 0x050:	/* PRCM_VOLTCTRL */
        return s->voltctrl;
    case 0x054:	/* PRCM_VOLTST */
        return s->voltctrl & 3;

    case 0x060:	/* PRCM_CLKSRC_CTRL */
        return s->clksrc[0];
    case 0x070:	/* PRCM_CLKOUT_CTRL */
        return s->clkout[0];
    case 0x078:	/* PRCM_CLKEMUL_CTRL */
        return s->clkemul[0];
    case 0x080:	/* PRCM_CLKCFG_CTRL */
    case 0x084:	/* PRCM_CLKCFG_STATUS */
        return 0;

    case 0x090:	/* PRCM_VOLTSETUP */
        return s->setuptime[0];

    case 0x094:	/* PRCM_CLKSSETUP */
        return s->setuptime[1];

    case 0x098:	/* PRCM_POLCTRL */
        return s->clkpol[0];

    case 0x0b0:	/* GENERAL_PURPOSE1 */
    case 0x0b4:	/* GENERAL_PURPOSE2 */
    case 0x0b8:	/* GENERAL_PURPOSE3 */
    case 0x0bc:	/* GENERAL_PURPOSE4 */
    case 0x0c0:	/* GENERAL_PURPOSE5 */
    case 0x0c4:	/* GENERAL_PURPOSE6 */
    case 0x0c8:	/* GENERAL_PURPOSE7 */
    case 0x0cc:	/* GENERAL_PURPOSE8 */
    case 0x0d0:	/* GENERAL_PURPOSE9 */
    case 0x0d4:	/* GENERAL_PURPOSE10 */
    case 0x0d8:	/* GENERAL_PURPOSE11 */
    case 0x0dc:	/* GENERAL_PURPOSE12 */
    case 0x0e0:	/* GENERAL_PURPOSE13 */
    case 0x0e4:	/* GENERAL_PURPOSE14 */
    case 0x0e8:	/* GENERAL_PURPOSE15 */
    case 0x0ec:	/* GENERAL_PURPOSE16 */
    case 0x0f0:	/* GENERAL_PURPOSE17 */
    case 0x0f4:	/* GENERAL_PURPOSE18 */
    case 0x0f8:	/* GENERAL_PURPOSE19 */
    case 0x0fc:	/* GENERAL_PURPOSE20 */
        return s->scratch[(addr - 0xb0) >> 2];

    case 0x140:	/* CM_CLKSEL_MPU */
        return s->clksel[0];
    case 0x148:	/* CM_CLKSTCTRL_MPU */
        return s->clkctrl[0];

    case 0x158:	/* RM_RSTST_MPU */
        return s->rst[0];
    case 0x1c8:	/* PM_WKDEP_MPU */
        return s->wkup[0];
    case 0x1d4:	/* PM_EVGENCTRL_MPU */
        return s->ev;
    case 0x1d8:	/* PM_EVEGENONTIM_MPU */
        return s->evtime[0];
    case 0x1dc:	/* PM_EVEGENOFFTIM_MPU */
        return s->evtime[1];
    case 0x1e0:	/* PM_PWSTCTRL_MPU */
        return s->power[0];
    case 0x1e4:	/* PM_PWSTST_MPU */
        return 0;

    case 0x200:	/* CM_FCLKEN1_CORE */
        return s->clken[0];
    case 0x204:	/* CM_FCLKEN2_CORE */
        return s->clken[1];
    case 0x210:	/* CM_ICLKEN1_CORE */
        return s->clken[2];
    case 0x214:	/* CM_ICLKEN2_CORE */
        return s->clken[3];
    case 0x21c:	/* CM_ICLKEN4_CORE */
        return s->clken[4];

    case 0x220:	/* CM_IDLEST1_CORE */
        /* TODO: check the actual iclk status */
        return 0x7ffffff9;
    case 0x224:	/* CM_IDLEST2_CORE */
        /* TODO: check the actual iclk status */
        return 0x00000007;
    case 0x22c:	/* CM_IDLEST4_CORE */
        /* TODO: check the actual iclk status */
        return 0x0000001f;

    case 0x230:	/* CM_AUTOIDLE1_CORE */
        return s->clkidle[0];
    case 0x234:	/* CM_AUTOIDLE2_CORE */
        return s->clkidle[1];
    case 0x238:	/* CM_AUTOIDLE3_CORE */
        return s->clkidle[2];
    case 0x23c:	/* CM_AUTOIDLE4_CORE */
        return s->clkidle[3];

    case 0x240:	/* CM_CLKSEL1_CORE */
        return s->clksel[1];
    case 0x244:	/* CM_CLKSEL2_CORE */
        return s->clksel[2];

    case 0x248:	/* CM_CLKSTCTRL_CORE */
        return s->clkctrl[1];

    case 0x2a0:	/* PM_WKEN1_CORE */
        return s->wken[0];
    case 0x2a4:	/* PM_WKEN2_CORE */
        return s->wken[1];

    case 0x2b0:	/* PM_WKST1_CORE */
        return s->wkst[0];
    case 0x2b4:	/* PM_WKST2_CORE */
        return s->wkst[1];
    case 0x2c8:	/* PM_WKDEP_CORE */
        return 0x1e;

    case 0x2e0:	/* PM_PWSTCTRL_CORE */
        return s->power[1];
    case 0x2e4:	/* PM_PWSTST_CORE */
        return 0x000030 | (s->power[1] & 0xfc00);

    case 0x300:	/* CM_FCLKEN_GFX */
        return s->clken[5];
    case 0x310:	/* CM_ICLKEN_GFX */
        return s->clken[6];
    case 0x320:	/* CM_IDLEST_GFX */
        /* TODO: check the actual iclk status */
        return 0x00000001;
    case 0x340:	/* CM_CLKSEL_GFX */
        return s->clksel[3];
    case 0x348:	/* CM_CLKSTCTRL_GFX */
        return s->clkctrl[2];
    case 0x350:	/* RM_RSTCTRL_GFX */
        return s->rstctrl[0];
    case 0x358:	/* RM_RSTST_GFX */
        return s->rst[1];
    case 0x3c8:	/* PM_WKDEP_GFX */
        return s->wkup[1];

    case 0x3e0:	/* PM_PWSTCTRL_GFX */
        return s->power[2];
    case 0x3e4:	/* PM_PWSTST_GFX */
        return s->power[2] & 3;

    case 0x400:	/* CM_FCLKEN_WKUP */
        return s->clken[7];
    case 0x410:	/* CM_ICLKEN_WKUP */
        return s->clken[8];
    case 0x420:	/* CM_IDLEST_WKUP */
        /* TODO: check the actual iclk status */
        return 0x0000003f;
    case 0x430:	/* CM_AUTOIDLE_WKUP */
        return s->clkidle[4];
    case 0x440:	/* CM_CLKSEL_WKUP */
        return s->clksel[4];
    case 0x450:	/* RM_RSTCTRL_WKUP */
        return 0;
    case 0x454:	/* RM_RSTTIME_WKUP */
        return s->rsttime_wkup;
    case 0x458:	/* RM_RSTST_WKUP */
        return s->rst[2];
    case 0x4a0:	/* PM_WKEN_WKUP */
        return s->wken[2];
    case 0x4b0:	/* PM_WKST_WKUP */
        return s->wkst[2];

    case 0x500:	/* CM_CLKEN_PLL */
        return s->clken[9];
    case 0x520:	/* CM_IDLEST_CKGEN */
        ret = 0x0000070 | (s->apll_lock[0] << 9) | (s->apll_lock[1] << 8);
        if (!(s->clksel[6] & 3))
            /* Core uses 32-kHz clock */
            ret |= 3 << 0;
        else if (!s->dpll_lock)
            /* DPLL not locked, core uses ref_clk */
            ret |= 1 << 0;
        else
            /* Core uses DPLL */
            ret |= 2 << 0;
        return ret;
    case 0x530:	/* CM_AUTOIDLE_PLL */
        return s->clkidle[5];
    case 0x540:	/* CM_CLKSEL1_PLL */
        return s->clksel[5];
    case 0x544:	/* CM_CLKSEL2_PLL */
        return s->clksel[6];

    case 0x800:	/* CM_FCLKEN_DSP */
        return s->clken[10];
    case 0x810:	/* CM_ICLKEN_DSP */
        return s->clken[11];
    case 0x820:	/* CM_IDLEST_DSP */
        /* TODO: check the actual iclk status */
        return 0x00000103;
    case 0x830:	/* CM_AUTOIDLE_DSP */
        return s->clkidle[6];
    case 0x840:	/* CM_CLKSEL_DSP */
        return s->clksel[7];
    case 0x848:	/* CM_CLKSTCTRL_DSP */
        return s->clkctrl[3];
    case 0x850:	/* RM_RSTCTRL_DSP */
        return 0;
    case 0x858:	/* RM_RSTST_DSP */
        return s->rst[3];
    case 0x8c8:	/* PM_WKDEP_DSP */
        return s->wkup[2];
    case 0x8e0:	/* PM_PWSTCTRL_DSP */
        return s->power[3];
    case 0x8e4:	/* PM_PWSTST_DSP */
        return 0x008030 | (s->power[3] & 0x3003);

    case 0x8f0:	/* PRCM_IRQSTATUS_DSP */
        return s->irqst[1];
    case 0x8f4:	/* PRCM_IRQENABLE_DSP */
        return s->irqen[1];

    case 0x8f8:	/* PRCM_IRQSTATUS_IVA */
        return s->irqst[2];
    case 0x8fc:	/* PRCM_IRQENABLE_IVA */
        return s->irqen[2];
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_prcm_apll_update(struct omap_prcm_s *s)
{
    int mode[2];

    mode[0] = (s->clken[9] >> 6) & 3;
    s->apll_lock[0] = (mode[0] == 3);
    mode[1] = (s->clken[9] >> 2) & 3;
    s->apll_lock[1] = (mode[1] == 3);
    /* TODO: update clocks */

    if (mode[0] == 1 || mode[0] == 2 || mode[1] == 1 || mode[1] == 2)
        fprintf(stderr, "%s: bad EN_54M_PLL or bad EN_96M_PLL\n",
                        __func__);
}

static void omap_prcm_dpll_update(struct omap_prcm_s *s)
{
    omap_clk dpll = omap_findclk(s->mpu, "dpll");
    omap_clk dpll_x2 = omap_findclk(s->mpu, "dpll");
    omap_clk core = omap_findclk(s->mpu, "core_clk");
    int mode = (s->clken[9] >> 0) & 3;
    int mult, div;

    mult = (s->clksel[5] >> 12) & 0x3ff;
    div = (s->clksel[5] >> 8) & 0xf;
    if (mult == 0 || mult == 1)
        mode = 1;	/* Bypass */

    s->dpll_lock = 0;
    switch (mode) {
    case 0:
        fprintf(stderr, "%s: bad EN_DPLL\n", __func__);
        break;
    case 1:	/* Low-power bypass mode (Default) */
    case 2:	/* Fast-relock bypass mode */
        omap_clk_setrate(dpll, 1, 1);
        omap_clk_setrate(dpll_x2, 1, 1);
        break;
    case 3:	/* Lock mode */
        s->dpll_lock = 1; /* After 20 FINT cycles (ref_clk / (div + 1)).  */

        omap_clk_setrate(dpll, div + 1, mult);
        omap_clk_setrate(dpll_x2, div + 1, mult * 2);
        break;
    }

    switch ((s->clksel[6] >> 0) & 3) {
    case 0:
        omap_clk_reparent(core, omap_findclk(s->mpu, "clk32-kHz"));
        break;
    case 1:
        omap_clk_reparent(core, dpll);
        break;
    case 2:
        /* Default */
        omap_clk_reparent(core, dpll_x2);
        break;
    case 3:
        fprintf(stderr, "%s: bad CORE_CLK_SRC\n", __func__);
        break;
    }
}

static void omap_prcm_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct omap_prcm_s *s = opaque;

    if (size != 4) {
        omap_badwidth_write32(opaque, addr, value);
        return;
    }

    switch (addr) {
    case 0x000:	/* PRCM_REVISION */
    case 0x054:	/* PRCM_VOLTST */
    case 0x084:	/* PRCM_CLKCFG_STATUS */
    case 0x1e4:	/* PM_PWSTST_MPU */
    case 0x220:	/* CM_IDLEST1_CORE */
    case 0x224:	/* CM_IDLEST2_CORE */
    case 0x22c:	/* CM_IDLEST4_CORE */
    case 0x2c8:	/* PM_WKDEP_CORE */
    case 0x2e4:	/* PM_PWSTST_CORE */
    case 0x320:	/* CM_IDLEST_GFX */
    case 0x3e4:	/* PM_PWSTST_GFX */
    case 0x420:	/* CM_IDLEST_WKUP */
    case 0x520:	/* CM_IDLEST_CKGEN */
    case 0x820:	/* CM_IDLEST_DSP */
    case 0x8e4:	/* PM_PWSTST_DSP */
        OMAP_RO_REG(addr);
        return;

    case 0x010:	/* PRCM_SYSCONFIG */
        s->sysconfig = value & 1;
        break;

    case 0x018:	/* PRCM_IRQSTATUS_MPU */
        s->irqst[0] &= ~value;
        omap_prcm_int_update(s, 0);
        break;
    case 0x01c:	/* PRCM_IRQENABLE_MPU */
        s->irqen[0] = value & 0x3f;
        omap_prcm_int_update(s, 0);
        break;

    case 0x050:	/* PRCM_VOLTCTRL */
        s->voltctrl = value & 0xf1c3;
        break;

    case 0x060:	/* PRCM_CLKSRC_CTRL */
        s->clksrc[0] = value & 0xdb;
        /* TODO update clocks */
        break;

    case 0x070:	/* PRCM_CLKOUT_CTRL */
        s->clkout[0] = value & 0xbbbb;
        /* TODO update clocks */
        break;

    case 0x078:	/* PRCM_CLKEMUL_CTRL */
        s->clkemul[0] = value & 1;
        /* TODO update clocks */
        break;

    case 0x080:	/* PRCM_CLKCFG_CTRL */
        break;

    case 0x090:	/* PRCM_VOLTSETUP */
        s->setuptime[0] = value & 0xffff;
        break;
    case 0x094:	/* PRCM_CLKSSETUP */
        s->setuptime[1] = value & 0xffff;
        break;

    case 0x098:	/* PRCM_POLCTRL */
        s->clkpol[0] = value & 0x701;
        break;

    case 0x0b0:	/* GENERAL_PURPOSE1 */
    case 0x0b4:	/* GENERAL_PURPOSE2 */
    case 0x0b8:	/* GENERAL_PURPOSE3 */
    case 0x0bc:	/* GENERAL_PURPOSE4 */
    case 0x0c0:	/* GENERAL_PURPOSE5 */
    case 0x0c4:	/* GENERAL_PURPOSE6 */
    case 0x0c8:	/* GENERAL_PURPOSE7 */
    case 0x0cc:	/* GENERAL_PURPOSE8 */
    case 0x0d0:	/* GENERAL_PURPOSE9 */
    case 0x0d4:	/* GENERAL_PURPOSE10 */
    case 0x0d8:	/* GENERAL_PURPOSE11 */
    case 0x0dc:	/* GENERAL_PURPOSE12 */
    case 0x0e0:	/* GENERAL_PURPOSE13 */
    case 0x0e4:	/* GENERAL_PURPOSE14 */
    case 0x0e8:	/* GENERAL_PURPOSE15 */
    case 0x0ec:	/* GENERAL_PURPOSE16 */
    case 0x0f0:	/* GENERAL_PURPOSE17 */
    case 0x0f4:	/* GENERAL_PURPOSE18 */
    case 0x0f8:	/* GENERAL_PURPOSE19 */
    case 0x0fc:	/* GENERAL_PURPOSE20 */
        s->scratch[(addr - 0xb0) >> 2] = value;
        break;

    case 0x140:	/* CM_CLKSEL_MPU */
        s->clksel[0] = value & 0x1f;
        /* TODO update clocks */
        break;
    case 0x148:	/* CM_CLKSTCTRL_MPU */
        s->clkctrl[0] = value & 0x1f;
        break;

    case 0x158:	/* RM_RSTST_MPU */
        s->rst[0] &= ~value;
        break;
    case 0x1c8:	/* PM_WKDEP_MPU */
        s->wkup[0] = value & 0x15;
        break;

    case 0x1d4:	/* PM_EVGENCTRL_MPU */
        s->ev = value & 0x1f;
        break;
    case 0x1d8:	/* PM_EVEGENONTIM_MPU */
        s->evtime[0] = value;
        break;
    case 0x1dc:	/* PM_EVEGENOFFTIM_MPU */
        s->evtime[1] = value;
        break;

    case 0x1e0:	/* PM_PWSTCTRL_MPU */
        s->power[0] = value & 0xc0f;
        break;

    case 0x200:	/* CM_FCLKEN1_CORE */
        s->clken[0] = value & 0xbfffffff;
        /* TODO update clocks */
        /* The EN_EAC bit only gets/puts func_96m_clk.  */
        break;
    case 0x204:	/* CM_FCLKEN2_CORE */
        s->clken[1] = value & 0x00000007;
        /* TODO update clocks */
        break;
    case 0x210:	/* CM_ICLKEN1_CORE */
        s->clken[2] = value & 0xfffffff9;
        /* TODO update clocks */
        /* The EN_EAC bit only gets/puts core_l4_iclk.  */
        break;
    case 0x214:	/* CM_ICLKEN2_CORE */
        s->clken[3] = value & 0x00000007;
        /* TODO update clocks */
        break;
    case 0x21c:	/* CM_ICLKEN4_CORE */
        s->clken[4] = value & 0x0000001f;
        /* TODO update clocks */
        break;

    case 0x230:	/* CM_AUTOIDLE1_CORE */
        s->clkidle[0] = value & 0xfffffff9;
        /* TODO update clocks */
        break;
    case 0x234:	/* CM_AUTOIDLE2_CORE */
        s->clkidle[1] = value & 0x00000007;
        /* TODO update clocks */
        break;
    case 0x238:	/* CM_AUTOIDLE3_CORE */
        s->clkidle[2] = value & 0x00000007;
        /* TODO update clocks */
        break;
    case 0x23c:	/* CM_AUTOIDLE4_CORE */
        s->clkidle[3] = value & 0x0000001f;
        /* TODO update clocks */
        break;

    case 0x240:	/* CM_CLKSEL1_CORE */
        s->clksel[1] = value & 0x0fffbf7f;
        /* TODO update clocks */
        break;

    case 0x244:	/* CM_CLKSEL2_CORE */
        s->clksel[2] = value & 0x00fffffc;
        /* TODO update clocks */
        break;

    case 0x248:	/* CM_CLKSTCTRL_CORE */
        s->clkctrl[1] = value & 0x7;
        break;

    case 0x2a0:	/* PM_WKEN1_CORE */
        s->wken[0] = value & 0x04667ff8;
        break;
    case 0x2a4:	/* PM_WKEN2_CORE */
        s->wken[1] = value & 0x00000005;
        break;

    case 0x2b0:	/* PM_WKST1_CORE */
        s->wkst[0] &= ~value;
        break;
    case 0x2b4:	/* PM_WKST2_CORE */
        s->wkst[1] &= ~value;
        break;

    case 0x2e0:	/* PM_PWSTCTRL_CORE */
        s->power[1] = (value & 0x00fc3f) | (1 << 2);
        break;

    case 0x300:	/* CM_FCLKEN_GFX */
        s->clken[5] = value & 6;
        /* TODO update clocks */
        break;
    case 0x310:	/* CM_ICLKEN_GFX */
        s->clken[6] = value & 1;
        /* TODO update clocks */
        break;
    case 0x340:	/* CM_CLKSEL_GFX */
        s->clksel[3] = value & 7;
        /* TODO update clocks */
        break;
    case 0x348:	/* CM_CLKSTCTRL_GFX */
        s->clkctrl[2] = value & 1;
        break;
    case 0x350:	/* RM_RSTCTRL_GFX */
        s->rstctrl[0] = value & 1;
        /* TODO: reset */
        break;
    case 0x358:	/* RM_RSTST_GFX */
        s->rst[1] &= ~value;
        break;
    case 0x3c8:	/* PM_WKDEP_GFX */
        s->wkup[1] = value & 0x13;
        break;
    case 0x3e0:	/* PM_PWSTCTRL_GFX */
        s->power[2] = (value & 0x00c0f) | (3 << 2);
        break;

    case 0x400:	/* CM_FCLKEN_WKUP */
        s->clken[7] = value & 0xd;
        /* TODO update clocks */
        break;
    case 0x410:	/* CM_ICLKEN_WKUP */
        s->clken[8] = value & 0x3f;
        /* TODO update clocks */
        break;
    case 0x430:	/* CM_AUTOIDLE_WKUP */
        s->clkidle[4] = value & 0x0000003f;
        /* TODO update clocks */
        break;
    case 0x440:	/* CM_CLKSEL_WKUP */
        s->clksel[4] = value & 3;
        /* TODO update clocks */
        break;
    case 0x450:	/* RM_RSTCTRL_WKUP */
        /* TODO: reset */
        if (value & 2)
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        break;
    case 0x454:	/* RM_RSTTIME_WKUP */
        s->rsttime_wkup = value & 0x1fff;
        break;
    case 0x458:	/* RM_RSTST_WKUP */
        s->rst[2] &= ~value;
        break;
    case 0x4a0:	/* PM_WKEN_WKUP */
        s->wken[2] = value & 0x00000005;
        break;
    case 0x4b0:	/* PM_WKST_WKUP */
        s->wkst[2] &= ~value;
        break;

    case 0x500:	/* CM_CLKEN_PLL */
        if (value & 0xffffff30)
            fprintf(stderr, "%s: write 0s in CM_CLKEN_PLL for "
                            "future compatibility\n", __func__);
        if ((s->clken[9] ^ value) & 0xcc) {
            s->clken[9] &= ~0xcc;
            s->clken[9] |= value & 0xcc;
            omap_prcm_apll_update(s);
        }
        if ((s->clken[9] ^ value) & 3) {
            s->clken[9] &= ~3;
            s->clken[9] |= value & 3;
            omap_prcm_dpll_update(s);
        }
        break;
    case 0x530:	/* CM_AUTOIDLE_PLL */
        s->clkidle[5] = value & 0x000000cf;
        /* TODO update clocks */
        break;
    case 0x540:	/* CM_CLKSEL1_PLL */
        if (value & 0xfc4000d7)
            fprintf(stderr, "%s: write 0s in CM_CLKSEL1_PLL for "
                            "future compatibility\n", __func__);
        if ((s->clksel[5] ^ value) & 0x003fff00) {
            s->clksel[5] = value & 0x03bfff28;
            omap_prcm_dpll_update(s);
        }
        /* TODO update the other clocks */

        s->clksel[5] = value & 0x03bfff28;
        break;
    case 0x544:	/* CM_CLKSEL2_PLL */
        if (value & ~3)
            fprintf(stderr, "%s: write 0s in CM_CLKSEL2_PLL[31:2] for "
                            "future compatibility\n", __func__);
        if (s->clksel[6] != (value & 3)) {
            s->clksel[6] = value & 3;
            omap_prcm_dpll_update(s);
        }
        break;

    case 0x800:	/* CM_FCLKEN_DSP */
        s->clken[10] = value & 0x501;
        /* TODO update clocks */
        break;
    case 0x810:	/* CM_ICLKEN_DSP */
        s->clken[11] = value & 0x2;
        /* TODO update clocks */
        break;
    case 0x830:	/* CM_AUTOIDLE_DSP */
        s->clkidle[6] = value & 0x2;
        /* TODO update clocks */
        break;
    case 0x840:	/* CM_CLKSEL_DSP */
        s->clksel[7] = value & 0x3fff;
        /* TODO update clocks */
        break;
    case 0x848:	/* CM_CLKSTCTRL_DSP */
        s->clkctrl[3] = value & 0x101;
        break;
    case 0x850:	/* RM_RSTCTRL_DSP */
        /* TODO: reset */
        break;
    case 0x858:	/* RM_RSTST_DSP */
        s->rst[3] &= ~value;
        break;
    case 0x8c8:	/* PM_WKDEP_DSP */
        s->wkup[2] = value & 0x13;
        break;
    case 0x8e0:	/* PM_PWSTCTRL_DSP */
        s->power[3] = (value & 0x03017) | (3 << 2);
        break;

    case 0x8f0:	/* PRCM_IRQSTATUS_DSP */
        s->irqst[1] &= ~value;
        omap_prcm_int_update(s, 1);
        break;
    case 0x8f4:	/* PRCM_IRQENABLE_DSP */
        s->irqen[1] = value & 0x7;
        omap_prcm_int_update(s, 1);
        break;

    case 0x8f8:	/* PRCM_IRQSTATUS_IVA */
        s->irqst[2] &= ~value;
        omap_prcm_int_update(s, 2);
        break;
    case 0x8fc:	/* PRCM_IRQENABLE_IVA */
        s->irqen[2] = value & 0x7;
        omap_prcm_int_update(s, 2);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static const MemoryRegionOps omap_prcm_ops = {
    .read = omap_prcm_read,
    .write = omap_prcm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_prcm_reset(struct omap_prcm_s *s)
{
    s->sysconfig = 0;
    s->irqst[0] = 0;
    s->irqst[1] = 0;
    s->irqst[2] = 0;
    s->irqen[0] = 0;
    s->irqen[1] = 0;
    s->irqen[2] = 0;
    s->voltctrl = 0x1040;
    s->ev = 0x14;
    s->evtime[0] = 0;
    s->evtime[1] = 0;
    s->clkctrl[0] = 0;
    s->clkctrl[1] = 0;
    s->clkctrl[2] = 0;
    s->clkctrl[3] = 0;
    s->clken[1] = 7;
    s->clken[3] = 7;
    s->clken[4] = 0;
    s->clken[5] = 0;
    s->clken[6] = 0;
    s->clken[7] = 0xc;
    s->clken[8] = 0x3e;
    s->clken[9] = 0x0d;
    s->clken[10] = 0;
    s->clken[11] = 0;
    s->clkidle[0] = 0;
    s->clkidle[2] = 7;
    s->clkidle[3] = 0;
    s->clkidle[4] = 0;
    s->clkidle[5] = 0x0c;
    s->clkidle[6] = 0;
    s->clksel[0] = 0x01;
    s->clksel[1] = 0x02100121;
    s->clksel[2] = 0x00000000;
    s->clksel[3] = 0x01;
    s->clksel[4] = 0;
    s->clksel[7] = 0x0121;
    s->wkup[0] = 0x15;
    s->wkup[1] = 0x13;
    s->wkup[2] = 0x13;
    s->wken[0] = 0x04667ff8;
    s->wken[1] = 0x00000005;
    s->wken[2] = 5;
    s->wkst[0] = 0;
    s->wkst[1] = 0;
    s->wkst[2] = 0;
    s->power[0] = 0x00c;
    s->power[1] = 4;
    s->power[2] = 0x0000c;
    s->power[3] = 0x14;
    s->rstctrl[0] = 1;
    s->rst[3] = 1;
    omap_prcm_apll_update(s);
    omap_prcm_dpll_update(s);
}

static void omap_prcm_coldreset(struct omap_prcm_s *s)
{
    s->setuptime[0] = 0;
    s->setuptime[1] = 0;
    memset(&s->scratch, 0, sizeof(s->scratch));
    s->rst[0] = 0x01;
    s->rst[1] = 0x00;
    s->rst[2] = 0x01;
    s->clken[0] = 0;
    s->clken[2] = 0;
    s->clkidle[1] = 0;
    s->clksel[5] = 0;
    s->clksel[6] = 2;
    s->clksrc[0] = 0x43;
    s->clkout[0] = 0x0303;
    s->clkemul[0] = 0;
    s->clkpol[0] = 0x100;
    s->rsttime_wkup = 0x1002;

    omap_prcm_reset(s);
}

static struct omap_prcm_s *omap_prcm_init(struct omap_target_agent_s *ta,
                qemu_irq mpu_int, qemu_irq dsp_int, qemu_irq iva_int,
                struct omap_mpu_state_s *mpu)
{
    struct omap_prcm_s *s = g_new0(struct omap_prcm_s, 1);

    s->irq[0] = mpu_int;
    s->irq[1] = dsp_int;
    s->irq[2] = iva_int;
    s->mpu = mpu;
    omap_prcm_coldreset(s);

    memory_region_init_io(&s->iomem0, NULL, &omap_prcm_ops, s, "omap.pcrm0",
                          omap_l4_region_size(ta, 0));
    memory_region_init_io(&s->iomem1, NULL, &omap_prcm_ops, s, "omap.pcrm1",
                          omap_l4_region_size(ta, 1));
    omap_l4_attach(ta, 0, &s->iomem0);
    omap_l4_attach(ta, 1, &s->iomem1);

    return s;
}

/* System and Pinout control */
struct omap_sysctl_s {
    struct omap_mpu_state_s *mpu;
    MemoryRegion iomem;

    uint32_t sysconfig;
    uint32_t devconfig;
    uint32_t psaconfig;
    uint32_t padconf[0x45];
    uint8_t obs;
    uint32_t msuspendmux[5];
};

static uint32_t omap_sysctl_read8(void *opaque, hwaddr addr)
{

    struct omap_sysctl_s *s = opaque;
    int pad_offset, byte_offset;
    int value;

    switch (addr) {
    case 0x030 ... 0x140:	/* CONTROL_PADCONF - only used in the POP */
        pad_offset = (addr - 0x30) >> 2;
        byte_offset = (addr - 0x30) & (4 - 1);

        value = s->padconf[pad_offset];
        value = (value >> (byte_offset * 8)) & 0xff;

        return value;

    default:
        break;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static uint32_t omap_sysctl_read(void *opaque, hwaddr addr)
{
    struct omap_sysctl_s *s = opaque;

    switch (addr) {
    case 0x000:	/* CONTROL_REVISION */
        return 0x20;

    case 0x010:	/* CONTROL_SYSCONFIG */
        return s->sysconfig;

    case 0x030 ... 0x140:	/* CONTROL_PADCONF - only used in the POP */
        return s->padconf[(addr - 0x30) >> 2];

    case 0x270:	/* CONTROL_DEBOBS */
        return s->obs;

    case 0x274:	/* CONTROL_DEVCONF */
        return s->devconfig;

    case 0x28c:	/* CONTROL_EMU_SUPPORT */
        return 0;

    case 0x290:	/* CONTROL_MSUSPENDMUX_0 */
        return s->msuspendmux[0];
    case 0x294:	/* CONTROL_MSUSPENDMUX_1 */
        return s->msuspendmux[1];
    case 0x298:	/* CONTROL_MSUSPENDMUX_2 */
        return s->msuspendmux[2];
    case 0x29c:	/* CONTROL_MSUSPENDMUX_3 */
        return s->msuspendmux[3];
    case 0x2a0:	/* CONTROL_MSUSPENDMUX_4 */
        return s->msuspendmux[4];
    case 0x2a4:	/* CONTROL_MSUSPENDMUX_5 */
        return 0;

    case 0x2b8:	/* CONTROL_PSA_CTRL */
        return s->psaconfig;
    case 0x2bc:	/* CONTROL_PSA_CMD */
    case 0x2c0:	/* CONTROL_PSA_VALUE */
        return 0;

    case 0x2b0:	/* CONTROL_SEC_CTRL */
        return 0x800000f1;
    case 0x2d0:	/* CONTROL_SEC_EMU */
        return 0x80000015;
    case 0x2d4:	/* CONTROL_SEC_TAP */
        return 0x8000007f;
    case 0x2b4:	/* CONTROL_SEC_TEST */
    case 0x2f0:	/* CONTROL_SEC_STATUS */
    case 0x2f4:	/* CONTROL_SEC_ERR_STATUS */
        /* Secure mode is not present on general-pusrpose device.  Outside
         * secure mode these values cannot be read or written.  */
        return 0;

    case 0x2d8:	/* CONTROL_OCM_RAM_PERM */
        return 0xff;
    case 0x2dc:	/* CONTROL_OCM_PUB_RAM_ADD */
    case 0x2e0:	/* CONTROL_EXT_SEC_RAM_START_ADD */
    case 0x2e4:	/* CONTROL_EXT_SEC_RAM_STOP_ADD */
        /* No secure mode so no Extended Secure RAM present.  */
        return 0;

    case 0x2f8:	/* CONTROL_STATUS */
        /* Device Type => General-purpose */
        return 0x0300;
    case 0x2fc:	/* CONTROL_GENERAL_PURPOSE_STATUS */

    case 0x300:	/* CONTROL_RPUB_KEY_H_0 */
    case 0x304:	/* CONTROL_RPUB_KEY_H_1 */
    case 0x308:	/* CONTROL_RPUB_KEY_H_2 */
    case 0x30c:	/* CONTROL_RPUB_KEY_H_3 */
        return 0xdecafbad;

    case 0x310:	/* CONTROL_RAND_KEY_0 */
    case 0x314:	/* CONTROL_RAND_KEY_1 */
    case 0x318:	/* CONTROL_RAND_KEY_2 */
    case 0x31c:	/* CONTROL_RAND_KEY_3 */
    case 0x320:	/* CONTROL_CUST_KEY_0 */
    case 0x324:	/* CONTROL_CUST_KEY_1 */
    case 0x330:	/* CONTROL_TEST_KEY_0 */
    case 0x334:	/* CONTROL_TEST_KEY_1 */
    case 0x338:	/* CONTROL_TEST_KEY_2 */
    case 0x33c:	/* CONTROL_TEST_KEY_3 */
    case 0x340:	/* CONTROL_TEST_KEY_4 */
    case 0x344:	/* CONTROL_TEST_KEY_5 */
    case 0x348:	/* CONTROL_TEST_KEY_6 */
    case 0x34c:	/* CONTROL_TEST_KEY_7 */
    case 0x350:	/* CONTROL_TEST_KEY_8 */
    case 0x354:	/* CONTROL_TEST_KEY_9 */
        /* Can only be accessed in secure mode and when C_FieldAccEnable
         * bit is set in CONTROL_SEC_CTRL.
         * TODO: otherwise an interconnect access error is generated.  */
        return 0;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_sysctl_write8(void *opaque, hwaddr addr, uint32_t value)
{
    struct omap_sysctl_s *s = opaque;
    int pad_offset, byte_offset;
    int prev_value;

    switch (addr) {
    case 0x030 ... 0x140:	/* CONTROL_PADCONF - only used in the POP */
        pad_offset = (addr - 0x30) >> 2;
        byte_offset = (addr - 0x30) & (4 - 1);

        prev_value = s->padconf[pad_offset];
        prev_value &= ~(0xff << (byte_offset * 8));
        prev_value |= ((value & 0x1f1f1f1f) << (byte_offset * 8)) & 0x1f1f1f1f;
        s->padconf[pad_offset] = prev_value;
        break;

    default:
        OMAP_BAD_REG(addr);
        break;
    }
}

static void omap_sysctl_write(void *opaque, hwaddr addr, uint32_t value)
{
    struct omap_sysctl_s *s = opaque;

    switch (addr) {
    case 0x000:	/* CONTROL_REVISION */
    case 0x2a4:	/* CONTROL_MSUSPENDMUX_5 */
    case 0x2c0:	/* CONTROL_PSA_VALUE */
    case 0x2f8:	/* CONTROL_STATUS */
    case 0x2fc:	/* CONTROL_GENERAL_PURPOSE_STATUS */
    case 0x300:	/* CONTROL_RPUB_KEY_H_0 */
    case 0x304:	/* CONTROL_RPUB_KEY_H_1 */
    case 0x308:	/* CONTROL_RPUB_KEY_H_2 */
    case 0x30c:	/* CONTROL_RPUB_KEY_H_3 */
    case 0x310:	/* CONTROL_RAND_KEY_0 */
    case 0x314:	/* CONTROL_RAND_KEY_1 */
    case 0x318:	/* CONTROL_RAND_KEY_2 */
    case 0x31c:	/* CONTROL_RAND_KEY_3 */
    case 0x320:	/* CONTROL_CUST_KEY_0 */
    case 0x324:	/* CONTROL_CUST_KEY_1 */
    case 0x330:	/* CONTROL_TEST_KEY_0 */
    case 0x334:	/* CONTROL_TEST_KEY_1 */
    case 0x338:	/* CONTROL_TEST_KEY_2 */
    case 0x33c:	/* CONTROL_TEST_KEY_3 */
    case 0x340:	/* CONTROL_TEST_KEY_4 */
    case 0x344:	/* CONTROL_TEST_KEY_5 */
    case 0x348:	/* CONTROL_TEST_KEY_6 */
    case 0x34c:	/* CONTROL_TEST_KEY_7 */
    case 0x350:	/* CONTROL_TEST_KEY_8 */
    case 0x354:	/* CONTROL_TEST_KEY_9 */
        OMAP_RO_REG(addr);
        return;

    case 0x010:	/* CONTROL_SYSCONFIG */
        s->sysconfig = value & 0x1e;
        break;

    case 0x030 ... 0x140:	/* CONTROL_PADCONF - only used in the POP */
        /* XXX: should check constant bits */
        s->padconf[(addr - 0x30) >> 2] = value & 0x1f1f1f1f;
        break;

    case 0x270:	/* CONTROL_DEBOBS */
        s->obs = value & 0xff;
        break;

    case 0x274:	/* CONTROL_DEVCONF */
        s->devconfig = value & 0xffffc7ff;
        break;

    case 0x28c:	/* CONTROL_EMU_SUPPORT */
        break;

    case 0x290:	/* CONTROL_MSUSPENDMUX_0 */
        s->msuspendmux[0] = value & 0x3fffffff;
        break;
    case 0x294:	/* CONTROL_MSUSPENDMUX_1 */
        s->msuspendmux[1] = value & 0x3fffffff;
        break;
    case 0x298:	/* CONTROL_MSUSPENDMUX_2 */
        s->msuspendmux[2] = value & 0x3fffffff;
        break;
    case 0x29c:	/* CONTROL_MSUSPENDMUX_3 */
        s->msuspendmux[3] = value & 0x3fffffff;
        break;
    case 0x2a0:	/* CONTROL_MSUSPENDMUX_4 */
        s->msuspendmux[4] = value & 0x3fffffff;
        break;

    case 0x2b8:	/* CONTROL_PSA_CTRL */
        s->psaconfig = value & 0x1c;
        s->psaconfig |= (value & 0x20) ? 2 : 1;
        break;
    case 0x2bc:	/* CONTROL_PSA_CMD */
        break;

    case 0x2b0:	/* CONTROL_SEC_CTRL */
    case 0x2b4:	/* CONTROL_SEC_TEST */
    case 0x2d0:	/* CONTROL_SEC_EMU */
    case 0x2d4:	/* CONTROL_SEC_TAP */
    case 0x2d8:	/* CONTROL_OCM_RAM_PERM */
    case 0x2dc:	/* CONTROL_OCM_PUB_RAM_ADD */
    case 0x2e0:	/* CONTROL_EXT_SEC_RAM_START_ADD */
    case 0x2e4:	/* CONTROL_EXT_SEC_RAM_STOP_ADD */
    case 0x2f0:	/* CONTROL_SEC_STATUS */
    case 0x2f4:	/* CONTROL_SEC_ERR_STATUS */
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static uint64_t omap_sysctl_readfn(void *opaque, hwaddr addr,
                                   unsigned size)
{
    switch (size) {
    case 1:
        return omap_sysctl_read8(opaque, addr);
    case 2:
        return omap_badwidth_read32(opaque, addr); /* TODO */
    case 4:
        return omap_sysctl_read(opaque, addr);
    default:
        g_assert_not_reached();
    }
}

static void omap_sysctl_writefn(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        omap_sysctl_write8(opaque, addr, value);
        break;
    case 2:
        omap_badwidth_write32(opaque, addr, value); /* TODO */
        break;
    case 4:
        omap_sysctl_write(opaque, addr, value);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps omap_sysctl_ops = {
    .read = omap_sysctl_readfn,
    .write = omap_sysctl_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_sysctl_reset(struct omap_sysctl_s *s)
{
    /* (power-on reset) */
    s->sysconfig = 0;
    s->obs = 0;
    s->devconfig = 0x0c000000;
    s->msuspendmux[0] = 0x00000000;
    s->msuspendmux[1] = 0x00000000;
    s->msuspendmux[2] = 0x00000000;
    s->msuspendmux[3] = 0x00000000;
    s->msuspendmux[4] = 0x00000000;
    s->psaconfig = 1;

    s->padconf[0x00] = 0x000f0f0f;
    s->padconf[0x01] = 0x00000000;
    s->padconf[0x02] = 0x00000000;
    s->padconf[0x03] = 0x00000000;
    s->padconf[0x04] = 0x00000000;
    s->padconf[0x05] = 0x00000000;
    s->padconf[0x06] = 0x00000000;
    s->padconf[0x07] = 0x00000000;
    s->padconf[0x08] = 0x08080800;
    s->padconf[0x09] = 0x08080808;
    s->padconf[0x0a] = 0x08080808;
    s->padconf[0x0b] = 0x08080808;
    s->padconf[0x0c] = 0x08080808;
    s->padconf[0x0d] = 0x08080800;
    s->padconf[0x0e] = 0x08080808;
    s->padconf[0x0f] = 0x08080808;
    s->padconf[0x10] = 0x18181808;	/* | 0x07070700 if SBoot3 */
    s->padconf[0x11] = 0x18181818;	/* | 0x07070707 if SBoot3 */
    s->padconf[0x12] = 0x18181818;	/* | 0x07070707 if SBoot3 */
    s->padconf[0x13] = 0x18181818;	/* | 0x07070707 if SBoot3 */
    s->padconf[0x14] = 0x18181818;	/* | 0x00070707 if SBoot3 */
    s->padconf[0x15] = 0x18181818;
    s->padconf[0x16] = 0x18181818;	/* | 0x07000000 if SBoot3 */
    s->padconf[0x17] = 0x1f001f00;
    s->padconf[0x18] = 0x1f1f1f1f;
    s->padconf[0x19] = 0x00000000;
    s->padconf[0x1a] = 0x1f180000;
    s->padconf[0x1b] = 0x00001f1f;
    s->padconf[0x1c] = 0x1f001f00;
    s->padconf[0x1d] = 0x00000000;
    s->padconf[0x1e] = 0x00000000;
    s->padconf[0x1f] = 0x08000000;
    s->padconf[0x20] = 0x08080808;
    s->padconf[0x21] = 0x08080808;
    s->padconf[0x22] = 0x0f080808;
    s->padconf[0x23] = 0x0f0f0f0f;
    s->padconf[0x24] = 0x000f0f0f;
    s->padconf[0x25] = 0x1f1f1f0f;
    s->padconf[0x26] = 0x080f0f1f;
    s->padconf[0x27] = 0x070f1808;
    s->padconf[0x28] = 0x0f070707;
    s->padconf[0x29] = 0x000f0f1f;
    s->padconf[0x2a] = 0x0f0f0f1f;
    s->padconf[0x2b] = 0x08000000;
    s->padconf[0x2c] = 0x0000001f;
    s->padconf[0x2d] = 0x0f0f1f00;
    s->padconf[0x2e] = 0x1f1f0f0f;
    s->padconf[0x2f] = 0x0f1f1f1f;
    s->padconf[0x30] = 0x0f0f0f0f;
    s->padconf[0x31] = 0x0f1f0f1f;
    s->padconf[0x32] = 0x0f0f0f0f;
    s->padconf[0x33] = 0x0f1f0f1f;
    s->padconf[0x34] = 0x1f1f0f0f;
    s->padconf[0x35] = 0x0f0f1f1f;
    s->padconf[0x36] = 0x0f0f1f0f;
    s->padconf[0x37] = 0x0f0f0f0f;
    s->padconf[0x38] = 0x1f18180f;
    s->padconf[0x39] = 0x1f1f1f1f;
    s->padconf[0x3a] = 0x00001f1f;
    s->padconf[0x3b] = 0x00000000;
    s->padconf[0x3c] = 0x00000000;
    s->padconf[0x3d] = 0x0f0f0f0f;
    s->padconf[0x3e] = 0x18000f0f;
    s->padconf[0x3f] = 0x00070000;
    s->padconf[0x40] = 0x00000707;
    s->padconf[0x41] = 0x0f1f0700;
    s->padconf[0x42] = 0x1f1f070f;
    s->padconf[0x43] = 0x0008081f;
    s->padconf[0x44] = 0x00000800;
}

static struct omap_sysctl_s *omap_sysctl_init(struct omap_target_agent_s *ta,
                omap_clk iclk, struct omap_mpu_state_s *mpu)
{
    struct omap_sysctl_s *s = g_new0(struct omap_sysctl_s, 1);

    s->mpu = mpu;
    omap_sysctl_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_sysctl_ops, s, "omap.sysctl",
                          omap_l4_region_size(ta, 0));
    omap_l4_attach(ta, 0, &s->iomem);

    return s;
}

/* General chip reset */
static void omap2_mpu_reset(void *opaque)
{
    struct omap_mpu_state_s *mpu = opaque;

    omap_dma_reset(mpu->dma);
    omap_prcm_reset(mpu->prcm);
    omap_sysctl_reset(mpu->sysc);
    omap_gp_timer_reset(mpu->gptimer[0]);
    omap_gp_timer_reset(mpu->gptimer[1]);
    omap_gp_timer_reset(mpu->gptimer[2]);
    omap_gp_timer_reset(mpu->gptimer[3]);
    omap_gp_timer_reset(mpu->gptimer[4]);
    omap_gp_timer_reset(mpu->gptimer[5]);
    omap_gp_timer_reset(mpu->gptimer[6]);
    omap_gp_timer_reset(mpu->gptimer[7]);
    omap_gp_timer_reset(mpu->gptimer[8]);
    omap_gp_timer_reset(mpu->gptimer[9]);
    omap_gp_timer_reset(mpu->gptimer[10]);
    omap_gp_timer_reset(mpu->gptimer[11]);
    omap_synctimer_reset(mpu->synctimer);
    omap_sdrc_reset(mpu->sdrc);
    omap_gpmc_reset(mpu->gpmc);
    omap_dss_reset(mpu->dss);
    omap_uart_reset(mpu->uart[0]);
    omap_uart_reset(mpu->uart[1]);
    omap_uart_reset(mpu->uart[2]);
    omap_mmc_reset(mpu->mmc);
    omap_mcspi_reset(mpu->mcspi[0]);
    omap_mcspi_reset(mpu->mcspi[1]);
    cpu_reset(CPU(mpu->cpu));
}

static int omap2_validate_addr(struct omap_mpu_state_s *s,
                hwaddr addr)
{
    return 1;
}

static const struct dma_irq_map omap2_dma_irq_map[] = {
    { 0, OMAP_INT_24XX_SDMA_IRQ0 },
    { 0, OMAP_INT_24XX_SDMA_IRQ1 },
    { 0, OMAP_INT_24XX_SDMA_IRQ2 },
    { 0, OMAP_INT_24XX_SDMA_IRQ3 },
};

struct omap_mpu_state_s *omap2420_mpu_init(MemoryRegion *sdram,
                const char *cpu_type)
{
    struct omap_mpu_state_s *s = g_new0(struct omap_mpu_state_s, 1);
    qemu_irq dma_irqs[4];
    DriveInfo *dinfo;
    int i;
    SysBusDevice *busdev;
    struct omap_target_agent_s *ta;
    MemoryRegion *sysmem = get_system_memory();

    /* Core */
    s->mpu_model = omap2420;
    s->cpu = ARM_CPU(cpu_create(cpu_type));
    s->sram_size = OMAP242X_SRAM_SIZE;

    s->wakeup = qemu_allocate_irq(omap_mpu_wakeup, s, 0);

    /* Clocks */
    omap_clk_init(s);

    /* Memory-mapped stuff */
    memory_region_init_ram(&s->sram, NULL, "omap2.sram", s->sram_size,
                           &error_fatal);
    memory_region_add_subregion(sysmem, OMAP2_SRAM_BASE, &s->sram);

    s->l4 = omap_l4_init(sysmem, OMAP2_L4_BASE, 54);

    /* Actually mapped at any 2K boundary in the ARM11 private-peripheral if */
    s->ih[0] = qdev_new("omap2-intc");
    qdev_prop_set_uint8(s->ih[0], "revision", 0x21);
    omap_intc_set_fclk(OMAP_INTC(s->ih[0]), omap_findclk(s, "mpu_intc_fclk"));
    omap_intc_set_iclk(OMAP_INTC(s->ih[0]), omap_findclk(s, "mpu_intc_iclk"));
    busdev = SYS_BUS_DEVICE(s->ih[0]);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(busdev, 1,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ));
    sysbus_mmio_map(busdev, 0, 0x480fe000);
    s->prcm = omap_prcm_init(omap_l4tao(s->l4, 3),
                             qdev_get_gpio_in(s->ih[0],
                                              OMAP_INT_24XX_PRCM_MPU_IRQ),
                             NULL, NULL, s);

    s->sysc = omap_sysctl_init(omap_l4tao(s->l4, 1),
                    omap_findclk(s, "omapctrl_iclk"), s);

    for (i = 0; i < 4; i++) {
        dma_irqs[i] = qdev_get_gpio_in(s->ih[omap2_dma_irq_map[i].ih],
                                       omap2_dma_irq_map[i].intr);
    }
    s->dma = omap_dma4_init(0x48056000, dma_irqs, sysmem, s, 256, 32,
                    omap_findclk(s, "sdma_iclk"),
                    omap_findclk(s, "sdma_fclk"));
    s->port->addr_valid = omap2_validate_addr;

    /* Register SDRAM and SRAM ports for fast DMA transfers.  */
    soc_dma_port_add_mem(s->dma, memory_region_get_ram_ptr(sdram),
                         OMAP2_Q2_BASE, memory_region_size(sdram));
    soc_dma_port_add_mem(s->dma, memory_region_get_ram_ptr(&s->sram),
                         OMAP2_SRAM_BASE, s->sram_size);

    s->uart[0] = omap2_uart_init(sysmem, omap_l4ta(s->l4, 19),
                                 qdev_get_gpio_in(s->ih[0],
                                                  OMAP_INT_24XX_UART1_IRQ),
                    omap_findclk(s, "uart1_fclk"),
                    omap_findclk(s, "uart1_iclk"),
                    s->drq[OMAP24XX_DMA_UART1_TX],
                    s->drq[OMAP24XX_DMA_UART1_RX],
                    "uart1",
                    serial_hd(0));
    s->uart[1] = omap2_uart_init(sysmem, omap_l4ta(s->l4, 20),
                                 qdev_get_gpio_in(s->ih[0],
                                                  OMAP_INT_24XX_UART2_IRQ),
                    omap_findclk(s, "uart2_fclk"),
                    omap_findclk(s, "uart2_iclk"),
                    s->drq[OMAP24XX_DMA_UART2_TX],
                    s->drq[OMAP24XX_DMA_UART2_RX],
                    "uart2",
                    serial_hd(0) ? serial_hd(1) : NULL);
    s->uart[2] = omap2_uart_init(sysmem, omap_l4ta(s->l4, 21),
                                 qdev_get_gpio_in(s->ih[0],
                                                  OMAP_INT_24XX_UART3_IRQ),
                    omap_findclk(s, "uart3_fclk"),
                    omap_findclk(s, "uart3_iclk"),
                    s->drq[OMAP24XX_DMA_UART3_TX],
                    s->drq[OMAP24XX_DMA_UART3_RX],
                    "uart3",
                    serial_hd(0) && serial_hd(1) ? serial_hd(2) : NULL);

    s->gptimer[0] = omap_gp_timer_init(omap_l4ta(s->l4, 7),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER1),
                    omap_findclk(s, "wu_gpt1_clk"),
                    omap_findclk(s, "wu_l4_iclk"));
    s->gptimer[1] = omap_gp_timer_init(omap_l4ta(s->l4, 8),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER2),
                    omap_findclk(s, "core_gpt2_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[2] = omap_gp_timer_init(omap_l4ta(s->l4, 22),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER3),
                    omap_findclk(s, "core_gpt3_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[3] = omap_gp_timer_init(omap_l4ta(s->l4, 23),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER4),
                    omap_findclk(s, "core_gpt4_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[4] = omap_gp_timer_init(omap_l4ta(s->l4, 24),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER5),
                    omap_findclk(s, "core_gpt5_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[5] = omap_gp_timer_init(omap_l4ta(s->l4, 25),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER6),
                    omap_findclk(s, "core_gpt6_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[6] = omap_gp_timer_init(omap_l4ta(s->l4, 26),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER7),
                    omap_findclk(s, "core_gpt7_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[7] = omap_gp_timer_init(omap_l4ta(s->l4, 27),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER8),
                    omap_findclk(s, "core_gpt8_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[8] = omap_gp_timer_init(omap_l4ta(s->l4, 28),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER9),
                    omap_findclk(s, "core_gpt9_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[9] = omap_gp_timer_init(omap_l4ta(s->l4, 29),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER10),
                    omap_findclk(s, "core_gpt10_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[10] = omap_gp_timer_init(omap_l4ta(s->l4, 30),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER11),
                    omap_findclk(s, "core_gpt11_clk"),
                    omap_findclk(s, "core_l4_iclk"));
    s->gptimer[11] = omap_gp_timer_init(omap_l4ta(s->l4, 31),
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPTIMER12),
                    omap_findclk(s, "core_gpt12_clk"),
                    omap_findclk(s, "core_l4_iclk"));

    omap_tap_init(omap_l4ta(s->l4, 2), s);

    s->synctimer = omap_synctimer_init(omap_l4tao(s->l4, 2), s,
                    omap_findclk(s, "clk32-kHz"),
                    omap_findclk(s, "core_l4_iclk"));

    s->i2c[0] = qdev_new("omap_i2c");
    qdev_prop_set_uint8(s->i2c[0], "revision", 0x34);
    omap_i2c_set_iclk(OMAP_I2C(s->i2c[0]), omap_findclk(s, "i2c1.iclk"));
    omap_i2c_set_fclk(OMAP_I2C(s->i2c[0]), omap_findclk(s, "i2c1.fclk"));
    busdev = SYS_BUS_DEVICE(s->i2c[0]);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0,
                       qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_I2C1_IRQ));
    sysbus_connect_irq(busdev, 1, s->drq[OMAP24XX_DMA_I2C1_TX]);
    sysbus_connect_irq(busdev, 2, s->drq[OMAP24XX_DMA_I2C1_RX]);
    sysbus_mmio_map(busdev, 0, omap_l4_region_base(omap_l4tao(s->l4, 5), 0));

    s->i2c[1] = qdev_new("omap_i2c");
    qdev_prop_set_uint8(s->i2c[1], "revision", 0x34);
    omap_i2c_set_iclk(OMAP_I2C(s->i2c[1]), omap_findclk(s, "i2c2.iclk"));
    omap_i2c_set_fclk(OMAP_I2C(s->i2c[1]), omap_findclk(s, "i2c2.fclk"));
    busdev = SYS_BUS_DEVICE(s->i2c[1]);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0,
                       qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_I2C2_IRQ));
    sysbus_connect_irq(busdev, 1, s->drq[OMAP24XX_DMA_I2C2_TX]);
    sysbus_connect_irq(busdev, 2, s->drq[OMAP24XX_DMA_I2C2_RX]);
    sysbus_mmio_map(busdev, 0, omap_l4_region_base(omap_l4tao(s->l4, 6), 0));

    s->gpio = qdev_new("omap2-gpio");
    qdev_prop_set_int32(s->gpio, "mpu_model", s->mpu_model);
    omap2_gpio_set_iclk(OMAP2_GPIO(s->gpio), omap_findclk(s, "gpio_iclk"));
    omap2_gpio_set_fclk(OMAP2_GPIO(s->gpio), 0, omap_findclk(s, "gpio1_dbclk"));
    omap2_gpio_set_fclk(OMAP2_GPIO(s->gpio), 1, omap_findclk(s, "gpio2_dbclk"));
    omap2_gpio_set_fclk(OMAP2_GPIO(s->gpio), 2, omap_findclk(s, "gpio3_dbclk"));
    omap2_gpio_set_fclk(OMAP2_GPIO(s->gpio), 3, omap_findclk(s, "gpio4_dbclk"));
    if (s->mpu_model == omap2430) {
        omap2_gpio_set_fclk(OMAP2_GPIO(s->gpio), 4,
                            omap_findclk(s, "gpio5_dbclk"));
    }
    busdev = SYS_BUS_DEVICE(s->gpio);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0,
                       qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPIO_BANK1));
    sysbus_connect_irq(busdev, 3,
                       qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPIO_BANK2));
    sysbus_connect_irq(busdev, 6,
                       qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPIO_BANK3));
    sysbus_connect_irq(busdev, 9,
                       qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPIO_BANK4));
    if (s->mpu_model == omap2430) {
        sysbus_connect_irq(busdev, 12,
                           qdev_get_gpio_in(s->ih[0],
                                            OMAP_INT_243X_GPIO_BANK5));
    }
    ta = omap_l4ta(s->l4, 3);
    sysbus_mmio_map(busdev, 0, omap_l4_region_base(ta, 1));
    sysbus_mmio_map(busdev, 1, omap_l4_region_base(ta, 0));
    sysbus_mmio_map(busdev, 2, omap_l4_region_base(ta, 2));
    sysbus_mmio_map(busdev, 3, omap_l4_region_base(ta, 4));
    sysbus_mmio_map(busdev, 4, omap_l4_region_base(ta, 5));

    s->sdrc = omap_sdrc_init(sysmem, 0x68009000);
    s->gpmc = omap_gpmc_init(s, 0x6800a000,
                             qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_GPMC_IRQ),
                             s->drq[OMAP24XX_DMA_GPMC]);

    dinfo = drive_get(IF_SD, 0, 0);
    if (!dinfo && !qtest_enabled()) {
        warn_report("missing SecureDigital device");
    }
    s->mmc = omap2_mmc_init(omap_l4tao(s->l4, 9),
                    dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_MMC_IRQ),
                    &s->drq[OMAP24XX_DMA_MMC1_TX],
                    omap_findclk(s, "mmc_fclk"), omap_findclk(s, "mmc_iclk"));

    s->mcspi[0] = omap_mcspi_init(omap_l4ta(s->l4, 35), 4,
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_MCSPI1_IRQ),
                    &s->drq[OMAP24XX_DMA_SPI1_TX0],
                    omap_findclk(s, "spi1_fclk"),
                    omap_findclk(s, "spi1_iclk"));
    s->mcspi[1] = omap_mcspi_init(omap_l4ta(s->l4, 36), 2,
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_MCSPI2_IRQ),
                    &s->drq[OMAP24XX_DMA_SPI2_TX0],
                    omap_findclk(s, "spi2_fclk"),
                    omap_findclk(s, "spi2_iclk"));

    s->dss = omap_dss_init(omap_l4ta(s->l4, 10), sysmem, 0x68000800,
                    /* XXX wire M_IRQ_25, D_L2_IRQ_30 and I_IRQ_13 together */
                    qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_DSS_IRQ),
                           s->drq[OMAP24XX_DMA_DSS],
                    omap_findclk(s, "dss_clk1"), omap_findclk(s, "dss_clk2"),
                    omap_findclk(s, "dss_54m_clk"),
                    omap_findclk(s, "dss_l3_iclk"),
                    omap_findclk(s, "dss_l4_iclk"));

    omap_sti_init(omap_l4ta(s->l4, 18), sysmem, 0x54000000,
                  qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_STI),
                  omap_findclk(s, "emul_ck"),
                    serial_hd(0) && serial_hd(1) && serial_hd(2) ?
                    serial_hd(3) : NULL);

    s->eac = omap_eac_init(omap_l4ta(s->l4, 32),
                           qdev_get_gpio_in(s->ih[0], OMAP_INT_24XX_EAC_IRQ),
                    /* Ten consecutive lines */
                    &s->drq[OMAP24XX_DMA_EAC_AC_RD],
                    omap_findclk(s, "func_96m_clk"),
                    omap_findclk(s, "core_l4_iclk"));

    /* All register mappings (including those not currently implemented):
     * SystemControlMod	48000000 - 48000fff
     * SystemControlL4	48001000 - 48001fff
     * 32kHz Timer Mod	48004000 - 48004fff
     * 32kHz Timer L4	48005000 - 48005fff
     * PRCM ModA	48008000 - 480087ff
     * PRCM ModB	48008800 - 48008fff
     * PRCM L4		48009000 - 48009fff
     * TEST-BCM Mod	48012000 - 48012fff
     * TEST-BCM L4	48013000 - 48013fff
     * TEST-TAP Mod	48014000 - 48014fff
     * TEST-TAP L4	48015000 - 48015fff
     * GPIO1 Mod	48018000 - 48018fff
     * GPIO Top		48019000 - 48019fff
     * GPIO2 Mod	4801a000 - 4801afff
     * GPIO L4		4801b000 - 4801bfff
     * GPIO3 Mod	4801c000 - 4801cfff
     * GPIO4 Mod	4801e000 - 4801efff
     * WDTIMER1 Mod	48020000 - 48010fff
     * WDTIMER Top	48021000 - 48011fff
     * WDTIMER2 Mod	48022000 - 48012fff
     * WDTIMER L4	48023000 - 48013fff
     * WDTIMER3 Mod	48024000 - 48014fff
     * WDTIMER3 L4	48025000 - 48015fff
     * WDTIMER4 Mod	48026000 - 48016fff
     * WDTIMER4 L4	48027000 - 48017fff
     * GPTIMER1 Mod	48028000 - 48018fff
     * GPTIMER1 L4	48029000 - 48019fff
     * GPTIMER2 Mod	4802a000 - 4801afff
     * GPTIMER2 L4	4802b000 - 4801bfff
     * L4-Config AP	48040000 - 480407ff
     * L4-Config IP	48040800 - 48040fff
     * L4-Config LA	48041000 - 48041fff
     * ARM11ETB Mod	48048000 - 48049fff
     * ARM11ETB L4	4804a000 - 4804afff
     * DISPLAY Top	48050000 - 480503ff
     * DISPLAY DISPC	48050400 - 480507ff
     * DISPLAY RFBI	48050800 - 48050bff
     * DISPLAY VENC	48050c00 - 48050fff
     * DISPLAY L4	48051000 - 48051fff
     * CAMERA Top	48052000 - 480523ff
     * CAMERA core	48052400 - 480527ff
     * CAMERA DMA	48052800 - 48052bff
     * CAMERA MMU	48052c00 - 48052fff
     * CAMERA L4	48053000 - 48053fff
     * SDMA Mod		48056000 - 48056fff
     * SDMA L4		48057000 - 48057fff
     * SSI Top		48058000 - 48058fff
     * SSI GDD		48059000 - 48059fff
     * SSI Port1	4805a000 - 4805afff
     * SSI Port2	4805b000 - 4805bfff
     * SSI L4		4805c000 - 4805cfff
     * USB Mod		4805e000 - 480fefff
     * USB L4		4805f000 - 480fffff
     * WIN_TRACER1 Mod	48060000 - 48060fff
     * WIN_TRACER1 L4	48061000 - 48061fff
     * WIN_TRACER2 Mod	48062000 - 48062fff
     * WIN_TRACER2 L4	48063000 - 48063fff
     * WIN_TRACER3 Mod	48064000 - 48064fff
     * WIN_TRACER3 L4	48065000 - 48065fff
     * WIN_TRACER4 Top	48066000 - 480660ff
     * WIN_TRACER4 ETT	48066100 - 480661ff
     * WIN_TRACER4 WT	48066200 - 480662ff
     * WIN_TRACER4 L4	48067000 - 48067fff
     * XTI Mod		48068000 - 48068fff
     * XTI L4		48069000 - 48069fff
     * UART1 Mod	4806a000 - 4806afff
     * UART1 L4		4806b000 - 4806bfff
     * UART2 Mod	4806c000 - 4806cfff
     * UART2 L4		4806d000 - 4806dfff
     * UART3 Mod	4806e000 - 4806efff
     * UART3 L4		4806f000 - 4806ffff
     * I2C1 Mod		48070000 - 48070fff
     * I2C1 L4		48071000 - 48071fff
     * I2C2 Mod		48072000 - 48072fff
     * I2C2 L4		48073000 - 48073fff
     * McBSP1 Mod	48074000 - 48074fff
     * McBSP1 L4	48075000 - 48075fff
     * McBSP2 Mod	48076000 - 48076fff
     * McBSP2 L4	48077000 - 48077fff
     * GPTIMER3 Mod	48078000 - 48078fff
     * GPTIMER3 L4	48079000 - 48079fff
     * GPTIMER4 Mod	4807a000 - 4807afff
     * GPTIMER4 L4	4807b000 - 4807bfff
     * GPTIMER5 Mod	4807c000 - 4807cfff
     * GPTIMER5 L4	4807d000 - 4807dfff
     * GPTIMER6 Mod	4807e000 - 4807efff
     * GPTIMER6 L4	4807f000 - 4807ffff
     * GPTIMER7 Mod	48080000 - 48080fff
     * GPTIMER7 L4	48081000 - 48081fff
     * GPTIMER8 Mod	48082000 - 48082fff
     * GPTIMER8 L4	48083000 - 48083fff
     * GPTIMER9 Mod	48084000 - 48084fff
     * GPTIMER9 L4	48085000 - 48085fff
     * GPTIMER10 Mod	48086000 - 48086fff
     * GPTIMER10 L4	48087000 - 48087fff
     * GPTIMER11 Mod	48088000 - 48088fff
     * GPTIMER11 L4	48089000 - 48089fff
     * GPTIMER12 Mod	4808a000 - 4808afff
     * GPTIMER12 L4	4808b000 - 4808bfff
     * EAC Mod		48090000 - 48090fff
     * EAC L4		48091000 - 48091fff
     * FAC Mod		48092000 - 48092fff
     * FAC L4		48093000 - 48093fff
     * MAILBOX Mod	48094000 - 48094fff
     * MAILBOX L4	48095000 - 48095fff
     * SPI1 Mod		48098000 - 48098fff
     * SPI1 L4		48099000 - 48099fff
     * SPI2 Mod		4809a000 - 4809afff
     * SPI2 L4		4809b000 - 4809bfff
     * MMC/SDIO Mod	4809c000 - 4809cfff
     * MMC/SDIO L4	4809d000 - 4809dfff
     * MS_PRO Mod	4809e000 - 4809efff
     * MS_PRO L4	4809f000 - 4809ffff
     * RNG Mod		480a0000 - 480a0fff
     * RNG L4		480a1000 - 480a1fff
     * DES3DES Mod	480a2000 - 480a2fff
     * DES3DES L4	480a3000 - 480a3fff
     * SHA1MD5 Mod	480a4000 - 480a4fff
     * SHA1MD5 L4	480a5000 - 480a5fff
     * AES Mod		480a6000 - 480a6fff
     * AES L4		480a7000 - 480a7fff
     * PKA Mod		480a8000 - 480a9fff
     * PKA L4		480aa000 - 480aafff
     * MG Mod		480b0000 - 480b0fff
     * MG L4		480b1000 - 480b1fff
     * HDQ/1-wire Mod	480b2000 - 480b2fff
     * HDQ/1-wire L4	480b3000 - 480b3fff
     * MPU interrupt	480fe000 - 480fefff
     * STI channel base	54000000 - 5400ffff
     * IVA RAM		5c000000 - 5c01ffff
     * IVA ROM		5c020000 - 5c027fff
     * IMG_BUF_A	5c040000 - 5c040fff
     * IMG_BUF_B	5c042000 - 5c042fff
     * VLCDS		5c048000 - 5c0487ff
     * IMX_COEF		5c049000 - 5c04afff
     * IMX_CMD		5c051000 - 5c051fff
     * VLCDQ		5c053000 - 5c0533ff
     * VLCDH		5c054000 - 5c054fff
     * SEQ_CMD		5c055000 - 5c055fff
     * IMX_REG		5c056000 - 5c0560ff
     * VLCD_REG		5c056100 - 5c0561ff
     * SEQ_REG		5c056200 - 5c0562ff
     * IMG_BUF_REG	5c056300 - 5c0563ff
     * SEQIRQ_REG	5c056400 - 5c0564ff
     * OCP_REG		5c060000 - 5c060fff
     * SYSC_REG		5c070000 - 5c070fff
     * MMU_REG		5d000000 - 5d000fff
     * sDMA R		68000400 - 680005ff
     * sDMA W		68000600 - 680007ff
     * Display Control	68000800 - 680009ff
     * DSP subsystem	68000a00 - 68000bff
     * MPU subsystem	68000c00 - 68000dff
     * IVA subsystem	68001000 - 680011ff
     * USB		68001200 - 680013ff
     * Camera		68001400 - 680015ff
     * VLYNQ (firewall)	68001800 - 68001bff
     * VLYNQ		68001e00 - 68001fff
     * SSI		68002000 - 680021ff
     * L4		68002400 - 680025ff
     * DSP (firewall)	68002800 - 68002bff
     * DSP subsystem	68002e00 - 68002fff
     * IVA (firewall)	68003000 - 680033ff
     * IVA		68003600 - 680037ff
     * GFX		68003a00 - 68003bff
     * CMDWR emulation	68003c00 - 68003dff
     * SMS		68004000 - 680041ff
     * OCM		68004200 - 680043ff
     * GPMC		68004400 - 680045ff
     * RAM (firewall)	68005000 - 680053ff
     * RAM (err login)	68005400 - 680057ff
     * ROM (firewall)	68005800 - 68005bff
     * ROM (err login)	68005c00 - 68005fff
     * GPMC (firewall)	68006000 - 680063ff
     * GPMC (err login)	68006400 - 680067ff
     * SMS (err login)	68006c00 - 68006fff
     * SMS registers	68008000 - 68008fff
     * SDRC registers	68009000 - 68009fff
     * GPMC registers	6800a000   6800afff
     */

    qemu_register_reset(omap2_mpu_reset, s);

    return s;
}
