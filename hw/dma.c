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
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "cpu.h"
#include "vl.h"

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

#define MEM_REAL(addr) ((addr)+(uint32_t)(phys_ram_base))
#define LENOFA(a) ((int) (sizeof(a)/sizeof(a[0])))

struct dma_regs {
    int now[2];
    uint16_t base[2];
    uint8_t mode;
    uint8_t page;
    uint8_t dack;
    uint8_t eop;
    DMA_read_handler read_handler;
    DMA_misc_handler misc_handler;
};

#define ADDR 0
#define COUNT 1

static struct dma_cont {
    uint8_t status;
    uint8_t command;
    uint8_t mask;
    uint8_t flip_flop;
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

static void write_page (CPUState *env, uint32_t nport, uint32_t data)
{
    int ichan;
    int ncont;
    static int channels[8] = {-1, 2, 3, 1, -1, -1, -1, 0};

    ncont = nport > 0x87;
    ichan = channels[nport - 0x80 - (ncont << 3)];

    if (-1 == ichan) {
        log ("invalid channel %#x %#x\n", nport, data);
        return;
    }

    dma_controllers[ncont].regs[ichan].page = data;
}

static void init_chan (int ncont, int ichan)
{
    struct dma_regs *r;

    r = dma_controllers[ncont].regs + ichan;
    r->now[ADDR] = r->base[0] << ncont;
    r->now[COUNT] = 0;
}

static inline int getff (int ncont)
{
    int ff;

    ff = dma_controllers[ncont].flip_flop;
    dma_controllers[ncont].flip_flop = !ff;
    return ff;
}

static uint32_t read_chan (CPUState *env, uint32_t nport)
{
    int ff;
    int ncont, ichan, nreg;
    struct dma_regs *r;
    int val;

    ncont = nport > 7;
    ichan = (nport >> (1 + ncont)) & 3;
    nreg = (nport >> ncont) & 1;
    r = dma_controllers[ncont].regs + ichan;

    ff = getff (ncont);

    if (nreg)
        val = (r->base[COUNT] << ncont) - r->now[COUNT];
    else
        val = r->now[ADDR] + r->now[COUNT];

    return (val >> (ncont + (ff << 3))) & 0xff;
}

static void write_chan (uint32_t nport, int size, uint32_t data)
{
    int ncont, ichan, nreg;
    struct dma_regs *r;

    ncont = nport > 7;
    ichan = (nport >> (1 + ncont)) & 3;
    nreg = (nport >> ncont) & 1;
    r = dma_controllers[ncont].regs + ichan;

    if (2 == size) {
        r->base[nreg] = data;
        init_chan (ncont, ichan);
    }
    else {
        if (getff (ncont)) {
            r->base[nreg] = (r->base[nreg] & 0xff) | ((data << 8) & 0xff00);
            init_chan (ncont, ichan);
        }
        else {
            r->base[nreg] = (r->base[nreg] & 0xff00) | (data & 0xff);
        }
    }
}
static void write_chanb (CPUState *env, uint32_t nport, uint32_t data)
{
    write_chan (nport, 1, data);
}

static void write_chanw (CPUState *env, uint32_t nport, uint32_t data)
{
    write_chan (nport, 2, data);
}

static void write_cont (CPUState *env, uint32_t nport, uint32_t data)
{
    int iport, ichan, ncont;
    struct dma_cont *d;

    ncont = nport > 0xf;
    ichan = -1;

    d = dma_controllers + ncont;
    if (ncont) {
        iport = ((nport - 0xd0) >> 1) + 8;
    }
    else {
        iport = nport;
    }

    switch (iport) {
    case 8:                     /* command */
        if (data && (data | CMD_NOT_SUPPORTED)) {
            log ("command %#x not supported\n", data);
            goto error;
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
        goto error;
    }

#ifdef DEBUG_DMA
    if (0xc != iport) {
        linfo ("nport %#06x, ncont %d, ichan % 2d, val %#06x\n",
               nport, d != dma_controllers, ichan, data);
    }
#endif
    return;

 error:
    abort ();
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
    int irq;
    uint32_t addr;
/*     int ai, dir; */

    r = dma_controllers[ncont].regs + ichan;
/*   ai = r->mode & 16; */
/*   dir = r->mode & 32 ? -1 : 1; */

    addr = MEM_REAL ((r->page << 16) | r->now[ADDR]);

    irq = -1;
    n = r->read_handler (addr, (r->base[COUNT] << ncont) + (1 << ncont), &irq);
    r->now[COUNT] = n;

    ldebug ("dma_pos %d irq %d size %d\n",
            n, irq, (r->base[1] << ncont) + (1 << ncont));

    if (-1 != irq) {
        pic_set_irq (irq, 1);
    }
}

void DMA_run (void)
{
    static int in_dma;
    struct dma_cont *d;
    int icont, ichan;

    if (in_dma) {
        log ("attempt to re-enter dma\n");
        return;
    }

    in_dma = 1;
    d = dma_controllers;

    for (icont = 0; icont < 2; icont++, d++) {
        for (ichan = 0; ichan < 4; ichan++) {
            int mask;

            mask = 1 << ichan;

            if ((0 == (d->mask & mask)) && (0 != (d->status & (mask << 4))))
                channel_run (icont, ichan);
        }
    }
    in_dma = 0;
}

void DMA_register_channel (int nchan,
                           DMA_read_handler read_handler,
                           DMA_misc_handler misc_handler)
{
    struct dma_regs *r;
    int ichan, ncont;

    ncont = nchan > 3;
    ichan = nchan & 3;

    r = dma_controllers[ncont].regs + ichan;
    r->read_handler = read_handler;
    r->misc_handler = misc_handler;
}

void DMA_init (void)
{
    int i;
    int page_port_list[] = { 0x1, 0x2, 0x3, 0x7 };

    for (i = 0; i < 8; i++) {
        register_ioport_write (i, 1, write_chanb, 1);
        register_ioport_write (i, 1, write_chanw, 2);

        register_ioport_write (0xc0 + (i << 1), 1, write_chanb, 1);
        register_ioport_write (0xc0 + (i << 1), 1, write_chanw, 2);

        register_ioport_read (i, 1, read_chan, 1);
        register_ioport_read (0xc0 + (i << 1), 1, read_chan, 2);
    }

    for (i = 0; i < LENOFA (page_port_list); i++) {
        register_ioport_write (page_port_list[i] + 0x80, 1, write_page, 1);
        register_ioport_write (page_port_list[i] + 0x88, 1, write_page, 1);
    }

    for (i = 0; i < 8; i++) {
        register_ioport_write (i + 8, 1, write_cont, 1);
        register_ioport_write (0xd0 + (i << 1), 1, write_cont, 1);
    }

    write_cont (NULL, 0xd, 0);
    write_cont (NULL, 0xdd, 0);
}
