/*
 * QEMU SCI/SCIF serial port emulation
 *
 * Copyright (c) 2007 Magnus Damm
 *
 * Based on serial.c - QEMU 16450 UART emulation
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw.h"
#include "sh.h"
#include "qemu-char.h"
#include "exec-memory.h"

//#define DEBUG_SERIAL

#define SH_SERIAL_FLAG_TEND (1 << 0)
#define SH_SERIAL_FLAG_TDE  (1 << 1)
#define SH_SERIAL_FLAG_RDF  (1 << 2)
#define SH_SERIAL_FLAG_BRK  (1 << 3)
#define SH_SERIAL_FLAG_DR   (1 << 4)

#define SH_RX_FIFO_LENGTH (16)

typedef struct {
    MemoryRegion iomem;
    MemoryRegion iomem_p4;
    MemoryRegion iomem_a7;
    uint8_t smr;
    uint8_t brr;
    uint8_t scr;
    uint8_t dr; /* ftdr / tdr */
    uint8_t sr; /* fsr / ssr */
    uint16_t fcr;
    uint8_t sptr;

    uint8_t rx_fifo[SH_RX_FIFO_LENGTH]; /* frdr / rdr */
    uint8_t rx_cnt;
    uint8_t rx_tail;
    uint8_t rx_head;

    int freq;
    int feat;
    int flags;
    int rtrg;

    CharDriverState *chr;

    qemu_irq eri;
    qemu_irq rxi;
    qemu_irq txi;
    qemu_irq tei;
    qemu_irq bri;
} sh_serial_state;

static void sh_serial_clear_fifo(sh_serial_state * s)
{
    memset(s->rx_fifo, 0, SH_RX_FIFO_LENGTH);
    s->rx_cnt = 0;
    s->rx_head = 0;
    s->rx_tail = 0;
}

static void sh_serial_write(void *opaque, target_phys_addr_t offs,
                            uint64_t val, unsigned size)
{
    sh_serial_state *s = opaque;
    unsigned char ch;

#ifdef DEBUG_SERIAL
    printf("sh_serial: write offs=0x%02x val=0x%02x\n",
	   offs, val);
#endif
    switch(offs) {
    case 0x00: /* SMR */
        s->smr = val & ((s->feat & SH_SERIAL_FEAT_SCIF) ? 0x7b : 0xff);
        return;
    case 0x04: /* BRR */
        s->brr = val;
	return;
    case 0x08: /* SCR */
        /* TODO : For SH7751, SCIF mask should be 0xfb. */
        s->scr = val & ((s->feat & SH_SERIAL_FEAT_SCIF) ? 0xfa : 0xff);
        if (!(val & (1 << 5)))
            s->flags |= SH_SERIAL_FLAG_TEND;
        if ((s->feat & SH_SERIAL_FEAT_SCIF) && s->txi) {
	    qemu_set_irq(s->txi, val & (1 << 7));
        }
        if (!(val & (1 << 6))) {
	    qemu_set_irq(s->rxi, 0);
        }
        return;
    case 0x0c: /* FTDR / TDR */
        if (s->chr) {
            ch = val;
            qemu_chr_fe_write(s->chr, &ch, 1);
	}
	s->dr = val;
	s->flags &= ~SH_SERIAL_FLAG_TDE;
        return;
#if 0
    case 0x14: /* FRDR / RDR */
        ret = 0;
        break;
#endif
    }
    if (s->feat & SH_SERIAL_FEAT_SCIF) {
        switch(offs) {
        case 0x10: /* FSR */
            if (!(val & (1 << 6)))
                s->flags &= ~SH_SERIAL_FLAG_TEND;
            if (!(val & (1 << 5)))
                s->flags &= ~SH_SERIAL_FLAG_TDE;
            if (!(val & (1 << 4)))
                s->flags &= ~SH_SERIAL_FLAG_BRK;
            if (!(val & (1 << 1)))
                s->flags &= ~SH_SERIAL_FLAG_RDF;
            if (!(val & (1 << 0)))
                s->flags &= ~SH_SERIAL_FLAG_DR;

            if (!(val & (1 << 1)) || !(val & (1 << 0))) {
                if (s->rxi) {
                    qemu_set_irq(s->rxi, 0);
                }
            }
            return;
        case 0x18: /* FCR */
            s->fcr = val;
            switch ((val >> 6) & 3) {
            case 0:
                s->rtrg = 1;
                break;
            case 1:
                s->rtrg = 4;
                break;
            case 2:
                s->rtrg = 8;
                break;
            case 3:
                s->rtrg = 14;
                break;
            }
            if (val & (1 << 1)) {
                sh_serial_clear_fifo(s);
                s->sr &= ~(1 << 1);
            }

            return;
        case 0x20: /* SPTR */
            s->sptr = val & 0xf3;
            return;
        case 0x24: /* LSR */
            return;
        }
    }
    else {
        switch(offs) {
#if 0
        case 0x0c:
            ret = s->dr;
            break;
        case 0x10:
            ret = 0;
            break;
#endif
        case 0x1c:
            s->sptr = val & 0x8f;
            return;
        }
    }

    fprintf(stderr, "sh_serial: unsupported write to 0x%02x\n", offs);
    abort();
}

static uint64_t sh_serial_read(void *opaque, target_phys_addr_t offs,
                               unsigned size)
{
    sh_serial_state *s = opaque;
    uint32_t ret = ~0;

#if 0
    switch(offs) {
    case 0x00:
        ret = s->smr;
        break;
    case 0x04:
        ret = s->brr;
	break;
    case 0x08:
        ret = s->scr;
        break;
    case 0x14:
        ret = 0;
        break;
    }
#endif
    if (s->feat & SH_SERIAL_FEAT_SCIF) {
        switch(offs) {
        case 0x00: /* SMR */
            ret = s->smr;
            break;
        case 0x08: /* SCR */
            ret = s->scr;
            break;
        case 0x10: /* FSR */
            ret = 0;
            if (s->flags & SH_SERIAL_FLAG_TEND)
                ret |= (1 << 6);
            if (s->flags & SH_SERIAL_FLAG_TDE)
                ret |= (1 << 5);
            if (s->flags & SH_SERIAL_FLAG_BRK)
                ret |= (1 << 4);
            if (s->flags & SH_SERIAL_FLAG_RDF)
                ret |= (1 << 1);
            if (s->flags & SH_SERIAL_FLAG_DR)
                ret |= (1 << 0);

            if (s->scr & (1 << 5))
                s->flags |= SH_SERIAL_FLAG_TDE | SH_SERIAL_FLAG_TEND;

            break;
        case 0x14:
            if (s->rx_cnt > 0) {
                ret = s->rx_fifo[s->rx_tail++];
                s->rx_cnt--;
                if (s->rx_tail == SH_RX_FIFO_LENGTH)
                    s->rx_tail = 0;
                if (s->rx_cnt < s->rtrg)
                    s->flags &= ~SH_SERIAL_FLAG_RDF;
            }
            break;
#if 0
        case 0x18:
            ret = s->fcr;
            break;
#endif
        case 0x1c:
            ret = s->rx_cnt;
            break;
        case 0x20:
            ret = s->sptr;
            break;
        case 0x24:
            ret = 0;
            break;
        }
    }
    else {
        switch(offs) {
#if 0
        case 0x0c:
            ret = s->dr;
            break;
        case 0x10:
            ret = 0;
            break;
        case 0x14:
            ret = s->rx_fifo[0];
            break;
#endif
        case 0x1c:
            ret = s->sptr;
            break;
        }
    }
#ifdef DEBUG_SERIAL
    printf("sh_serial: read offs=0x%02x val=0x%x\n",
	   offs, ret);
#endif

    if (ret & ~((1 << 16) - 1)) {
        fprintf(stderr, "sh_serial: unsupported read from 0x%02x\n", offs);
        abort();
    }

    return ret;
}

static int sh_serial_can_receive(sh_serial_state *s)
{
    return s->scr & (1 << 4);
}

static void sh_serial_receive_break(sh_serial_state *s)
{
    if (s->feat & SH_SERIAL_FEAT_SCIF)
        s->sr |= (1 << 4);
}

static int sh_serial_can_receive1(void *opaque)
{
    sh_serial_state *s = opaque;
    return sh_serial_can_receive(s);
}

static void sh_serial_receive1(void *opaque, const uint8_t *buf, int size)
{
    sh_serial_state *s = opaque;

    if (s->feat & SH_SERIAL_FEAT_SCIF) {
        int i;
        for (i = 0; i < size; i++) {
            if (s->rx_cnt < SH_RX_FIFO_LENGTH) {
                s->rx_fifo[s->rx_head++] = buf[i];
                if (s->rx_head == SH_RX_FIFO_LENGTH) {
                    s->rx_head = 0;
                }
                s->rx_cnt++;
                if (s->rx_cnt >= s->rtrg) {
                    s->flags |= SH_SERIAL_FLAG_RDF;
                    if (s->scr & (1 << 6) && s->rxi) {
                        qemu_set_irq(s->rxi, 1);
                    }
                }
            }
        }
    } else {
        s->rx_fifo[0] = buf[0];
    }
}

static void sh_serial_event(void *opaque, int event)
{
    sh_serial_state *s = opaque;
    if (event == CHR_EVENT_BREAK)
        sh_serial_receive_break(s);
}

static const MemoryRegionOps sh_serial_ops = {
    .read = sh_serial_read,
    .write = sh_serial_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void sh_serial_init(MemoryRegion *sysmem,
                    target_phys_addr_t base, int feat,
                    uint32_t freq, CharDriverState *chr,
                    qemu_irq eri_source,
                    qemu_irq rxi_source,
                    qemu_irq txi_source,
                    qemu_irq tei_source,
                    qemu_irq bri_source)
{
    sh_serial_state *s;

    s = g_malloc0(sizeof(sh_serial_state));

    s->feat = feat;
    s->flags = SH_SERIAL_FLAG_TEND | SH_SERIAL_FLAG_TDE;
    s->rtrg = 1;

    s->smr = 0;
    s->brr = 0xff;
    s->scr = 1 << 5; /* pretend that TX is enabled so early printk works */
    s->sptr = 0;

    if (feat & SH_SERIAL_FEAT_SCIF) {
        s->fcr = 0;
    }
    else {
        s->dr = 0xff;
    }

    sh_serial_clear_fifo(s);

    memory_region_init_io(&s->iomem, &sh_serial_ops, s,
                          "serial", 0x100000000ULL);

    memory_region_init_alias(&s->iomem_p4, "serial-p4", &s->iomem,
                             0, 0x28);
    memory_region_add_subregion(sysmem, P4ADDR(base), &s->iomem_p4);

    memory_region_init_alias(&s->iomem_a7, "serial-a7", &s->iomem,
                             0, 0x28);
    memory_region_add_subregion(sysmem, A7ADDR(base), &s->iomem_a7);

    s->chr = chr;

    if (chr)
        qemu_chr_add_handlers(chr, sh_serial_can_receive1, sh_serial_receive1,
			      sh_serial_event, s);

    s->eri = eri_source;
    s->rxi = rxi_source;
    s->txi = txi_source;
    s->tei = tei_source;
    s->bri = bri_source;
}
