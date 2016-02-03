/*
 * QEMU DMA emulation
 *
 * Copyright (c) 2003-2004 Vassili Karpov (malc)
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
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/isa/isa.h"
#include "qemu/main-loop.h"
#include "trace.h"

/* #define DEBUG_DMA */

#define dolog(...) fprintf (stderr, "dma: " __VA_ARGS__)
#ifdef DEBUG_DMA
#define linfo(...) fprintf (stderr, "dma: " __VA_ARGS__)
#define ldebug(...) fprintf (stderr, "dma: " __VA_ARGS__)
#else
#define linfo(...)
#define ldebug(...)
#endif

typedef struct I8257Regs {
    int now[2];
    uint16_t base[2];
    uint8_t mode;
    uint8_t page;
    uint8_t pageh;
    uint8_t dack;
    uint8_t eop;
    DMA_transfer_handler transfer_handler;
    void *opaque;
} I8257Regs;

#define ADDR 0
#define COUNT 1

typedef struct I8257State {
    uint8_t status;
    uint8_t command;
    uint8_t mask;
    uint8_t flip_flop;
    int dshift;
    I8257Regs regs[4];
    MemoryRegion channel_io;
    MemoryRegion cont_io;

    QEMUBH *dma_bh;
    bool dma_bh_scheduled;
    int running;
} I8257State;

static I8257State dma_controllers[2];

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

static void i8257_dma_run(void *opaque);

static int channels[8] = {-1, 2, 3, 1, -1, -1, -1, 0};

static void i8257_write_page(void *opaque, uint32_t nport, uint32_t data)
{
    I8257State *d = opaque;
    int ichan;

    ichan = channels[nport & 7];
    if (-1 == ichan) {
        dolog ("invalid channel %#x %#x\n", nport, data);
        return;
    }
    d->regs[ichan].page = data;
}

static void i8257_write_pageh(void *opaque, uint32_t nport, uint32_t data)
{
    I8257State *d = opaque;
    int ichan;

    ichan = channels[nport & 7];
    if (-1 == ichan) {
        dolog ("invalid channel %#x %#x\n", nport, data);
        return;
    }
    d->regs[ichan].pageh = data;
}

static uint32_t i8257_read_page(void *opaque, uint32_t nport)
{
    I8257State *d = opaque;
    int ichan;

    ichan = channels[nport & 7];
    if (-1 == ichan) {
        dolog ("invalid channel read %#x\n", nport);
        return 0;
    }
    return d->regs[ichan].page;
}

static uint32_t i8257_read_pageh(void *opaque, uint32_t nport)
{
    I8257State *d = opaque;
    int ichan;

    ichan = channels[nport & 7];
    if (-1 == ichan) {
        dolog ("invalid channel read %#x\n", nport);
        return 0;
    }
    return d->regs[ichan].pageh;
}

static inline void i8257_init_chan(I8257State *d, int ichan)
{
    I8257Regs *r;

    r = d->regs + ichan;
    r->now[ADDR] = r->base[ADDR] << d->dshift;
    r->now[COUNT] = 0;
}

static inline int i8257_getff(I8257State *d)
{
    int ff;

    ff = d->flip_flop;
    d->flip_flop = !ff;
    return ff;
}

static uint64_t i8257_read_chan(void *opaque, hwaddr nport, unsigned size)
{
    I8257State *d = opaque;
    int ichan, nreg, iport, ff, val, dir;
    I8257Regs *r;

    iport = (nport >> d->dshift) & 0x0f;
    ichan = iport >> 1;
    nreg = iport & 1;
    r = d->regs + ichan;

    dir = ((r->mode >> 5) & 1) ? -1 : 1;
    ff = i8257_getff(d);
    if (nreg)
        val = (r->base[COUNT] << d->dshift) - r->now[COUNT];
    else
        val = r->now[ADDR] + r->now[COUNT] * dir;

    ldebug ("read_chan %#x -> %d\n", iport, val);
    return (val >> (d->dshift + (ff << 3))) & 0xff;
}

static void i8257_write_chan(void *opaque, hwaddr nport, uint64_t data,
                             unsigned int size)
{
    I8257State *d = opaque;
    int iport, ichan, nreg;
    I8257Regs *r;

    iport = (nport >> d->dshift) & 0x0f;
    ichan = iport >> 1;
    nreg = iport & 1;
    r = d->regs + ichan;
    if (i8257_getff(d)) {
        r->base[nreg] = (r->base[nreg] & 0xff) | ((data << 8) & 0xff00);
        i8257_init_chan(d, ichan);
    } else {
        r->base[nreg] = (r->base[nreg] & 0xff00) | (data & 0xff);
    }
}

static void i8257_write_cont(void *opaque, hwaddr nport, uint64_t data,
                             unsigned int size)
{
    I8257State *d = opaque;
    int iport, ichan = 0;

    iport = (nport >> d->dshift) & 0x0f;
    switch (iport) {
    case 0x00:                  /* command */
        if ((data != 0) && (data & CMD_NOT_SUPPORTED)) {
            dolog("command %"PRIx64" not supported\n", data);
            return;
        }
        d->command = data;
        break;

    case 0x01:
        ichan = data & 3;
        if (data & 4) {
            d->status |= 1 << (ichan + 4);
        }
        else {
            d->status &= ~(1 << (ichan + 4));
        }
        d->status &= ~(1 << ichan);
        i8257_dma_run(d);
        break;

    case 0x02:                  /* single mask */
        if (data & 4)
            d->mask |= 1 << (data & 3);
        else
            d->mask &= ~(1 << (data & 3));
        i8257_dma_run(d);
        break;

    case 0x03:                  /* mode */
        {
            ichan = data & 3;
#ifdef DEBUG_DMA
            {
                int op, ai, dir, opmode;
                op = (data >> 2) & 3;
                ai = (data >> 4) & 1;
                dir = (data >> 5) & 1;
                opmode = (data >> 6) & 3;

                linfo ("ichan %d, op %d, ai %d, dir %d, opmode %d\n",
                       ichan, op, ai, dir, opmode);
            }
#endif
            d->regs[ichan].mode = data;
            break;
        }

    case 0x04:                  /* clear flip flop */
        d->flip_flop = 0;
        break;

    case 0x05:                  /* reset */
        d->flip_flop = 0;
        d->mask = ~0;
        d->status = 0;
        d->command = 0;
        break;

    case 0x06:                  /* clear mask for all channels */
        d->mask = 0;
        i8257_dma_run(d);
        break;

    case 0x07:                  /* write mask for all channels */
        d->mask = data;
        i8257_dma_run(d);
        break;

    default:
        dolog ("unknown iport %#x\n", iport);
        break;
    }

#ifdef DEBUG_DMA
    if (0xc != iport) {
        linfo ("write_cont: nport %#06x, ichan % 2d, val %#06x\n",
               nport, ichan, data);
    }
#endif
}

static uint64_t i8257_read_cont(void *opaque, hwaddr nport, unsigned size)
{
    I8257State *d = opaque;
    int iport, val;

    iport = (nport >> d->dshift) & 0x0f;
    switch (iport) {
    case 0x00:                  /* status */
        val = d->status;
        d->status &= 0xf0;
        break;
    case 0x01:                  /* mask */
        val = d->mask;
        break;
    default:
        val = 0;
        break;
    }

    ldebug ("read_cont: nport %#06x, iport %#04x val %#x\n", nport, iport, val);
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
    i8257_dma_run(&dma_controllers[ncont]);
}

void DMA_release_DREQ (int nchan)
{
    int ncont, ichan;

    ncont = nchan > 3;
    ichan = nchan & 3;
    linfo ("released cont=%d chan=%d\n", ncont, ichan);
    dma_controllers[ncont].status &= ~(1 << (ichan + 4));
    i8257_dma_run(&dma_controllers[ncont]);
}

static void i8257_channel_run(I8257State *d, int ichan)
{
    int ncont = d->dshift;
    int n;
    I8257Regs *r = &d->regs[ichan];
#ifdef DEBUG_DMA
    int dir, opmode;

    dir = (r->mode >> 5) & 1;
    opmode = (r->mode >> 6) & 3;

    if (dir) {
        dolog ("DMA in address decrement mode\n");
    }
    if (opmode != 1) {
        dolog ("DMA not in single mode select %#x\n", opmode);
    }
#endif

    n = r->transfer_handler (r->opaque, ichan + (ncont << 2),
                             r->now[COUNT], (r->base[COUNT] + 1) << ncont);
    r->now[COUNT] = n;
    ldebug ("dma_pos %d size %d\n", n, (r->base[COUNT] + 1) << ncont);
}

static void i8257_dma_run(void *opaque)
{
    I8257State *d = opaque;
    int ichan;
    int rearm = 0;

    if (d->running) {
        rearm = 1;
        goto out;
    } else {
        d->running = 1;
    }

    for (ichan = 0; ichan < 4; ichan++) {
        int mask;

        mask = 1 << ichan;

        if ((0 == (d->mask & mask)) && (0 != (d->status & (mask << 4)))) {
            i8257_channel_run(d, ichan);
            rearm = 1;
        }
    }

    d->running = 0;
out:
    if (rearm) {
        qemu_bh_schedule_idle(d->dma_bh);
        d->dma_bh_scheduled = true;
    }
}

void DMA_register_channel (int nchan,
                           DMA_transfer_handler transfer_handler,
                           void *opaque)
{
    I8257Regs *r;
    int ichan, ncont;

    ncont = nchan > 3;
    ichan = nchan & 3;

    r = dma_controllers[ncont].regs + ichan;
    r->transfer_handler = transfer_handler;
    r->opaque = opaque;
}

int DMA_read_memory (int nchan, void *buf, int pos, int len)
{
    I8257Regs *r = &dma_controllers[nchan > 3].regs[nchan & 3];
    hwaddr addr = ((r->pageh & 0x7f) << 24) | (r->page << 16) | r->now[ADDR];

    if (r->mode & 0x20) {
        int i;
        uint8_t *p = buf;

        cpu_physical_memory_read (addr - pos - len, buf, len);
        /* What about 16bit transfers? */
        for (i = 0; i < len >> 1; i++) {
            uint8_t b = p[len - i - 1];
            p[i] = b;
        }
    }
    else
        cpu_physical_memory_read (addr + pos, buf, len);

    return len;
}

int DMA_write_memory (int nchan, void *buf, int pos, int len)
{
    I8257Regs *r = &dma_controllers[nchan > 3].regs[nchan & 3];
    hwaddr addr = ((r->pageh & 0x7f) << 24) | (r->page << 16) | r->now[ADDR];

    if (r->mode & 0x20) {
        int i;
        uint8_t *p = buf;

        cpu_physical_memory_write (addr - pos - len, buf, len);
        /* What about 16bit transfers? */
        for (i = 0; i < len; i++) {
            uint8_t b = p[len - i - 1];
            p[i] = b;
        }
    }
    else
        cpu_physical_memory_write (addr + pos, buf, len);

    return len;
}

/* request the emulator to transfer a new DMA memory block ASAP (even
 * if the idle bottom half would not have exited the iothread yet).
 */
void DMA_schedule(void)
{
    if (dma_controllers[0].dma_bh_scheduled ||
        dma_controllers[1].dma_bh_scheduled) {
        qemu_notify_event();
    }
}

static void i8257_reset(void *opaque)
{
    I8257State *d = opaque;
    i8257_write_cont(d, (0x05 << d->dshift), 0, 1);
}

static int i8257_phony_handler(void *opaque, int nchan, int dma_pos,
                               int dma_len)
{
    trace_i8257_unregistered_dma(nchan, dma_pos, dma_len);
    return dma_pos;
}


static const MemoryRegionOps channel_io_ops = {
    .read = i8257_read_chan,
    .write = i8257_write_chan,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

/* IOport from page_base */
static const MemoryRegionPortio page_portio_list[] = {
    { 0x01, 3, 1, .write = i8257_write_page, .read = i8257_read_page, },
    { 0x07, 1, 1, .write = i8257_write_page, .read = i8257_read_page, },
    PORTIO_END_OF_LIST(),
};

/* IOport from pageh_base */
static const MemoryRegionPortio pageh_portio_list[] = {
    { 0x01, 3, 1, .write = i8257_write_pageh, .read = i8257_read_pageh, },
    { 0x07, 3, 1, .write = i8257_write_pageh, .read = i8257_read_pageh, },
    PORTIO_END_OF_LIST(),
};

static const MemoryRegionOps cont_io_ops = {
    .read = i8257_read_cont,
    .write = i8257_write_cont,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

/* dshift = 0: 8 bit DMA, 1 = 16 bit DMA */
static void dma_init2(I8257State *d, int base, int dshift,
                      int page_base, int pageh_base)
{
    int i;

    d->dshift = dshift;

    memory_region_init_io(&d->channel_io, NULL, &channel_io_ops, d,
                          "dma-chan", 8 << d->dshift);
    memory_region_add_subregion(isa_address_space_io(NULL),
                                base, &d->channel_io);

    isa_register_portio_list(NULL, page_base, page_portio_list, d,
                             "dma-page");
    if (pageh_base >= 0) {
        isa_register_portio_list(NULL, pageh_base, pageh_portio_list, d,
                                 "dma-pageh");
    }

    memory_region_init_io(&d->cont_io, NULL, &cont_io_ops, d, "dma-cont",
                          8 << d->dshift);
    memory_region_add_subregion(isa_address_space_io(NULL),
                                base + (8 << d->dshift), &d->cont_io);

    qemu_register_reset(i8257_reset, d);
    i8257_reset(d);
    for (i = 0; i < ARRAY_SIZE (d->regs); ++i) {
        d->regs[i].transfer_handler = i8257_phony_handler;
    }

    d->dma_bh = qemu_bh_new(i8257_dma_run, d);
}

static const VMStateDescription vmstate_i8257_regs = {
    .name = "dma_regs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_ARRAY(now, I8257Regs, 2),
        VMSTATE_UINT16_ARRAY(base, I8257Regs, 2),
        VMSTATE_UINT8(mode, I8257Regs),
        VMSTATE_UINT8(page, I8257Regs),
        VMSTATE_UINT8(pageh, I8257Regs),
        VMSTATE_UINT8(dack, I8257Regs),
        VMSTATE_UINT8(eop, I8257Regs),
        VMSTATE_END_OF_LIST()
    }
};

static int i8257_post_load(void *opaque, int version_id)
{
    I8257State *d = opaque;
    i8257_dma_run(d);

    return 0;
}

static const VMStateDescription vmstate_dma = {
    .name = "dma",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = i8257_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(command, I8257State),
        VMSTATE_UINT8(mask, I8257State),
        VMSTATE_UINT8(flip_flop, I8257State),
        VMSTATE_INT32(dshift, I8257State),
        VMSTATE_STRUCT_ARRAY(regs, I8257State, 4, 1, vmstate_i8257_regs,
                             I8257Regs),
        VMSTATE_END_OF_LIST()
    }
};

void DMA_init(ISABus *bus, int high_page_enable)
{
    dma_init2(&dma_controllers[0], 0x00, 0, 0x80, high_page_enable ? 0x480 : -1);
    dma_init2(&dma_controllers[1], 0xc0, 1, 0x88, high_page_enable ? 0x488 : -1);
    vmstate_register (NULL, 0, &vmstate_dma, &dma_controllers[0]);
    vmstate_register (NULL, 1, &vmstate_dma, &dma_controllers[1]);
}
