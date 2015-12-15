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
#include "hw/hw.h"
#include "hw/mips/mips.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "trace.h"

/********************************************************/
/* rc4030 emulation                                     */

#define MAX_TL_ENTRIES 512

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

typedef struct rc4030State
{
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

    /* biggest translation table */
    MemoryRegion dma_tt;
    /* translation table memory region alias, added to system RAM */
    MemoryRegion dma_tt_alias;
    /* whole DMA memory region, root of DMA address space */
    MemoryRegion dma_mr;
    /* translation table entry aliases, added to DMA memory region */
    MemoryRegion dma_mrs[MAX_TL_ENTRIES];
    AddressSpace dma_as;

    MemoryRegion iomem_chipset;
    MemoryRegion iomem_jazzio;
} rc4030State;

static void set_next_tick(rc4030State *s)
{
    qemu_irq_lower(s->timer_irq);
    uint32_t tm_hz;

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
        if (s->cache_bmask == (uint32_t)-1)
            s->cache_bmask = 0;
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

static void rc4030_dma_as_update_one(rc4030State *s, int index, uint32_t frame)
{
    if (index < MAX_TL_ENTRIES) {
        memory_region_set_enabled(&s->dma_mrs[index], false);
    }

    if (!frame) {
        return;
    }

    if (index >= MAX_TL_ENTRIES) {
        qemu_log_mask(LOG_UNIMP,
                      "rc4030: trying to use too high "
                      "translation table entry %d (max allowed=%d)",
                      index, MAX_TL_ENTRIES);
        return;
    }
    memory_region_set_alias_offset(&s->dma_mrs[index], frame);
    memory_region_set_enabled(&s->dma_mrs[index], true);
}

static void rc4030_dma_tt_write(void *opaque, hwaddr addr, uint64_t data,
                                unsigned int size)
{
    rc4030State *s = opaque;

    /* write memory */
    memcpy(memory_region_get_ram_ptr(&s->dma_tt) + addr, &data, size);

    /* update dma address space (only if frame field has been written) */
    if (addr % sizeof(dma_pagetable_entry) == 0) {
        int index = addr / sizeof(dma_pagetable_entry);
        memory_region_transaction_begin();
        rc4030_dma_as_update_one(s, index, (uint32_t)data);
        memory_region_transaction_commit();
    }
}

static const MemoryRegionOps rc4030_dma_tt_ops = {
    .write = rc4030_dma_tt_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void rc4030_dma_tt_update(rc4030State *s, uint32_t new_tl_base,
                                 uint32_t new_tl_limit)
{
    int entries, i;
    dma_pagetable_entry *dma_tl_contents;

    if (s->dma_tl_limit) {
        /* write old dma tl table to physical memory */
        memory_region_del_subregion(get_system_memory(), &s->dma_tt_alias);
        cpu_physical_memory_write(s->dma_tl_limit & 0x7fffffff,
                                  memory_region_get_ram_ptr(&s->dma_tt),
                                  memory_region_size(&s->dma_tt_alias));
    }
    object_unparent(OBJECT(&s->dma_tt_alias));

    s->dma_tl_base = new_tl_base;
    s->dma_tl_limit = new_tl_limit;
    new_tl_base &= 0x7fffffff;

    if (s->dma_tl_limit) {
        uint64_t dma_tt_size;
        if (s->dma_tl_limit <= memory_region_size(&s->dma_tt)) {
            dma_tt_size = s->dma_tl_limit;
        } else {
            dma_tt_size = memory_region_size(&s->dma_tt);
        }
        memory_region_init_alias(&s->dma_tt_alias, OBJECT(s),
                                 "dma-table-alias",
                                 &s->dma_tt, 0, dma_tt_size);
        dma_tl_contents = memory_region_get_ram_ptr(&s->dma_tt);
        cpu_physical_memory_read(new_tl_base, dma_tl_contents, dma_tt_size);

        memory_region_transaction_begin();
        entries = dma_tt_size / sizeof(dma_pagetable_entry);
        for (i = 0; i < entries; i++) {
            rc4030_dma_as_update_one(s, i, dma_tl_contents[i].frame);
        }
        memory_region_add_subregion(get_system_memory(), new_tl_base,
                                    &s->dma_tt_alias);
        memory_region_transaction_commit();
    } else {
        memory_region_init(&s->dma_tt_alias, OBJECT(s),
                           "dma-table-alias", 0);
    }
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
        rc4030_dma_tt_update(s, val, s->dma_tl_limit);
        break;
    /* DMA transl. table limit */
    case 0x0020:
        rc4030_dma_tt_update(s, s->dma_tl_base, val);
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
        s->itr = val;
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

    if (pending != 0)
        qemu_irq_raise(s->jazz_bus_irq);
    else
        qemu_irq_lower(s->jazz_bus_irq);
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

static void rc4030_reset(DeviceState *dev)
{
    rc4030State *s = RC4030(dev);
    int i;

    s->config = 0x410; /* some boards seem to accept 0x104 too */
    s->revision = 1;
    s->invalid_address_register = 0;

    memset(s->dma_regs, 0, sizeof(s->dma_regs));
    rc4030_dma_tt_update(s, 0, 0);

    s->remote_failed_address = s->memory_failed_address = 0;
    s->cache_maint = 0;
    s->cache_ptag = s->cache_ltag = 0;
    s->cache_bmask = 0;

    s->memory_refresh_rate = 0x18186;
    s->nvram_protect = 7;
    for (i = 0; i < 15; i++)
        s->rem_speed[i] = 7;
    s->imr_jazz = 0x10; /* XXX: required by firmware, but why? */
    s->isr_jazz = 0;

    s->itr = 0;

    qemu_irq_lower(s->timer_irq);
    qemu_irq_lower(s->jazz_bus_irq);
}

static int rc4030_load(QEMUFile *f, void *opaque, int version_id)
{
    rc4030State* s = opaque;
    int i, j;

    if (version_id != 2)
        return -EINVAL;

    s->config = qemu_get_be32(f);
    s->invalid_address_register = qemu_get_be32(f);
    for (i = 0; i < 8; i++)
        for (j = 0; j < 4; j++)
            s->dma_regs[i][j] = qemu_get_be32(f);
    s->dma_tl_base = qemu_get_be32(f);
    s->dma_tl_limit = qemu_get_be32(f);
    s->cache_maint = qemu_get_be32(f);
    s->remote_failed_address = qemu_get_be32(f);
    s->memory_failed_address = qemu_get_be32(f);
    s->cache_ptag = qemu_get_be32(f);
    s->cache_ltag = qemu_get_be32(f);
    s->cache_bmask = qemu_get_be32(f);
    s->memory_refresh_rate = qemu_get_be32(f);
    s->nvram_protect = qemu_get_be32(f);
    for (i = 0; i < 15; i++)
        s->rem_speed[i] = qemu_get_be32(f);
    s->imr_jazz = qemu_get_be32(f);
    s->isr_jazz = qemu_get_be32(f);
    s->itr = qemu_get_be32(f);

    set_next_tick(s);
    update_jazz_irq(s);

    return 0;
}

static void rc4030_save(QEMUFile *f, void *opaque)
{
    rc4030State* s = opaque;
    int i, j;

    qemu_put_be32(f, s->config);
    qemu_put_be32(f, s->invalid_address_register);
    for (i = 0; i < 8; i++)
        for (j = 0; j < 4; j++)
            qemu_put_be32(f, s->dma_regs[i][j]);
    qemu_put_be32(f, s->dma_tl_base);
    qemu_put_be32(f, s->dma_tl_limit);
    qemu_put_be32(f, s->cache_maint);
    qemu_put_be32(f, s->remote_failed_address);
    qemu_put_be32(f, s->memory_failed_address);
    qemu_put_be32(f, s->cache_ptag);
    qemu_put_be32(f, s->cache_ltag);
    qemu_put_be32(f, s->cache_bmask);
    qemu_put_be32(f, s->memory_refresh_rate);
    qemu_put_be32(f, s->nvram_protect);
    for (i = 0; i < 15; i++)
        qemu_put_be32(f, s->rem_speed[i]);
    qemu_put_be32(f, s->imr_jazz);
    qemu_put_be32(f, s->isr_jazz);
    qemu_put_be32(f, s->itr);
}

static void rc4030_do_dma(void *opaque, int n, uint8_t *buf, int len, int is_write)
{
    rc4030State *s = opaque;
    hwaddr dma_addr;
    int dev_to_mem;

    s->dma_regs[n][DMA_REG_ENABLE] &= ~(DMA_FLAG_TC_INTR | DMA_FLAG_MEM_INTR | DMA_FLAG_ADDR_INTR);

    /* Check DMA channel consistency */
    dev_to_mem = (s->dma_regs[n][DMA_REG_ENABLE] & DMA_FLAG_MEM_TO_DEV) ? 0 : 1;
    if (!(s->dma_regs[n][DMA_REG_ENABLE] & DMA_FLAG_ENABLE) ||
        (is_write != dev_to_mem)) {
        s->dma_regs[n][DMA_REG_ENABLE] |= DMA_FLAG_MEM_INTR;
        s->nmi_interrupt |= 1 << n;
        return;
    }

    /* Get start address and len */
    if (len > s->dma_regs[n][DMA_REG_COUNT])
        len = s->dma_regs[n][DMA_REG_COUNT];
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
    rc4030_do_dma(s->opaque, s->n, buf, len, 0);
}

void rc4030_dma_write(void *dma, uint8_t *buf, int len)
{
    rc4030_dma s = dma;
    rc4030_do_dma(s->opaque, s->n, buf, len, 1);
}

static rc4030_dma *rc4030_allocate_dmas(void *opaque, int n)
{
    rc4030_dma *s;
    struct rc4030DMAState *p;
    int i;

    s = (rc4030_dma *)g_malloc0(sizeof(rc4030_dma) * n);
    p = (struct rc4030DMAState *)g_malloc0(sizeof(struct rc4030DMAState) * n);
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

    register_savevm(NULL, "rc4030", 0, 2, rc4030_save, rc4030_load, s);

    sysbus_init_mmio(sysbus, &s->iomem_chipset);
    sysbus_init_mmio(sysbus, &s->iomem_jazzio);
}

static void rc4030_realize(DeviceState *dev, Error **errp)
{
    rc4030State *s = RC4030(dev);
    Object *o = OBJECT(dev);
    int i;

    s->periodic_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     rc4030_periodic_timer, s);

    memory_region_init_io(&s->iomem_chipset, NULL, &rc4030_ops, s,
                          "rc4030.chipset", 0x300);
    memory_region_init_io(&s->iomem_jazzio, NULL, &jazzio_ops, s,
                          "rc4030.jazzio", 0x00001000);

    memory_region_init_rom_device(&s->dma_tt, o,
                                  &rc4030_dma_tt_ops, s, "dma-table",
                                  MAX_TL_ENTRIES * sizeof(dma_pagetable_entry),
                                  NULL);
    memory_region_init(&s->dma_tt_alias, o, "dma-table-alias", 0);
    memory_region_init(&s->dma_mr, o, "dma", INT32_MAX);
    for (i = 0; i < MAX_TL_ENTRIES; ++i) {
        memory_region_init_alias(&s->dma_mrs[i], o, "dma-alias",
                                 get_system_memory(), 0, DMA_PAGESIZE);
        memory_region_set_enabled(&s->dma_mrs[i], false);
        memory_region_add_subregion(&s->dma_mr, i * DMA_PAGESIZE,
                                    &s->dma_mrs[i]);
    }
    address_space_init(&s->dma_as, &s->dma_mr, "rc4030-dma");
}

static void rc4030_unrealize(DeviceState *dev, Error **errp)
{
    rc4030State *s = RC4030(dev);
    int i;

    timer_free(s->periodic_timer);

    address_space_destroy(&s->dma_as);
    object_unparent(OBJECT(&s->dma_tt));
    object_unparent(OBJECT(&s->dma_tt_alias));
    object_unparent(OBJECT(&s->dma_mr));
    for (i = 0; i < MAX_TL_ENTRIES; ++i) {
        memory_region_del_subregion(&s->dma_mr, &s->dma_mrs[i]);
        object_unparent(OBJECT(&s->dma_mrs[i]));
    }
}

static void rc4030_class_init(ObjectClass *klass, void *class_data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rc4030_realize;
    dc->unrealize = rc4030_unrealize;
    dc->reset = rc4030_reset;
}

static const TypeInfo rc4030_info = {
    .name = TYPE_RC4030,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(rc4030State),
    .instance_init = rc4030_initfn,
    .class_init = rc4030_class_init,
};

static void rc4030_register_types(void)
{
    type_register_static(&rc4030_info);
}

type_init(rc4030_register_types)

DeviceState *rc4030_init(rc4030_dma **dmas, MemoryRegion **dma_mr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, TYPE_RC4030);
    qdev_init_nofail(dev);

    *dmas = rc4030_allocate_dmas(dev, 4);
    *dma_mr = &RC4030(dev)->dma_mr;
    return dev;
}
