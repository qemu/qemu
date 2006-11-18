/*
 * QEMU Sparc SLAVIO interrupt controller emulation
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
//#define DEBUG_IRQ_COUNT
//#define DEBUG_IRQ

#ifdef DEBUG_IRQ
#define DPRINTF(fmt, args...) \
do { printf("IRQ: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

/*
 * Registers of interrupt controller in sun4m.
 *
 * This is the interrupt controller part of chip STP2001 (Slave I/O), also
 * produced as NCR89C105. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C105.txt
 *
 * There is a system master controller and one for each cpu.
 * 
 */

#define MAX_CPUS 16

typedef struct SLAVIO_INTCTLState {
    uint32_t intreg_pending[MAX_CPUS];
    uint32_t intregm_pending;
    uint32_t intregm_disabled;
    uint32_t target_cpu;
#ifdef DEBUG_IRQ_COUNT
    uint64_t irq_count[32];
#endif
    CPUState *cpu_envs[MAX_CPUS];
} SLAVIO_INTCTLState;

#define INTCTL_MAXADDR 0xf
#define INTCTLM_MAXADDR 0xf
static void slavio_check_interrupts(void *opaque);

// per-cpu interrupt controller
static uint32_t slavio_intctl_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SLAVIO_INTCTLState *s = opaque;
    uint32_t saddr;
    int cpu;

    cpu = (addr & (MAX_CPUS - 1) * TARGET_PAGE_SIZE) >> 12;
    saddr = (addr & INTCTL_MAXADDR) >> 2;
    switch (saddr) {
    case 0:
	return s->intreg_pending[cpu];
    default:
	break;
    }
    return 0;
}

static void slavio_intctl_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SLAVIO_INTCTLState *s = opaque;
    uint32_t saddr;
    int cpu;

    cpu = (addr & (MAX_CPUS - 1) * TARGET_PAGE_SIZE) >> 12;
    saddr = (addr & INTCTL_MAXADDR) >> 2;
    switch (saddr) {
    case 1: // clear pending softints
	if (val & 0x4000)
	    val |= 80000000;
	val &= 0xfffe0000;
	s->intreg_pending[cpu] &= ~val;
	DPRINTF("Cleared cpu %d irq mask %x, curmask %x\n", cpu, val, s->intreg_pending[cpu]);
	break;
    case 2: // set softint
	val &= 0xfffe0000;
	s->intreg_pending[cpu] |= val;
        slavio_check_interrupts(s);
	DPRINTF("Set cpu %d irq mask %x, curmask %x\n", cpu, val, s->intreg_pending[cpu]);
	break;
    default:
	break;
    }
}

static CPUReadMemoryFunc *slavio_intctl_mem_read[3] = {
    slavio_intctl_mem_readl,
    slavio_intctl_mem_readl,
    slavio_intctl_mem_readl,
};

static CPUWriteMemoryFunc *slavio_intctl_mem_write[3] = {
    slavio_intctl_mem_writel,
    slavio_intctl_mem_writel,
    slavio_intctl_mem_writel,
};

// master system interrupt controller
static uint32_t slavio_intctlm_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SLAVIO_INTCTLState *s = opaque;
    uint32_t saddr;

    saddr = (addr & INTCTLM_MAXADDR) >> 2;
    switch (saddr) {
    case 0:
	return s->intregm_pending & 0x7fffffff;
    case 1:
	return s->intregm_disabled;
    case 4:
	return s->target_cpu;
    default:
	break;
    }
    return 0;
}

static void slavio_intctlm_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SLAVIO_INTCTLState *s = opaque;
    uint32_t saddr;

    saddr = (addr & INTCTLM_MAXADDR) >> 2;
    switch (saddr) {
    case 2: // clear (enable)
	// Force clear unused bits
	val &= ~0x4fb2007f;
	s->intregm_disabled &= ~val;
	DPRINTF("Enabled master irq mask %x, curmask %x\n", val, s->intregm_disabled);
	slavio_check_interrupts(s);
	break;
    case 3: // set (disable, clear pending)
	// Force clear unused bits
	val &= ~0x4fb2007f;
	s->intregm_disabled |= val;
	s->intregm_pending &= ~val;
	DPRINTF("Disabled master irq mask %x, curmask %x\n", val, s->intregm_disabled);
	break;
    case 4:
	s->target_cpu = val & (MAX_CPUS - 1);
	DPRINTF("Set master irq cpu %d\n", s->target_cpu);
	break;
    default:
	break;
    }
}

static CPUReadMemoryFunc *slavio_intctlm_mem_read[3] = {
    slavio_intctlm_mem_readl,
    slavio_intctlm_mem_readl,
    slavio_intctlm_mem_readl,
};

static CPUWriteMemoryFunc *slavio_intctlm_mem_write[3] = {
    slavio_intctlm_mem_writel,
    slavio_intctlm_mem_writel,
    slavio_intctlm_mem_writel,
};

void slavio_pic_info(void *opaque)
{
    SLAVIO_INTCTLState *s = opaque;
    int i;

    for (i = 0; i < MAX_CPUS; i++) {
	term_printf("per-cpu %d: pending 0x%08x\n", i, s->intreg_pending[i]);
    }
    term_printf("master: pending 0x%08x, disabled 0x%08x\n", s->intregm_pending, s->intregm_disabled);
}

void slavio_irq_info(void *opaque)
{
#ifndef DEBUG_IRQ_COUNT
    term_printf("irq statistic code not compiled.\n");
#else
    SLAVIO_INTCTLState *s = opaque;
    int i;
    int64_t count;

    term_printf("IRQ statistics:\n");
    for (i = 0; i < 32; i++) {
        count = s->irq_count[i];
        if (count > 0)
            term_printf("%2d: %" PRId64 "\n", i, count);
    }
#endif
}

static const uint32_t intbit_to_level[32] = {
    2, 3, 5, 7, 9, 11, 0, 14,	3, 5, 7, 9, 11, 13, 12, 12,
    6, 0, 4, 10, 8, 0, 11, 0,	0, 0, 0, 0, 15, 0, 15, 0,
};

static void slavio_check_interrupts(void *opaque)
{
    CPUState *env;
    SLAVIO_INTCTLState *s = opaque;
    uint32_t pending = s->intregm_pending;
    unsigned int i, j, max = 0;

    pending &= ~s->intregm_disabled;

    if (pending && !(s->intregm_disabled & 0x80000000)) {
	for (i = 0; i < 32; i++) {
	    if (pending & (1 << i)) {
		if (max < intbit_to_level[i])
		    max = intbit_to_level[i];
	    }
	}
        env = s->cpu_envs[s->target_cpu];
        if (!env) {
	    DPRINTF("No CPU %d, not triggered (pending %x)\n", s->target_cpu, pending);
        }
	else {
            if (env->halted)
                env->halted = 0;
            if (env->interrupt_index == 0) {
                DPRINTF("Triggered CPU %d pil %d\n", s->target_cpu, max);
#ifdef DEBUG_IRQ_COUNT
                s->irq_count[max]++;
#endif
                env->interrupt_index = TT_EXTINT | max;
                cpu_interrupt(env, CPU_INTERRUPT_HARD);
            }
            else
                DPRINTF("Not triggered (pending %x), pending exception %x\n", pending, env->interrupt_index);
	}
    }
    else
	DPRINTF("Not triggered (pending %x), disabled %x\n", pending, s->intregm_disabled);
    
    for (i = 0; i < MAX_CPUS; i++) {
        max = 0;
        env = s->cpu_envs[i];
        if (!env)
            continue;
        for (j = 17; j < 32; j++) {
            if (s->intreg_pending[i] & (1 << j)) {
                if (max < j - 16)
                    max = j - 16;
            }
        }
	if (max > 0) {
            if (env->halted)
                env->halted = 0;
            if (env->interrupt_index == 0) {
                DPRINTF("Triggered softint %d for cpu %d (pending %x)\n", max, i, pending);
#ifdef DEBUG_IRQ_COUNT
                s->irq_count[max]++;
#endif
                env->interrupt_index = TT_EXTINT | max;
                cpu_interrupt(env, CPU_INTERRUPT_HARD);
            }
        }
    }
}

/*
 * "irq" here is the bit number in the system interrupt register to
 * separate serial and keyboard interrupts sharing a level.
 */
void slavio_pic_set_irq(void *opaque, int irq, int level)
{
    SLAVIO_INTCTLState *s = opaque;

    DPRINTF("Set cpu %d irq %d level %d\n", s->target_cpu, irq, level);
    if (irq < 32) {
	uint32_t mask = 1 << irq;
	uint32_t pil = intbit_to_level[irq];
	if (pil > 0) {
	    if (level) {
		s->intregm_pending |= mask;
		s->intreg_pending[s->target_cpu] |= 1 << pil;
	    }
	    else {
		s->intregm_pending &= ~mask;
		s->intreg_pending[s->target_cpu] &= ~(1 << pil);
	    }
	}
    }
    slavio_check_interrupts(s);
}

void slavio_pic_set_irq_cpu(void *opaque, int irq, int level, unsigned int cpu)
{
    SLAVIO_INTCTLState *s = opaque;

    DPRINTF("Set cpu %d local irq %d level %d\n", cpu, irq, level);
    if (cpu == (unsigned int)-1) {
        slavio_pic_set_irq(opaque, irq, level);
        return;
    }
    if (irq < 32) {
	uint32_t pil = intbit_to_level[irq];
    	if (pil > 0) {
	    if (level) {
		s->intreg_pending[cpu] |= 1 << pil;
	    }
	    else {
		s->intreg_pending[cpu] &= ~(1 << pil);
	    }
	}
    }
    slavio_check_interrupts(s);
}

static void slavio_intctl_save(QEMUFile *f, void *opaque)
{
    SLAVIO_INTCTLState *s = opaque;
    int i;
    
    for (i = 0; i < MAX_CPUS; i++) {
	qemu_put_be32s(f, &s->intreg_pending[i]);
    }
    qemu_put_be32s(f, &s->intregm_pending);
    qemu_put_be32s(f, &s->intregm_disabled);
    qemu_put_be32s(f, &s->target_cpu);
}

static int slavio_intctl_load(QEMUFile *f, void *opaque, int version_id)
{
    SLAVIO_INTCTLState *s = opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    for (i = 0; i < MAX_CPUS; i++) {
	qemu_get_be32s(f, &s->intreg_pending[i]);
    }
    qemu_get_be32s(f, &s->intregm_pending);
    qemu_get_be32s(f, &s->intregm_disabled);
    qemu_get_be32s(f, &s->target_cpu);
    return 0;
}

static void slavio_intctl_reset(void *opaque)
{
    SLAVIO_INTCTLState *s = opaque;
    int i;

    for (i = 0; i < MAX_CPUS; i++) {
	s->intreg_pending[i] = 0;
    }
    s->intregm_disabled = ~0xffb2007f;
    s->intregm_pending = 0;
    s->target_cpu = 0;
}

void slavio_intctl_set_cpu(void *opaque, unsigned int cpu, CPUState *env)
{
    SLAVIO_INTCTLState *s = opaque;
    s->cpu_envs[cpu] = env;
}

void *slavio_intctl_init(uint32_t addr, uint32_t addrg)
{
    int slavio_intctl_io_memory, slavio_intctlm_io_memory, i;
    SLAVIO_INTCTLState *s;

    s = qemu_mallocz(sizeof(SLAVIO_INTCTLState));
    if (!s)
        return NULL;

    for (i = 0; i < MAX_CPUS; i++) {
	slavio_intctl_io_memory = cpu_register_io_memory(0, slavio_intctl_mem_read, slavio_intctl_mem_write, s);
	cpu_register_physical_memory(addr + i * TARGET_PAGE_SIZE, INTCTL_MAXADDR, slavio_intctl_io_memory);
    }

    slavio_intctlm_io_memory = cpu_register_io_memory(0, slavio_intctlm_mem_read, slavio_intctlm_mem_write, s);
    cpu_register_physical_memory(addrg, INTCTLM_MAXADDR, slavio_intctlm_io_memory);

    register_savevm("slavio_intctl", addr, 1, slavio_intctl_save, slavio_intctl_load, s);
    qemu_register_reset(slavio_intctl_reset, s);
    slavio_intctl_reset(s);
    return s;
}

