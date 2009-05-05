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
#include "etraxfs.h"

#define D(x)

#define RW_TR_CTRL     (0x00 / 4)
#define RW_TR_DMA_EN   (0x04 / 4)
#define RW_REC_CTRL    (0x08 / 4)
#define RW_DOUT        (0x1c / 4)
#define RS_STAT_DIN    (0x20 / 4)
#define R_STAT_DIN     (0x24 / 4)
#define RW_INTR_MASK   (0x2c / 4)
#define RW_ACK_INTR    (0x30 / 4)
#define R_INTR         (0x34 / 4)
#define R_MASKED_INTR  (0x38 / 4)
#define R_MAX          (0x3c / 4)

#define STAT_DAV     16
#define STAT_TR_IDLE 22
#define STAT_TR_RDY  24

struct etrax_serial
{
	CPUState *env;
	CharDriverState *chr;
	qemu_irq *irq;

	/* This pending thing is a hack.  */
	int pending_tx;

	/* Control registers.  */
	uint32_t regs[R_MAX];
};

static void ser_update_irq(struct etrax_serial *s)
{
	s->regs[R_INTR] &= ~(s->regs[RW_ACK_INTR]);
	s->regs[R_MASKED_INTR] = s->regs[R_INTR] & s->regs[RW_INTR_MASK];

	qemu_set_irq(s->irq[0], !!s->regs[R_MASKED_INTR]);
	s->regs[RW_ACK_INTR] = 0;
}

static uint32_t ser_readl (void *opaque, target_phys_addr_t addr)
{
	struct etrax_serial *s = opaque;
	D(CPUState *env = s->env);
	uint32_t r = 0;

	addr >>= 2;
	switch (addr)
	{
		case R_STAT_DIN:
			r = s->regs[RS_STAT_DIN];
			break;
		case RS_STAT_DIN:
			r = s->regs[addr];
			/* Read side-effect: clear dav.  */
			s->regs[addr] &= ~(1 << STAT_DAV);
			break;
		default:
			r = s->regs[addr];
			D(printf ("%s %x=%x\n", __func__, addr, r));
			break;
	}
	return r;
}

static void
ser_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	struct etrax_serial *s = opaque;
	unsigned char ch = value;
	D(CPUState *env = s->env);

	D(printf ("%s %x %x\n",  __func__, addr, value));
	addr >>= 2;
	switch (addr)
	{
		case RW_DOUT:
			qemu_chr_write(s->chr, &ch, 1);
			s->regs[R_INTR] |= 1;
			s->pending_tx = 1;
			s->regs[addr] = value;
			break;
		case RW_ACK_INTR:
			s->regs[addr] = value;
			if (s->pending_tx && (s->regs[addr] & 1)) {
				s->regs[R_INTR] |= 1;
				s->pending_tx = 0;
				s->regs[addr] &= ~1;
			}
			break;
		default:
			s->regs[addr] = value;
			break;
	}
	ser_update_irq(s);
}

static CPUReadMemoryFunc *ser_read[] = {
	NULL, NULL,
	&ser_readl,
};

static CPUWriteMemoryFunc *ser_write[] = {
	NULL, NULL,
	&ser_writel,
};

static void serial_receive(void *opaque, const uint8_t *buf, int size)
{
	struct etrax_serial *s = opaque;

	s->regs[R_INTR] |= 8;
	s->regs[RS_STAT_DIN] &= ~0xff;
	s->regs[RS_STAT_DIN] |= (buf[0] & 0xff);
	s->regs[RS_STAT_DIN] |= (1 << STAT_DAV); /* dav.  */
	ser_update_irq(s);
}

static int serial_can_receive(void *opaque)
{
	struct etrax_serial *s = opaque;
	int r;

	/* Is the receiver enabled?  */
	r = s->regs[RW_REC_CTRL] & 1;

	/* Pending rx data?  */
	r |= !(s->regs[R_INTR] & 8);
	return r;
}

static void serial_event(void *opaque, int event)
{

}

void etraxfs_ser_init(CPUState *env, qemu_irq *irq, CharDriverState *chr,
		      target_phys_addr_t base)
{
	struct etrax_serial *s;
	int ser_regs;

	s = qemu_mallocz(sizeof *s);

	s->env = env;
	s->irq = irq;
	s->chr = chr;

	/* transmitter begins ready and idle.  */
	s->regs[RS_STAT_DIN] |= (1 << STAT_TR_RDY);
	s->regs[RS_STAT_DIN] |= (1 << STAT_TR_IDLE);

	qemu_chr_add_handlers(chr, serial_can_receive, serial_receive,
			      serial_event, s);

	ser_regs = cpu_register_io_memory(0, ser_read, ser_write, s);
	cpu_register_physical_memory (base, R_MAX * 4, ser_regs);
}
