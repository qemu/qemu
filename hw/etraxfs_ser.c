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
#include <ctype.h>
#include "hw.h"

#define D(x)

#define RW_TR_DMA_EN 0x04
#define RW_DOUT 0x1c
#define RW_STAT_DIN 0x20
#define R_STAT_DIN 0x24

static uint32_t ser_readb (void *opaque, target_phys_addr_t addr)
{
	D(CPUState *env = opaque);
	D(printf ("%s %x pc=%x\n", __func__, addr, env->pc));
	return 0;
}
static uint32_t ser_readw (void *opaque, target_phys_addr_t addr)
{
	D(CPUState *env = opaque);
	D(printf ("%s %x pc=%x\n", __func__, addr, env->pc));
	return 0;
}

static uint32_t ser_readl (void *opaque, target_phys_addr_t addr)
{
	D(CPUState *env = opaque);
	uint32_t r = 0;

	switch (addr & 0xfff)
	{
		case RW_TR_DMA_EN:
			break;
		case R_STAT_DIN:
			r |= 1 << 24; /* set tr_rdy.  */
			r |= 1 << 22; /* set tr_idle.  */
			break;

		default:
			D(printf ("%s %x p=%x\n", __func__, addr, env->pc));
			break;
	}
	return r;
}

static void
ser_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	D(CPUState *env = opaque);
 	D(printf ("%s %x %x pc=%x\n", __func__, addr, value, env->pc));
}
static void
ser_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	D(CPUState *env = opaque);
	D(printf ("%s %x %x pc=%x\n", __func__, addr, value, env->pc));
}
static void
ser_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	D(CPUState *env = opaque);

	switch (addr & 0xfff)
	{
		case RW_TR_DMA_EN:
			break;
		case RW_DOUT:
			if (isprint(value) || isspace(value))
				putchar(value);
			else
				putchar('.');
			fflush(stdout);
			break;
		default:
			D(printf ("%s %x %x pc=%x\n",
				  __func__, addr, value, env->pc));
			break;
	}
}

static CPUReadMemoryFunc *ser_read[] = {
	&ser_readb,
	&ser_readw,
	&ser_readl,
};

static CPUWriteMemoryFunc *ser_write[] = {
	&ser_writeb,
	&ser_writew,
	&ser_writel,
};

void etraxfs_ser_init(CPUState *env, qemu_irq *irqs, target_phys_addr_t base)
{
	int ser_regs;
	ser_regs = cpu_register_io_memory(0, ser_read, ser_write, env);
	cpu_register_physical_memory (base, 0x3c, ser_regs);
}
