/*
 * QEMU Sparc SLAVIO serial port emulation
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

//#define DEBUG_SERIAL

/* debug keyboard */
//#define DEBUG_KBD

/* debug keyboard : only mouse */
//#define DEBUG_MOUSE

/*
 * This is the serial port, mouse and keyboard part of chip STP2001
 * (Slave I/O), also produced as NCR89C105. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C105.txt
 * 
 * The serial ports implement full AMD AM8530 or Zilog Z8530 chips,
 * mouse and keyboard ports don't implement all functions and they are
 * only asynchronous. There is no DMA.
 *
 */

typedef struct ChannelState {
    int irq;
    int reg;
    int rxint, txint;
    uint8_t rx, tx, wregs[16], rregs[16];
    CharDriverState *chr;
} ChannelState;

struct SerialState {
    struct ChannelState chn[2];
};

#define SERIAL_MAXADDR 7

static void slavio_serial_update_irq(ChannelState *s)
{
    if ((s->wregs[1] & 1) && // interrupts enabled
	(((s->wregs[1] & 2) && s->txint == 1) || // tx ints enabled, pending
	 ((((s->wregs[1] & 0x18) == 8) || ((s->wregs[1] & 0x18) == 0x10)) &&
	  s->rxint == 1) || // rx ints enabled, pending
	 ((s->wregs[15] & 0x80) && (s->rregs[0] & 0x80)))) { // break int e&p
        pic_set_irq(s->irq, 1);
    } else {
        pic_set_irq(s->irq, 0);
    }
}

static void slavio_serial_reset_chn(ChannelState *s)
{
    int i;

    s->reg = 0;
    for (i = 0; i < SERIAL_MAXADDR; i++) {
	s->rregs[i] = 0;
	s->wregs[i] = 0;
    }
    s->wregs[4] = 4;
    s->wregs[9] = 0xc0;
    s->wregs[11] = 8;
    s->wregs[14] = 0x30;
    s->wregs[15] = 0xf8;
    s->rregs[0] = 0x44;
    s->rregs[1] = 6;

    s->rx = s->tx = 0;
    s->rxint = s->txint = 0;
}

static void slavio_serial_reset(void *opaque)
{
    SerialState *s = opaque;
    slavio_serial_reset_chn(&s->chn[0]);
    slavio_serial_reset_chn(&s->chn[1]);
}

static void slavio_serial_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SerialState *ser = opaque;
    ChannelState *s;
    uint32_t saddr;
    int newreg, channel;

    val &= 0xff;
    saddr = (addr & 3) >> 1;
    channel = (addr & SERIAL_MAXADDR) >> 2;
    s = &ser->chn[channel];
    switch (saddr) {
    case 0:
	newreg = 0;
	switch (s->reg) {
	case 0:
	    newreg = val & 7;
	    val &= 0x38;
	    switch (val) {
	    case 8:
		s->reg |= 0x8;
		break;
	    case 0x20:
		s->rxint = 0;
		break;
	    case 0x28:
		s->txint = 0;
		break;
	    default:
		break;
	    }
	    break;
	case 1 ... 8:
	case 10 ... 15:
	    s->wregs[s->reg] = val;
	    break;
	case 9:
	    switch (val & 0xc0) {
	    case 0:
	    default:
		break;
	    case 0x40:
		slavio_serial_reset_chn(&ser->chn[1]);
		return;
	    case 0x80:
		slavio_serial_reset_chn(&ser->chn[0]);
		return;
	    case 0xc0:
		slavio_serial_reset(ser);
		return;
	    }
	    break;
	default:
	    break;
	}
	if (s->reg == 0)
	    s->reg = newreg;
	else
	    s->reg = 0;
	break;
    case 1:
	if (s->wregs[5] & 8) { // tx enabled
	    s->tx = val;
	    if (s->chr)
		qemu_chr_write(s->chr, &s->tx, 1);
	    s->txint = 1;
	}
	break;
    default:
	break;
    }
}

static uint32_t slavio_serial_mem_readb(void *opaque, target_phys_addr_t addr)
{
    SerialState *ser = opaque;
    ChannelState *s;
    uint32_t saddr;
    uint32_t ret;
    int channel;

    saddr = (addr & 3) >> 1;
    channel = (addr & SERIAL_MAXADDR) >> 2;
    s = &ser->chn[channel];
    switch (saddr) {
    case 0:
	ret = s->rregs[s->reg];
	s->reg = 0;
	return ret;
    case 1:
	s->rregs[0] &= ~1;
	return s->rx;
    default:
	break;
    }
    return 0;
}

static int serial_can_receive(void *opaque)
{
    ChannelState *s = opaque;
    if (((s->wregs[3] & 1) == 0) // Rx not enabled
	|| ((s->rregs[0] & 1) == 1)) // char already available
	return 0;
    else
	return 1;
}

static void serial_receive_byte(ChannelState *s, int ch)
{
    s->rregs[0] |= 1;
    s->rx = ch;
    s->rxint = 1;
    slavio_serial_update_irq(s);
}

static void serial_receive_break(ChannelState *s)
{
    s->rregs[0] |= 0x80;
    slavio_serial_update_irq(s);
}

static void serial_receive1(void *opaque, const uint8_t *buf, int size)
{
    ChannelState *s = opaque;
    serial_receive_byte(s, buf[0]);
}

static void serial_event(void *opaque, int event)
{
    ChannelState *s = opaque;
    if (event == CHR_EVENT_BREAK)
        serial_receive_break(s);
}

static CPUReadMemoryFunc *slavio_serial_mem_read[3] = {
    slavio_serial_mem_readb,
    slavio_serial_mem_readb,
    slavio_serial_mem_readb,
};

static CPUWriteMemoryFunc *slavio_serial_mem_write[3] = {
    slavio_serial_mem_writeb,
    slavio_serial_mem_writeb,
    slavio_serial_mem_writeb,
};

static void slavio_serial_save_chn(QEMUFile *f, ChannelState *s)
{
    qemu_put_be32s(f, &s->irq);
    qemu_put_be32s(f, &s->reg);
    qemu_put_be32s(f, &s->rxint);
    qemu_put_be32s(f, &s->txint);
    qemu_put_8s(f, &s->rx);
    qemu_put_8s(f, &s->tx);
    qemu_put_buffer(f, s->wregs, 16);
    qemu_put_buffer(f, s->rregs, 16);
}

static void slavio_serial_save(QEMUFile *f, void *opaque)
{
    SerialState *s = opaque;

    slavio_serial_save_chn(f, &s->chn[0]);
    slavio_serial_save_chn(f, &s->chn[1]);
}

static int slavio_serial_load_chn(QEMUFile *f, ChannelState *s, int version_id)
{
    if (version_id != 1)
        return -EINVAL;

    qemu_get_be32s(f, &s->irq);
    qemu_get_be32s(f, &s->reg);
    qemu_get_be32s(f, &s->rxint);
    qemu_get_be32s(f, &s->txint);
    qemu_get_8s(f, &s->rx);
    qemu_get_8s(f, &s->tx);
    qemu_get_buffer(f, s->wregs, 16);
    qemu_get_buffer(f, s->rregs, 16);
    return 0;
}

static int slavio_serial_load(QEMUFile *f, void *opaque, int version_id)
{
    SerialState *s = opaque;
    int ret;

    ret = slavio_serial_load_chn(f, &s->chn[0], version_id);
    if (ret != 0)
	return ret;
    ret = slavio_serial_load_chn(f, &s->chn[1], version_id);
    return ret;

}

SerialState *slavio_serial_init(int base, int irq, CharDriverState *chr1, CharDriverState *chr2)
{
    int slavio_serial_io_memory;
    SerialState *s;

    s = qemu_mallocz(sizeof(SerialState));
    if (!s)
        return NULL;
    s->chn[0].irq = irq;
    s->chn[1].irq = irq;
    s->chn[0].chr = chr1;
    s->chn[1].chr = chr2;

    slavio_serial_io_memory = cpu_register_io_memory(0, slavio_serial_mem_read, slavio_serial_mem_write, s);
    cpu_register_physical_memory(base, SERIAL_MAXADDR, slavio_serial_io_memory);

    if (chr1) {
	qemu_chr_add_read_handler(chr1, serial_can_receive, serial_receive1, &s->chn[0]);
	qemu_chr_add_event_handler(chr1, serial_event);
    }
    if (chr2) {
	qemu_chr_add_read_handler(chr2, serial_can_receive, serial_receive1, &s->chn[1]);
	qemu_chr_add_event_handler(chr2, serial_event);
    }
    register_savevm("slavio_serial", base, 1, slavio_serial_save, slavio_serial_load, s);
    qemu_register_reset(slavio_serial_reset, s);
    slavio_serial_reset(s);
    return s;
}

static void sunkbd_event(void *opaque, int ch)
{
    ChannelState *s = opaque;
    // XXX: PC -> Sun Type 5 translation?
    serial_receive_byte(s, ch);
}

static void sunmouse_event(void *opaque, 
                               int dx, int dy, int dz, int buttons_state)
{
    ChannelState *s = opaque;
    int ch;

    // XXX
    ch = 0x42;
    serial_receive_byte(s, ch);
}

void slavio_serial_ms_kbd_init(int base, int irq)
{
    int slavio_serial_io_memory;
    SerialState *s;

    s = qemu_mallocz(sizeof(SerialState));
    if (!s)
        return;
    s->chn[0].irq = irq;
    s->chn[1].irq = irq;
    s->chn[0].chr = NULL;
    s->chn[1].chr = NULL;

    slavio_serial_io_memory = cpu_register_io_memory(0, slavio_serial_mem_read, slavio_serial_mem_write, s);
    cpu_register_physical_memory(base, SERIAL_MAXADDR, slavio_serial_io_memory);

    qemu_add_kbd_event_handler(sunkbd_event, &s->chn[0]);
    qemu_add_mouse_event_handler(sunmouse_event, &s->chn[1]);
    qemu_register_reset(slavio_serial_reset, s);
    slavio_serial_reset(s);
}
