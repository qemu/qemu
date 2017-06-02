/*
 * QEMU Sparc32 DMA controller emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Modifications:
 *  2010-Feb-14 Artyom Tarasenko : reworked irq generation
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
#include "hw/sparc/sparc32_dma.h"
#include "hw/sparc/sun4m.h"
#include "hw/sysbus.h"
#include "trace.h"

/*
 * This is the DMA controller part of chip STP2000 (Master I/O), also
 * produced as NCR89C100. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C100.txt
 * and
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/DMA2.txt
 */

#define DMA_REGS 4
#define DMA_SIZE (4 * sizeof(uint32_t))
/* We need the mask, because one instance of the device is not page
   aligned (ledma, start address 0x0010) */
#define DMA_MASK (DMA_SIZE - 1)
/* OBP says 0x20 bytes for ledma, the extras are aliased to espdma */
#define DMA_ETH_SIZE (8 * sizeof(uint32_t))
#define DMA_MAX_REG_OFFSET (2 * DMA_SIZE - 1)

#define DMA_VER 0xa0000000
#define DMA_INTR 1
#define DMA_INTREN 0x10
#define DMA_WRITE_MEM 0x100
#define DMA_EN 0x200
#define DMA_LOADED 0x04000000
#define DMA_DRAIN_FIFO 0x40
#define DMA_RESET 0x80

/* XXX SCSI and ethernet should have different read-only bit masks */
#define DMA_CSR_RO_MASK 0xfe000007

#define TYPE_SPARC32_DMA "sparc32_dma"
#define SPARC32_DMA(obj) OBJECT_CHECK(DMAState, (obj), TYPE_SPARC32_DMA)

typedef struct DMAState DMAState;

struct DMAState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t dmaregs[DMA_REGS];
    qemu_irq irq;
    void *iommu;
    qemu_irq gpio[2];
    uint32_t is_ledma;
};

enum {
    GPIO_RESET = 0,
    GPIO_DMA,
};

/* Note: on sparc, the lance 16 bit bus is swapped */
void ledma_memory_read(void *opaque, hwaddr addr,
                       uint8_t *buf, int len, int do_bswap)
{
    DMAState *s = opaque;
    int i;

    addr |= s->dmaregs[3];
    trace_ledma_memory_read(addr);
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

void ledma_memory_write(void *opaque, hwaddr addr,
                        uint8_t *buf, int len, int do_bswap)
{
    DMAState *s = opaque;
    int l, i;
    uint16_t tmp_buf[32];

    addr |= s->dmaregs[3];
    trace_ledma_memory_write(addr);
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

static void dma_set_irq(void *opaque, int irq, int level)
{
    DMAState *s = opaque;
    if (level) {
        s->dmaregs[0] |= DMA_INTR;
        if (s->dmaregs[0] & DMA_INTREN) {
            trace_sparc32_dma_set_irq_raise();
            qemu_irq_raise(s->irq);
        }
    } else {
        if (s->dmaregs[0] & DMA_INTR) {
            s->dmaregs[0] &= ~DMA_INTR;
            if (s->dmaregs[0] & DMA_INTREN) {
                trace_sparc32_dma_set_irq_lower();
                qemu_irq_lower(s->irq);
            }
        }
    }
}

void espdma_memory_read(void *opaque, uint8_t *buf, int len)
{
    DMAState *s = opaque;

    trace_espdma_memory_read(s->dmaregs[1]);
    sparc_iommu_memory_read(s->iommu, s->dmaregs[1], buf, len);
    s->dmaregs[1] += len;
}

void espdma_memory_write(void *opaque, uint8_t *buf, int len)
{
    DMAState *s = opaque;

    trace_espdma_memory_write(s->dmaregs[1]);
    sparc_iommu_memory_write(s->iommu, s->dmaregs[1], buf, len);
    s->dmaregs[1] += len;
}

static uint64_t dma_mem_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    DMAState *s = opaque;
    uint32_t saddr;

    if (s->is_ledma && (addr > DMA_MAX_REG_OFFSET)) {
        /* aliased to espdma, but we can't get there from here */
        /* buggy driver if using undocumented behavior, just return 0 */
        trace_sparc32_dma_mem_readl(addr, 0);
        return 0;
    }
    saddr = (addr & DMA_MASK) >> 2;
    trace_sparc32_dma_mem_readl(addr, s->dmaregs[saddr]);
    return s->dmaregs[saddr];
}

static void dma_mem_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    DMAState *s = opaque;
    uint32_t saddr;

    if (s->is_ledma && (addr > DMA_MAX_REG_OFFSET)) {
        /* aliased to espdma, but we can't get there from here */
        trace_sparc32_dma_mem_writel(addr, 0, val);
        return;
    }
    saddr = (addr & DMA_MASK) >> 2;
    trace_sparc32_dma_mem_writel(addr, s->dmaregs[saddr], val);
    switch (saddr) {
    case 0:
        if (val & DMA_INTREN) {
            if (s->dmaregs[0] & DMA_INTR) {
                trace_sparc32_dma_set_irq_raise();
                qemu_irq_raise(s->irq);
            }
        } else {
            if (s->dmaregs[0] & (DMA_INTR | DMA_INTREN)) {
                trace_sparc32_dma_set_irq_lower();
                qemu_irq_lower(s->irq);
            }
        }
        if (val & DMA_RESET) {
            qemu_irq_raise(s->gpio[GPIO_RESET]);
            qemu_irq_lower(s->gpio[GPIO_RESET]);
        } else if (val & DMA_DRAIN_FIFO) {
            val &= ~DMA_DRAIN_FIFO;
        } else if (val == 0)
            val = DMA_DRAIN_FIFO;

        if (val & DMA_EN && !(s->dmaregs[0] & DMA_EN)) {
            trace_sparc32_dma_enable_raise();
            qemu_irq_raise(s->gpio[GPIO_DMA]);
        } else if (!(val & DMA_EN) && !!(s->dmaregs[0] & DMA_EN)) {
            trace_sparc32_dma_enable_lower();
            qemu_irq_lower(s->gpio[GPIO_DMA]);
        }

        val &= ~DMA_CSR_RO_MASK;
        val |= DMA_VER;
        s->dmaregs[0] = (s->dmaregs[0] & DMA_CSR_RO_MASK) | val;
        break;
    case 1:
        s->dmaregs[0] |= DMA_LOADED;
        /* fall through */
    default:
        s->dmaregs[saddr] = val;
        break;
    }
}

static const MemoryRegionOps dma_mem_ops = {
    .read = dma_mem_read,
    .write = dma_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void dma_reset(DeviceState *d)
{
    DMAState *s = SPARC32_DMA(d);

    memset(s->dmaregs, 0, DMA_SIZE);
    s->dmaregs[0] = DMA_VER;
}

static const VMStateDescription vmstate_dma = {
    .name ="sparc32_dma",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(dmaregs, DMAState, DMA_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void sparc32_dma_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    DMAState *s = SPARC32_DMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);

    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(dev, dma_set_irq, 1);
    qdev_init_gpio_out(dev, s->gpio, 2);
}

static void sparc32_dma_realize(DeviceState *dev, Error **errp)
{
    DMAState *s = SPARC32_DMA(dev);
    int reg_size;

    reg_size = s->is_ledma ? DMA_ETH_SIZE : DMA_SIZE;
    memory_region_init_io(&s->iomem, OBJECT(dev), &dma_mem_ops, s,
                          "dma", reg_size);
}

static Property sparc32_dma_properties[] = {
    DEFINE_PROP_PTR("iommu_opaque", DMAState, iommu),
    DEFINE_PROP_UINT32("is_ledma", DMAState, is_ledma, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void sparc32_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = dma_reset;
    dc->vmsd = &vmstate_dma;
    dc->props = sparc32_dma_properties;
    dc->realize = sparc32_dma_realize;
    /* Reason: pointer property "iommu_opaque" */
    dc->user_creatable = false;
}

static const TypeInfo sparc32_dma_info = {
    .name          = TYPE_SPARC32_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DMAState),
    .instance_init = sparc32_dma_init,
    .class_init    = sparc32_dma_class_init,
};

static void sparc32_dma_register_types(void)
{
    type_register_static(&sparc32_dma_info);
}

type_init(sparc32_dma_register_types)
