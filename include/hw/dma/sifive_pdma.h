/*
 * SiFive Platform DMA emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
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

#ifndef SIFIVE_PDMA_H
#define SIFIVE_PDMA_H

struct sifive_pdma_chan {
    uint32_t control;
    uint32_t next_config;
    uint64_t next_bytes;
    uint64_t next_dst;
    uint64_t next_src;
    uint32_t exec_config;
    uint64_t exec_bytes;
    uint64_t exec_dst;
    uint64_t exec_src;
    int state;
};

#define SIFIVE_PDMA_CHANS           4
#define SIFIVE_PDMA_IRQS            (SIFIVE_PDMA_CHANS * 2)
#define SIFIVE_PDMA_REG_SIZE        0x100000
#define SIFIVE_PDMA_CHAN_NO(reg)    ((reg & (SIFIVE_PDMA_REG_SIZE - 1)) >> 12)

typedef struct SiFivePDMAState {
    SysBusDevice parent;
    MemoryRegion iomem;
    qemu_irq irq[SIFIVE_PDMA_IRQS];

    struct sifive_pdma_chan chan[SIFIVE_PDMA_CHANS];
} SiFivePDMAState;

#define TYPE_SIFIVE_PDMA    "sifive.pdma"

#define SIFIVE_PDMA(obj)    \
    OBJECT_CHECK(SiFivePDMAState, (obj), TYPE_SIFIVE_PDMA)

#endif /* SIFIVE_PDMA_H */
