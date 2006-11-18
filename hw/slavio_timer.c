/*
 * QEMU Sparc SLAVIO timer controller emulation
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

//#define DEBUG_TIMER

#ifdef DEBUG_TIMER
#define DPRINTF(fmt, args...) \
do { printf("TIMER: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

/*
 * Registers of hardware timer in sun4m.
 *
 * This is the timer/counter part of chip STP2001 (Slave I/O), also
 * produced as NCR89C105. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C105.txt
 * 
 * The 31-bit counter is incremented every 500ns by bit 9. Bits 8..0
 * are zero. Bit 31 is 1 when count has been reached.
 *
 * Per-CPU timers interrupt local CPU, system timer uses normal
 * interrupt routing.
 *
 */

typedef struct SLAVIO_TIMERState {
    uint32_t limit, count, counthigh;
    int64_t count_load_time;
    int64_t expire_time;
    int64_t stop_time, tick_offset;
    QEMUTimer *irq_timer;
    int irq;
    int reached, stopped;
    int mode; // 0 = processor, 1 = user, 2 = system
    unsigned int cpu;
} SLAVIO_TIMERState;

#define TIMER_MAXADDR 0x1f
#define CNT_FREQ 2000000

// Update count, set irq, update expire_time
static void slavio_timer_get_out(SLAVIO_TIMERState *s)
{
    int out;
    int64_t diff, ticks, count;
    uint32_t limit;

    // There are three clock tick units: CPU ticks, register units
    // (nanoseconds), and counter ticks (500 ns).
    if (s->mode == 1 && s->stopped)
	ticks = s->stop_time;
    else
	ticks = qemu_get_clock(vm_clock) - s->tick_offset;

    out = (ticks > s->expire_time);
    if (out)
	s->reached = 0x80000000;
    if (!s->limit)
	limit = 0x7fffffff;
    else
	limit = s->limit;

    // Convert register units to counter ticks
    limit = limit >> 9;

    // Convert cpu ticks to counter ticks
    diff = muldiv64(ticks - s->count_load_time, CNT_FREQ, ticks_per_sec);

    // Calculate what the counter should be, convert to register
    // units
    count = diff % limit;
    s->count = count << 9;
    s->counthigh = count >> 22;

    // Expire time: CPU ticks left to next interrupt
    // Convert remaining counter ticks to CPU ticks
    s->expire_time = ticks + muldiv64(limit - count, ticks_per_sec, CNT_FREQ);

    DPRINTF("irq %d limit %d reached %d d %" PRId64 " count %d s->c %x diff %" PRId64 " stopped %d mode %d\n", s->irq, limit, s->reached?1:0, (ticks-s->count_load_time), count, s->count, s->expire_time - ticks, s->stopped, s->mode);

    if (s->mode != 1)
	pic_set_irq_cpu(s->irq, out, s->cpu);
}

// timer callback
static void slavio_timer_irq(void *opaque)
{
    SLAVIO_TIMERState *s = opaque;

    if (!s->irq_timer)
        return;
    slavio_timer_get_out(s);
    if (s->mode != 1)
	qemu_mod_timer(s->irq_timer, s->expire_time);
}

static uint32_t slavio_timer_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SLAVIO_TIMERState *s = opaque;
    uint32_t saddr;

    saddr = (addr & TIMER_MAXADDR) >> 2;
    switch (saddr) {
    case 0:
	// read limit (system counter mode) or read most signifying
	// part of counter (user mode)
	if (s->mode != 1) {
	    // clear irq
	    pic_set_irq_cpu(s->irq, 0, s->cpu);
	    s->count_load_time = qemu_get_clock(vm_clock);
	    s->reached = 0;
	    return s->limit;
	}
	else {
	    slavio_timer_get_out(s);
	    return s->counthigh & 0x7fffffff;
	}
    case 1:
	// read counter and reached bit (system mode) or read lsbits
	// of counter (user mode)
	slavio_timer_get_out(s);
	if (s->mode != 1)
	    return (s->count & 0x7fffffff) | s->reached;
	else
	    return s->count;
    case 3:
	// read start/stop status
	return s->stopped;
    case 4:
	// read user/system mode
	return s->mode & 1;
    default:
	return 0;
    }
}

static void slavio_timer_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SLAVIO_TIMERState *s = opaque;
    uint32_t saddr;

    saddr = (addr & TIMER_MAXADDR) >> 2;
    switch (saddr) {
    case 0:
	// set limit, reset counter
	s->count_load_time = qemu_get_clock(vm_clock);
	// fall through
    case 2:
	// set limit without resetting counter
	if (!val)
	    s->limit = 0x7fffffff;
	else
	    s->limit = val & 0x7fffffff;
	slavio_timer_irq(s);
	break;
    case 3:
	// start/stop user counter
	if (s->mode == 1) {
	    if (val & 1) {
		s->stop_time = qemu_get_clock(vm_clock);
		s->stopped = 1;
	    }
	    else {
		if (s->stopped)
		    s->tick_offset += qemu_get_clock(vm_clock) - s->stop_time;
		s->stopped = 0;
	    }
	}
	break;
    case 4:
	// bit 0: user (1) or system (0) counter mode
	if (s->mode == 0 || s->mode == 1)
	    s->mode = val & 1;
	break;
    default:
	break;
    }
}

static CPUReadMemoryFunc *slavio_timer_mem_read[3] = {
    slavio_timer_mem_readl,
    slavio_timer_mem_readl,
    slavio_timer_mem_readl,
};

static CPUWriteMemoryFunc *slavio_timer_mem_write[3] = {
    slavio_timer_mem_writel,
    slavio_timer_mem_writel,
    slavio_timer_mem_writel,
};

static void slavio_timer_save(QEMUFile *f, void *opaque)
{
    SLAVIO_TIMERState *s = opaque;

    qemu_put_be32s(f, &s->limit);
    qemu_put_be32s(f, &s->count);
    qemu_put_be32s(f, &s->counthigh);
    qemu_put_be64s(f, &s->count_load_time);
    qemu_put_be64s(f, &s->expire_time);
    qemu_put_be64s(f, &s->stop_time);
    qemu_put_be64s(f, &s->tick_offset);
    qemu_put_be32s(f, &s->irq);
    qemu_put_be32s(f, &s->reached);
    qemu_put_be32s(f, &s->stopped);
    qemu_put_be32s(f, &s->mode);
}

static int slavio_timer_load(QEMUFile *f, void *opaque, int version_id)
{
    SLAVIO_TIMERState *s = opaque;
    
    if (version_id != 1)
        return -EINVAL;

    qemu_get_be32s(f, &s->limit);
    qemu_get_be32s(f, &s->count);
    qemu_get_be32s(f, &s->counthigh);
    qemu_get_be64s(f, &s->count_load_time);
    qemu_get_be64s(f, &s->expire_time);
    qemu_get_be64s(f, &s->stop_time);
    qemu_get_be64s(f, &s->tick_offset);
    qemu_get_be32s(f, &s->irq);
    qemu_get_be32s(f, &s->reached);
    qemu_get_be32s(f, &s->stopped);
    qemu_get_be32s(f, &s->mode);
    return 0;
}

static void slavio_timer_reset(void *opaque)
{
    SLAVIO_TIMERState *s = opaque;

    s->limit = 0;
    s->count = 0;
    s->count_load_time = qemu_get_clock(vm_clock);;
    s->stop_time = s->count_load_time;
    s->tick_offset = 0;
    s->reached = 0;
    s->mode &= 2;
    s->stopped = 1;
    slavio_timer_get_out(s);
}

void slavio_timer_init(uint32_t addr, int irq, int mode, unsigned int cpu)
{
    int slavio_timer_io_memory;
    SLAVIO_TIMERState *s;

    s = qemu_mallocz(sizeof(SLAVIO_TIMERState));
    if (!s)
        return;
    s->irq = irq;
    s->mode = mode;
    s->cpu = cpu;
    s->irq_timer = qemu_new_timer(vm_clock, slavio_timer_irq, s);

    slavio_timer_io_memory = cpu_register_io_memory(0, slavio_timer_mem_read,
						    slavio_timer_mem_write, s);
    cpu_register_physical_memory(addr, TIMER_MAXADDR, slavio_timer_io_memory);
    register_savevm("slavio_timer", addr, 1, slavio_timer_save, slavio_timer_load, s);
    qemu_register_reset(slavio_timer_reset, s);
    slavio_timer_reset(s);
}
