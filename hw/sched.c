/*
 * QEMU interrupt controller emulation
 * 
 * Copyright (c) 2003-2004 Fabrice Bellard
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
//#define DEBUG_IRQ_COUNT

/* These registers are used for sending/receiving irqs from/to
 * different cpu's.
 */
struct sun4m_intreg_percpu {
	unsigned int tbt;        /* Intrs pending for this cpu, by PIL. */
	/* These next two registers are WRITE-ONLY and are only
	 * "on bit" sensitive, "off bits" written have NO affect.
	 */
	unsigned int clear;  /* Clear this cpus irqs here. */
	unsigned int set;    /* Set this cpus irqs here. */
};
/*
 * djhr
 * Actually the clear and set fields in this struct are misleading..
 * according to the SLAVIO manual (and the same applies for the SEC)
 * the clear field clears bits in the mask which will ENABLE that IRQ
 * the set field sets bits in the mask to DISABLE the IRQ.
 *
 * Also the undirected_xx address in the SLAVIO is defined as
 * RESERVED and write only..
 *
 * DAVEM_NOTE: The SLAVIO only specifies behavior on uniprocessor
 *             sun4m machines, for MP the layout makes more sense.
 */
struct sun4m_intreg_master {
	unsigned int tbt;        /* IRQ's that are pending, see sun4m masks. */
	unsigned int irqs;       /* Master IRQ bits. */

	/* Again, like the above, two these registers are WRITE-ONLY. */
	unsigned int clear;      /* Clear master IRQ's by setting bits here. */
	unsigned int set;        /* Set master IRQ's by setting bits here. */

	/* This register is both READ and WRITE. */
	unsigned int undirected_target;  /* Which cpu gets undirected irqs. */
};

#define SUN4M_INT_ENABLE        0x80000000
#define SUN4M_INT_E14           0x00000080
#define SUN4M_INT_E10           0x00080000

#define SUN4M_HARD_INT(x)       (0x000000001 << (x))
#define SUN4M_SOFT_INT(x)       (0x000010000 << (x))

#define SUN4M_INT_MASKALL       0x80000000        /* mask all interrupts */
#define SUN4M_INT_MODULE_ERR    0x40000000        /* module error */
#define SUN4M_INT_M2S_WRITE     0x20000000        /* write buffer error */
#define SUN4M_INT_ECC           0x10000000        /* ecc memory error */
#define SUN4M_INT_FLOPPY        0x00400000        /* floppy disk */
#define SUN4M_INT_MODULE        0x00200000        /* module interrupt */
#define SUN4M_INT_VIDEO         0x00100000        /* onboard video */
#define SUN4M_INT_REALTIME      0x00080000        /* system timer */
#define SUN4M_INT_SCSI          0x00040000        /* onboard scsi */
#define SUN4M_INT_AUDIO         0x00020000        /* audio/isdn */
#define SUN4M_INT_ETHERNET      0x00010000        /* onboard ethernet */
#define SUN4M_INT_SERIAL        0x00008000        /* serial ports */
#define SUN4M_INT_SBUSBITS      0x00003F80        /* sbus int bits */

#define SUN4M_INT_SBUS(x)       (1 << (x+7))
#define SUN4M_INT_VME(x)        (1 << (x))

typedef struct SCHEDState {
    uint32_t addr, addrg;
    uint32_t intreg_pending;
    uint32_t intreg_enabled;
    uint32_t intregm_pending;
    uint32_t intregm_enabled;
} SCHEDState;

static SCHEDState *ps;

#ifdef DEBUG_IRQ_COUNT
static uint64_t irq_count[32];
#endif

static uint32_t intreg_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SCHEDState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addr) >> 2;
    switch (saddr) {
    case 0:
	return s->intreg_pending;
	break;
    default:
	break;
    }
    return 0;
}

static void intreg_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SCHEDState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addr) >> 2;
    switch (saddr) {
    case 0:
	s->intreg_pending = val;
	break;
    case 1: // clear
	s->intreg_enabled &= ~val;
	break;
    case 2: // set
	s->intreg_enabled |= val;
	break;
    default:
	break;
    }
}

static CPUReadMemoryFunc *intreg_mem_read[3] = {
    intreg_mem_readl,
    intreg_mem_readl,
    intreg_mem_readl,
};

static CPUWriteMemoryFunc *intreg_mem_write[3] = {
    intreg_mem_writel,
    intreg_mem_writel,
    intreg_mem_writel,
};

static uint32_t intregm_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SCHEDState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addrg) >> 2;
    switch (saddr) {
    case 0:
	return s->intregm_pending;
	break;
    case 1:
	return s->intregm_enabled;
	break;
    default:
	break;
    }
    return 0;
}

static void intregm_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SCHEDState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addrg) >> 2;
    switch (saddr) {
    case 0:
	s->intregm_pending = val;
	break;
    case 1:
	s->intregm_enabled = val;
	break;
    case 2: // clear
	s->intregm_enabled &= ~val;
	break;
    case 3: // set
	s->intregm_enabled |= val;
	break;
    default:
	break;
    }
}

static CPUReadMemoryFunc *intregm_mem_read[3] = {
    intregm_mem_readl,
    intregm_mem_readl,
    intregm_mem_readl,
};

static CPUWriteMemoryFunc *intregm_mem_write[3] = {
    intregm_mem_writel,
    intregm_mem_writel,
    intregm_mem_writel,
};

void pic_info(void)
{
    term_printf("per-cpu: pending 0x%08x, enabled 0x%08x\n", ps->intreg_pending, ps->intreg_enabled);
    term_printf("master: pending 0x%08x, enabled 0x%08x\n", ps->intregm_pending, ps->intregm_enabled);
}

void irq_info(void)
{
#ifndef DEBUG_IRQ_COUNT
    term_printf("irq statistic code not compiled.\n");
#else
    int i;
    int64_t count;

    term_printf("IRQ statistics:\n");
    for (i = 0; i < 32; i++) {
        count = irq_count[i];
        if (count > 0)
            term_printf("%2d: %lld\n", i, count);
    }
#endif
}

static const unsigned int intr_to_mask[16] = {
	0,	0,	0,	0,	0,	0, SUN4M_INT_ETHERNET,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
};

void pic_set_irq(int irq, int level)
{
    if (irq < 16) {
	unsigned int mask = intr_to_mask[irq];
	ps->intreg_pending |= 1 << irq;
	if (ps->intregm_enabled & mask) {
	    cpu_single_env->interrupt_index = irq;
	    cpu_interrupt(cpu_single_env, CPU_INTERRUPT_HARD);
	}
    }
#ifdef DEBUG_IRQ_COUNT
    if (level == 1)
	irq_count[irq]++;
#endif
}

void sched_init(uint32_t addr, uint32_t addrg)
{
    int intreg_io_memory, intregm_io_memory;
    SCHEDState *s;

    s = qemu_mallocz(sizeof(SCHEDState));
    if (!s)
        return;
    s->addr = addr;
    s->addrg = addrg;

    intreg_io_memory = cpu_register_io_memory(0, intreg_mem_read, intreg_mem_write, s);
    cpu_register_physical_memory(addr, 3, intreg_io_memory);

    intregm_io_memory = cpu_register_io_memory(0, intregm_mem_read, intregm_mem_write, s);
    cpu_register_physical_memory(addrg, 5, intregm_io_memory);

    ps = s;
}

