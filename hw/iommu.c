/*
 * QEMU SPARC iommu emulation
 *
 * Copyright (c) 2003 Fabrice Bellard
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

/* The IOMMU registers occupy three pages in IO space. */
struct iommu_regs {
	/* First page */
	volatile unsigned long control;    /* IOMMU control */
	volatile unsigned long base;       /* Physical base of iopte page table */
	volatile unsigned long _unused1[3];
	volatile unsigned long tlbflush;   /* write only */
	volatile unsigned long pageflush;  /* write only */
	volatile unsigned long _unused2[1017];
	/* Second page */
	volatile unsigned long afsr;       /* Async-fault status register */
	volatile unsigned long afar;       /* Async-fault physical address */
	volatile unsigned long _unused3[2];
	volatile unsigned long sbuscfg0;   /* SBUS configuration registers, per-slot */
	volatile unsigned long sbuscfg1;
	volatile unsigned long sbuscfg2;
	volatile unsigned long sbuscfg3;
	volatile unsigned long mfsr;       /* Memory-fault status register */
	volatile unsigned long mfar;       /* Memory-fault physical address */
	volatile unsigned long _unused4[1014];
	/* Third page */
	volatile unsigned long mid;        /* IOMMU module-id */
};

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

#define IOMMU_AFSR_ERR      0x80000000 /* LE, TO, or BE asserted */
#define IOMMU_AFSR_LE       0x40000000 /* SBUS reports error after transaction */
#define IOMMU_AFSR_TO       0x20000000 /* Write access took more than 12.8 us. */
#define IOMMU_AFSR_BE       0x10000000 /* Write access received error acknowledge */
#define IOMMU_AFSR_SIZE     0x0e000000 /* Size of transaction causing error */
#define IOMMU_AFSR_S        0x01000000 /* Sparc was in supervisor mode */
#define IOMMU_AFSR_RESV     0x00f00000 /* Reserver, forced to 0x8 by hardware */
#define IOMMU_AFSR_ME       0x00080000 /* Multiple errors occurred */
#define IOMMU_AFSR_RD       0x00040000 /* A read operation was in progress */
#define IOMMU_AFSR_FAV      0x00020000 /* IOMMU afar has valid contents */

#define IOMMU_SBCFG_SAB30   0x00010000 /* Phys-address bit 30 when bypass enabled */
#define IOMMU_SBCFG_BA16    0x00000004 /* Slave supports 16 byte bursts */
#define IOMMU_SBCFG_BA8     0x00000002 /* Slave supports 8 byte bursts */
#define IOMMU_SBCFG_BYPASS  0x00000001 /* Bypass IOMMU, treat all addresses
					  produced by this device as pure
					  physical. */

#define IOMMU_MFSR_ERR      0x80000000 /* One or more of PERR1 or PERR0 */
#define IOMMU_MFSR_S        0x01000000 /* Sparc was in supervisor mode */
#define IOMMU_MFSR_CPU      0x00800000 /* CPU transaction caused parity error */
#define IOMMU_MFSR_ME       0x00080000 /* Multiple parity errors occurred */
#define IOMMU_MFSR_PERR     0x00006000 /* high bit indicates parity error occurred
					  on the even word of the access, low bit
					  indicated odd word caused the parity error */
#define IOMMU_MFSR_BM       0x00001000 /* Error occurred while in boot mode */
#define IOMMU_MFSR_C        0x00000800 /* Address causing error was marked cacheable */
#define IOMMU_MFSR_RTYP     0x000000f0 /* Memory request transaction type */

#define IOMMU_MID_SBAE      0x001f0000 /* SBus arbitration enable */
#define IOMMU_MID_SE        0x00100000 /* Enables SCSI/ETHERNET arbitration */
#define IOMMU_MID_SB3       0x00080000 /* Enable SBUS device 3 arbitration */
#define IOMMU_MID_SB2       0x00040000 /* Enable SBUS device 2 arbitration */
#define IOMMU_MID_SB1       0x00020000 /* Enable SBUS device 1 arbitration */
#define IOMMU_MID_SB0       0x00010000 /* Enable SBUS device 0 arbitration */
#define IOMMU_MID_MID       0x0000000f /* Module-id, hardcoded to 0x8 */

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
    uint32_t regs[sizeof(struct iommu_regs)];
    uint32_t iostart;
} IOMMUState;

static IOMMUState *ps;

static uint32_t iommu_mem_readw(void *opaque, target_phys_addr_t addr)
{
    IOMMUState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addr) >> 2;
    switch (saddr) {
    default:
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

uint32_t iommu_translate(uint32_t addr)
{
    uint32_t *iopte = (void *)(ps->regs[1] << 4), pa;

    iopte += ((addr - ps->iostart) >> PAGE_SHIFT);
    cpu_physical_memory_rw((uint32_t)iopte, (void *) &pa, 4, 0);
    bswap32s(&pa);
    pa = (pa & IOPTE_PAGE) << 4;		/* Loose higher bits of 36 */
    return pa + (addr & PAGE_MASK);
}

void iommu_init(uint32_t addr)
{
    IOMMUState *s;
    int iommu_io_memory;

    s = qemu_mallocz(sizeof(IOMMUState));
    if (!s)
        return;

    s->addr = addr;

    iommu_io_memory = cpu_register_io_memory(0, iommu_mem_read, iommu_mem_write, s);
    cpu_register_physical_memory(addr, sizeof(struct iommu_regs),
                                 iommu_io_memory);
    
    ps = s;
}

