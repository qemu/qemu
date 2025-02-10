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
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sparc/sparc32_dma.h"
#include "hw/sparc/sun4m_iommu.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "system/dma.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "trace.h"

/*
 * This is the DMA controller part of chip STP2000 (Master I/O), also
 * produced as NCR89C100. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C100.txt
 * and
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/DMA2.txt
 */

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

enum {
    GPIO_RESET = 0,
    GPIO_DMA,
};

/* Note: on sparc, the lance 16 bit bus is swapped */
void ledma_memory_read(void *opaque, hwaddr addr,
                       uint8_t *buf, int len, int do_bswap)
{
    DMADeviceState *s = opaque;
    IOMMUState *is = (IOMMUState *)s->iommu;
    int i;

    addr |= s->dmaregs[3];
    trace_ledma_memory_read(addr, len);
    if (do_bswap) {
        dma_memory_read(&is->iommu_as, addr, buf, len, MEMTXATTRS_UNSPECIFIED);
    } else {
        addr &= ~1;
        len &= ~1;
        dma_memory_read(&is->iommu_as, addr, buf, len, MEMTXATTRS_UNSPECIFIED);
        for(i = 0; i < len; i += 2) {
            bswap16s((uint16_t *)(buf + i));
        }
    }
}

void ledma_memory_write(void *opaque, hwaddr addr,
                        uint8_t *buf, int len, int do_bswap)
{
    DMADeviceState *s = opaque;
    IOMMUState *is = (IOMMUState *)s->iommu;
    int l, i;
    uint16_t tmp_buf[32];

    addr |= s->dmaregs[3];
    trace_ledma_memory_write(addr, len);
    if (do_bswap) {
        dma_memory_write(&is->iommu_as, addr, buf, len,
                         MEMTXATTRS_UNSPECIFIED);
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
            dma_memory_write(&is->iommu_as, addr, tmp_buf, l,
                             MEMTXATTRS_UNSPECIFIED);
            len -= l;
            buf += l;
            addr += l;
        }
    }
}

static void dma_set_irq(void *opaque, int irq, int level)
{
    DMADeviceState *s = opaque;
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
    DMADeviceState *s = opaque;
    IOMMUState *is = (IOMMUState *)s->iommu;

    trace_espdma_memory_read(s->dmaregs[1], len);
    dma_memory_read(&is->iommu_as, s->dmaregs[1], buf, len,
                    MEMTXATTRS_UNSPECIFIED);
    s->dmaregs[1] += len;
}

void espdma_memory_write(void *opaque, uint8_t *buf, int len)
{
    DMADeviceState *s = opaque;
    IOMMUState *is = (IOMMUState *)s->iommu;

    trace_espdma_memory_write(s->dmaregs[1], len);
    dma_memory_write(&is->iommu_as, s->dmaregs[1], buf, len,
                     MEMTXATTRS_UNSPECIFIED);
    s->dmaregs[1] += len;
}

static uint64_t dma_mem_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    DMADeviceState *s = opaque;
    uint32_t saddr;

    saddr = (addr & DMA_MASK) >> 2;
    trace_sparc32_dma_mem_readl(addr, s->dmaregs[saddr]);
    return s->dmaregs[saddr];
}

static void dma_mem_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    DMADeviceState *s = opaque;
    uint32_t saddr;

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

static void sparc32_dma_device_reset(DeviceState *d)
{
    DMADeviceState *s = SPARC32_DMA_DEVICE(d);

    memset(s->dmaregs, 0, DMA_SIZE);
    s->dmaregs[0] = DMA_VER;
}

static const VMStateDescription vmstate_sparc32_dma_device = {
    .name ="sparc32_dma",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(dmaregs, DMADeviceState, DMA_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void sparc32_dma_device_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    DMADeviceState *s = SPARC32_DMA_DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);

    sysbus_init_mmio(sbd, &s->iomem);

    object_property_add_link(OBJECT(dev), "iommu", TYPE_SUN4M_IOMMU,
                             (Object **) &s->iommu,
                             qdev_prop_allow_set_link_before_realize,
                             0);

    qdev_init_gpio_in(dev, dma_set_irq, 1);
    qdev_init_gpio_out(dev, s->gpio, 2);
}

static void sparc32_dma_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, sparc32_dma_device_reset);
    dc->vmsd = &vmstate_sparc32_dma_device;
}

static const TypeInfo sparc32_dma_device_info = {
    .name          = TYPE_SPARC32_DMA_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .abstract      = true,
    .instance_size = sizeof(DMADeviceState),
    .instance_init = sparc32_dma_device_init,
    .class_init    = sparc32_dma_device_class_init,
};

static void sparc32_espdma_device_init(Object *obj)
{
    DMADeviceState *s = SPARC32_DMA_DEVICE(obj);
    ESPDMADeviceState *es = SPARC32_ESPDMA_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &dma_mem_ops, s,
                          "espdma-mmio", DMA_SIZE);

    object_initialize_child(obj, "esp", &es->esp, TYPE_SYSBUS_ESP);
}

static void sparc32_espdma_device_realize(DeviceState *dev, Error **errp)
{
    ESPDMADeviceState *es = SPARC32_ESPDMA_DEVICE(dev);
    SysBusESPState *sysbus = SYSBUS_ESP(&es->esp);
    ESPState *esp = &sysbus->esp;

    esp->dma_memory_read = espdma_memory_read;
    esp->dma_memory_write = espdma_memory_write;
    esp->dma_opaque = SPARC32_DMA_DEVICE(dev);
    sysbus->it_shift = 2;
    esp->dma_enabled = 1;
    sysbus_realize(SYS_BUS_DEVICE(sysbus), &error_fatal);
}

static void sparc32_espdma_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sparc32_espdma_device_realize;
}

static const TypeInfo sparc32_espdma_device_info = {
    .name          = TYPE_SPARC32_ESPDMA_DEVICE,
    .parent        = TYPE_SPARC32_DMA_DEVICE,
    .instance_size = sizeof(ESPDMADeviceState),
    .instance_init = sparc32_espdma_device_init,
    .class_init    = sparc32_espdma_device_class_init,
};

static void sparc32_ledma_device_init(Object *obj)
{
    DMADeviceState *s = SPARC32_DMA_DEVICE(obj);
    LEDMADeviceState *ls = SPARC32_LEDMA_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &dma_mem_ops, s,
                          "ledma-mmio", DMA_SIZE);

    object_initialize_child(obj, "lance", &ls->lance, TYPE_LANCE);
}

static void sparc32_ledma_device_realize(DeviceState *dev, Error **errp)
{
    LEDMADeviceState *s = SPARC32_LEDMA_DEVICE(dev);
    SysBusPCNetState *lance = SYSBUS_PCNET(&s->lance);

    object_property_set_link(OBJECT(lance), "dma", OBJECT(dev), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(lance), &error_fatal);
}

static void sparc32_ledma_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sparc32_ledma_device_realize;
}

static const TypeInfo sparc32_ledma_device_info = {
    .name          = TYPE_SPARC32_LEDMA_DEVICE,
    .parent        = TYPE_SPARC32_DMA_DEVICE,
    .instance_size = sizeof(LEDMADeviceState),
    .instance_init = sparc32_ledma_device_init,
    .class_init    = sparc32_ledma_device_class_init,
};

static void sparc32_dma_realize(DeviceState *dev, Error **errp)
{
    SPARC32DMAState *s = SPARC32_DMA(dev);
    DeviceState *espdma, *esp, *ledma, *lance;
    SysBusDevice *sbd;
    Object *iommu;

    iommu = object_resolve_path_type("", TYPE_SUN4M_IOMMU, NULL);
    if (!iommu) {
        error_setg(errp, "unable to locate sun4m IOMMU device");
        return;
    }

    espdma = DEVICE(&s->espdma);
    object_property_set_link(OBJECT(espdma), "iommu", iommu, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(espdma), &error_fatal);

    esp = DEVICE(object_resolve_path_component(OBJECT(espdma), "esp"));
    sbd = SYS_BUS_DEVICE(esp);
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(espdma, 0));
    qdev_connect_gpio_out(espdma, 0, qdev_get_gpio_in(esp, 0));
    qdev_connect_gpio_out(espdma, 1, qdev_get_gpio_in(esp, 1));

    sbd = SYS_BUS_DEVICE(espdma);
    memory_region_add_subregion(&s->dmamem, 0x0,
                                sysbus_mmio_get_region(sbd, 0));

    ledma = DEVICE(&s->ledma);
    object_property_set_link(OBJECT(ledma), "iommu", iommu, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(ledma), &error_fatal);

    lance = DEVICE(object_resolve_path_component(OBJECT(ledma), "lance"));
    sbd = SYS_BUS_DEVICE(lance);
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(ledma, 0));
    qdev_connect_gpio_out(ledma, 0, qdev_get_gpio_in(lance, 0));

    sbd = SYS_BUS_DEVICE(ledma);
    memory_region_add_subregion(&s->dmamem, 0x10,
                                sysbus_mmio_get_region(sbd, 0));

    /* Add ledma alias to handle SunOS 5.7 - Solaris 9 invalid access bug */
    memory_region_init_alias(&s->ledma_alias, OBJECT(dev), "ledma-alias",
                             sysbus_mmio_get_region(sbd, 0), 0x4, 0x4);
    memory_region_add_subregion(&s->dmamem, 0x20, &s->ledma_alias);
}

static void sparc32_dma_init(Object *obj)
{
    SPARC32DMAState *s = SPARC32_DMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init(&s->dmamem, OBJECT(s), "dma", DMA_SIZE + DMA_ETH_SIZE);
    sysbus_init_mmio(sbd, &s->dmamem);

    object_initialize_child(obj, "espdma", &s->espdma,
                            TYPE_SPARC32_ESPDMA_DEVICE);
    object_initialize_child(obj, "ledma", &s->ledma,
                            TYPE_SPARC32_LEDMA_DEVICE);
}

static void sparc32_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sparc32_dma_realize;
}

static const TypeInfo sparc32_dma_info = {
    .name          = TYPE_SPARC32_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SPARC32DMAState),
    .instance_init = sparc32_dma_init,
    .class_init    = sparc32_dma_class_init,
};


static void sparc32_dma_register_types(void)
{
    type_register_static(&sparc32_dma_device_info);
    type_register_static(&sparc32_espdma_device_info);
    type_register_static(&sparc32_ledma_device_info);
    type_register_static(&sparc32_dma_info);
}

type_init(sparc32_dma_register_types)
