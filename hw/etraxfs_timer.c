/*
 * QEMU ETRAX System Emulator
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

void etrax_ack_irq(CPUState *env, uint32_t mask);

#define R_TIME 0xb001e038
#define RW_TMR0_DIV 0xb001e000
#define R_TMR0_DATA 0xb001e004
#define RW_TMR0_CTRL 0xb001e008
#define RW_TMR1_DIV 0xb001e010
#define R_TMR1_DATA 0xb001e014
#define RW_TMR1_CTRL 0xb001e018

#define RW_INTR_MASK 0xb001e048
#define RW_ACK_INTR 0xb001e04c
#define R_INTR 0xb001e050
#define R_MASKED_INTR 0xb001e054


uint32_t rw_intr_mask;
uint32_t rw_ack_intr;
uint32_t r_intr;

struct fs_timer_t {
	QEMUBH *bh;
	unsigned int limit;
	int scale;
	ptimer_state *ptimer;
	CPUState *env;
	qemu_irq *irq;
	uint32_t mask;
};

static struct fs_timer_t timer0;

/* diff two timevals.  Return a single int in us. */
int diff_timeval_us(struct timeval *a, struct timeval *b)
{
        int diff;

        /* assume these values are signed.  */
        diff = (a->tv_sec - b->tv_sec) * 1000 * 1000;
        diff += (a->tv_usec - b->tv_usec);
        return diff;
}

static uint32_t timer_readb (void *opaque, target_phys_addr_t addr)
{
	CPUState *env = opaque;
	uint32_t r = 0;
	printf ("%s %x pc=%x\n", __func__, addr, env->pc);
	return r;
}
static uint32_t timer_readw (void *opaque, target_phys_addr_t addr)
{
	CPUState *env = opaque;
	uint32_t r = 0;
	printf ("%s %x pc=%x\n", __func__, addr, env->pc);
	return r;
}

static uint32_t timer_readl (void *opaque, target_phys_addr_t addr)
{
	CPUState *env = opaque;
	uint32_t r = 0;

	switch (addr) {
	case R_TMR0_DATA:
		break;
	case R_TMR1_DATA:
		printf ("R_TMR1_DATA\n");
		break;
	case R_TIME:
	{
		static struct timeval last;
		struct timeval now;
		gettimeofday(&now, NULL);
		if (!(last.tv_sec == 0 && last.tv_usec == 0)) {
			r = diff_timeval_us(&now, &last);
			r *= 1000; /* convert to ns.  */
			r++; /* make sure we increase for each call.  */
		}
		last = now;
		break;
	}

	case RW_INTR_MASK:
		r = rw_intr_mask;
		break;
	case R_MASKED_INTR:
		r = r_intr & rw_intr_mask;
		break;
	default:
		printf ("%s %x p=%x\n", __func__, addr, env->pc);
		break;
	}
	return r;
}

static void
timer_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	CPUState *env = opaque;
	printf ("%s %x %x pc=%x\n", __func__, addr, value, env->pc);
}
static void
timer_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	CPUState *env = opaque;
	printf ("%s %x %x pc=%x\n", __func__, addr, value, env->pc);
}

static void write_ctrl(struct fs_timer_t *t, uint32_t v)
{
	int op;
	int freq;
	int freq_hz;

	op = v & 3;
	freq = v >> 2;
	freq_hz = 32000000;

	switch (freq)
	{
	case 0:
	case 1:
		printf ("extern or disabled timer clock?\n");
		break;
	case 4: freq_hz =  29493000; break;
	case 5: freq_hz =  32000000; break;
	case 6: freq_hz =  32768000; break;
	case 7: freq_hz = 100000000; break;
	default:
		abort();
		break;
	}

	printf ("freq_hz=%d limit=%d\n", freq_hz, t->limit);
	t->scale = 0;
	if (t->limit > 2048)
	{
		t->scale = 2048;
		ptimer_set_period(timer0.ptimer, freq_hz / t->scale);
	}

	printf ("op=%d\n", op);
	switch (op)
	{
		case 0:
			printf ("limit=%d %d\n", t->limit, t->limit/t->scale);
			ptimer_set_limit(t->ptimer, t->limit / t->scale, 1);
			break;
		case 1:
			ptimer_stop(t->ptimer);
			break;
		case 2:
			ptimer_run(t->ptimer, 0);
			break;
		default:
			abort();
			break;
	}
}

static void timer_ack_irq(void)
{
	if (!(r_intr & timer0.mask & rw_intr_mask)) {
		qemu_irq_lower(timer0.irq[0]);
		etrax_ack_irq(timer0.env, 1 << 0x1b);
	}
}

static void
timer_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	CPUState *env = opaque;
	printf ("%s %x %x pc=%x\n",
		__func__, addr, value, env->pc);
	switch (addr)
	{
		case RW_TMR0_DIV:
			printf ("RW_TMR0_DIV=%x\n", value);
			timer0.limit = value;
			break;
		case RW_TMR0_CTRL:
			printf ("RW_TMR0_CTRL=%x\n", value);
			write_ctrl(&timer0, value);
			break;
		case RW_TMR1_DIV:
			printf ("RW_TMR1_DIV=%x\n", value);
			break;
		case RW_TMR1_CTRL:
			printf ("RW_TMR1_CTRL=%x\n", value);
			break;
		case RW_INTR_MASK:
			printf ("RW_INTR_MASK=%x\n", value);
			rw_intr_mask = value;
			break;
		case RW_ACK_INTR:
			r_intr &= ~value;
			timer_ack_irq();
			break;
		default:
			printf ("%s %x %x pc=%x\n",
				__func__, addr, value, env->pc);
			break;
	}
}

static CPUReadMemoryFunc *timer_read[] = {
    &timer_readb,
    &timer_readw,
    &timer_readl,
};

static CPUWriteMemoryFunc *timer_write[] = {
    &timer_writeb,
    &timer_writew,
    &timer_writel,
};

static void timer_irq(void *opaque)
{
	struct fs_timer_t *t = opaque;

	r_intr |= t->mask;
	if (t->mask & rw_intr_mask) {
		qemu_irq_raise(t->irq[0]);
	}
}

void etraxfs_timer_init(CPUState *env, qemu_irq *irqs)
{
	int timer_regs;

	timer0.bh = qemu_bh_new(timer_irq, &timer0);
	timer0.ptimer = ptimer_init(timer0.bh);
	timer0.irq = irqs + 0x1b;
	timer0.mask = 1;
	timer0.env = env;

	timer_regs = cpu_register_io_memory(0, timer_read, timer_write, env);
	cpu_register_physical_memory (0xb001e000, 0x5c, timer_regs);
}
