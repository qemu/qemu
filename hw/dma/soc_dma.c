/*
 * On-chip DMA controller framework.
 *
 * Copyright (C) 2008 Nokia Corporation
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
#include "qemu-common.h"
#include "qemu/timer.h"
#include "hw/arm/soc_dma.h"

static void transfer_mem2mem(struct soc_dma_ch_s *ch)
{
    memcpy(ch->paddr[0], ch->paddr[1], ch->bytes);
    ch->paddr[0] += ch->bytes;
    ch->paddr[1] += ch->bytes;
}

static void transfer_mem2fifo(struct soc_dma_ch_s *ch)
{
    ch->io_fn[1](ch->io_opaque[1], ch->paddr[0], ch->bytes);
    ch->paddr[0] += ch->bytes;
}

static void transfer_fifo2mem(struct soc_dma_ch_s *ch)
{
    ch->io_fn[0](ch->io_opaque[0], ch->paddr[1], ch->bytes);
    ch->paddr[1] += ch->bytes;
}

/* This is further optimisable but isn't very important because often
 * DMA peripherals forbid this kind of transfers and even when they don't,
 * oprating systems may not need to use them.  */
static void *fifo_buf;
static int fifo_size;
static void transfer_fifo2fifo(struct soc_dma_ch_s *ch)
{
    if (ch->bytes > fifo_size)
        fifo_buf = g_realloc(fifo_buf, fifo_size = ch->bytes);

    /* Implement as transfer_fifo2linear + transfer_linear2fifo.  */
    ch->io_fn[0](ch->io_opaque[0], fifo_buf, ch->bytes);
    ch->io_fn[1](ch->io_opaque[1], fifo_buf, ch->bytes);
}

struct dma_s {
    struct soc_dma_s soc;
    int chnum;
    uint64_t ch_enable_mask;
    int64_t channel_freq;
    int enabled_count;

    struct memmap_entry_s {
        enum soc_dma_port_type type;
        hwaddr addr;
        union {
           struct {
               void *opaque;
               soc_dma_io_t fn;
               int out;
           } fifo;
           struct {
               void *base;
               size_t size;
           } mem;
        } u;
    } *memmap;
    int memmap_size;

    struct soc_dma_ch_s ch[0];
};

static void soc_dma_ch_schedule(struct soc_dma_ch_s *ch, int delay_bytes)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    struct dma_s *dma = (struct dma_s *) ch->dma;

    timer_mod(ch->timer, now + delay_bytes / dma->channel_freq);
}

static void soc_dma_ch_run(void *opaque)
{
    struct soc_dma_ch_s *ch = (struct soc_dma_ch_s *) opaque;

    ch->running = 1;
    ch->dma->setup_fn(ch);
    ch->transfer_fn(ch);
    ch->running = 0;

    if (ch->enable)
        soc_dma_ch_schedule(ch, ch->bytes);
    ch->bytes = 0;
}

static inline struct memmap_entry_s *soc_dma_lookup(struct dma_s *dma,
                hwaddr addr)
{
    struct memmap_entry_s *lo;
    int hi;

    lo = dma->memmap;
    hi = dma->memmap_size;

    while (hi > 1) {
        hi /= 2;
        if (lo[hi].addr <= addr)
            lo += hi;
    }

    return lo;
}

static inline enum soc_dma_port_type soc_dma_ch_update_type(
                struct soc_dma_ch_s *ch, int port)
{
    struct dma_s *dma = (struct dma_s *) ch->dma;
    struct memmap_entry_s *entry = soc_dma_lookup(dma, ch->vaddr[port]);

    if (entry->type == soc_dma_port_fifo) {
        while (entry < dma->memmap + dma->memmap_size &&
                        entry->u.fifo.out != port)
            entry ++;
        if (entry->addr != ch->vaddr[port] || entry->u.fifo.out != port)
            return soc_dma_port_other;

        if (ch->type[port] != soc_dma_access_const)
            return soc_dma_port_other;

        ch->io_fn[port] = entry->u.fifo.fn;
        ch->io_opaque[port] = entry->u.fifo.opaque;
        return soc_dma_port_fifo;
    } else if (entry->type == soc_dma_port_mem) {
        if (entry->addr > ch->vaddr[port] ||
                        entry->addr + entry->u.mem.size <= ch->vaddr[port])
            return soc_dma_port_other;

        /* TODO: support constant memory address for source port as used for
         * drawing solid rectangles by PalmOS(R).  */
        if (ch->type[port] != soc_dma_access_const)
            return soc_dma_port_other;

        ch->paddr[port] = (uint8_t *) entry->u.mem.base +
                (ch->vaddr[port] - entry->addr);
        /* TODO: save bytes left to the end of the mapping somewhere so we
         * can check we're not reading beyond it.  */
        return soc_dma_port_mem;
    } else
        return soc_dma_port_other;
}

void soc_dma_ch_update(struct soc_dma_ch_s *ch)
{
    enum soc_dma_port_type src, dst;

    src = soc_dma_ch_update_type(ch, 0);
    if (src == soc_dma_port_other) {
        ch->update = 0;
        ch->transfer_fn = ch->dma->transfer_fn;
        return;
    }
    dst = soc_dma_ch_update_type(ch, 1);

    /* TODO: use src and dst as array indices.  */
    if (src == soc_dma_port_mem && dst == soc_dma_port_mem)
        ch->transfer_fn = transfer_mem2mem;
    else if (src == soc_dma_port_mem && dst == soc_dma_port_fifo)
        ch->transfer_fn = transfer_mem2fifo;
    else if (src == soc_dma_port_fifo && dst == soc_dma_port_mem)
        ch->transfer_fn = transfer_fifo2mem;
    else if (src == soc_dma_port_fifo && dst == soc_dma_port_fifo)
        ch->transfer_fn = transfer_fifo2fifo;
    else
        ch->transfer_fn = ch->dma->transfer_fn;

    ch->update = (dst != soc_dma_port_other);
}

static void soc_dma_ch_freq_update(struct dma_s *s)
{
    if (s->enabled_count)
        /* We completely ignore channel priorities and stuff */
        s->channel_freq = s->soc.freq / s->enabled_count;
    else {
        /* TODO: Signal that we want to disable the functional clock and let
         * the platform code decide what to do with it, i.e. check that
         * auto-idle is enabled in the clock controller and if we are stopping
         * the clock, do the same with any parent clocks that had only one
         * user keeping them on and auto-idle enabled.  */
    }
}

void soc_dma_set_request(struct soc_dma_ch_s *ch, int level)
{
    struct dma_s *dma = (struct dma_s *) ch->dma;

    dma->enabled_count += level - ch->enable;

    if (level)
        dma->ch_enable_mask |= 1 << ch->num;
    else
        dma->ch_enable_mask &= ~(1 << ch->num);

    if (level != ch->enable) {
        soc_dma_ch_freq_update(dma);
        ch->enable = level;

        if (!ch->enable)
            timer_del(ch->timer);
        else if (!ch->running)
            soc_dma_ch_run(ch);
        else
            soc_dma_ch_schedule(ch, 1);
    }
}

void soc_dma_reset(struct soc_dma_s *soc)
{
    struct dma_s *s = (struct dma_s *) soc;

    s->soc.drqbmp = 0;
    s->ch_enable_mask = 0;
    s->enabled_count = 0;
    soc_dma_ch_freq_update(s);
}

/* TODO: take a functional-clock argument */
struct soc_dma_s *soc_dma_init(int n)
{
    int i;
    struct dma_s *s = g_malloc0(sizeof(*s) + n * sizeof(*s->ch));

    s->chnum = n;
    s->soc.ch = s->ch;
    for (i = 0; i < n; i ++) {
        s->ch[i].dma = &s->soc;
        s->ch[i].num = i;
        s->ch[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, soc_dma_ch_run, &s->ch[i]);
    }

    soc_dma_reset(&s->soc);
    fifo_size = 0;

    return &s->soc;
}

void soc_dma_port_add_fifo(struct soc_dma_s *soc, hwaddr virt_base,
                soc_dma_io_t fn, void *opaque, int out)
{
    struct memmap_entry_s *entry;
    struct dma_s *dma = (struct dma_s *) soc;

    dma->memmap = g_realloc(dma->memmap, sizeof(*entry) *
                    (dma->memmap_size + 1));
    entry = soc_dma_lookup(dma, virt_base);

    if (dma->memmap_size) {
        if (entry->type == soc_dma_port_mem) {
            if (entry->addr <= virt_base &&
                            entry->addr + entry->u.mem.size > virt_base) {
                fprintf(stderr, "%s: FIFO at %"PRIx64
                                " collides with RAM region at %"PRIx64
                                "-%"PRIx64 "\n", __func__,
                                virt_base, entry->addr,
                                (entry->addr + entry->u.mem.size));
                exit(-1);
            }

            if (entry->addr <= virt_base)
                entry ++;
        } else
            while (entry < dma->memmap + dma->memmap_size &&
                            entry->addr <= virt_base) {
                if (entry->addr == virt_base && entry->u.fifo.out == out) {
                    fprintf(stderr, "%s: FIFO at %"PRIx64
                                    " collides FIFO at %"PRIx64 "\n",
                                    __func__, virt_base, entry->addr);
                    exit(-1);
                }

                entry ++;
            }

        memmove(entry + 1, entry,
                        (uint8_t *) (dma->memmap + dma->memmap_size ++) -
                        (uint8_t *) entry);
    } else
        dma->memmap_size ++;

    entry->addr          = virt_base;
    entry->type          = soc_dma_port_fifo;
    entry->u.fifo.fn     = fn;
    entry->u.fifo.opaque = opaque;
    entry->u.fifo.out    = out;
}

void soc_dma_port_add_mem(struct soc_dma_s *soc, uint8_t *phys_base,
                hwaddr virt_base, size_t size)
{
    struct memmap_entry_s *entry;
    struct dma_s *dma = (struct dma_s *) soc;

    dma->memmap = g_realloc(dma->memmap, sizeof(*entry) *
                    (dma->memmap_size + 1));
    entry = soc_dma_lookup(dma, virt_base);

    if (dma->memmap_size) {
        if (entry->type == soc_dma_port_mem) {
            if ((entry->addr >= virt_base && entry->addr < virt_base + size) ||
                            (entry->addr <= virt_base &&
                             entry->addr + entry->u.mem.size > virt_base)) {
                fprintf(stderr, "%s: RAM at %"PRIx64 "-%"PRIx64
                                " collides with RAM region at %"PRIx64
                                "-%"PRIx64 "\n", __func__,
                                virt_base, virt_base + size,
                                entry->addr, entry->addr + entry->u.mem.size);
                exit(-1);
            }

            if (entry->addr <= virt_base)
                entry ++;
        } else {
            if (entry->addr >= virt_base &&
                            entry->addr < virt_base + size) {
                fprintf(stderr, "%s: RAM at %"PRIx64 "-%"PRIx64
                                " collides with FIFO at %"PRIx64
                                "\n", __func__,
                                virt_base, virt_base + size,
                                entry->addr);
                exit(-1);
            }

            while (entry < dma->memmap + dma->memmap_size &&
                            entry->addr <= virt_base)
                entry ++;
	}

        memmove(entry + 1, entry,
                        (uint8_t *) (dma->memmap + dma->memmap_size ++) -
                        (uint8_t *) entry);
    } else
        dma->memmap_size ++;

    entry->addr          = virt_base;
    entry->type          = soc_dma_port_mem;
    entry->u.mem.base    = phys_base;
    entry->u.mem.size    = size;
}

/* TODO: port removal for ports like PCMCIA memory */
