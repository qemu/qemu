/*
 * QEMU 16450 / 16550 UART emulation
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
#include <assert.h>     /* assert */
#include "hw.h"
#include "qemu-char.h"
#include "isa.h"
#include "pc.h"

#define SERIAL_VERSION 2

#define CONFIG_16550A

//~ #define DEBUG_SERIAL

#if defined(DEBUG_SERIAL)
# define logout(fmt, ...) fprintf(stderr, "UART\t%-24s" fmt, __func__, ##__VA_ARGS__)
#else
# define logout(fmt, ...) ((void)0)
#endif

#define UART_LCR_DLAB	0x80	/* Divisor latch access bit */

//~ #define UART_IER_BIT7	0x80	/* FIFOs enabled */
//~ #define UART_IER_BIT6	0x40	/* FIFOs enabled */
#define UART_IER_MSI	0x08	/* Enable Modem status interrupt */
#define UART_IER_RLSI	0x04	/* Enable receiver line status interrupt */
#define UART_IER_THRI	0x02	/* Enable Transmitter holding register int. */
#define UART_IER_RDI	0x01	/* Enable receiver data interrupt */

#define UART_IIR_NO_INT	0x01	/* No interrupts pending */
#define UART_IIR_ID	0x06	/* Mask for the interrupt ID */

#define UART_IIR_MSI	0x00	/* Modem status interrupt */
#define UART_IIR_THRI	0x02	/* Transmitter holding register empty */
#define UART_IIR_RDI	0x04	/* Receiver data interrupt */
#define UART_IIR_RLSI	0x06	/* Receiver line status interrupt */
#if defined(CONFIG_16550A)
#define UART_IIR_CTI    0x0C    /* Character Timeout Indication */

#define UART_IIR_FENF   0x80    /* Fifo enabled, but not functioning */
#define UART_IIR_FE     0xC0    /* Fifo enabled */
#endif

/*
 * These are the definitions for the Modem Control Register
 */
#define UART_MCR_LOOP	0x10	/* Enable loopback test mode */
#define UART_MCR_OUT2	0x08	/* Out2 complement */
#define UART_MCR_OUT1	0x04	/* Out1 complement */
#define UART_MCR_RTS	0x02	/* RTS complement */
#define UART_MCR_DTR	0x01	/* DTR complement */

/*
 * These are the definitions for the Modem Status Register
 */
#define UART_MSR_DCD	0x80	/* Data Carrier Detect */
#define UART_MSR_RI	0x40	/* Ring Indicator */
#define UART_MSR_DSR	0x20	/* Data Set Ready */
#define UART_MSR_CTS	0x10	/* Clear to Send */
#define UART_MSR_DDCD	0x08	/* Delta DCD */
#define UART_MSR_TERI	0x04	/* Trailing edge ring indicator */
#define UART_MSR_DDSR	0x02	/* Delta DSR */
#define UART_MSR_DCTS	0x01	/* Delta CTS */
#define UART_MSR_ANY_DELTA 0x0F	/* Any of the delta bits! */

#define UART_LSR_TEMT	0x40	/* Transmitter empty */
#define UART_LSR_THRE	0x20	/* Transmit-hold-register empty */
#define UART_LSR_BI	0x10	/* Break interrupt indicator */
#define UART_LSR_FE	0x08	/* Frame error indicator */
#define UART_LSR_PE	0x04	/* Parity error indicator */
#define UART_LSR_OE	0x02	/* Overrun error indicator */
#define UART_LSR_DR	0x01	/* Receiver data ready */

#if defined(CONFIG_16550A)

/*
 * These are the definitions for the Fifo Control Register
 */

#define UART_FCR_ITL_MASQ   0xC0 /* Masq for Interrupt Trigger Level */

#define UART_FCR_ITL_1      0x00 /* 1 byte Interrupt Trigger Level */
#define UART_FCR_ITL_4      0x40 /* 4 bytes Interrupt Trigger Level */
#define UART_FCR_ITL_8      0x80 /* 8 bytes Interrupt Trigger Level */
#define UART_FCR_ITL_14     0xC0 /* 14 bytes Interrupt Trigger Level */

#define UART_FCR_DMS        0x08    /* DMA Mode Select */
#define UART_FCR_XFR        0x04    /* XMIT Fifo Reset */
#define UART_FCR_RFR        0x02    /* RCVR Fifo Reset */
#define UART_FCR_FE         0x01    /* FIFO Enable */

#define UART_FIFO_LENGTH    16     /* 16550A Fifo Length */

#endif

static int serial_instance;

typedef enum {
  uart16450,
  uart16550,
} emulation_t;

struct SerialState {
    uint16_t divider;
    uint8_t rbr; /* receive register */
    uint8_t ier;
    uint8_t iir; /* read only */
    uint8_t fcr; /* write only */
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr; /* read only */
    uint8_t msr; /* read only */
    uint8_t scr;
    /* NOTE: this hidden state is necessary for tx irq generation as
       it can be reset while reading iir */
    int thr_ipending;
    qemu_irq irq;
    CharDriverState *chr;
    int last_break_enable;
    target_ulong base;
    //~ int it_shift;
    emulation_t emulation;
    uint32_t frequency;
    uint8_t  fifo[UART_FIFO_LENGTH];
};

static void serial_update_irq(SerialState *s)
{
    if ((s->lsr & UART_LSR_DR) && (s->ier & UART_IER_RDI)) {
        logout("rx interrupt\n");
        s->iir = UART_IIR_RDI;
        qemu_irq_raise(s->irq);
    } else if (s->thr_ipending && (s->ier & UART_IER_THRI)) {
        logout("tx interrupt\n");
        s->iir = UART_IIR_THRI;
        qemu_irq_raise(s->irq);
    } else {
        logout("no interrupt\n");
        s->iir = UART_IIR_NO_INT;
        qemu_irq_lower(s->irq);
    }
}

static void serial_update_parameters(SerialState *s)
{
    int speed, parity, data_bits, stop_bits;
    QEMUSerialSetParams ssp;

    if (s->lcr & 0x08) {
        if (s->lcr & 0x10)
            parity = 'E';
        else
            parity = 'O';
    } else {
            parity = 'N';
    }
    if (s->lcr & 0x04) 
        stop_bits = 2;
    else
        stop_bits = 1;
    data_bits = (s->lcr & 0x03) + 5;
    if (s->divider == 0)
        return;
    speed = s->frequency / s->divider;
    ssp.speed = speed;
    ssp.parity = parity;
    ssp.data_bits = data_bits;
    ssp.stop_bits = stop_bits;
    qemu_chr_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
#if 1
    fprintf(stderr,
           "uart irq=%p divider=%d speed=%d parity=%c data=%d stop=%d (%s)\n", 
           s->irq, s->divider, speed, parity, data_bits, stop_bits,
           mips_backtrace());
#endif
}

void serial_write(void *opaque, uint32_t addr, uint32_t val)
{
    SerialState *s = opaque;
    unsigned char ch;
    
    addr -= s->base;
    assert(addr < 8);
    logout("addr=0x%02x val=0x%02x\n", addr, val);
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0xff00) | val;
            serial_update_parameters(s);
        } else {
            s->thr_ipending = 0;
            s->lsr &= ~UART_LSR_THRE;
            serial_update_irq(s);
            ch = val;
            qemu_chr_write(s->chr, &ch, 1);
            s->thr_ipending = 1;
            s->lsr |= UART_LSR_THRE;
            s->lsr |= UART_LSR_TEMT;
            serial_update_irq(s);
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0x00ff) | (val << 8);
            serial_update_parameters(s);
        } else {
            s->ier = val & 0x0f;
            if (s->lsr & UART_LSR_THRE) {
                s->thr_ipending = 1;
            }
            serial_update_irq(s);
        }
        break;
    case 2:
        /* fifo control register */
        if (s->emulation == uart16550) {
            if (!(s->fcr & 0x01) && (val & 0x01)) {
                logout("enable fifo\n");
            } else if ((s->fcr & 0x01) && !(val & 0x01)) {
                logout("disable fifo\n");
                memset(s->fifo, 0, sizeof(s->fifo));
            }
            if (val & UART_FCR_FE) {
                s->iir |= UART_IIR_FE;
            } else {
                s->iir &= ~UART_IIR_FE;
            }
            s->fcr = val;
            s->thr_ipending = 1;
            //~ s->lsr |= UART_LSR_THRE;
            //~ s->lsr |= UART_LSR_TEMT;
            serial_update_irq(s);
        }
        break;
    case 3:
        {
            int break_enable;
            s->lcr = val;
            serial_update_parameters(s);
            break_enable = (val >> 6) & 1;
            if (break_enable != s->last_break_enable) {
                s->last_break_enable = break_enable;
                qemu_chr_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_BREAK, 
                               &break_enable);
            }
        }
        break;
    case 4:
        s->mcr = val & 0x1f;
        break;
    case 5:
        break;
    case 6:
        break;
    case 7:
        s->scr = val;
        break;
    }
}

uint32_t serial_read(void *opaque, uint32_t addr)
{
    SerialState *s = opaque;
    uint32_t ret;

    addr -= s->base;
    assert(addr < 8);
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            ret = s->divider & 0xff; 
        } else {
            ret = s->rbr;
            s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
            serial_update_irq(s);
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            ret = (s->divider >> 8) & 0xff;
        } else {
            ret = s->ier;
        }
        break;
    case 2:
        ret = s->iir;
        /* reset THR pending bit */
        if ((ret & 0x7) == UART_IIR_THRI)
            s->thr_ipending = 0;
        serial_update_irq(s);
        break;
    case 3:
        ret = s->lcr;
        break;
    case 4:
        ret = s->mcr;
        break;
    case 5:
        ret = s->lsr;
        break;
    case 6:
        if (s->mcr & UART_MCR_LOOP) {
            /* in loopback, the modem output pins are connected to the
               inputs */
            ret = (s->mcr & 0x0c) << 4;
            ret |= (s->mcr & 0x02) << 3;
            ret |= (s->mcr & 0x01) << 5;
        } else {
            ret = s->msr;
        }
        break;
    case 7:
        ret = s->scr;
        break;
    }
    logout("addr=0x%02x val=0x%02x\n", addr, ret);
    return ret;
}

static int serial_can_receive(SerialState *s)
{
    return !(s->lsr & UART_LSR_DR);
}

static void serial_receive_byte(SerialState *s, int ch)
{
    s->rbr = ch;
    s->lsr |= UART_LSR_DR;
    serial_update_irq(s);
}

static void serial_receive_break(SerialState *s)
{
    s->rbr = 0;
    s->lsr |= UART_LSR_BI | UART_LSR_DR;
    serial_update_irq(s);
}

static int serial_can_receive1(void *opaque)
{
    SerialState *s = opaque;
    return serial_can_receive(s);
}

static void serial_receive1(void *opaque, const uint8_t *buf, int size)
{
    SerialState *s = opaque;
    serial_receive_byte(s, buf[0]);
}

static void serial_event(void *opaque, int event)
{
    SerialState *s = opaque;
    if (event == CHR_EVENT_BREAK)
        serial_receive_break(s);
}

static void serial_save(QEMUFile *f, void *opaque)
{
    SerialState *s = opaque;

    qemu_put_be16s(f,&s->divider);
    qemu_put_8s(f,&s->rbr);
    qemu_put_8s(f,&s->ier);
    qemu_put_8s(f,&s->iir);
    qemu_put_8s(f,&s->lcr);
    qemu_put_8s(f,&s->mcr);
    qemu_put_8s(f,&s->lsr);
    qemu_put_8s(f,&s->msr);
    qemu_put_8s(f,&s->scr);
}

static int serial_load(QEMUFile *f, void *opaque, int version_id)
{
    SerialState *s = opaque;

    if(version_id > SERIAL_VERSION)
        return -EINVAL;

    if(version_id >= SERIAL_VERSION)
        qemu_get_be16s(f,&s->divider);
    else
        s->divider = qemu_get_byte(f);
    qemu_get_8s(f,&s->rbr);
    qemu_get_8s(f,&s->ier);
    qemu_get_8s(f,&s->iir);
    qemu_get_8s(f,&s->lcr);
    qemu_get_8s(f,&s->mcr);
    qemu_get_8s(f,&s->lsr);
    qemu_get_8s(f,&s->msr);
    qemu_get_8s(f,&s->scr);

    return 0;
}

static void serial_reset(void *opaque)
{
    SerialState *s = (SerialState *)opaque;
    s->ier = 0;
    s->iir = UART_IIR_NO_INT;
    s->fcr = 0;
    s->lcr = 0;
    s->mcr = 0;
    s->lsr = UART_LSR_TEMT | UART_LSR_THRE;
    s->msr = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;
}

void serial_frequency(SerialState *s, uint32_t frequency)
{
    s->frequency = frequency;
}

/* If fd is zero, it means that the serial device uses the console */
SerialState *serial_16450_init(int base, qemu_irq irq, CharDriverState *chr)
{
    SerialState *s;

    fprintf(stderr, "%s:%u\n", __FILE__, __LINE__);

    s = qemu_mallocz(sizeof(SerialState));
    if (!s)
        return NULL;
    s->irq = irq;
    s->base = base;
    s->emulation = uart16450;
    s->frequency = 115200;
    serial_reset(s);

    register_savevm("serial", serial_instance, SERIAL_VERSION,
                    serial_save, serial_load, s);
    serial_instance++;

    if (base != 0) {
        register_ioport_write(base, 8, 1, serial_write, s);
        register_ioport_read(base, 8, 1, serial_read, s);
    }

    s->chr = chr;
    qemu_chr_add_handlers(chr, serial_can_receive1, serial_receive1,
                          serial_event, s);
    qemu_register_reset(serial_reset, s);

    return s;
}

SerialState *serial_16550_init(int base, qemu_irq irq, CharDriverState *chr)
{
    SerialState *s;
    s = serial_16450_init(base, irq, chr);
    s->emulation = uart16550;
    return s;
}
