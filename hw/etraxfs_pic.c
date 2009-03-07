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

struct fs_pic_state_t
{
	CPUState *env;

	uint32_t rw_mask;
	/* Active interrupt lines.  */
	uint32_t r_vect;
	/* Active lines, gated through the mask.  */
	uint32_t r_masked_vect;
	uint32_t r_nmi;
	uint32_t r_guru;
};

static void pic_update(struct fs_pic_state_t *fs)
{	
	CPUState *env = fs->env;
	int i;
	uint32_t vector = 0;

	fs->r_masked_vect = fs->r_vect & fs->rw_mask;

	/* The ETRAX interrupt controller signals interrupts to teh core
	   through an interrupt request wire and an irq vector bus. If 
	   multiple interrupts are simultaneously active it chooses vector 
	   0x30 and lets the sw choose the priorities.  */
	if (fs->r_masked_vect) {
		uint32_t mv = fs->r_masked_vect;
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

	switch (addr)
	{
		case 0x0: 
			rval = fs->rw_mask;
			break;
		case 0x4: 
			rval = fs->r_vect;
			break;
		case 0x8: 
			rval = fs->r_masked_vect;
			break;
		case 0xc: 
			rval = fs->r_nmi;
			break;
		case 0x10: 
			rval = fs->r_guru;
			break;
		default:
			cpu_abort(fs->env, "invalid PIC register.\n");
			break;

	}
	D(printf("%s %x=%x\n", __func__, addr, rval));
	return rval;
}

static void
pic_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	struct fs_pic_state_t *fs = opaque;
	D(printf("%s addr=%x val=%x\n", __func__, addr, value));
	switch (addr) 
	{
		case 0x0: 
			fs->rw_mask = value;
			pic_update(fs);
			break;
		default:
			cpu_abort(fs->env, "invalid PIC register.\n");
			break;
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

	D(printf("%s irq=%d level=%d mask=%x v=%x mv=%x\n", 
		 __func__, irq, level,
		 fs->rw_mask, fs->r_vect, fs->r_masked_vect));

	irq -= 1;
	fs->r_vect &= ~(1 << irq);
	fs->r_vect |= (!!level << irq);

	pic_update(fs);
}

static void nmi_handler(void *opaque, int irq, int level)
{	
	struct fs_pic_state_t *fs = (void *)opaque;
	CPUState *env = fs->env;
	uint32_t mask;

	mask = 1 << irq;
	if (level)
		fs->r_nmi |= mask;
	else
		fs->r_nmi &= ~mask;

	if (fs->r_nmi)
		cpu_interrupt(env, CPU_INTERRUPT_NMI);
	else
		cpu_reset_interrupt(env, CPU_INTERRUPT_NMI);
}

static void guru_handler(void *opaque, int irq, int level)
{	
	struct fs_pic_state_t *fs = (void *)opaque;
	CPUState *env = fs->env;
	cpu_abort(env, "%s unsupported exception\n", __func__);

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
	cpu_register_physical_memory(base, 0x14, intr_vect_regs);

	return pic;
}
