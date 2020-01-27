/*
 * QEMU Sun4m iommu emulation
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
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
#include "hw/sparc/sun4m_iommu.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "trace.h"

/*
 * I/O MMU used by Sun4m systems
 *
 * Chipset docs:
 * "Sun-4M System Architecture (revision 2.0) by Chuck Narad", 950-1373-01,
 * http://mediacast.sun.com/users/Barton808/media/Sun4M_SystemArchitecture_edited2.pdf
 */

#define IOMMU_CTRL          (0x0000 >> 2)
#define IOMMU_CTRL_IMPL     0xf0000000 /* Implementation */
#define IOMMU_CTRL_VERS     0x0f000000 /* Version */
#define IOMMU_CTRL_RNGE     0x0000001c /* Mapping RANGE */
#define IOMMU_RNGE_16MB     0x00000000 /* 0xff000000 -> 0xffffffff */
#define IOMMU_RNGE_32MB     0x00000004 /* 0xfe000000 -> 0xffffffff */
#define IOMMU_RNGE_64MB     0x00000008 /* 0xfc000000 -> 0xffffffff */
#define IOMMU_RNGE_128MB    0x0000000c /* 0xf8000000 -> 0xffffffff */
#define IOMMU_RNGE_256MB    0x00000010 /* 0xf0000000 -> 0xffffffff */
#define IOMMU_RNGE_512MB    0x00000014 /* 0xe0000000 -> 0xffffffff */
#define IOMMU_RNGE_1GB      0x00000018 /* 0xc0000000 -> 0xffffffff */
#define IOMMU_RNGE_2GB      0x0000001c /* 0x80000000 -> 0xffffffff */
#define IOMMU_CTRL_ENAB     0x00000001 /* IOMMU Enable */
#define IOMMU_CTRL_MASK     0x0000001d

#define IOMMU_BASE          (0x0004 >> 2)
#define IOMMU_BASE_MASK     0x07fffc00

#define IOMMU_TLBFLUSH      (0x0014 >> 2)
#define IOMMU_TLBFLUSH_MASK 0xffffffff

#define IOMMU_PGFLUSH       (0x0018 >> 2)
#define IOMMU_PGFLUSH_MASK  0xffffffff

#define IOMMU_AFSR          (0x1000 >> 2)
#define IOMMU_AFSR_ERR      0x80000000 /* LE, TO, or BE asserted */
#define IOMMU_AFSR_LE       0x40000000 /* SBUS reports error after
                                          transaction */
#define IOMMU_AFSR_TO       0x20000000 /* Write access took more than
                                          12.8 us. */
#define IOMMU_AFSR_BE       0x10000000 /* Write access received error
                                          acknowledge */
#define IOMMU_AFSR_SIZE     0x0e000000 /* Size of transaction causing error */
#define IOMMU_AFSR_S        0x01000000 /* Sparc was in supervisor mode */
#define IOMMU_AFSR_RESV     0x00800000 /* Reserved, forced to 0x8 by
                                          hardware */
#define IOMMU_AFSR_ME       0x00080000 /* Multiple errors occurred */
#define IOMMU_AFSR_RD       0x00040000 /* A read operation was in progress */
#define IOMMU_AFSR_FAV      0x00020000 /* IOMMU afar has valid contents */
#define IOMMU_AFSR_MASK     0xff0fffff

#define IOMMU_AFAR          (0x1004 >> 2)

#define IOMMU_AER           (0x1008 >> 2) /* Arbiter Enable Register */
#define IOMMU_AER_EN_P0_ARB 0x00000001    /* MBus master 0x8 (Always 1) */
#define IOMMU_AER_EN_P1_ARB 0x00000002    /* MBus master 0x9 */
#define IOMMU_AER_EN_P2_ARB 0x00000004    /* MBus master 0xa */
#define IOMMU_AER_EN_P3_ARB 0x00000008    /* MBus master 0xb */
#define IOMMU_AER_EN_0      0x00010000    /* SBus slot 0 */
#define IOMMU_AER_EN_1      0x00020000    /* SBus slot 1 */
#define IOMMU_AER_EN_2      0x00040000    /* SBus slot 2 */
#define IOMMU_AER_EN_3      0x00080000    /* SBus slot 3 */
#define IOMMU_AER_EN_F      0x00100000    /* SBus on-board */
#define IOMMU_AER_SBW       0x80000000    /* S-to-M asynchronous writes */
#define IOMMU_AER_MASK      0x801f000f

#define IOMMU_SBCFG0        (0x1010 >> 2) /* SBUS configration per-slot */
#define IOMMU_SBCFG1        (0x1014 >> 2) /* SBUS configration per-slot */
#define IOMMU_SBCFG2        (0x1018 >> 2) /* SBUS configration per-slot */
#define IOMMU_SBCFG3        (0x101c >> 2) /* SBUS configration per-slot */
#define IOMMU_SBCFG_SAB30   0x00010000 /* Phys-address bit 30 when
                                          bypass enabled */
#define IOMMU_SBCFG_BA16    0x00000004 /* Slave supports 16 byte bursts */
#define IOMMU_SBCFG_BA8     0x00000002 /* Slave supports 8 byte bursts */
#define IOMMU_SBCFG_BYPASS  0x00000001 /* Bypass IOMMU, treat all addresses
                                          produced by this device as pure
                                          physical. */
#define IOMMU_SBCFG_MASK    0x00010003

#define IOMMU_ARBEN         (0x2000 >> 2) /* SBUS arbitration enable */
#define IOMMU_ARBEN_MASK    0x001f0000
#define IOMMU_MID           0x00000008

#define IOMMU_MASK_ID       (0x3018 >> 2) /* Mask ID */
#define IOMMU_MASK_ID_MASK  0x00ffffff

#define IOMMU_MSII_MASK     0x26000000 /* microSPARC II mask number */
#define IOMMU_TS_MASK       0x23000000 /* turboSPARC mask number */

/* The format of an iopte in the page tables */
#define IOPTE_PAGE          0xffffff00 /* Physical page number (PA[35:12]) */
#define IOPTE_CACHE         0x00000080 /* Cached (in vme IOCACHE or
                                          Viking/MXCC) */
#define IOPTE_WRITE         0x00000004 /* Writable */
#define IOPTE_VALID         0x00000002 /* IOPTE is valid */
#define IOPTE_WAZ           0x00000001 /* Write as zeros */

#define IOMMU_PAGE_SHIFT    12
#define IOMMU_PAGE_SIZE     (1 << IOMMU_PAGE_SHIFT)
#define IOMMU_PAGE_MASK     (~(IOMMU_PAGE_SIZE - 1))

static uint64_t iommu_mem_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    IOMMUState *s = opaque;
    hwaddr saddr;
    uint32_t ret;

    saddr = addr >> 2;
    switch (saddr) {
    default:
        ret = s->regs[saddr];
        break;
    case IOMMU_AFAR:
    case IOMMU_AFSR:
        ret = s->regs[saddr];
        qemu_irq_lower(s->irq);
        break;
    }
    trace_sun4m_iommu_mem_readl(saddr, ret);
    return ret;
}

static void iommu_mem_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    IOMMUState *s = opaque;
    hwaddr saddr;

    saddr = addr >> 2;
    trace_sun4m_iommu_mem_writel(saddr, val);
    switch (saddr) {
    case IOMMU_CTRL:
        switch (val & IOMMU_CTRL_RNGE) {
        case IOMMU_RNGE_16MB:
            s->iostart = 0xffffffffff000000ULL;
            break;
        case IOMMU_RNGE_32MB:
            s->iostart = 0xfffffffffe000000ULL;
            break;
        case IOMMU_RNGE_64MB:
            s->iostart = 0xfffffffffc000000ULL;
            break;
        case IOMMU_RNGE_128MB:
            s->iostart = 0xfffffffff8000000ULL;
            break;
        case IOMMU_RNGE_256MB:
            s->iostart = 0xfffffffff0000000ULL;
            break;
        case IOMMU_RNGE_512MB:
            s->iostart = 0xffffffffe0000000ULL;
            break;
        case IOMMU_RNGE_1GB:
            s->iostart = 0xffffffffc0000000ULL;
            break;
        default:
        case IOMMU_RNGE_2GB:
            s->iostart = 0xffffffff80000000ULL;
            break;
        }
        trace_sun4m_iommu_mem_writel_ctrl(s->iostart);
        s->regs[saddr] = ((val & IOMMU_CTRL_MASK) | s->version);
        break;
    case IOMMU_BASE:
        s->regs[saddr] = val & IOMMU_BASE_MASK;
        break;
    case IOMMU_TLBFLUSH:
        trace_sun4m_iommu_mem_writel_tlbflush(val);
        s->regs[saddr] = val & IOMMU_TLBFLUSH_MASK;
        break;
    case IOMMU_PGFLUSH:
        trace_sun4m_iommu_mem_writel_pgflush(val);
        s->regs[saddr] = val & IOMMU_PGFLUSH_MASK;
        break;
    case IOMMU_AFAR:
        s->regs[saddr] = val;
        qemu_irq_lower(s->irq);
        break;
    case IOMMU_AER:
        s->regs[saddr] = (val & IOMMU_AER_MASK) | IOMMU_AER_EN_P0_ARB;
        break;
    case IOMMU_AFSR:
        s->regs[saddr] = (val & IOMMU_AFSR_MASK) | IOMMU_AFSR_RESV;
        qemu_irq_lower(s->irq);
        break;
    case IOMMU_SBCFG0:
    case IOMMU_SBCFG1:
    case IOMMU_SBCFG2:
    case IOMMU_SBCFG3:
        s->regs[saddr] = val & IOMMU_SBCFG_MASK;
        break;
    case IOMMU_ARBEN:
        /* XXX implement SBus probing: fault when reading unmapped
           addresses, fault cause and address stored to MMU/IOMMU */
        s->regs[saddr] = (val & IOMMU_ARBEN_MASK) | IOMMU_MID;
        break;
    case IOMMU_MASK_ID:
        s->regs[saddr] |= val & IOMMU_MASK_ID_MASK;
        break;
    default:
        s->regs[saddr] = val;
        break;
    }
}

static const MemoryRegionOps iommu_mem_ops = {
    .read = iommu_mem_read,
    .write = iommu_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint32_t iommu_page_get_flags(IOMMUState *s, hwaddr addr)
{
    uint32_t ret;
    hwaddr iopte;
    hwaddr pa = addr;

    iopte = s->regs[IOMMU_BASE] << 4;
    addr &= ~s->iostart;
    iopte += (addr >> (IOMMU_PAGE_SHIFT - 2)) & ~3;
    ret = address_space_ldl_be(&address_space_memory, iopte,
                               MEMTXATTRS_UNSPECIFIED, NULL);
    trace_sun4m_iommu_page_get_flags(pa, iopte, ret);
    return ret;
}

static hwaddr iommu_translate_pa(hwaddr addr,
                                             uint32_t pte)
{
    hwaddr pa;

    pa = ((pte & IOPTE_PAGE) << 4) + (addr & ~IOMMU_PAGE_MASK);
    trace_sun4m_iommu_translate_pa(addr, pa, pte);
    return pa;
}

static void iommu_bad_addr(IOMMUState *s, hwaddr addr,
                           int is_write)
{
    trace_sun4m_iommu_bad_addr(addr);
    s->regs[IOMMU_AFSR] = IOMMU_AFSR_ERR | IOMMU_AFSR_LE | IOMMU_AFSR_RESV |
        IOMMU_AFSR_FAV;
    if (!is_write) {
        s->regs[IOMMU_AFSR] |= IOMMU_AFSR_RD;
    }
    s->regs[IOMMU_AFAR] = addr;
    qemu_irq_raise(s->irq);
}

/* Called from RCU critical section */
static IOMMUTLBEntry sun4m_translate_iommu(IOMMUMemoryRegion *iommu,
                                           hwaddr addr,
                                           IOMMUAccessFlags flags,
                                           int iommu_idx)
{
    IOMMUState *is = container_of(iommu, IOMMUState, iommu);
    hwaddr page, pa;
    int is_write = (flags & IOMMU_WO) ? 1 : 0;
    uint32_t pte;
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = 0,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    page = addr & IOMMU_PAGE_MASK;
    pte = iommu_page_get_flags(is, page);
    if (!(pte & IOPTE_VALID)) {
        iommu_bad_addr(is, page, is_write);
        return ret;
    }

    pa = iommu_translate_pa(addr, pte);
    if (is_write && !(pte & IOPTE_WRITE)) {
        iommu_bad_addr(is, page, is_write);
        return ret;
    }

    if (pte & IOPTE_WRITE) {
        ret.perm = IOMMU_RW;
    } else {
        ret.perm = IOMMU_RO;
    }

    ret.iova = page;
    ret.translated_addr = pa;
    ret.addr_mask = ~IOMMU_PAGE_MASK;

    return ret;
}

static const VMStateDescription vmstate_iommu = {
    .name = "iommu",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IOMMUState, IOMMU_NREGS),
        VMSTATE_UINT64(iostart, IOMMUState),
        VMSTATE_END_OF_LIST()
    }
};

static void iommu_reset(DeviceState *d)
{
    IOMMUState *s = SUN4M_IOMMU(d);

    memset(s->regs, 0, IOMMU_NREGS * 4);
    s->iostart = 0;
    s->regs[IOMMU_CTRL] = s->version;
    s->regs[IOMMU_ARBEN] = IOMMU_MID;
    s->regs[IOMMU_AFSR] = IOMMU_AFSR_RESV;
    s->regs[IOMMU_AER] = IOMMU_AER_EN_P0_ARB | IOMMU_AER_EN_P1_ARB;
    s->regs[IOMMU_MASK_ID] = IOMMU_TS_MASK;
}

static void iommu_init(Object *obj)
{
    IOMMUState *s = SUN4M_IOMMU(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_iommu(&s->iommu, sizeof(s->iommu),
                             TYPE_SUN4M_IOMMU_MEMORY_REGION, OBJECT(dev),
                             "iommu-sun4m", UINT64_MAX);
    address_space_init(&s->iommu_as, MEMORY_REGION(&s->iommu), "iommu-as");

    sysbus_init_irq(dev, &s->irq);

    memory_region_init_io(&s->iomem, obj, &iommu_mem_ops, s, "iommu",
                          IOMMU_NREGS * sizeof(uint32_t));
    sysbus_init_mmio(dev, &s->iomem);
}

static Property iommu_properties[] = {
    DEFINE_PROP_UINT32("version", IOMMUState, version, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void iommu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = iommu_reset;
    dc->vmsd = &vmstate_iommu;
    device_class_set_props(dc, iommu_properties);
}

static const TypeInfo iommu_info = {
    .name          = TYPE_SUN4M_IOMMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IOMMUState),
    .instance_init = iommu_init,
    .class_init    = iommu_class_init,
};

static void sun4m_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = sun4m_translate_iommu;
}

static const TypeInfo sun4m_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_SUN4M_IOMMU_MEMORY_REGION,
    .class_init = sun4m_iommu_memory_region_class_init,
};

static void iommu_register_types(void)
{
    type_register_static(&iommu_info);
    type_register_static(&sun4m_iommu_memory_region_info);
}

type_init(iommu_register_types)
