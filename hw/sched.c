/*
 * QEMU interrupt controller & timer emulation
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

#define PHYS_JJ_CLOCK	0x71D00000
#define PHYS_JJ_CLOCK1	0x71D10000
#define PHYS_JJ_INTR0	0x71E00000	/* CPU0 interrupt control registers */
#define PHYS_JJ_INTR_G	0x71E10000	/* Master interrupt control registers */

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
/*
 * Registers of hardware timer in sun4m.
 */
struct sun4m_timer_percpu {
	volatile unsigned int l14_timer_limit; /* Initial value is 0x009c4000 */
	volatile unsigned int l14_cur_count;
};

struct sun4m_timer_global {
        volatile unsigned int l10_timer_limit;
        volatile unsigned int l10_cur_count;
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
    uint32_t intreg_pending;
    uint32_t intreg_enabled;
    uint32_t intregm_pending;
    uint32_t intregm_enabled;
    uint32_t timer_regs[2];
    uint32_t timerm_regs[2];
} SCHEDState;

static SCHEDState *ps;

static int intreg_io_memory, intregm_io_memory,
    timer_io_memory, timerm_io_memory;

static void sched_reset(SCHEDState *s)
{
}

static uint32_t intreg_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SCHEDState *s = opaque;
    uint32_t saddr;

    saddr = (addr - PHYS_JJ_INTR0) >> 2;
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

    saddr = (addr - PHYS_JJ_INTR0) >> 2;
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

    saddr = (addr - PHYS_JJ_INTR_G) >> 2;
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

    saddr = (addr - PHYS_JJ_INTR_G) >> 2;
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

static uint32_t timer_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SCHEDState *s = opaque;
    uint32_t saddr;

    saddr = (addr - PHYS_JJ_CLOCK) >> 2;
    switch (saddr) {
    default:
	return s->timer_regs[saddr];
	break;
    }
    return 0;
}

static void timer_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SCHEDState *s = opaque;
    uint32_t saddr;

    saddr = (addr - PHYS_JJ_CLOCK) >> 2;
    switch (saddr) {
    default:
	s->timer_regs[saddr] = val;
	break;
    }
}

static CPUReadMemoryFunc *timer_mem_read[3] = {
    timer_mem_readl,
    timer_mem_readl,
    timer_mem_readl,
};

static CPUWriteMemoryFunc *timer_mem_write[3] = {
    timer_mem_writel,
    timer_mem_writel,
    timer_mem_writel,
};

static uint32_t timerm_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SCHEDState *s = opaque;
    uint32_t saddr;

    saddr = (addr - PHYS_JJ_CLOCK1) >> 2;
    switch (saddr) {
    default:
	return s->timerm_regs[saddr];
	break;
    }
    return 0;
}

static void timerm_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SCHEDState *s = opaque;
    uint32_t saddr;

    saddr = (addr - PHYS_JJ_CLOCK1) >> 2;
    switch (saddr) {
    default:
	s->timerm_regs[saddr] = val;
	break;
    }
}

static CPUReadMemoryFunc *timerm_mem_read[3] = {
    timerm_mem_readl,
    timerm_mem_readl,
    timerm_mem_readl,
};

static CPUWriteMemoryFunc *timerm_mem_write[3] = {
    timerm_mem_writel,
    timerm_mem_writel,
    timerm_mem_writel,
};

void pic_info() {}
void irq_info() {}

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
}

void sched_init()
{
    SCHEDState *s;

    s = qemu_mallocz(sizeof(SCHEDState));
    if (!s)
        return;

    intreg_io_memory = cpu_register_io_memory(0, intreg_mem_read, intreg_mem_write, s);
    cpu_register_physical_memory(PHYS_JJ_INTR0, 3, intreg_io_memory);

    intregm_io_memory = cpu_register_io_memory(0, intregm_mem_read, intregm_mem_write, s);
    cpu_register_physical_memory(PHYS_JJ_INTR_G, 5, intregm_io_memory);

    timer_io_memory = cpu_register_io_memory(0, timer_mem_read, timer_mem_write, s);
    cpu_register_physical_memory(PHYS_JJ_CLOCK, 2, timer_io_memory);

    timerm_io_memory = cpu_register_io_memory(0, timerm_mem_read, timerm_mem_write, s);
    cpu_register_physical_memory(PHYS_JJ_CLOCK1, 2, timerm_io_memory);

    sched_reset(s);
    ps = s;
}

