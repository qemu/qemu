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

#ifndef HW_SOC_DMA_H
#define HW_SOC_DMA_H 1


#include "exec/memory.h"
#include "hw/irq.h"

struct soc_dma_s;
struct soc_dma_ch_s;
typedef void (*soc_dma_io_t)(void *opaque, uint8_t *buf, int len);
typedef void (*soc_dma_transfer_t)(struct soc_dma_ch_s *ch);

enum soc_dma_port_type {
    soc_dma_port_mem,
    soc_dma_port_fifo,
    soc_dma_port_other,
};

enum soc_dma_access_type {
    soc_dma_access_const,
    soc_dma_access_linear,
    soc_dma_access_other,
};

struct soc_dma_ch_s {
    /* Private */
    struct soc_dma_s *dma;
    int num;
    QEMUTimer *timer;

    /* Set by soc_dma.c */
    int enable;
    int update;

    /* This should be set by dma->setup_fn().  */
    int bytes;
    /* Initialised by the DMA module, call soc_dma_ch_update after writing.  */
    enum soc_dma_access_type type[2];
    hwaddr vaddr[2];	/* Updated by .transfer_fn().  */
    /* Private */
    void *paddr[2];
    soc_dma_io_t io_fn[2];
    void *io_opaque[2];

    int running;
    soc_dma_transfer_t transfer_fn;

    /* Set and used by the DMA module.  */
    void *opaque;
};

struct soc_dma_s {
    /* Following fields are set by the SoC DMA module and can be used
     * by anybody.  */
    uint64_t drqbmp;	/* Is zeroed by soc_dma_reset() */
    qemu_irq *drq;
    void *opaque;
    int64_t freq;
    soc_dma_transfer_t transfer_fn;
    soc_dma_transfer_t setup_fn;
    /* Set by soc_dma_init() for use by the DMA module.  */
    struct soc_dma_ch_s *ch;
};

/* Call to activate or stop a DMA channel.  */
void soc_dma_set_request(struct soc_dma_ch_s *ch, int level);
/* Call after every write to one of the following fields and before
 * calling soc_dma_set_request(ch, 1):
 *   ch->type[0...1],
 *   ch->vaddr[0...1],
 *   ch->paddr[0...1],
 * or after a soc_dma_port_add_fifo() or soc_dma_port_add_mem().  */
void soc_dma_ch_update(struct soc_dma_ch_s *ch);

/* The SoC should call this when the DMA module is being reset.  */
void soc_dma_reset(struct soc_dma_s *s);
struct soc_dma_s *soc_dma_init(int n);

void soc_dma_port_add_fifo(struct soc_dma_s *dma, hwaddr virt_base,
                soc_dma_io_t fn, void *opaque, int out);
void soc_dma_port_add_mem(struct soc_dma_s *dma, uint8_t *phys_base,
                hwaddr virt_base, size_t size);

static inline void soc_dma_port_add_fifo_in(struct soc_dma_s *dma,
                hwaddr virt_base, soc_dma_io_t fn, void *opaque)
{
    return soc_dma_port_add_fifo(dma, virt_base, fn, opaque, 0);
}

static inline void soc_dma_port_add_fifo_out(struct soc_dma_s *dma,
                hwaddr virt_base, soc_dma_io_t fn, void *opaque)
{
    return soc_dma_port_add_fifo(dma, virt_base, fn, opaque, 1);
}

#endif
