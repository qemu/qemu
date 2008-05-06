/*
 * QEMU ETRAX Timers
 *
 * Copyright (c) 2007 Edgar E. Iglesias, Axis Communications AB.
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
#include <stdio.h>
#include <sys/time.h>
#include "hw.h"
#include "qemu-timer.h"

#define D(x)

#define RW_TMR0_DIV   0x00
#define R_TMR0_DATA   0x04
#define RW_TMR0_CTRL  0x08
#define RW_TMR1_DIV   0x10
#define R_TMR1_DATA   0x14
#define RW_TMR1_CTRL  0x18
#define R_TIME        0x38
#define RW_WD_CTRL    0x40
#define RW_INTR_MASK  0x48
#define RW_ACK_INTR   0x4c
#define R_INTR        0x50
#define R_MASKED_INTR 0x54

struct fs_timer_t {
	CPUState *env;
	qemu_irq *irq;
	target_phys_addr_t base;

	QEMUBH *bh;
	ptimer_state *ptimer;
	struct timeval last;

	/* Control registers.  */
	uint32_t rw_tmr0_div;
	uint32_t r_tmr0_data;
	uint32_t rw_tmr0_ctrl;

	uint32_t rw_tmr1_div;
	uint32_t r_tmr1_data;
	uint32_t rw_tmr1_ctrl;

	uint32_t rw_intr_mask;
	uint32_t rw_ack_intr;
	uint32_t r_intr;
	uint32_t r_masked_intr;
};

static uint32_t timer_rinvalid (void *opaque, target_phys_addr_t addr)
{
	struct fs_timer_t *t = opaque;
	CPUState *env = t->env;
	cpu_abort(env, "Unsupported short access. reg=%x pc=%x.\n", 
		  addr, env->pc);
	return 0;
}

static uint32_t timer_readl (void *opaque, target_phys_addr_t addr)
{
	struct fs_timer_t *t = opaque;
	D(CPUState *env = t->env);
	uint32_t r = 0;

	/* Make addr relative to this instances base.  */
	addr -= t->base;
	switch (addr) {
	case R_TMR0_DATA:
		break;
	case R_TMR1_DATA:
		D(printf ("R_TMR1_DATA\n"));
		break;
	case R_TIME:
		r = qemu_get_clock(vm_clock) * 10;
		break;
	case RW_INTR_MASK:
		r = t->rw_intr_mask;
		break;
	case R_MASKED_INTR:
		r = t->r_intr & t->rw_intr_mask;
		break;
	default:
		D(printf ("%s %x p=%x\n", __func__, addr, env->pc));
		break;
	}
	return r;
}

static void
timer_winvalid (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	struct fs_timer_t *t = opaque;
	CPUState *env = t->env;
	cpu_abort(env, "Unsupported short access. reg=%x pc=%x.\n", 
		  addr, env->pc);
}

#define TIMER_SLOWDOWN 1
static void update_ctrl(struct fs_timer_t *t)
{
	unsigned int op;
	unsigned int freq;
	unsigned int freq_hz;
	unsigned int div;

	op = t->rw_tmr0_ctrl & 3;
	freq = t->rw_tmr0_ctrl >> 2;
	freq_hz = 32000000;

	switch (freq)
	{
	case 0:
	case 1:
		D(printf ("extern or disabled timer clock?\n"));
		break;
	case 4: freq_hz =  29493000; break;
	case 5: freq_hz =  32000000; break;
	case 6: freq_hz =  32768000; break;
	case 7: freq_hz = 100000000; break;
	default:
		abort();
		break;
	}

	D(printf ("freq_hz=%d div=%d\n", freq_hz, t->rw_tmr0_div));
	div = t->rw_tmr0_div * TIMER_SLOWDOWN;
	div >>= 15;
	freq_hz >>= 15;
	ptimer_set_freq(t->ptimer, freq_hz);
	ptimer_set_limit(t->ptimer, div, 0);

	switch (op)
	{
		case 0:
			/* Load.  */
			ptimer_set_limit(t->ptimer, div, 1);
			ptimer_run(t->ptimer, 1);
			break;
		case 1:
			/* Hold.  */
			ptimer_stop(t->ptimer);
			break;
		case 2:
			/* Run.  */
			ptimer_run(t->ptimer, 0);
			break;
		default:
			abort();
			break;
	}
}

static void timer_update_irq(struct fs_timer_t *t)
{
	t->r_intr &= ~(t->rw_ack_intr);
	t->r_masked_intr = t->r_intr & t->rw_intr_mask;

	D(printf("%s: masked_intr=%x\n", __func__, t->r_masked_intr));
	if (t->r_masked_intr & 1)
		qemu_irq_raise(t->irq[0]);
	else
		qemu_irq_lower(t->irq[0]);
}

static void timer_hit(void *opaque)
{
	struct fs_timer_t *t = opaque;
	t->r_intr |= 1;
	timer_update_irq(t);
}

static void
timer_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	struct fs_timer_t *t = opaque;
	CPUState *env = t->env;

	/* Make addr relative to this instances base.  */
	addr -= t->base;
	switch (addr)
	{
		case RW_TMR0_DIV:
			t->rw_tmr0_div = value;
			break;
		case RW_TMR0_CTRL:
			D(printf ("RW_TMR0_CTRL=%x\n", value));
			t->rw_tmr0_ctrl = value;
			update_ctrl(t);
			break;
		case RW_TMR1_DIV:
			t->rw_tmr1_div = value;
			break;
		case RW_TMR1_CTRL:
			D(printf ("RW_TMR1_CTRL=%x\n", value));
			break;
		case RW_INTR_MASK:
			D(printf ("RW_INTR_MASK=%x\n", value));
			t->rw_intr_mask = value;
			timer_update_irq(t);
			break;
		case RW_WD_CTRL:
			D(printf ("RW_WD_CTRL=%x\n", value));
			break;
		case RW_ACK_INTR:
			t->rw_ack_intr = value;
			timer_update_irq(t);
			t->rw_ack_intr = 0;
			break;
		default:
			printf ("%s %x %x pc=%x\n",
				__func__, addr, value, env->pc);
			break;
	}
}

static CPUReadMemoryFunc *timer_read[] = {
    &timer_rinvalid,
    &timer_rinvalid,
    &timer_readl,
};

static CPUWriteMemoryFunc *timer_write[] = {
    &timer_winvalid,
    &timer_winvalid,
    &timer_writel,
};

void etraxfs_timer_init(CPUState *env, qemu_irq *irqs, 
			target_phys_addr_t base)
{
	static struct fs_timer_t *t;
	int timer_regs;

	t = qemu_mallocz(sizeof *t);
	if (!t)
		return;

	t->bh = qemu_bh_new(timer_hit, t);
	t->ptimer = ptimer_init(t->bh);
	t->irq = irqs;
	t->env = env;
	t->base = base;

	timer_regs = cpu_register_io_memory(0, timer_read, timer_write, t);
	cpu_register_physical_memory (base, 0x5c, timer_regs);
}
