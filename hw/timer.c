/*
 * QEMU Sparc timer controller emulation
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

typedef struct TIMERState {
    uint32_t addr;
    uint32_t timer_regs[2];
    int irq;
} TIMERState;

static uint32_t timer_mem_readl(void *opaque, target_phys_addr_t addr)
{
    TIMERState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addr) >> 2;
    switch (saddr) {
    default:
	return s->timer_regs[saddr];
	break;
    }
    return 0;
}

static void timer_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    TIMERState *s = opaque;
    uint32_t saddr;

    saddr = (addr - s->addr) >> 2;
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

void timer_init(uint32_t addr, int irq)
{
    int timer_io_memory;
    TIMERState *s;

    s = qemu_mallocz(sizeof(TIMERState));
    if (!s)
        return;
    s->addr = addr;
    s->irq = irq;

    timer_io_memory = cpu_register_io_memory(0, timer_mem_read, timer_mem_write, s);
    cpu_register_physical_memory(addr, 2, timer_io_memory);
}
