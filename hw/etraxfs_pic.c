/*
 * QEMU ETRAX Interrupt Controller.
 *
 * Copyright (c) 2008 Edgar E. Iglesias, Axis Communications AB.
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
#include "hw.h"
#include "pc.h"
#include "etraxfs.h"

#define D(x)

#define R_RW_MASK	0
#define R_R_VECT	1
#define R_R_MASKED_VECT	2
#define R_R_NMI		3
#define R_R_GURU	4
#define R_MAX		5

struct fs_pic_state_t
{
	CPUState *env;
	uint32_t regs[R_MAX];
};

static void pic_update(struct fs_pic_state_t *fs)
{	
	CPUState *env = fs->env;
	uint32_t vector = 0;
	int i;

	fs->regs[R_R_MASKED_VECT] = fs->regs[R_R_VECT] & fs->regs[R_RW_MASK];

	/* The ETRAX interrupt controller signals interrupts to teh core
	   through an interrupt request wire and an irq vector bus. If 
	   multiple interrupts are simultaneously active it chooses vector 
	   0x30 and lets the sw choose the priorities.  */
	if (fs->regs[R_R_MASKED_VECT]) {
		uint32_t mv = fs->regs[R_R_MASKED_VECT];
		for (i = 0; i < 31; i++) {
			if (mv & 1) {
				vector = 0x31 + i;
				/* Check for multiple interrupts.  */
				if (mv > 1)
					vector = 0x30;
				break;
			}
			mv >>= 1;
		}
		if (vector) {
			env->interrupt_vector = vector;
			D(printf("%s vector=%x\n", __func__, vector));
			cpu_interrupt(env, CPU_INTERRUPT_HARD);
		}
	} else {
		env->interrupt_vector = 0;
		cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
		D(printf("%s reset irqs\n", __func__));
	}
}

static uint32_t pic_readl (void *opaque, target_phys_addr_t addr)
{
	struct fs_pic_state_t *fs = opaque;
	uint32_t rval;

	rval = fs->regs[addr >> 2];
	D(printf("%s %x=%x\n", __func__, addr, rval));
	return rval;
}

static void
pic_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	struct fs_pic_state_t *fs = opaque;
	D(printf("%s addr=%x val=%x\n", __func__, addr, value));

	if (addr == R_RW_MASK) {
		fs->regs[R_RW_MASK] = value;
		pic_update(fs);
	}
}

static CPUReadMemoryFunc *pic_read[] = {
	NULL, NULL,
	&pic_readl,
};

static CPUWriteMemoryFunc *pic_write[] = {
	NULL, NULL,
	&pic_writel,
};

void pic_info(Monitor *mon)
{
}

void irq_info(Monitor *mon)
{
}

static void irq_handler(void *opaque, int irq, int level)
{	
	struct fs_pic_state_t *fs = (void *)opaque;
	irq -= 1;
	fs->regs[R_R_VECT] &= ~(1 << irq);
	fs->regs[R_R_VECT] |= (!!level << irq);

	pic_update(fs);
}

static void nmi_handler(void *opaque, int irq, int level)
{	
	struct fs_pic_state_t *fs = (void *)opaque;
	CPUState *env = fs->env;
	uint32_t mask;

	mask = 1 << irq;
	if (level)
		fs->regs[R_R_NMI] |= mask;
	else
		fs->regs[R_R_NMI] &= ~mask;

	if (fs->regs[R_R_NMI])
		cpu_interrupt(env, CPU_INTERRUPT_NMI);
	else
		cpu_reset_interrupt(env, CPU_INTERRUPT_NMI);
}

static void guru_handler(void *opaque, int irq, int level)
{	
	struct fs_pic_state_t *fs = (void *)opaque;
	cpu_abort(fs->env, "%s unsupported exception\n", __func__);
}

struct etraxfs_pic *etraxfs_pic_init(CPUState *env, target_phys_addr_t base)
{
	struct fs_pic_state_t *fs = NULL;
	struct etraxfs_pic *pic = NULL;
	int intr_vect_regs;

	pic = qemu_mallocz(sizeof *pic);
	pic->internal = fs = qemu_mallocz(sizeof *fs);

	fs->env = env;
	pic->irq = qemu_allocate_irqs(irq_handler, fs, 30);
	pic->nmi = qemu_allocate_irqs(nmi_handler, fs, 2);
	pic->guru = qemu_allocate_irqs(guru_handler, fs, 1);

	intr_vect_regs = cpu_register_io_memory(0, pic_read, pic_write, fs);
	cpu_register_physical_memory(base, R_MAX * 4, intr_vect_regs);

	return pic;
}
