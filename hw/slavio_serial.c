/*
 * QEMU Sparc SLAVIO serial port emulation
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
/* debug serial */
//#define DEBUG_SERIAL

/* debug keyboard */
//#define DEBUG_KBD

/* debug mouse */
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

/*
 * Modifications:
 *  2006-Aug-10  Igor Kovalenko :   Renamed KBDQueue to SERIOQueue, implemented
 *                                  serial mouse queue.
 *                                  Implemented serial mouse protocol.
 */

#ifdef DEBUG_SERIAL
#define SER_DPRINTF(fmt, args...) \
do { printf("SER: " fmt , ##args); } while (0)
#define pic_set_irq(irq, level) \
do { printf("SER: set_irq(%d): %d\n", (irq), (level)); pic_set_irq((irq),(level));} while (0)
#else
#define SER_DPRINTF(fmt, args...)
#endif
#ifdef DEBUG_KBD
#define KBD_DPRINTF(fmt, args...) \
do { printf("KBD: " fmt , ##args); } while (0)
#else
#define KBD_DPRINTF(fmt, args...)
#endif
#ifdef DEBUG_MOUSE
#define MS_DPRINTF(fmt, args...) \
do { printf("MSC: " fmt , ##args); } while (0)
#else
#define MS_DPRINTF(fmt, args...)
#endif

typedef enum {
    chn_a, chn_b,
} chn_id_t;

#define CHN_C(s) ((s)->chn == chn_b? 'b' : 'a')

typedef enum {
    ser, kbd, mouse,
} chn_type_t;

#define SERIO_QUEUE_SIZE 256

typedef struct {
    uint8_t data[SERIO_QUEUE_SIZE];
    int rptr, wptr, count;
} SERIOQueue;

typedef struct ChannelState {
    int irq;
    int reg;
    int rxint, txint, rxint_under_svc, txint_under_svc;
    chn_id_t chn; // this channel, A (base+4) or B (base+0)
    chn_type_t type;
    struct ChannelState *otherchn;
    uint8_t rx, tx, wregs[16], rregs[16];
    SERIOQueue queue;
    CharDriverState *chr;
} ChannelState;

struct SerialState {
    struct ChannelState chn[2];
};

#define SERIAL_MAXADDR 7

static void handle_kbd_command(ChannelState *s, int val);
static int serial_can_receive(void *opaque);
static void serial_receive_byte(ChannelState *s, int ch);
static inline void set_txint(ChannelState *s);

static void put_queue(void *opaque, int b)
{
    ChannelState *s = opaque;
    SERIOQueue *q = &s->queue;

    SER_DPRINTF("channel %c put: 0x%02x\n", CHN_C(s), b);
    if (q->count >= SERIO_QUEUE_SIZE)
        return;
    q->data[q->wptr] = b;
    if (++q->wptr == SERIO_QUEUE_SIZE)
        q->wptr = 0;
    q->count++;
    serial_receive_byte(s, 0);
}

static uint32_t get_queue(void *opaque)
{
    ChannelState *s = opaque;
    SERIOQueue *q = &s->queue;
    int val;
    
    if (q->count == 0) {
	return 0;
    } else {
        val = q->data[q->rptr];
        if (++q->rptr == SERIO_QUEUE_SIZE)
            q->rptr = 0;
        q->count--;
    }
    KBD_DPRINTF("channel %c get 0x%02x\n", CHN_C(s), val);
    if (q->count > 0)
	serial_receive_byte(s, 0);
    return val;
}

static int slavio_serial_update_irq_chn(ChannelState *s)
{
    if ((s->wregs[1] & 1) && // interrupts enabled
	(((s->wregs[1] & 2) && s->txint == 1) || // tx ints enabled, pending
	 ((((s->wregs[1] & 0x18) == 8) || ((s->wregs[1] & 0x18) == 0x10)) &&
	  s->rxint == 1) || // rx ints enabled, pending
	 ((s->wregs[15] & 0x80) && (s->rregs[0] & 0x80)))) { // break int e&p
        return 1;
    }
    return 0;
}

static void slavio_serial_update_irq(ChannelState *s)
{
    int irq;

    irq = slavio_serial_update_irq_chn(s);
    irq |= slavio_serial_update_irq_chn(s->otherchn);

    pic_set_irq(s->irq, irq);
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
    s->rxint_under_svc = s->txint_under_svc = 0;
}

static void slavio_serial_reset(void *opaque)
{
    SerialState *s = opaque;
    slavio_serial_reset_chn(&s->chn[0]);
    slavio_serial_reset_chn(&s->chn[1]);
}

static inline void clr_rxint(ChannelState *s)
{
    s->rxint = 0;
    s->rxint_under_svc = 0;
    if (s->chn == chn_a)
        s->rregs[3] &= ~0x20;
    else
        s->otherchn->rregs[3] &= ~4;
    if (s->txint)
        set_txint(s);
    else
        s->rregs[2] = 6;
    slavio_serial_update_irq(s);
}

static inline void set_rxint(ChannelState *s)
{
    s->rxint = 1;
    if (!s->txint_under_svc) {
        s->rxint_under_svc = 1;
        if (s->chn == chn_a)
            s->rregs[3] |= 0x20;
        else
            s->otherchn->rregs[3] |= 4;
        s->rregs[2] = 4;
        slavio_serial_update_irq(s);
    }
}

static inline void clr_txint(ChannelState *s)
{
    s->txint = 0;
    s->txint_under_svc = 0;
    if (s->chn == chn_a)
        s->rregs[3] &= ~0x10;
    else
        s->otherchn->rregs[3] &= ~2;
    if (s->rxint)
        set_rxint(s);
    else
        s->rregs[2] = 6;
    slavio_serial_update_irq(s);
}

static inline void set_txint(ChannelState *s)
{
    s->txint = 1;
    if (!s->rxint_under_svc) {
        s->txint_under_svc = 1;
        if (s->chn == chn_a)
            s->rregs[3] |= 0x10;
        else
            s->otherchn->rregs[3] |= 2;
        s->rregs[2] = 0;
        slavio_serial_update_irq(s);
    }
}

static void slavio_serial_update_parameters(ChannelState *s)
{
    int speed, parity, data_bits, stop_bits;
    QEMUSerialSetParams ssp;

    if (!s->chr || s->type != ser)
        return;

    if (s->wregs[4] & 1) {
        if (s->wregs[4] & 2)
            parity = 'E';
        else
            parity = 'O';
    } else {
        parity = 'N';
    }
    if ((s->wregs[4] & 0x0c) == 0x0c)
        stop_bits = 2;
    else
        stop_bits = 1;
    switch (s->wregs[5] & 0x60) {
    case 0x00:
        data_bits = 5;
        break;
    case 0x20:
        data_bits = 7;
        break;
    case 0x40:
        data_bits = 6;
        break;
    default:
    case 0x60:
        data_bits = 8;
        break;
    }
    speed = 2457600 / ((s->wregs[12] | (s->wregs[13] << 8)) + 2);
    switch (s->wregs[4] & 0xc0) {
    case 0x00:
        break;
    case 0x40:
        speed /= 16;
        break;
    case 0x80:
        speed /= 32;
        break;
    default:
    case 0xc0:
        speed /= 64;
        break;
    }
    ssp.speed = speed;
    ssp.parity = parity;
    ssp.data_bits = data_bits;
    ssp.stop_bits = stop_bits;
    SER_DPRINTF("channel %c: speed=%d parity=%c data=%d stop=%d\n", CHN_C(s),
                speed, parity, data_bits, stop_bits);
    qemu_chr_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
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
	SER_DPRINTF("Write channel %c, reg[%d] = %2.2x\n", CHN_C(s), s->reg, val & 0xff);
	newreg = 0;
	switch (s->reg) {
	case 0:
	    newreg = val & 7;
	    val &= 0x38;
	    switch (val) {
	    case 8:
		newreg |= 0x8;
		break;
	    case 0x28:
                clr_txint(s);
		break;
	    case 0x38:
                if (s->rxint_under_svc)
                    clr_rxint(s);
                else if (s->txint_under_svc)
                    clr_txint(s);
		break;
	    default:
		break;
	    }
	    break;
        case 1 ... 3:
        case 6 ... 8:
        case 10 ... 11:
        case 14 ... 15:
	    s->wregs[s->reg] = val;
	    break;
        case 4:
        case 5:
        case 12:
        case 13:
	    s->wregs[s->reg] = val;
            slavio_serial_update_parameters(s);
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
	SER_DPRINTF("Write channel %c, ch %d\n", CHN_C(s), val);
	if (s->wregs[5] & 8) { // tx enabled
	    s->tx = val;
	    if (s->chr)
		qemu_chr_write(s->chr, &s->tx, 1);
	    else if (s->type == kbd) {
		handle_kbd_command(s, val);
	    }
	    s->rregs[0] |= 4; // Tx buffer empty
	    s->rregs[1] |= 1; // All sent
            set_txint(s);
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
	SER_DPRINTF("Read channel %c, reg[%d] = %2.2x\n", CHN_C(s), s->reg, s->rregs[s->reg]);
	ret = s->rregs[s->reg];
	s->reg = 0;
	return ret;
    case 1:
	s->rregs[0] &= ~1;
        clr_rxint(s);
	if (s->type == kbd || s->type == mouse)
	    ret = get_queue(s);
	else
	    ret = s->rx;
	SER_DPRINTF("Read channel %c, ch %d\n", CHN_C(s), ret);
	return ret;
    default:
	break;
    }
    return 0;
}

static int serial_can_receive(void *opaque)
{
    ChannelState *s = opaque;
    int ret;

    if (((s->wregs[3] & 1) == 0) // Rx not enabled
	|| ((s->rregs[0] & 1) == 1)) // char already available
	ret = 0;
    else
	ret = 1;
    //SER_DPRINTF("channel %c can receive %d\n", CHN_C(s), ret);
    return ret;
}

static void serial_receive_byte(ChannelState *s, int ch)
{
    SER_DPRINTF("channel %c put ch %d\n", CHN_C(s), ch);
    s->rregs[0] |= 1;
    s->rx = ch;
    set_rxint(s);
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
    qemu_put_be32s(f, &s->rxint_under_svc);
    qemu_put_be32s(f, &s->txint_under_svc);
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
    if (version_id > 2)
        return -EINVAL;

    qemu_get_be32s(f, &s->irq);
    qemu_get_be32s(f, &s->reg);
    qemu_get_be32s(f, &s->rxint);
    qemu_get_be32s(f, &s->txint);
    if (version_id >= 2) {
        qemu_get_be32s(f, &s->rxint_under_svc);
        qemu_get_be32s(f, &s->txint_under_svc);
    }
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
    int slavio_serial_io_memory, i;
    SerialState *s;

    s = qemu_mallocz(sizeof(SerialState));
    if (!s)
        return NULL;

    slavio_serial_io_memory = cpu_register_io_memory(0, slavio_serial_mem_read, slavio_serial_mem_write, s);
    cpu_register_physical_memory(base, SERIAL_MAXADDR, slavio_serial_io_memory);

    s->chn[0].chr = chr1;
    s->chn[1].chr = chr2;

    for (i = 0; i < 2; i++) {
	s->chn[i].irq = irq;
	s->chn[i].chn = 1 - i;
	s->chn[i].type = ser;
	if (s->chn[i].chr) {
	    qemu_chr_add_handlers(s->chn[i].chr, serial_can_receive,
                                  serial_receive1, serial_event, &s->chn[i]);
	}
    }
    s->chn[0].otherchn = &s->chn[1];
    s->chn[1].otherchn = &s->chn[0];
    register_savevm("slavio_serial", base, 2, slavio_serial_save, slavio_serial_load, s);
    qemu_register_reset(slavio_serial_reset, s);
    slavio_serial_reset(s);
    return s;
}

static const uint8_t keycodes[128] = {
    127, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 43, 53,
    54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 89, 76, 77, 78,
    79, 80, 81, 82, 83, 84, 85, 86, 87, 42, 99, 88, 100, 101, 102, 103,
    104, 105, 106, 107, 108, 109, 110, 47, 19, 121, 119, 5, 6, 8, 10, 12,
    14, 16, 17, 18, 7, 98, 23, 68, 69, 70, 71, 91, 92, 93, 125, 112,
    113, 114, 94, 50, 0, 0, 124, 9, 11, 0, 0, 0, 0, 0, 0, 0,
    90, 0, 46, 22, 13, 111, 52, 20, 96, 24, 28, 74, 27, 123, 44, 66,
    0, 45, 2, 4, 48, 0, 0, 21, 0, 0, 0, 0, 0, 120, 122, 67,
};

static void sunkbd_event(void *opaque, int ch)
{
    ChannelState *s = opaque;
    int release = ch & 0x80;

    ch = keycodes[ch & 0x7f];
    KBD_DPRINTF("Keycode %d (%s)\n", ch, release? "release" : "press");
    put_queue(s, ch | release);
}

static void handle_kbd_command(ChannelState *s, int val)
{
    KBD_DPRINTF("Command %d\n", val);
    switch (val) {
    case 1: // Reset, return type code
	put_queue(s, 0xff);
	put_queue(s, 5); // Type 5
	break;
    case 7: // Query layout
	put_queue(s, 0xfe);
	put_queue(s, 0x20); // XXX, layout?
	break;
    default:
	break;
    }
}

static void sunmouse_event(void *opaque, 
                               int dx, int dy, int dz, int buttons_state)
{
    ChannelState *s = opaque;
    int ch;

    /* XXX: SDL sometimes generates nul events: we delete them */
    if (dx == 0 && dy == 0 && dz == 0 && buttons_state == 0)
        return;
    MS_DPRINTF("dx=%d dy=%d buttons=%01x\n", dx, dy, buttons_state);

    ch = 0x80 | 0x7; /* protocol start byte, no buttons pressed */

    if (buttons_state & MOUSE_EVENT_LBUTTON)
        ch ^= 0x4;
    if (buttons_state & MOUSE_EVENT_MBUTTON)
        ch ^= 0x2;
    if (buttons_state & MOUSE_EVENT_RBUTTON)
        ch ^= 0x1;

    put_queue(s, ch);

    ch = dx;

    if (ch > 127)
        ch=127;
    else if (ch < -127)
        ch=-127;

    put_queue(s, ch & 0xff);

    ch = -dy;

    if (ch > 127)
        ch=127;
    else if (ch < -127)
        ch=-127;

    put_queue(s, ch & 0xff);

    // MSC protocol specify two extra motion bytes

    put_queue(s, 0);
    put_queue(s, 0);
}

void slavio_serial_ms_kbd_init(int base, int irq)
{
    int slavio_serial_io_memory, i;
    SerialState *s;

    s = qemu_mallocz(sizeof(SerialState));
    if (!s)
        return;
    for (i = 0; i < 2; i++) {
	s->chn[i].irq = irq;
	s->chn[i].chn = 1 - i;
	s->chn[i].chr = NULL;
    }
    s->chn[0].otherchn = &s->chn[1];
    s->chn[1].otherchn = &s->chn[0];
    s->chn[0].type = mouse;
    s->chn[1].type = kbd;

    slavio_serial_io_memory = cpu_register_io_memory(0, slavio_serial_mem_read, slavio_serial_mem_write, s);
    cpu_register_physical_memory(base, SERIAL_MAXADDR, slavio_serial_io_memory);

    qemu_add_mouse_event_handler(sunmouse_event, &s->chn[0], 0, "QEMU Sun Mouse");
    qemu_add_kbd_event_handler(sunkbd_event, &s->chn[1]);
    qemu_register_reset(slavio_serial_reset, s);
    slavio_serial_reset(s);
}
