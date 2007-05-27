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
    qemu_irq irq;
    ptimer_state *timer;
    uint32_t count, counthigh, reached;
    uint64_t limit;
    int stopped;
    int mode; // 0 = processor, 1 = user, 2 = system
} SLAVIO_TIMERState;

#define TIMER_MAXADDR 0x1f
#define TIMER_SIZE (TIMER_MAXADDR + 1)

// Update count, set irq, update expire_time
// Convert from ptimer countdown units
static void slavio_timer_get_out(SLAVIO_TIMERState *s)
{
    uint64_t count;

    count = s->limit - (ptimer_get_count(s->timer) << 9);
    DPRINTF("get_out: limit %" PRIx64 " count %x%08x\n", s->limit, s->counthigh,
            s->count);
    s->count = count & 0xfffffe00;
    s->counthigh = count >> 32;
}

// timer callback
static void slavio_timer_irq(void *opaque)
{
    SLAVIO_TIMERState *s = opaque;

    slavio_timer_get_out(s);
    DPRINTF("callback: count %x%08x\n", s->counthigh, s->count);
    s->reached = 0x80000000;
    if (s->mode != 1)
	qemu_irq_raise(s->irq);
}

static uint32_t slavio_timer_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SLAVIO_TIMERState *s = opaque;
    uint32_t saddr, ret;

    saddr = (addr & TIMER_MAXADDR) >> 2;
    switch (saddr) {
    case 0:
	// read limit (system counter mode) or read most signifying
	// part of counter (user mode)
	if (s->mode != 1) {
	    // clear irq
            qemu_irq_lower(s->irq);
	    s->reached = 0;
            ret = s->limit & 0x7fffffff;
	}
	else {
	    slavio_timer_get_out(s);
            ret = s->counthigh & 0x7fffffff;
	}
        break;
    case 1:
	// read counter and reached bit (system mode) or read lsbits
	// of counter (user mode)
	slavio_timer_get_out(s);
	if (s->mode != 1)
            ret = (s->count & 0x7fffffff) | s->reached;
	else
            ret = s->count;
        break;
    case 3:
	// read start/stop status
        ret = s->stopped;
        break;
    case 4:
	// read user/system mode
        ret = s->mode & 1;
        break;
    default:
        ret = 0;
        break;
    }
    DPRINTF("read " TARGET_FMT_plx " = %08x\n", addr, ret);

    return ret;
}

static void slavio_timer_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SLAVIO_TIMERState *s = opaque;
    uint32_t saddr;
    int reload = 0;

    DPRINTF("write " TARGET_FMT_plx " %08x\n", addr, val);
    saddr = (addr & TIMER_MAXADDR) >> 2;
    switch (saddr) {
    case 0:
	// set limit, reset counter
        reload = 1;
	qemu_irq_lower(s->irq);
	// fall through
    case 2:
	// set limit without resetting counter
        s->limit = val & 0x7ffffe00ULL;
        if (!s->limit)
            s->limit = 0x7ffffe00ULL;
        ptimer_set_limit(s->timer, s->limit >> 9, reload);
	break;
    case 3:
	// start/stop user counter
	if (s->mode == 1) {
	    if (val & 1) {
                ptimer_stop(s->timer);
		s->stopped = 1;
	    }
	    else {
                ptimer_run(s->timer, 0);
		s->stopped = 0;
	    }
	}
	break;
    case 4:
	// bit 0: user (1) or system (0) counter mode
	if (s->mode == 0 || s->mode == 1)
	    s->mode = val & 1;
        if (s->mode == 1) {
            qemu_irq_lower(s->irq);
            s->limit = -1ULL;
        }
        ptimer_set_limit(s->timer, s->limit >> 9, 1);
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

    qemu_put_be64s(f, &s->limit);
    qemu_put_be32s(f, &s->count);
    qemu_put_be32s(f, &s->counthigh);
    qemu_put_be32(f, 0); // Was irq
    qemu_put_be32s(f, &s->reached);
    qemu_put_be32s(f, &s->stopped);
    qemu_put_be32s(f, &s->mode);
    qemu_put_ptimer(f, s->timer);
}

static int slavio_timer_load(QEMUFile *f, void *opaque, int version_id)
{
    SLAVIO_TIMERState *s = opaque;
    uint32_t tmp;
    
    if (version_id != 2)
        return -EINVAL;

    qemu_get_be64s(f, &s->limit);
    qemu_get_be32s(f, &s->count);
    qemu_get_be32s(f, &s->counthigh);
    qemu_get_be32s(f, &tmp); // Was irq
    qemu_get_be32s(f, &s->reached);
    qemu_get_be32s(f, &s->stopped);
    qemu_get_be32s(f, &s->mode);
    qemu_get_ptimer(f, s->timer);

    return 0;
}

static void slavio_timer_reset(void *opaque)
{
    SLAVIO_TIMERState *s = opaque;

    s->limit = 0x7ffffe00ULL;
    s->count = 0;
    s->reached = 0;
    s->mode &= 2;
    ptimer_set_limit(s->timer, s->limit >> 9, 1);
    ptimer_run(s->timer, 0);
    s->stopped = 1;
    qemu_irq_lower(s->irq);
}

void slavio_timer_init(target_phys_addr_t addr, qemu_irq irq, int mode)
{
    int slavio_timer_io_memory;
    SLAVIO_TIMERState *s;
    QEMUBH *bh;

    s = qemu_mallocz(sizeof(SLAVIO_TIMERState));
    if (!s)
        return;
    s->irq = irq;
    s->mode = mode;
    bh = qemu_bh_new(slavio_timer_irq, s);
    s->timer = ptimer_init(bh);
    ptimer_set_period(s->timer, 500ULL);

    slavio_timer_io_memory = cpu_register_io_memory(0, slavio_timer_mem_read,
						    slavio_timer_mem_write, s);
    cpu_register_physical_memory(addr, TIMER_SIZE, slavio_timer_io_memory);
    register_savevm("slavio_timer", addr, 2, slavio_timer_save, slavio_timer_load, s);
    qemu_register_reset(slavio_timer_reset, s);
    slavio_timer_reset(s);
}
