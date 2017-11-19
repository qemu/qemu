/*
 * CSKY dma header.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CSKY_DMA_H
#define CSKY_DMA_H

#define NR_DMA_CHAN       4   /* the total number of DMA channels */

typedef struct {
    hwaddr src;
    hwaddr dest;
    uint32_t ctrl[2];
    uint32_t conf[2];
    int chan_enable;
} csky_dma_channel;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    int dma_enable;
    uint32_t tfr_int;
    uint32_t block_int;
    uint32_t srctran_int;
    uint32_t dsttran_int;
    uint32_t err_int;
    uint32_t tfr_int_mask;
    uint32_t block_int_mask;
    uint32_t srctran_int_mask;
    uint32_t dsttran_int_mask;
    uint32_t err_int_mask;
    uint32_t status_int;
    csky_dma_channel *chan;
} csky_dma_state;

csky_dma_state *csky_dma_create(const char *name, hwaddr addr, qemu_irq irq);

#endif
