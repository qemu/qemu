/*
 * QEMU Sparc32 DMA controller emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
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

/* debug DMA */
//#define DEBUG_DMA

/*
 * This is the DMA controller part of chip STP2000 (Master I/O), also
 * produced as NCR89C100. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C100.txt
 * and
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/DMA2.txt
 */

#ifdef DEBUG_DMA
#define DPRINTF(fmt, args...) \
do { printf("DMA: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#define DMA_REGS 8
#define DMA_MAXADDR (DMA_REGS * 4 - 1)

#define DMA_VER 0xa0000000
#define DMA_INTR 1
#define DMA_INTREN 0x10
#define DMA_WRITE_MEM 0x100
#define DMA_LOADED 0x04000000
#define DMA_RESET 0x80

typedef struct DMAState DMAState;

struct DMAState {
    uint32_t dmaregs[DMA_REGS];
    qemu_irq espirq, leirq;
    void *iommu, *esp_opaque, *lance_opaque;
    qemu_irq *pic;
};

/* Note: on sparc, the lance 16 bit bus is swapped */
void ledma_memory_read(void *opaque, target_phys_addr_t addr, 
                       uint8_t *buf, int len, int do_bswap)
{
    DMAState *s = opaque;
    int i;

    DPRINTF("DMA write, direction: %c, addr 0x%8.8x\n",
            s->dmaregs[0] & DMA_WRITE_MEM ? 'w': 'r', s->dmaregs[1]);
    addr |= s->dmaregs[7];
    if (do_bswap) {
        sparc_iommu_memory_read(s->iommu, addr, buf, len);
    } else {
        addr &= ~1;
        len &= ~1;
        sparc_iommu_memory_read(s->iommu, addr, buf, len);
        for(i = 0; i < len; i += 2) {
            bswap16s((uint16_t *)(buf + i));
        }
    }
}

void ledma_memory_write(void *opaque, target_phys_addr_t addr, 
                        uint8_t *buf, int len, int do_bswap)
{
    DMAState *s = opaque;
    int l, i;
    uint16_t tmp_buf[32];

    DPRINTF("DMA read, direction: %c, addr 0x%8.8x\n",
            s->dmaregs[0] & DMA_WRITE_MEM ? 'w': 'r', s->dmaregs[1]);
    addr |= s->dmaregs[7];
    if (do_bswap) {
        sparc_iommu_memory_write(s->iommu, addr, buf, len);
    } else {
        addr &= ~1;
        len &= ~1;
        while (len > 0) {
            l = len;
            if (l > sizeof(tmp_buf))
                l = sizeof(tmp_buf);
            for(i = 0; i < l; i += 2) {
                tmp_buf[i >> 1] = bswap16(*(uint16_t *)(buf + i));
            }
            sparc_iommu_memory_write(s->iommu, addr, (uint8_t *)tmp_buf, l);
            len -= l;
            buf += l;
            addr += l;
        }
    }
}

void espdma_raise_irq(void *opaque)
{
    DMAState *s = opaque;

    DPRINTF("Raise ESP IRQ\n");
    s->dmaregs[0] |= DMA_INTR;
    qemu_irq_raise(s->espirq);
}

void espdma_clear_irq(void *opaque)
{
    DMAState *s = opaque;

    s->dmaregs[0] &= ~DMA_INTR;
    DPRINTF("Lower ESP IRQ\n");
    qemu_irq_lower(s->espirq);
}

void espdma_memory_read(void *opaque, uint8_t *buf, int len)
{
    DMAState *s = opaque;

    DPRINTF("DMA read, direction: %c, addr 0x%8.8x\n",
            s->dmaregs[0] & DMA_WRITE_MEM ? 'w': 'r', s->dmaregs[1]);
    sparc_iommu_memory_read(s->iommu, s->dmaregs[1], buf, len);
    s->dmaregs[0] |= DMA_INTR;
    s->dmaregs[1] += len;
}

void espdma_memory_write(void *opaque, uint8_t *buf, int len)
{
    DMAState *s = opaque;

    DPRINTF("DMA write, direction: %c, addr 0x%8.8x\n",
            s->dmaregs[0] & DMA_WRITE_MEM ? 'w': 'r', s->dmaregs[1]);
    sparc_iommu_memory_write(s->iommu, s->dmaregs[1], buf, len);
    s->dmaregs[0] |= DMA_INTR;
    s->dmaregs[1] += len;
}

static uint32_t dma_mem_readl(void *opaque, target_phys_addr_t addr)
{
    DMAState *s = opaque;
    uint32_t saddr;

    saddr = (addr & DMA_MAXADDR) >> 2;
    DPRINTF("read dmareg[%d]: 0x%8.8x\n", saddr, s->dmaregs[saddr]);

    return s->dmaregs[saddr];
}

static void dma_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    DMAState *s = opaque;
    uint32_t saddr;

    saddr = (addr & DMA_MAXADDR) >> 2;
    DPRINTF("write dmareg[%d]: 0x%8.8x -> 0x%8.8x\n", saddr, s->dmaregs[saddr], val);
    switch (saddr) {
    case 0:
        if (!(val & DMA_INTREN)) {
            DPRINTF("Lower ESP IRQ\n");
            qemu_irq_lower(s->espirq);
        }
        if (val & DMA_RESET) {
            esp_reset(s->esp_opaque);
        } else if (val & 0x40) {
            val &= ~0x40;
        } else if (val == 0)
            val = 0x40;
        val &= 0x0fffffff;
        val |= DMA_VER;
        break;
    case 1:
        s->dmaregs[0] |= DMA_LOADED;
        break;
    case 4:
        /* ??? Should this mask out the lance IRQ?  The NIC may re-assert
           this IRQ unexpectedly.  */
        if (!(val & DMA_INTREN)) {
            DPRINTF("Lower Lance IRQ\n");
            qemu_irq_lower(s->leirq);
        }
        if (val & DMA_RESET)
            pcnet_h_reset(s->lance_opaque);
        val &= 0x0fffffff;
        val |= DMA_VER;
        break;
    default:
        break;
    }
    s->dmaregs[saddr] = val;
}

static CPUReadMemoryFunc *dma_mem_read[3] = {
    dma_mem_readl,
    dma_mem_readl,
    dma_mem_readl,
};

static CPUWriteMemoryFunc *dma_mem_write[3] = {
    dma_mem_writel,
    dma_mem_writel,
    dma_mem_writel,
};

static void dma_reset(void *opaque)
{
    DMAState *s = opaque;

    memset(s->dmaregs, 0, DMA_REGS * 4);
    s->dmaregs[0] = DMA_VER;
    s->dmaregs[4] = DMA_VER;
}

static void dma_save(QEMUFile *f, void *opaque)
{
    DMAState *s = opaque;
    unsigned int i;

    for (i = 0; i < DMA_REGS; i++)
        qemu_put_be32s(f, &s->dmaregs[i]);
}

static int dma_load(QEMUFile *f, void *opaque, int version_id)
{
    DMAState *s = opaque;
    unsigned int i;

    if (version_id != 1)
        return -EINVAL;
    for (i = 0; i < DMA_REGS; i++)
        qemu_get_be32s(f, &s->dmaregs[i]);

    return 0;
}

void *sparc32_dma_init(target_phys_addr_t daddr, qemu_irq espirq,
                       qemu_irq leirq, void *iommu)
{
    DMAState *s;
    int dma_io_memory;

    s = qemu_mallocz(sizeof(DMAState));
    if (!s)
        return NULL;

    s->espirq = espirq;
    s->leirq = leirq;
    s->iommu = iommu;

    dma_io_memory = cpu_register_io_memory(0, dma_mem_read, dma_mem_write, s);
    cpu_register_physical_memory(daddr, 16 * 2, dma_io_memory);

    register_savevm("sparc32_dma", daddr, 1, dma_save, dma_load, s);
    qemu_register_reset(dma_reset, s);

    return s;
}

void sparc32_dma_set_reset_data(void *opaque, void *esp_opaque,
                                void *lance_opaque)
{
    DMAState *s = opaque;

    s->esp_opaque = esp_opaque;
    s->lance_opaque = lance_opaque;
}
