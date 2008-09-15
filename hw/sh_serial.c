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
#include <assert.h>

//#define DEBUG_SERIAL

#define SH_SERIAL_FLAG_TEND (1 << 0)
#define SH_SERIAL_FLAG_TDE  (1 << 1)
#define SH_SERIAL_FLAG_RDF  (1 << 2)
#define SH_SERIAL_FLAG_BRK  (1 << 3)
#define SH_SERIAL_FLAG_DR   (1 << 4)

#define SH_RX_FIFO_LENGTH (16)

typedef struct {
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

    target_phys_addr_t base;
    int freq;
    int feat;
    int flags;
    int rtrg;

    CharDriverState *chr;

    struct intc_source *eri;
    struct intc_source *rxi;
    struct intc_source *txi;
    struct intc_source *tei;
    struct intc_source *bri;
} sh_serial_state;

static void sh_serial_clear_fifo(sh_serial_state * s)
{
    memset(s->rx_fifo, 0, SH_RX_FIFO_LENGTH);
    s->rx_cnt = 0;
    s->rx_head = 0;
    s->rx_tail = 0;
}

static void sh_serial_ioport_write(void *opaque, uint32_t offs, uint32_t val)
{
    sh_serial_state *s = opaque;
    unsigned char ch;

#ifdef DEBUG_SERIAL
    printf("sh_serial: write base=0x%08lx offs=0x%02x val=0x%02x\n",
	   (unsigned long) s->base, offs, val);
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
            if ((val & (1 << 7)) && !(s->txi->asserted))
                sh_intc_toggle_source(s->txi, 0, 1);
            else if (!(val & (1 << 7)) && s->txi->asserted)
                sh_intc_toggle_source(s->txi, 0, -1);
        }
        if (!(val & (1 << 6)) && s->rxi->asserted) {
	    sh_intc_toggle_source(s->rxi, 0, -1);
        }
        return;
    case 0x0c: /* FTDR / TDR */
        if (s->chr) {
            ch = val;
            qemu_chr_write(s->chr, &ch, 1);
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
                if (s->rxi && s->rxi->asserted) {
                    sh_intc_toggle_source(s->rxi, 0, -1);
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
#if 0
        switch(offs) {
        case 0x0c:
            ret = s->dr;
            break;
        case 0x10:
            ret = 0;
            break;
        case 0x1c:
            ret = s->sptr;
            break;
        }
#endif
    }

    fprintf(stderr, "sh_serial: unsupported write to 0x%02x\n", offs);
    assert(0);
}

static uint32_t sh_serial_ioport_read(void *opaque, uint32_t offs)
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
#if 0
        switch(offs) {
        case 0x0c:
            ret = s->dr;
            break;
        case 0x10:
            ret = 0;
            break;
        case 0x14:
            ret = s->rx_fifo[0];
            break;
        case 0x1c:
            ret = s->sptr;
            break;
        }
#endif
    }
#ifdef DEBUG_SERIAL
    printf("sh_serial: read base=0x%08lx offs=0x%02x val=0x%x\n",
	   (unsigned long) s->base, offs, ret);
#endif

    if (ret & ~((1 << 16) - 1)) {
        fprintf(stderr, "sh_serial: unsupported read from 0x%02x\n", offs);
	assert(0);
    }

    return ret;
}

static int sh_serial_can_receive(sh_serial_state *s)
{
    return s->scr & (1 << 4);
}

static void sh_serial_receive_byte(sh_serial_state *s, int ch)
{
    if (s->feat & SH_SERIAL_FEAT_SCIF) {
        if (s->rx_cnt < SH_RX_FIFO_LENGTH) {
            s->rx_fifo[s->rx_head++] = ch;
            if (s->rx_head == SH_RX_FIFO_LENGTH)
                s->rx_head = 0;
            s->rx_cnt++;
            if (s->rx_cnt >= s->rtrg) {
                s->flags |= SH_SERIAL_FLAG_RDF;
                if (s->scr & (1 << 6) && s->rxi) {
                    sh_intc_toggle_source(s->rxi, 0, 1);
                }
            }
        }
    } else {
        s->rx_fifo[0] = ch;
    }
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
    sh_serial_receive_byte(s, buf[0]);
}

static void sh_serial_event(void *opaque, int event)
{
    sh_serial_state *s = opaque;
    if (event == CHR_EVENT_BREAK)
        sh_serial_receive_break(s);
}

static uint32_t sh_serial_read (void *opaque, target_phys_addr_t addr)
{
    sh_serial_state *s = opaque;
    return sh_serial_ioport_read(s, addr - s->base);
}

static void sh_serial_write (void *opaque,
                             target_phys_addr_t addr, uint32_t value)
{
    sh_serial_state *s = opaque;
    sh_serial_ioport_write(s, addr - s->base, value);
}

static CPUReadMemoryFunc *sh_serial_readfn[] = {
    &sh_serial_read,
    &sh_serial_read,
    &sh_serial_read,
};

static CPUWriteMemoryFunc *sh_serial_writefn[] = {
    &sh_serial_write,
    &sh_serial_write,
    &sh_serial_write,
};

void sh_serial_init (target_phys_addr_t base, int feat,
		     uint32_t freq, CharDriverState *chr,
		     struct intc_source *eri_source,
		     struct intc_source *rxi_source,
		     struct intc_source *txi_source,
		     struct intc_source *tei_source,
		     struct intc_source *bri_source)
{
    sh_serial_state *s;
    int s_io_memory;

    s = qemu_mallocz(sizeof(sh_serial_state));
    if (!s)
        return;

    s->base = base;
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

    s_io_memory = cpu_register_io_memory(0, sh_serial_readfn,
					 sh_serial_writefn, s);
    cpu_register_physical_memory(base, 0x28, s_io_memory);

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
