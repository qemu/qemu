/*
 * QEMU SPARC iommu emulation
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
#include "vl.h"

/* debug iommu */
//#define DEBUG_IOMMU

#ifdef DEBUG_IOMMU
#define DPRINTF(fmt, args...) \
do { printf("IOMMU: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#define IOMMU_NREGS (3*4096)
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

/* The format of an iopte in the page tables */
#define IOPTE_PAGE          0x07ffff00 /* Physical page number (PA[30:12]) */
#define IOPTE_CACHE         0x00000080 /* Cached (in vme IOCACHE or Viking/MXCC) */
#define IOPTE_WRITE         0x00000004 /* Writeable */
#define IOPTE_VALID         0x00000002 /* IOPTE is valid */
#define IOPTE_WAZ           0x00000001 /* Write as zeros */

#define PAGE_SHIFT      12
#define PAGE_SIZE       (1 << PAGE_SHIFT)
#define PAGE_MASK	(PAGE_SIZE - 1)

typedef struct IOMMUState {
    uint32_t addr;
    uint32_t regs[IOMMU_NREGS];
    uint32_t iostart;
} IOMMUState;

static uint32_t iommu_mem_readw(void *opaque, target_phys_addr_t addr)
{
    IOMMUState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addr) >> 2;
    switch (saddr) {
    default:
	DPRINTF("read reg[%d] = %x\n", saddr, s->regs[saddr]);
	return s->regs[saddr];
	break;
    }
    return 0;
}

static void iommu_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    IOMMUState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addr) >> 2;
    DPRINTF("write reg[%d] = %x\n", saddr, val);
    switch (saddr) {
    case 0:
	switch (val & IOMMU_CTRL_RNGE) {
	case IOMMU_RNGE_16MB:
	    s->iostart = 0xff000000;
	    break;
	case IOMMU_RNGE_32MB:
	    s->iostart = 0xfe000000;
	    break;
	case IOMMU_RNGE_64MB:
	    s->iostart = 0xfc000000;
	    break;
	case IOMMU_RNGE_128MB:
	    s->iostart = 0xf8000000;
	    break;
	case IOMMU_RNGE_256MB:
	    s->iostart = 0xf0000000;
	    break;
	case IOMMU_RNGE_512MB:
	    s->iostart = 0xe0000000;
	    break;
	case IOMMU_RNGE_1GB:
	    s->iostart = 0xc0000000;
	    break;
	default:
	case IOMMU_RNGE_2GB:
	    s->iostart = 0x80000000;
	    break;
	}
	DPRINTF("iostart = %x\n", s->iostart);
	/* Fall through */
    default:
	s->regs[saddr] = val;
	break;
    }
}

static CPUReadMemoryFunc *iommu_mem_read[3] = {
    iommu_mem_readw,
    iommu_mem_readw,
    iommu_mem_readw,
};

static CPUWriteMemoryFunc *iommu_mem_write[3] = {
    iommu_mem_writew,
    iommu_mem_writew,
    iommu_mem_writew,
};

uint32_t iommu_translate_local(void *opaque, uint32_t addr)
{
    IOMMUState *s = opaque;
    uint32_t iopte, pa, tmppte;

    iopte = s->regs[1] << 4;
    addr &= ~s->iostart;
    iopte += (addr >> (PAGE_SHIFT - 2)) & ~3;
    cpu_physical_memory_read(iopte, (void *) &pa, 4);
    bswap32s(&pa);
    tmppte = pa;
    pa = ((pa & IOPTE_PAGE) << 4) + (addr & PAGE_MASK);
    DPRINTF("xlate dva %x => pa %x (iopte[%x] = %x)\n", addr, pa, iopte, tmppte);
    return pa;
}

static void iommu_save(QEMUFile *f, void *opaque)
{
    IOMMUState *s = opaque;
    int i;
    
    qemu_put_be32s(f, &s->addr);
    for (i = 0; i < IOMMU_NREGS; i++)
	qemu_put_be32s(f, &s->regs[i]);
    qemu_put_be32s(f, &s->iostart);
}

static int iommu_load(QEMUFile *f, void *opaque, int version_id)
{
    IOMMUState *s = opaque;
    int i;
    
    if (version_id != 1)
        return -EINVAL;

    qemu_get_be32s(f, &s->addr);
    for (i = 0; i < IOMMU_NREGS; i++)
	qemu_put_be32s(f, &s->regs[i]);
    qemu_get_be32s(f, &s->iostart);

    return 0;
}

static void iommu_reset(void *opaque)
{
    IOMMUState *s = opaque;

    memset(s->regs, 0, IOMMU_NREGS * 4);
    s->iostart = 0;
}

void *iommu_init(uint32_t addr)
{
    IOMMUState *s;
    int iommu_io_memory;

    s = qemu_mallocz(sizeof(IOMMUState));
    if (!s)
        return NULL;

    s->addr = addr;

    iommu_io_memory = cpu_register_io_memory(0, iommu_mem_read, iommu_mem_write, s);
    cpu_register_physical_memory(addr, IOMMU_NREGS * 4, iommu_io_memory);
    
    register_savevm("iommu", addr, 1, iommu_save, iommu_load, s);
    qemu_register_reset(iommu_reset, s);
    return s;
}

