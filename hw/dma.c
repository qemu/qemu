/*
 * QEMU DMA emulation
 * 
 * Copyright (c) 2003 Vassili Karpov (malc)
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
#include "vl.h"

//#define DEBUG_DMA

#define log(...) fprintf (stderr, "dma: " __VA_ARGS__)
#ifdef DEBUG_DMA
#define lwarn(...) fprintf (stderr, "dma: " __VA_ARGS__)
#define linfo(...) fprintf (stderr, "dma: " __VA_ARGS__)
#define ldebug(...) fprintf (stderr, "dma: " __VA_ARGS__)
#else
#define lwarn(...)
#define linfo(...)
#define ldebug(...)
#endif

#define LENOFA(a) ((int) (sizeof(a)/sizeof(a[0])))

struct dma_regs {
    int now[2];
    uint16_t base[2];
    uint8_t mode;
    uint8_t page;
    uint8_t dack;
    uint8_t eop;
    DMA_transfer_handler transfer_handler;
    void *opaque;
};

#define ADDR 0
#define COUNT 1

static struct dma_cont {
    uint8_t status;
    uint8_t command;
    uint8_t mask;
    uint8_t flip_flop;
    int dshift;
    struct dma_regs regs[4];
} dma_controllers[2];

enum {
  CMD_MEMORY_TO_MEMORY = 0x01,
  CMD_FIXED_ADDRESS    = 0x02,
  CMD_BLOCK_CONTROLLER = 0x04,
  CMD_COMPRESSED_TIME  = 0x08,
  CMD_CYCLIC_PRIORITY  = 0x10,
  CMD_EXTENDED_WRITE   = 0x20,
  CMD_LOW_DREQ         = 0x40,
  CMD_LOW_DACK         = 0x80,
  CMD_NOT_SUPPORTED    = CMD_MEMORY_TO_MEMORY | CMD_FIXED_ADDRESS
  | CMD_COMPRESSED_TIME | CMD_CYCLIC_PRIORITY | CMD_EXTENDED_WRITE
  | CMD_LOW_DREQ | CMD_LOW_DACK

};

static int channels[8] = {-1, 2, 3, 1, -1, -1, -1, 0};

static void write_page (void *opaque, uint32_t nport, uint32_t data)
{
    struct dma_cont *d = opaque;
    int ichan;

    ichan = channels[nport & 7];

    if (-1 == ichan) {
        log ("invalid channel %#x %#x\n", nport, data);
        return;
    }
    d->regs[ichan].page = data;
}

static uint32_t read_page (void *opaque, uint32_t nport)
{
    struct dma_cont *d = opaque;
    int ichan;

    ichan = channels[nport & 7];

    if (-1 == ichan) {
        log ("invalid channel read %#x\n", nport);
        return 0;
    }
    return d->regs[ichan].page;
}

static inline void init_chan (struct dma_cont *d, int ichan)
{
    struct dma_regs *r;

    r = d->regs + ichan;
    r->now[ADDR] = r->base[0] << d->dshift;
    r->now[COUNT] = 0;
}

static inline int getff (struct dma_cont *d)
{
    int ff;

    ff = d->flip_flop;
    d->flip_flop = !ff;
    return ff;
}

static uint32_t read_chan (void *opaque, uint32_t nport)
{
    struct dma_cont *d = opaque;
    int ichan, nreg, iport, ff, val;
    struct dma_regs *r;

    iport = (nport >> d->dshift) & 0x0f;
    ichan = iport >> 1;
    nreg = iport & 1;
    r = d->regs + ichan;

    ff = getff (d);
    if (nreg)
        val = (r->base[COUNT] << d->dshift) - r->now[COUNT];
    else
        val = r->now[ADDR] + r->now[COUNT];

    return (val >> (d->dshift + (ff << 3))) & 0xff;
}

static void write_chan (void *opaque, uint32_t nport, uint32_t data)
{
    struct dma_cont *d = opaque;
    int iport, ichan, nreg;
    struct dma_regs *r;

    iport = (nport >> d->dshift) & 0x0f;
    ichan = iport >> 1;
    nreg = iport & 1;
    r = d->regs + ichan;
    if (getff (d)) {
        r->base[nreg] = (r->base[nreg] & 0xff) | ((data << 8) & 0xff00);
        init_chan (d, ichan);
    } else {
        r->base[nreg] = (r->base[nreg] & 0xff00) | (data & 0xff);
    }
}

static void write_cont (void *opaque, uint32_t nport, uint32_t data)
{
    struct dma_cont *d = opaque;
    int iport, ichan;

    iport = (nport >> d->dshift) & 0x0f;
    switch (iport) {
    case 8:                     /* command */
        if ((data != 0) && (data & CMD_NOT_SUPPORTED)) {
            log ("command %#x not supported\n", data);
            return;
        }
        d->command = data;
        break;

    case 9:
        ichan = data & 3;
        if (data & 4) {
            d->status |= 1 << (ichan + 4);
        }
        else {
            d->status &= ~(1 << (ichan + 4));
        }
        d->status &= ~(1 << ichan);
        break;

    case 0xa:                   /* single mask */
        if (data & 4)
            d->mask |= 1 << (data & 3);
        else
            d->mask &= ~(1 << (data & 3));
        break;

    case 0xb:                   /* mode */
        {
            ichan = data & 3;
#ifdef DEBUG_DMA
            int op;
            int ai;
            int dir;
            int opmode;

            op = (data >> 2) & 3;
            ai = (data >> 4) & 1;
            dir = (data >> 5) & 1;
            opmode = (data >> 6) & 3;

            linfo ("ichan %d, op %d, ai %d, dir %d, opmode %d\n",
                   ichan, op, ai, dir, opmode);
#endif

            d->regs[ichan].mode = data;
            break;
        }

    case 0xc:                   /* clear flip flop */
        d->flip_flop = 0;
        break;

    case 0xd:                   /* reset */
        d->flip_flop = 0;
        d->mask = ~0;
        d->status = 0;
        d->command = 0;
        break;

    case 0xe:                   /* clear mask for all channels */
        d->mask = 0;
        break;

    case 0xf:                   /* write mask for all channels */
        d->mask = data;
        break;

    default:
        log ("dma: unknown iport %#x\n", iport);
        break;
    }

#ifdef DEBUG_DMA
    if (0xc != iport) {
        linfo ("nport %#06x, ichan % 2d, val %#06x\n",
               nport, ichan, data);
    }
#endif
}

static uint32_t read_cont (void *opaque, uint32_t nport)
{
    struct dma_cont *d = opaque;
    int iport, val;
    
    iport = (nport >> d->dshift) & 0x0f;
    switch (iport) {
    case 0x08: /* status */
        val = d->status;
        d->status &= 0xf0;
        break;
    case 0x0f: /* mask */
        val = d->mask;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

int DMA_get_channel_mode (int nchan)
{
    return dma_controllers[nchan > 3].regs[nchan & 3].mode;
}

void DMA_hold_DREQ (int nchan)
{
    int ncont, ichan;

    ncont = nchan > 3;
    ichan = nchan & 3;
    linfo ("held cont=%d chan=%d\n", ncont, ichan);
    dma_controllers[ncont].status |= 1 << (ichan + 4);
}

void DMA_release_DREQ (int nchan)
{
    int ncont, ichan;

    ncont = nchan > 3;
    ichan = nchan & 3;
    linfo ("released cont=%d chan=%d\n", ncont, ichan);
    dma_controllers[ncont].status &= ~(1 << (ichan + 4));
}

static void channel_run (int ncont, int ichan)
{
    struct dma_regs *r;
    int n;
    target_ulong addr;
/*     int ai, dir; */

    r = dma_controllers[ncont].regs + ichan;
/*   ai = r->mode & 16; */
/*   dir = r->mode & 32 ? -1 : 1; */

    addr = (r->page << 16) | r->now[ADDR];
    n = r->transfer_handler (r->opaque, addr, 
                             (r->base[COUNT] << ncont) + (1 << ncont));
    r->now[COUNT] = n;

    ldebug ("dma_pos %d size %d\n",
            n, (r->base[1] << ncont) + (1 << ncont));
}

void DMA_run (void)
{
    struct dma_cont *d;
    int icont, ichan;

    d = dma_controllers;

    for (icont = 0; icont < 2; icont++, d++) {
        for (ichan = 0; ichan < 4; ichan++) {
            int mask;

            mask = 1 << ichan;

            if ((0 == (d->mask & mask)) && (0 != (d->status & (mask << 4))))
                channel_run (icont, ichan);
        }
    }
}

void DMA_register_channel (int nchan,
                           DMA_transfer_handler transfer_handler, 
                           void *opaque)
{
    struct dma_regs *r;
    int ichan, ncont;

    ncont = nchan > 3;
    ichan = nchan & 3;

    r = dma_controllers[ncont].regs + ichan;
    r->transfer_handler = transfer_handler;
    r->opaque = opaque;
}

/* request the emulator to transfer a new DMA memory block ASAP */
void DMA_schedule(int nchan)
{
    cpu_interrupt(cpu_single_env, CPU_INTERRUPT_EXIT);
}

/* dshift = 0: 8 bit DMA, 1 = 16 bit DMA */
static void dma_init2(struct dma_cont *d, int base, int dshift, int page_base)
{
    const static int page_port_list[] = { 0x1, 0x2, 0x3, 0x7 };
    int i;

    d->dshift = dshift;
    for (i = 0; i < 8; i++) {
        register_ioport_write (base + (i << dshift), 1, 1, write_chan, d);
        register_ioport_read (base + (i << dshift), 1, 1, read_chan, d);
    }
    for (i = 0; i < LENOFA (page_port_list); i++) {
        register_ioport_write (page_base + page_port_list[i], 1, 1, 
                               write_page, d);
        register_ioport_read (page_base + page_port_list[i], 1, 1, 
                              read_page, d);
    }
    for (i = 0; i < 8; i++) {
        register_ioport_write (base + ((i + 8) << dshift), 1, 1, 
                               write_cont, d);
        register_ioport_read (base + ((i + 8) << dshift), 1, 1, 
                              read_cont, d);
    }
    write_cont (d, base + (0x0d << dshift), 0);
}

void DMA_init (void)
{
    dma_init2(&dma_controllers[0], 0x00, 0, 0x80);
    dma_init2(&dma_controllers[1], 0xc0, 1, 0x88);
}
