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
#include "qemu-char.h"

#define D(x)

#define RW_TR_CTRL     0x00
#define RW_TR_DMA_EN   0x04
#define RW_REC_CTRL    0x08
#define RW_DOUT        0x1c
#define RS_STAT_DIN    0x20
#define R_STAT_DIN     0x24
#define RW_INTR_MASK   0x2c
#define RW_ACK_INTR    0x30
#define R_INTR         0x34
#define R_MASKED_INTR  0x38

#define STAT_DAV     16
#define STAT_TR_IDLE 22
#define STAT_TR_RDY  24

struct etrax_serial_t
{
	CPUState *env;
	CharDriverState *chr;
	qemu_irq *irq;

	int pending_tx;

	/* Control registers.  */
	uint32_t rw_tr_ctrl;
	uint32_t rw_tr_dma_en;
	uint32_t rw_rec_ctrl;
	uint32_t rs_stat_din;
	uint32_t r_stat_din;
	uint32_t rw_intr_mask;
	uint32_t rw_ack_intr;
	uint32_t r_intr;
	uint32_t r_masked_intr;
};

static void ser_update_irq(struct etrax_serial_t *s)
{
	uint32_t o_irq = s->r_masked_intr;

	s->r_intr &= ~(s->rw_ack_intr);
	s->r_masked_intr = s->r_intr & s->rw_intr_mask;

	if (o_irq != s->r_masked_intr) {
		D(printf("irq_mask=%x r_intr=%x rmi=%x airq=%x \n", 
			 s->rw_intr_mask, s->r_intr, 
			 s->r_masked_intr, s->rw_ack_intr));
		if (s->r_masked_intr)
			qemu_irq_raise(s->irq[0]);
		else
			qemu_irq_lower(s->irq[0]);
	}
	s->rw_ack_intr = 0;
}


static uint32_t ser_readb (void *opaque, target_phys_addr_t addr)
{
	D(CPUState *env = opaque);
	D(printf ("%s %x\n", __func__, addr));
	return 0;
}

static uint32_t ser_readl (void *opaque, target_phys_addr_t addr)
{
	struct etrax_serial_t *s = opaque;
	D(CPUState *env = s->env);
	uint32_t r = 0;

	switch (addr)
	{
		case RW_TR_CTRL:
			r = s->rw_tr_ctrl;
			break;
		case RW_TR_DMA_EN:
			r = s->rw_tr_dma_en;
			break;
		case RS_STAT_DIN:
			r = s->rs_stat_din;
			/* clear dav.  */
			s->rs_stat_din &= ~(1 << STAT_DAV);
			break;
		case R_STAT_DIN:
			r = s->rs_stat_din;
			break;
		case RW_ACK_INTR:
			D(printf("load rw_ack_intr=%x\n", s->rw_ack_intr));
			r = s->rw_ack_intr;
			break;
		case RW_INTR_MASK:
			r = s->rw_intr_mask;
			break;
		case R_INTR:
			D(printf("load r_intr=%x\n", s->r_intr));
			r = s->r_intr;
			break;
		case R_MASKED_INTR:
			D(printf("load r_maked_intr=%x\n", s->r_masked_intr));
			r = s->r_masked_intr;
			break;

		default:
			D(printf ("%s %x\n", __func__, addr));
			break;
	}
	return r;
}

static void
ser_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	D(struct etrax_serial_t *s = opaque);
	D(CPUState *env = s->env);
 	D(printf ("%s %x %x\n", __func__, addr, value));
}
static void
ser_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	struct etrax_serial_t *s = opaque;
	unsigned char ch = value;
	D(CPUState *env = s->env);

	switch (addr)
	{
		case RW_TR_CTRL:
			D(printf("rw_tr_ctrl=%x\n", value));
			s->rw_tr_ctrl = value;
			break;
		case RW_TR_DMA_EN:
			D(printf("rw_tr_dma_en=%x\n", value));
			s->rw_tr_dma_en = value;
			break;
		case RW_DOUT:
			qemu_chr_write(s->chr, &ch, 1);
			s->r_intr |= 1;
			s->pending_tx = 1;
			break;
		case RW_ACK_INTR:
			D(printf("rw_ack_intr=%x\n", value));
			s->rw_ack_intr = value;
			if (s->pending_tx && (s->rw_ack_intr & 1)) {
				s->r_intr |= 1;
				s->pending_tx = 0;
				s->rw_ack_intr &= ~1;
			}
			break;
		case RW_INTR_MASK:
			D(printf("r_intr_mask=%x\n", value));
			s->rw_intr_mask = value;
			break;
		default:
			D(printf ("%s %x %x\n",  __func__, addr, value));
			break;
	}
	ser_update_irq(s);
}

static CPUReadMemoryFunc *ser_read[] = {
	&ser_readb,
	&ser_readb,
	&ser_readl,
};

static CPUWriteMemoryFunc *ser_write[] = {
	&ser_writeb,
	&ser_writeb,
	&ser_writel,
};

static void serial_receive(void *opaque, const uint8_t *buf, int size)
{
	struct etrax_serial_t *s = opaque;

	s->r_intr |= 8;
	s->rs_stat_din &= ~0xff;
	s->rs_stat_din |= (buf[0] & 0xff);
	s->rs_stat_din |= (1 << STAT_DAV); /* dav.  */
	ser_update_irq(s);
}

static int serial_can_receive(void *opaque)
{
	struct etrax_serial_t *s = opaque;
	int r;

	/* Is the receiver enabled?  */
	r = s->rw_rec_ctrl & 1;

	/* Pending rx data?  */
	r |= !(s->r_intr & 8);
	return r;
}

static void serial_event(void *opaque, int event)
{

}

void etraxfs_ser_init(CPUState *env, qemu_irq *irq, CharDriverState *chr,
		      target_phys_addr_t base)
{
	struct etrax_serial_t *s;
	int ser_regs;

	s = qemu_mallocz(sizeof *s);

	s->env = env;
	s->irq = irq;
	s->chr = chr;

	/* transmitter begins ready and idle.  */
	s->rs_stat_din |= (1 << STAT_TR_RDY);
	s->rs_stat_din |= (1 << STAT_TR_IDLE);

	qemu_chr_add_handlers(chr, serial_can_receive, serial_receive,
			      serial_event, s);

	ser_regs = cpu_register_io_memory(0, ser_read, ser_write, s);
	cpu_register_physical_memory (base, 0x3c, ser_regs);
}
