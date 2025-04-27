/*
 * QEMU sun4u IOMMU emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2012,2013 Artyom Tarasenko
 * Copyright (c) 2017 Mark Cave-Ayland
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
#include "hw/sysbus.h"
#include "hw/sparc/sun4u_iommu.h"
#include "system/address-spaces.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"


#define IOMMU_PAGE_SIZE_8K      (1ULL << 13)
#define IOMMU_PAGE_MASK_8K      (~(IOMMU_PAGE_SIZE_8K - 1))
#define IOMMU_PAGE_SIZE_64K     (1ULL << 16)
#define IOMMU_PAGE_MASK_64K     (~(IOMMU_PAGE_SIZE_64K - 1))

#define IOMMU_CTRL              0x0
#define IOMMU_CTRL_TBW_SIZE     (1ULL << 2)
#define IOMMU_CTRL_MMU_EN       (1ULL)

#define IOMMU_CTRL_TSB_SHIFT    16

#define IOMMU_BASE              0x8
#define IOMMU_FLUSH             0x10

#define IOMMU_TTE_DATA_V        (1ULL << 63)
#define IOMMU_TTE_DATA_SIZE     (1ULL << 61)
#define IOMMU_TTE_DATA_W        (1ULL << 1)

#define IOMMU_TTE_PHYS_MASK_8K  0x1ffffffe000ULL
#define IOMMU_TTE_PHYS_MASK_64K 0x1ffffff8000ULL

#define IOMMU_TSB_8K_OFFSET_MASK_8M    0x00000000007fe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_16M   0x0000000000ffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_32M   0x0000000001ffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_64M   0x0000000003ffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_128M  0x0000000007ffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_256M  0x000000000fffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_512M  0x000000001fffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_1G    0x000000003fffe000ULL

#define IOMMU_TSB_64K_OFFSET_MASK_64M  0x0000000003ff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_128M 0x0000000007ff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_256M 0x000000000fff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_512M 0x000000001fff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_1G   0x000000003fff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_2G   0x000000007fff0000ULL


/* Called from RCU critical section */
static IOMMUTLBEntry sun4u_translate_iommu(IOMMUMemoryRegion *iommu,
                                           hwaddr addr,
                                           IOMMUAccessFlags flag, int iommu_idx)
{
    IOMMUState *is = container_of(iommu, IOMMUState, iommu);
    hwaddr baseaddr, offset;
    uint64_t tte;
    uint32_t tsbsize;
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = 0,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    if (!(is->regs[IOMMU_CTRL >> 3] & IOMMU_CTRL_MMU_EN)) {
        /* IOMMU disabled, passthrough using standard 8K page */
        ret.iova = addr & IOMMU_PAGE_MASK_8K;
        ret.translated_addr = addr;
        ret.addr_mask = IOMMU_PAGE_MASK_8K;
        ret.perm = IOMMU_RW;

        return ret;
    }

    baseaddr = is->regs[IOMMU_BASE >> 3];
    tsbsize = (is->regs[IOMMU_CTRL >> 3] >> IOMMU_CTRL_TSB_SHIFT) & 0x7;

    if (is->regs[IOMMU_CTRL >> 3] & IOMMU_CTRL_TBW_SIZE) {
        /* 64K */
        switch (tsbsize) {
        case 0:
            offset = (addr & IOMMU_TSB_64K_OFFSET_MASK_64M) >> 13;
            break;
        case 1:
            offset = (addr & IOMMU_TSB_64K_OFFSET_MASK_128M) >> 13;
            break;
        case 2:
            offset = (addr & IOMMU_TSB_64K_OFFSET_MASK_256M) >> 13;
            break;
        case 3:
            offset = (addr & IOMMU_TSB_64K_OFFSET_MASK_512M) >> 13;
            break;
        case 4:
            offset = (addr & IOMMU_TSB_64K_OFFSET_MASK_1G) >> 13;
            break;
        case 5:
            offset = (addr & IOMMU_TSB_64K_OFFSET_MASK_2G) >> 13;
            break;
        default:
            /* Not implemented, error */
            return ret;
        }
    } else {
        /* 8K */
        switch (tsbsize) {
        case 0:
            offset = (addr & IOMMU_TSB_8K_OFFSET_MASK_8M) >> 10;
            break;
        case 1:
            offset = (addr & IOMMU_TSB_8K_OFFSET_MASK_16M) >> 10;
            break;
        case 2:
            offset = (addr & IOMMU_TSB_8K_OFFSET_MASK_32M) >> 10;
            break;
        case 3:
            offset = (addr & IOMMU_TSB_8K_OFFSET_MASK_64M) >> 10;
            break;
        case 4:
            offset = (addr & IOMMU_TSB_8K_OFFSET_MASK_128M) >> 10;
            break;
        case 5:
            offset = (addr & IOMMU_TSB_8K_OFFSET_MASK_256M) >> 10;
            break;
        case 6:
            offset = (addr & IOMMU_TSB_8K_OFFSET_MASK_512M) >> 10;
            break;
        case 7:
            offset = (addr & IOMMU_TSB_8K_OFFSET_MASK_1G) >> 10;
            break;
        }
    }

    tte = address_space_ldq_be(&address_space_memory, baseaddr + offset,
                               MEMTXATTRS_UNSPECIFIED, NULL);

    if (!(tte & IOMMU_TTE_DATA_V)) {
        /* Invalid mapping */
        return ret;
    }

    if (tte & IOMMU_TTE_DATA_W) {
        /* Writable */
        ret.perm = IOMMU_RW;
    } else {
        ret.perm = IOMMU_RO;
    }

    /* Extract phys */
    if (tte & IOMMU_TTE_DATA_SIZE) {
        /* 64K */
        ret.iova = addr & IOMMU_PAGE_MASK_64K;
        ret.translated_addr = tte & IOMMU_TTE_PHYS_MASK_64K;
        ret.addr_mask = (IOMMU_PAGE_SIZE_64K - 1);
    } else {
        /* 8K */
        ret.iova = addr & IOMMU_PAGE_MASK_8K;
        ret.translated_addr = tte & IOMMU_TTE_PHYS_MASK_8K;
        ret.addr_mask = (IOMMU_PAGE_SIZE_8K - 1);
    }

    trace_sun4u_iommu_translate(ret.iova, ret.translated_addr, tte);

    return ret;
}

static void iommu_mem_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    IOMMUState *is = opaque;

    trace_sun4u_iommu_mem_write(addr, val, size);

    switch (addr) {
    case IOMMU_CTRL:
        if (size == 4) {
            is->regs[IOMMU_CTRL >> 3] &= 0xffffffffULL;
            is->regs[IOMMU_CTRL >> 3] |= val << 32;
        } else {
            is->regs[IOMMU_CTRL >> 3] = val;
        }
        break;
    case IOMMU_CTRL + 0x4:
        is->regs[IOMMU_CTRL >> 3] &= 0xffffffff00000000ULL;
        is->regs[IOMMU_CTRL >> 3] |= val & 0xffffffffULL;
        break;
    case IOMMU_BASE:
        if (size == 4) {
            is->regs[IOMMU_BASE >> 3] &= 0xffffffffULL;
            is->regs[IOMMU_BASE >> 3] |= val << 32;
        } else {
            is->regs[IOMMU_BASE >> 3] = val;
        }
        break;
    case IOMMU_BASE + 0x4:
        is->regs[IOMMU_BASE >> 3] &= 0xffffffff00000000ULL;
        is->regs[IOMMU_BASE >> 3] |= val & 0xffffffffULL;
        break;
    case IOMMU_FLUSH:
    case IOMMU_FLUSH + 0x4:
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                  "sun4u-iommu: Unimplemented register write "
                  "reg 0x%" HWADDR_PRIx " size 0x%x value 0x%" PRIx64 "\n",
                  addr, size, val);
        break;
    }
}

static uint64_t iommu_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    IOMMUState *is = opaque;
    uint64_t val;

    switch (addr) {
    case IOMMU_CTRL:
        if (size == 4) {
            val = is->regs[IOMMU_CTRL >> 3] >> 32;
        } else {
            val = is->regs[IOMMU_CTRL >> 3];
        }
        break;
    case IOMMU_CTRL + 0x4:
        val = is->regs[IOMMU_CTRL >> 3] & 0xffffffffULL;
        break;
    case IOMMU_BASE:
        if (size == 4) {
            val = is->regs[IOMMU_BASE >> 3] >> 32;
        } else {
            val = is->regs[IOMMU_BASE >> 3];
        }
        break;
    case IOMMU_BASE + 0x4:
        val = is->regs[IOMMU_BASE >> 3] & 0xffffffffULL;
        break;
    case IOMMU_FLUSH:
    case IOMMU_FLUSH + 0x4:
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "sun4u-iommu: Unimplemented register read "
                      "reg 0x%" HWADDR_PRIx " size 0x%x\n",
                      addr, size);
        val = 0;
        break;
    }

    trace_sun4u_iommu_mem_read(addr, val, size);

    return val;
}

static const MemoryRegionOps iommu_mem_ops = {
    .read = iommu_mem_read,
    .write = iommu_mem_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void iommu_reset(DeviceState *d)
{
    IOMMUState *s = SUN4U_IOMMU(d);

    memset(s->regs, 0, IOMMU_NREGS * sizeof(uint64_t));
}

static void iommu_init(Object *obj)
{
    IOMMUState *s = SUN4U_IOMMU(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_iommu(&s->iommu, sizeof(s->iommu),
                             TYPE_SUN4U_IOMMU_MEMORY_REGION, OBJECT(s),
                             "iommu-sun4u", UINT64_MAX);
    address_space_init(&s->iommu_as, MEMORY_REGION(&s->iommu), "iommu-as");

    memory_region_init_io(&s->iomem, obj, &iommu_mem_ops, s, "iommu",
                          IOMMU_NREGS * sizeof(uint64_t));
    sysbus_init_mmio(sbd, &s->iomem);
}

static void iommu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, iommu_reset);
}

static const TypeInfo iommu_info = {
    .name          = TYPE_SUN4U_IOMMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IOMMUState),
    .instance_init = iommu_init,
    .class_init    = iommu_class_init,
};

static void sun4u_iommu_memory_region_class_init(ObjectClass *klass,
                                                 const void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = sun4u_translate_iommu;
}

static const TypeInfo sun4u_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_SUN4U_IOMMU_MEMORY_REGION,
    .class_init = sun4u_iommu_memory_region_class_init,
};

static void iommu_register_types(void)
{
    type_register_static(&iommu_info);
    type_register_static(&sun4u_iommu_memory_region_info);
}

type_init(iommu_register_types)
