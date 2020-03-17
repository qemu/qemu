/*
 * QEMU JAZZ RC4030 chipset
 *
 * Copyright (c) 2007-2013 Hervé Poussineau
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
#include "qemu/units.h"
#include "hw/irq.h"
#include "hw/mips/mips.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "trace.h"

/********************************************************/
/* rc4030 emulation                                     */

typedef struct dma_pagetable_entry {
    int32_t frame;
    int32_t owner;
} QEMU_PACKED dma_pagetable_entry;

#define DMA_PAGESIZE    4096
#define DMA_REG_ENABLE  1
#define DMA_REG_COUNT   2
#define DMA_REG_ADDRESS 3

#define DMA_FLAG_ENABLE     0x0001
#define DMA_FLAG_MEM_TO_DEV 0x0002
#define DMA_FLAG_TC_INTR    0x0100
#define DMA_FLAG_MEM_INTR   0x0200
#define DMA_FLAG_ADDR_INTR  0x0400

#define TYPE_RC4030 "rc4030"
#define RC4030(obj) \
    OBJECT_CHECK(rc4030State, (obj), TYPE_RC4030)

#define TYPE_RC4030_IOMMU_MEMORY_REGION "rc4030-iommu-memory-region"

typedef struct rc4030State {

    SysBusDevice parent;

    uint32_t config; /* 0x0000: RC4030 config register */
    uint32_t revision; /* 0x0008: RC4030 Revision register */
    uint32_t invalid_address_register; /* 0x0010: Invalid Address register */

    /* DMA */
    uint32_t dma_regs[8][4];
    uint32_t dma_tl_base; /* 0x0018: DMA transl. table base */
    uint32_t dma_tl_limit; /* 0x0020: DMA transl. table limit */

    /* cache */
    uint32_t cache_maint; /* 0x0030: Cache Maintenance */
    uint32_t remote_failed_address; /* 0x0038: Remote Failed Address */
    uint32_t memory_failed_address; /* 0x0040: Memory Failed Address */
    uint32_t cache_ptag; /* 0x0048: I/O Cache Physical Tag */
    uint32_t cache_ltag; /* 0x0050: I/O Cache Logical Tag */
    uint32_t cache_bmask; /* 0x0058: I/O Cache Byte Mask */

    uint32_t nmi_interrupt; /* 0x0200: interrupt source */
    uint32_t memory_refresh_rate; /* 0x0210: memory refresh rate */
    uint32_t nvram_protect; /* 0x0220: NV ram protect register */
    uint32_t rem_speed[16];
    uint32_t imr_jazz; /* Local bus int enable mask */
    uint32_t isr_jazz; /* Local bus int source */

    /* timer */
    QEMUTimer *periodic_timer;
    uint32_t itr; /* Interval timer reload */

    qemu_irq timer_irq;
    qemu_irq jazz_bus_irq;

    /* whole DMA memory region, root of DMA address space */
    IOMMUMemoryRegion dma_mr;
    AddressSpace dma_as;

    MemoryRegion iomem_chipset;
    MemoryRegion iomem_jazzio;
} rc4030State;

static void set_next_tick(rc4030State *s)
{
    uint32_t tm_hz;
    qemu_irq_lower(s->timer_irq);

    tm_hz = 1000 / (s->itr + 1);

    timer_mod(s->periodic_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                   NANOSECONDS_PER_SECOND / tm_hz);
}

/* called for accesses to rc4030 */
static uint64_t rc4030_read(void *opaque, hwaddr addr, unsigned int size)
{
    rc4030State *s = opaque;
    uint32_t val;

    addr &= 0x3fff;
    switch (addr & ~0x3) {
    /* Global config register */
    case 0x0000:
        val = s->config;
        break;
    /* Revision register */
    case 0x0008:
        val = s->revision;
        break;
    /* Invalid Address register */
    case 0x0010:
        val = s->invalid_address_register;
        break;
    /* DMA transl. table base */
    case 0x0018:
        val = s->dma_tl_base;
        break;
    /* DMA transl. table limit */
    case 0x0020:
        val = s->dma_tl_limit;
        break;
    /* Remote Failed Address */
    case 0x0038:
        val = s->remote_failed_address;
        break;
    /* Memory Failed Address */
    case 0x0040:
        val = s->memory_failed_address;
        break;
    /* I/O Cache Byte Mask */
    case 0x0058:
        val = s->cache_bmask;
        /* HACK */
        if (s->cache_bmask == (uint32_t)-1) {
            s->cache_bmask = 0;
        }
        break;
    /* Remote Speed Registers */
    case 0x0070:
    case 0x0078:
    case 0x0080:
    case 0x0088:
    case 0x0090:
    case 0x0098:
    case 0x00a0:
    case 0x00a8:
    case 0x00b0:
    case 0x00b8:
    case 0x00c0:
    case 0x00c8:
    case 0x00d0:
    case 0x00d8:
    case 0x00e0:
    case 0x00e8:
        val = s->rem_speed[(addr - 0x0070) >> 3];
        break;
    /* DMA channel base address */
    case 0x0100:
    case 0x0108:
    case 0x0110:
    case 0x0118:
    case 0x0120:
    case 0x0128:
    case 0x0130:
    case 0x0138:
    case 0x0140:
    case 0x0148:
    case 0x0150:
    case 0x0158:
    case 0x0160:
    case 0x0168:
    case 0x0170:
    case 0x0178:
    case 0x0180:
    case 0x0188:
    case 0x0190:
    case 0x0198:
    case 0x01a0:
    case 0x01a8:
    case 0x01b0:
    case 0x01b8:
    case 0x01c0:
    case 0x01c8:
    case 0x01d0:
    case 0x01d8:
    case 0x01e0:
    case 0x01e8:
    case 0x01f0:
    case 0x01f8:
        {
            int entry = (addr - 0x0100) >> 5;
            int idx = (addr & 0x1f) >> 3;
            val = s->dma_regs[entry][idx];
        }
        break;
    /* Interrupt source */
    case 0x0200:
        val = s->nmi_interrupt;
        break;
    /* Error type */
    case 0x0208:
        val = 0;
        break;
    /* Memory refresh rate */
    case 0x0210:
        val = s->memory_refresh_rate;
        break;
    /* NV ram protect register */
    case 0x0220:
        val = s->nvram_protect;
        break;
    /* Interval timer count */
    case 0x0230:
        val = 0;
        qemu_irq_lower(s->timer_irq);
        break;
    /* EISA interrupt */
    case 0x0238:
        val = 7; /* FIXME: should be read from EISA controller */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rc4030: invalid read at 0x%x", (int)addr);
        val = 0;
        break;
    }

    if ((addr & ~3) != 0x230) {
        trace_rc4030_read(addr, val);
    }

    return val;
}

static void rc4030_write(void *opaque, hwaddr addr, uint64_t data,
                         unsigned int size)
{
    rc4030State *s = opaque;
    uint32_t val = data;
    addr &= 0x3fff;

    trace_rc4030_write(addr, val);

    switch (addr & ~0x3) {
    /* Global config register */
    case 0x0000:
        s->config = val;
        break;
    /* DMA transl. table base */
    case 0x0018:
        s->dma_tl_base = val;
        break;
    /* DMA transl. table limit */
    case 0x0020:
        s->dma_tl_limit = val;
        break;
    /* DMA transl. table invalidated */
    case 0x0028:
        break;
    /* Cache Maintenance */
    case 0x0030:
        s->cache_maint = val;
        break;
    /* I/O Cache Physical Tag */
    case 0x0048:
        s->cache_ptag = val;
        break;
    /* I/O Cache Logical Tag */
    case 0x0050:
        s->cache_ltag = val;
        break;
    /* I/O Cache Byte Mask */
    case 0x0058:
        s->cache_bmask |= val; /* HACK */
        break;
    /* I/O Cache Buffer Window */
    case 0x0060:
        /* HACK */
        if (s->cache_ltag == 0x80000001 && s->cache_bmask == 0xf0f0f0f) {
            hwaddr dest = s->cache_ptag & ~0x1;
            dest += (s->cache_maint & 0x3) << 3;
            cpu_physical_memory_write(dest, &val, 4);
        }
        break;
    /* Remote Speed Registers */
    case 0x0070:
    case 0x0078:
    case 0x0080:
    case 0x0088:
    case 0x0090:
    case 0x0098:
    case 0x00a0:
    case 0x00a8:
    case 0x00b0:
    case 0x00b8:
    case 0x00c0:
    case 0x00c8:
    case 0x00d0:
    case 0x00d8:
    case 0x00e0:
    case 0x00e8:
        s->rem_speed[(addr - 0x0070) >> 3] = val;
        break;
    /* DMA channel base address */
    case 0x0100:
    case 0x0108:
    case 0x0110:
    case 0x0118:
    case 0x0120:
    case 0x0128:
    case 0x0130:
    case 0x0138:
    case 0x0140:
    case 0x0148:
    case 0x0150:
    case 0x0158:
    case 0x0160:
    case 0x0168:
    case 0x0170:
    case 0x0178:
    case 0x0180:
    case 0x0188:
    case 0x0190:
    case 0x0198:
    case 0x01a0:
    case 0x01a8:
    case 0x01b0:
    case 0x01b8:
    case 0x01c0:
    case 0x01c8:
    case 0x01d0:
    case 0x01d8:
    case 0x01e0:
    case 0x01e8:
    case 0x01f0:
    case 0x01f8:
        {
            int entry = (addr - 0x0100) >> 5;
            int idx = (addr & 0x1f) >> 3;
            s->dma_regs[entry][idx] = val;
        }
        break;
    /* Memory refresh rate */
    case 0x0210:
        s->memory_refresh_rate = val;
        break;
    /* Interval timer reload */
    case 0x0228:
        s->itr = val & 0x01FF;
        qemu_irq_lower(s->timer_irq);
        set_next_tick(s);
        break;
    /* EISA interrupt */
    case 0x0238:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rc4030: invalid write of 0x%02x at 0x%x",
                      val, (int)addr);
        break;
    }
}

static const MemoryRegionOps rc4030_ops = {
    .read = rc4030_read,
    .write = rc4030_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void update_jazz_irq(rc4030State *s)
{
    uint16_t pending;

    pending = s->isr_jazz & s->imr_jazz;

    if (pending != 0) {
        qemu_irq_raise(s->jazz_bus_irq);
    } else {
        qemu_irq_lower(s->jazz_bus_irq);
    }
}

static void rc4030_irq_jazz_request(void *opaque, int irq, int level)
{
    rc4030State *s = opaque;

    if (level) {
        s->isr_jazz |= 1 << irq;
    } else {
        s->isr_jazz &= ~(1 << irq);
    }

    update_jazz_irq(s);
}

static void rc4030_periodic_timer(void *opaque)
{
    rc4030State *s = opaque;

    set_next_tick(s);
    qemu_irq_raise(s->timer_irq);
}

static uint64_t jazzio_read(void *opaque, hwaddr addr, unsigned int size)
{
    rc4030State *s = opaque;
    uint32_t val;
    uint32_t irq;
    addr &= 0xfff;

    switch (addr) {
    /* Local bus int source */
    case 0x00: {
        uint32_t pending = s->isr_jazz & s->imr_jazz;
        val = 0;
        irq = 0;
        while (pending) {
            if (pending & 1) {
                val = (irq + 1) << 2;
                break;
            }
            irq++;
            pending >>= 1;
        }
        break;
    }
    /* Local bus int enable mask */
    case 0x02:
        val = s->imr_jazz;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rc4030/jazzio: invalid read at 0x%x", (int)addr);
        val = 0;
        break;
    }

    trace_jazzio_read(addr, val);

    return val;
}

static void jazzio_write(void *opaque, hwaddr addr, uint64_t data,
                         unsigned int size)
{
    rc4030State *s = opaque;
    uint32_t val = data;
    addr &= 0xfff;

    trace_jazzio_write(addr, val);

    switch (addr) {
    /* Local bus int enable mask */
    case 0x02:
        s->imr_jazz = val;
        update_jazz_irq(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rc4030/jazzio: invalid write of 0x%02x at 0x%x",
                      val, (int)addr);
        break;
    }
}

static const MemoryRegionOps jazzio_ops = {
    .read = jazzio_read,
    .write = jazzio_write,
    .impl.min_access_size = 2,
    .impl.max_access_size = 2,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static IOMMUTLBEntry rc4030_dma_translate(IOMMUMemoryRegion *iommu, hwaddr addr,
                                          IOMMUAccessFlags flag, int iommu_idx)
{
    rc4030State *s = container_of(iommu, rc4030State, dma_mr);
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = addr & ~(DMA_PAGESIZE - 1),
        .translated_addr = 0,
        .addr_mask = DMA_PAGESIZE - 1,
        .perm = IOMMU_NONE,
    };
    uint64_t i, entry_address;
    dma_pagetable_entry entry;

    i = addr / DMA_PAGESIZE;
    if (i < s->dma_tl_limit / sizeof(entry)) {
        entry_address = (s->dma_tl_base & 0x7fffffff) + i * sizeof(entry);
        if (address_space_read(ret.target_as, entry_address,
                               MEMTXATTRS_UNSPECIFIED, &entry, sizeof(entry))
                == MEMTX_OK) {
            ret.translated_addr = entry.frame & ~(DMA_PAGESIZE - 1);
            ret.perm = IOMMU_RW;
        }
    }

    return ret;
}

static void rc4030_reset(DeviceState *dev)
{
    rc4030State *s = RC4030(dev);
    int i;

    s->config = 0x410; /* some boards seem to accept 0x104 too */
    s->revision = 1;
    s->invalid_address_register = 0;

    memset(s->dma_regs, 0, sizeof(s->dma_regs));

    s->remote_failed_address = s->memory_failed_address = 0;
    s->cache_maint = 0;
    s->cache_ptag = s->cache_ltag = 0;
    s->cache_bmask = 0;

    s->memory_refresh_rate = 0x18186;
    s->nvram_protect = 7;
    for (i = 0; i < 15; i++) {
        s->rem_speed[i] = 7;
    }
    s->imr_jazz = 0x10; /* XXX: required by firmware, but why? */
    s->isr_jazz = 0;

    s->itr = 0;

    qemu_irq_lower(s->timer_irq);
    qemu_irq_lower(s->jazz_bus_irq);
}

static int rc4030_post_load(void *opaque, int version_id)
{
    rc4030State *s = opaque;

    set_next_tick(s);
    update_jazz_irq(s);

    return 0;
}

static const VMStateDescription vmstate_rc4030 = {
    .name = "rc4030",
    .version_id = 3,
    .post_load = rc4030_post_load,
    .fields = (VMStateField []) {
        VMSTATE_UINT32(config, rc4030State),
        VMSTATE_UINT32(invalid_address_register, rc4030State),
        VMSTATE_UINT32_2DARRAY(dma_regs, rc4030State, 8, 4),
        VMSTATE_UINT32(dma_tl_base, rc4030State),
        VMSTATE_UINT32(dma_tl_limit, rc4030State),
        VMSTATE_UINT32(cache_maint, rc4030State),
        VMSTATE_UINT32(remote_failed_address, rc4030State),
        VMSTATE_UINT32(memory_failed_address, rc4030State),
        VMSTATE_UINT32(cache_ptag, rc4030State),
        VMSTATE_UINT32(cache_ltag, rc4030State),
        VMSTATE_UINT32(cache_bmask, rc4030State),
        VMSTATE_UINT32(memory_refresh_rate, rc4030State),
        VMSTATE_UINT32(nvram_protect, rc4030State),
        VMSTATE_UINT32_ARRAY(rem_speed, rc4030State, 16),
        VMSTATE_UINT32(imr_jazz, rc4030State),
        VMSTATE_UINT32(isr_jazz, rc4030State),
        VMSTATE_UINT32(itr, rc4030State),
        VMSTATE_END_OF_LIST()
    }
};

static void rc4030_do_dma(void *opaque, int n, uint8_t *buf,
                          int len, bool is_write)
{
    rc4030State *s = opaque;
    hwaddr dma_addr;
    int dev_to_mem;

    s->dma_regs[n][DMA_REG_ENABLE] &=
           ~(DMA_FLAG_TC_INTR | DMA_FLAG_MEM_INTR | DMA_FLAG_ADDR_INTR);

    /* Check DMA channel consistency */
    dev_to_mem = (s->dma_regs[n][DMA_REG_ENABLE] & DMA_FLAG_MEM_TO_DEV) ? 0 : 1;
    if (!(s->dma_regs[n][DMA_REG_ENABLE] & DMA_FLAG_ENABLE) ||
        (is_write != dev_to_mem)) {
        s->dma_regs[n][DMA_REG_ENABLE] |= DMA_FLAG_MEM_INTR;
        s->nmi_interrupt |= 1 << n;
        return;
    }

    /* Get start address and len */
    if (len > s->dma_regs[n][DMA_REG_COUNT]) {
        len = s->dma_regs[n][DMA_REG_COUNT];
    }
    dma_addr = s->dma_regs[n][DMA_REG_ADDRESS];

    /* Read/write data at right place */
    address_space_rw(&s->dma_as, dma_addr, MEMTXATTRS_UNSPECIFIED,
                     buf, len, is_write);

    s->dma_regs[n][DMA_REG_ENABLE] |= DMA_FLAG_TC_INTR;
    s->dma_regs[n][DMA_REG_COUNT] -= len;
}

struct rc4030DMAState {
    void *opaque;
    int n;
};

void rc4030_dma_read(void *dma, uint8_t *buf, int len)
{
    rc4030_dma s = dma;
    rc4030_do_dma(s->opaque, s->n, buf, len, false);
}

void rc4030_dma_write(void *dma, uint8_t *buf, int len)
{
    rc4030_dma s = dma;
    rc4030_do_dma(s->opaque, s->n, buf, len, true);
}

static rc4030_dma *rc4030_allocate_dmas(void *opaque, int n)
{
    rc4030_dma *s;
    struct rc4030DMAState *p;
    int i;

    s = (rc4030_dma *)g_new0(rc4030_dma, n);
    p = (struct rc4030DMAState *)g_new0(struct rc4030DMAState, n);
    for (i = 0; i < n; i++) {
        p->opaque = opaque;
        p->n = i;
        s[i] = p;
        p++;
    }
    return s;
}

static void rc4030_initfn(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    rc4030State *s = RC4030(obj);
    SysBusDevice *sysbus = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(dev, rc4030_irq_jazz_request, 16);

    sysbus_init_irq(sysbus, &s->timer_irq);
    sysbus_init_irq(sysbus, &s->jazz_bus_irq);

    sysbus_init_mmio(sysbus, &s->iomem_chipset);
    sysbus_init_mmio(sysbus, &s->iomem_jazzio);
}

static void rc4030_realize(DeviceState *dev, Error **errp)
{
    rc4030State *s = RC4030(dev);
    Object *o = OBJECT(dev);

    s->periodic_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     rc4030_periodic_timer, s);

    memory_region_init_io(&s->iomem_chipset, o, &rc4030_ops, s,
                          "rc4030.chipset", 0x300);
    memory_region_init_io(&s->iomem_jazzio, o, &jazzio_ops, s,
                          "rc4030.jazzio", 0x00001000);

    memory_region_init_iommu(&s->dma_mr, sizeof(s->dma_mr),
                             TYPE_RC4030_IOMMU_MEMORY_REGION,
                             o, "rc4030.dma", 4 * GiB);
    address_space_init(&s->dma_as, MEMORY_REGION(&s->dma_mr), "rc4030-dma");
}

static void rc4030_unrealize(DeviceState *dev, Error **errp)
{
    rc4030State *s = RC4030(dev);

    timer_free(s->periodic_timer);

    address_space_destroy(&s->dma_as);
    object_unparent(OBJECT(&s->dma_mr));
}

static void rc4030_class_init(ObjectClass *klass, void *class_data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rc4030_realize;
    dc->unrealize = rc4030_unrealize;
    dc->reset = rc4030_reset;
    dc->vmsd = &vmstate_rc4030;
}

static const TypeInfo rc4030_info = {
    .name = TYPE_RC4030,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(rc4030State),
    .instance_init = rc4030_initfn,
    .class_init = rc4030_class_init,
};

static void rc4030_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = rc4030_dma_translate;
}

static const TypeInfo rc4030_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_RC4030_IOMMU_MEMORY_REGION,
    .class_init = rc4030_iommu_memory_region_class_init,
};

static void rc4030_register_types(void)
{
    type_register_static(&rc4030_info);
    type_register_static(&rc4030_iommu_memory_region_info);
}

type_init(rc4030_register_types)

DeviceState *rc4030_init(rc4030_dma **dmas, IOMMUMemoryRegion **dma_mr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, TYPE_RC4030);
    qdev_init_nofail(dev);

    *dmas = rc4030_allocate_dmas(dev, 4);
    *dma_mr = &RC4030(dev)->dma_mr;
    return dev;
}
