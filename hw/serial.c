/*
 * QEMU 16550A UART emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2008 Citrix Systems, Inc.
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
#include "hw.h"
#include "qemu-char.h"
#include "isa.h"
#include "pc.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "exec-memory.h"

//#define DEBUG_SERIAL

#define UART_LCR_DLAB	0x80	/* Divisor latch access bit */

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
#define UART_IIR_CTI    0x0C    /* Character Timeout Indication */

#define UART_IIR_FENF   0x80    /* Fifo enabled, but not functionning */
#define UART_IIR_FE     0xC0    /* Fifo enabled */

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
#define UART_LSR_INT_ANY 0x1E	/* Any of the lsr-interrupt-triggering status bits */

/* Interrupt trigger levels. The byte-counts are for 16550A - in newer UARTs the byte-count for each ITL is higher. */

#define UART_FCR_ITL_1      0x00 /* 1 byte ITL */
#define UART_FCR_ITL_2      0x40 /* 4 bytes ITL */
#define UART_FCR_ITL_3      0x80 /* 8 bytes ITL */
#define UART_FCR_ITL_4      0xC0 /* 14 bytes ITL */

#define UART_FCR_DMS        0x08    /* DMA Mode Select */
#define UART_FCR_XFR        0x04    /* XMIT Fifo Reset */
#define UART_FCR_RFR        0x02    /* RCVR Fifo Reset */
#define UART_FCR_FE         0x01    /* FIFO Enable */

#define UART_FIFO_LENGTH    16      /* 16550A Fifo Length */

#define XMIT_FIFO           0
#define RECV_FIFO           1
#define MAX_XMIT_RETRY      4

#ifdef DEBUG_SERIAL
#define DPRINTF(fmt, ...) \
do { fprintf(stderr, "serial: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
do {} while (0)
#endif

typedef struct SerialFIFO {
    uint8_t data[UART_FIFO_LENGTH];
    uint8_t count;
    uint8_t itl;                        /* Interrupt Trigger Level */
    uint8_t tail;
    uint8_t head;
} SerialFIFO;

struct SerialState {
    uint16_t divider;
    uint8_t rbr; /* receive register */
    uint8_t thr; /* transmit holding register */
    uint8_t tsr; /* transmit shift register */
    uint8_t ier;
    uint8_t iir; /* read only */
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr; /* read only */
    uint8_t msr; /* read only */
    uint8_t scr;
    uint8_t fcr;
    uint8_t fcr_vmstate; /* we can't write directly this value
                            it has side effects */
    /* NOTE: this hidden state is necessary for tx irq generation as
       it can be reset while reading iir */
    int thr_ipending;
    qemu_irq irq;
    CharDriverState *chr;
    int last_break_enable;
    int it_shift;
    int baudbase;
    int tsr_retry;

    uint64_t last_xmit_ts;              /* Time when the last byte was successfully sent out of the tsr */
    SerialFIFO recv_fifo;
    SerialFIFO xmit_fifo;

    struct QEMUTimer *fifo_timeout_timer;
    int timeout_ipending;                   /* timeout interrupt pending state */
    struct QEMUTimer *transmit_timer;


    uint64_t char_transmit_time;               /* time to transmit a char in ticks*/
    int poll_msl;

    struct QEMUTimer *modem_status_poll;
    MemoryRegion io;
};

typedef struct ISASerialState {
    ISADevice dev;
    uint32_t index;
    uint32_t iobase;
    uint32_t isairq;
    SerialState state;
} ISASerialState;

static void serial_receive1(void *opaque, const uint8_t *buf, int size);

static void fifo_clear(SerialState *s, int fifo)
{
    SerialFIFO *f = (fifo) ? &s->recv_fifo : &s->xmit_fifo;
    memset(f->data, 0, UART_FIFO_LENGTH);
    f->count = 0;
    f->head = 0;
    f->tail = 0;
}

static int fifo_put(SerialState *s, int fifo, uint8_t chr)
{
    SerialFIFO *f = (fifo) ? &s->recv_fifo : &s->xmit_fifo;

    /* Receive overruns do not overwrite FIFO contents. */
    if (fifo == XMIT_FIFO || f->count < UART_FIFO_LENGTH) {

        f->data[f->head++] = chr;

        if (f->head == UART_FIFO_LENGTH)
            f->head = 0;
    }

    if (f->count < UART_FIFO_LENGTH)
        f->count++;
    else if (fifo == RECV_FIFO)
        s->lsr |= UART_LSR_OE;

    return 1;
}

static uint8_t fifo_get(SerialState *s, int fifo)
{
    SerialFIFO *f = (fifo) ? &s->recv_fifo : &s->xmit_fifo;
    uint8_t c;

    if(f->count == 0)
        return 0;

    c = f->data[f->tail++];
    if (f->tail == UART_FIFO_LENGTH)
        f->tail = 0;
    f->count--;

    return c;
}

static void serial_update_irq(SerialState *s)
{
    uint8_t tmp_iir = UART_IIR_NO_INT;

    if ((s->ier & UART_IER_RLSI) && (s->lsr & UART_LSR_INT_ANY)) {
        tmp_iir = UART_IIR_RLSI;
    } else if ((s->ier & UART_IER_RDI) && s->timeout_ipending) {
        /* Note that(s->ier & UART_IER_RDI) can mask this interrupt,
         * this is not in the specification but is observed on existing
         * hardware.  */
        tmp_iir = UART_IIR_CTI;
    } else if ((s->ier & UART_IER_RDI) && (s->lsr & UART_LSR_DR) &&
               (!(s->fcr & UART_FCR_FE) ||
                s->recv_fifo.count >= s->recv_fifo.itl)) {
        tmp_iir = UART_IIR_RDI;
    } else if ((s->ier & UART_IER_THRI) && s->thr_ipending) {
        tmp_iir = UART_IIR_THRI;
    } else if ((s->ier & UART_IER_MSI) && (s->msr & UART_MSR_ANY_DELTA)) {
        tmp_iir = UART_IIR_MSI;
    }

    s->iir = tmp_iir | (s->iir & 0xF0);

    if (tmp_iir != UART_IIR_NO_INT) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void serial_update_parameters(SerialState *s)
{
    int speed, parity, data_bits, stop_bits, frame_size;
    QEMUSerialSetParams ssp;

    if (s->divider == 0)
        return;

    /* Start bit. */
    frame_size = 1;
    if (s->lcr & 0x08) {
        /* Parity bit. */
        frame_size++;
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
    frame_size += data_bits + stop_bits;
    speed = s->baudbase / s->divider;
    ssp.speed = speed;
    ssp.parity = parity;
    ssp.data_bits = data_bits;
    ssp.stop_bits = stop_bits;
    s->char_transmit_time =  (get_ticks_per_sec() / speed) * frame_size;
    qemu_chr_fe_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);

    DPRINTF("speed=%d parity=%c data=%d stop=%d\n",
           speed, parity, data_bits, stop_bits);
}

static void serial_update_msl(SerialState *s)
{
    uint8_t omsr;
    int flags;

    qemu_del_timer(s->modem_status_poll);

    if (qemu_chr_fe_ioctl(s->chr,CHR_IOCTL_SERIAL_GET_TIOCM, &flags) == -ENOTSUP) {
        s->poll_msl = -1;
        return;
    }

    omsr = s->msr;

    s->msr = (flags & CHR_TIOCM_CTS) ? s->msr | UART_MSR_CTS : s->msr & ~UART_MSR_CTS;
    s->msr = (flags & CHR_TIOCM_DSR) ? s->msr | UART_MSR_DSR : s->msr & ~UART_MSR_DSR;
    s->msr = (flags & CHR_TIOCM_CAR) ? s->msr | UART_MSR_DCD : s->msr & ~UART_MSR_DCD;
    s->msr = (flags & CHR_TIOCM_RI) ? s->msr | UART_MSR_RI : s->msr & ~UART_MSR_RI;

    if (s->msr != omsr) {
         /* Set delta bits */
         s->msr = s->msr | ((s->msr >> 4) ^ (omsr >> 4));
         /* UART_MSR_TERI only if change was from 1 -> 0 */
         if ((s->msr & UART_MSR_TERI) && !(omsr & UART_MSR_RI))
             s->msr &= ~UART_MSR_TERI;
         serial_update_irq(s);
    }

    /* The real 16550A apparently has a 250ns response latency to line status changes.
       We'll be lazy and poll only every 10ms, and only poll it at all if MSI interrupts are turned on */

    if (s->poll_msl)
        qemu_mod_timer(s->modem_status_poll, qemu_get_clock_ns(vm_clock) + get_ticks_per_sec() / 100);
}

static void serial_xmit(void *opaque)
{
    SerialState *s = opaque;
    uint64_t new_xmit_ts = qemu_get_clock_ns(vm_clock);

    if (s->tsr_retry <= 0) {
        if (s->fcr & UART_FCR_FE) {
            s->tsr = fifo_get(s,XMIT_FIFO);
            if (!s->xmit_fifo.count)
                s->lsr |= UART_LSR_THRE;
        } else {
            s->tsr = s->thr;
            s->lsr |= UART_LSR_THRE;
        }
    }

    if (s->mcr & UART_MCR_LOOP) {
        /* in loopback mode, say that we just received a char */
        serial_receive1(s, &s->tsr, 1);
    } else if (qemu_chr_fe_write(s->chr, &s->tsr, 1) != 1) {
        if ((s->tsr_retry > 0) && (s->tsr_retry <= MAX_XMIT_RETRY)) {
            s->tsr_retry++;
            qemu_mod_timer(s->transmit_timer,  new_xmit_ts + s->char_transmit_time);
            return;
        } else if (s->poll_msl < 0) {
            /* If we exceed MAX_XMIT_RETRY and the backend is not a real serial port, then
            drop any further failed writes instantly, until we get one that goes through.
            This is to prevent guests that log to unconnected pipes or pty's from stalling. */
            s->tsr_retry = -1;
        }
    }
    else {
        s->tsr_retry = 0;
    }

    s->last_xmit_ts = qemu_get_clock_ns(vm_clock);
    if (!(s->lsr & UART_LSR_THRE))
        qemu_mod_timer(s->transmit_timer, s->last_xmit_ts + s->char_transmit_time);

    if (s->lsr & UART_LSR_THRE) {
        s->lsr |= UART_LSR_TEMT;
        s->thr_ipending = 1;
        serial_update_irq(s);
    }
}


static void serial_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    SerialState *s = opaque;

    addr &= 7;
    DPRINTF("write addr=0x%02x val=0x%02x\n", addr, val);
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0xff00) | val;
            serial_update_parameters(s);
        } else {
            s->thr = (uint8_t) val;
            if(s->fcr & UART_FCR_FE) {
                fifo_put(s, XMIT_FIFO, s->thr);
                s->thr_ipending = 0;
                s->lsr &= ~UART_LSR_TEMT;
                s->lsr &= ~UART_LSR_THRE;
                serial_update_irq(s);
            } else {
                s->thr_ipending = 0;
                s->lsr &= ~UART_LSR_THRE;
                serial_update_irq(s);
            }
            serial_xmit(s);
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0x00ff) | (val << 8);
            serial_update_parameters(s);
        } else {
            s->ier = val & 0x0f;
            /* If the backend device is a real serial port, turn polling of the modem
               status lines on physical port on or off depending on UART_IER_MSI state */
            if (s->poll_msl >= 0) {
                if (s->ier & UART_IER_MSI) {
                     s->poll_msl = 1;
                     serial_update_msl(s);
                } else {
                     qemu_del_timer(s->modem_status_poll);
                     s->poll_msl = 0;
                }
            }
            if (s->lsr & UART_LSR_THRE) {
                s->thr_ipending = 1;
                serial_update_irq(s);
            }
        }
        break;
    case 2:
        val = val & 0xFF;

        if (s->fcr == val)
            break;

        /* Did the enable/disable flag change? If so, make sure FIFOs get flushed */
        if ((val ^ s->fcr) & UART_FCR_FE)
            val |= UART_FCR_XFR | UART_FCR_RFR;

        /* FIFO clear */

        if (val & UART_FCR_RFR) {
            qemu_del_timer(s->fifo_timeout_timer);
            s->timeout_ipending=0;
            fifo_clear(s,RECV_FIFO);
        }

        if (val & UART_FCR_XFR) {
            fifo_clear(s,XMIT_FIFO);
        }

        if (val & UART_FCR_FE) {
            s->iir |= UART_IIR_FE;
            /* Set RECV_FIFO trigger Level */
            switch (val & 0xC0) {
            case UART_FCR_ITL_1:
                s->recv_fifo.itl = 1;
                break;
            case UART_FCR_ITL_2:
                s->recv_fifo.itl = 4;
                break;
            case UART_FCR_ITL_3:
                s->recv_fifo.itl = 8;
                break;
            case UART_FCR_ITL_4:
                s->recv_fifo.itl = 14;
                break;
            }
        } else
            s->iir &= ~UART_IIR_FE;

        /* Set fcr - or at least the bits in it that are supposed to "stick" */
        s->fcr = val & 0xC9;
        serial_update_irq(s);
        break;
    case 3:
        {
            int break_enable;
            s->lcr = val;
            serial_update_parameters(s);
            break_enable = (val >> 6) & 1;
            if (break_enable != s->last_break_enable) {
                s->last_break_enable = break_enable;
                qemu_chr_fe_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_BREAK,
                               &break_enable);
            }
        }
        break;
    case 4:
        {
            int flags;
            int old_mcr = s->mcr;
            s->mcr = val & 0x1f;
            if (val & UART_MCR_LOOP)
                break;

            if (s->poll_msl >= 0 && old_mcr != s->mcr) {

                qemu_chr_fe_ioctl(s->chr,CHR_IOCTL_SERIAL_GET_TIOCM, &flags);

                flags &= ~(CHR_TIOCM_RTS | CHR_TIOCM_DTR);

                if (val & UART_MCR_RTS)
                    flags |= CHR_TIOCM_RTS;
                if (val & UART_MCR_DTR)
                    flags |= CHR_TIOCM_DTR;

                qemu_chr_fe_ioctl(s->chr,CHR_IOCTL_SERIAL_SET_TIOCM, &flags);
                /* Update the modem status after a one-character-send wait-time, since there may be a response
                   from the device/computer at the other end of the serial line */
                qemu_mod_timer(s->modem_status_poll, qemu_get_clock_ns(vm_clock) + s->char_transmit_time);
            }
        }
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

static uint32_t serial_ioport_read(void *opaque, uint32_t addr)
{
    SerialState *s = opaque;
    uint32_t ret;

    addr &= 7;
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            ret = s->divider & 0xff;
        } else {
            if(s->fcr & UART_FCR_FE) {
                ret = fifo_get(s,RECV_FIFO);
                if (s->recv_fifo.count == 0)
                    s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
                else
                    qemu_mod_timer(s->fifo_timeout_timer, qemu_get_clock_ns (vm_clock) + s->char_transmit_time * 4);
                s->timeout_ipending = 0;
            } else {
                ret = s->rbr;
                s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
            }
            serial_update_irq(s);
            if (!(s->mcr & UART_MCR_LOOP)) {
                /* in loopback mode, don't receive any data */
                qemu_chr_accept_input(s->chr);
            }
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
        if ((ret & UART_IIR_ID) == UART_IIR_THRI) {
            s->thr_ipending = 0;
            serial_update_irq(s);
        }
        break;
    case 3:
        ret = s->lcr;
        break;
    case 4:
        ret = s->mcr;
        break;
    case 5:
        ret = s->lsr;
        /* Clear break and overrun interrupts */
        if (s->lsr & (UART_LSR_BI|UART_LSR_OE)) {
            s->lsr &= ~(UART_LSR_BI|UART_LSR_OE);
            serial_update_irq(s);
        }
        break;
    case 6:
        if (s->mcr & UART_MCR_LOOP) {
            /* in loopback, the modem output pins are connected to the
               inputs */
            ret = (s->mcr & 0x0c) << 4;
            ret |= (s->mcr & 0x02) << 3;
            ret |= (s->mcr & 0x01) << 5;
        } else {
            if (s->poll_msl >= 0)
                serial_update_msl(s);
            ret = s->msr;
            /* Clear delta bits & msr int after read, if they were set */
            if (s->msr & UART_MSR_ANY_DELTA) {
                s->msr &= 0xF0;
                serial_update_irq(s);
            }
        }
        break;
    case 7:
        ret = s->scr;
        break;
    }
    DPRINTF("read addr=0x%02x val=0x%02x\n", addr, ret);
    return ret;
}

static int serial_can_receive(SerialState *s)
{
    if(s->fcr & UART_FCR_FE) {
        if(s->recv_fifo.count < UART_FIFO_LENGTH)
        /* Advertise (fifo.itl - fifo.count) bytes when count < ITL, and 1 if above. If UART_FIFO_LENGTH - fifo.count is
        advertised the effect will be to almost always fill the fifo completely before the guest has a chance to respond,
        effectively overriding the ITL that the guest has set. */
             return (s->recv_fifo.count <= s->recv_fifo.itl) ? s->recv_fifo.itl - s->recv_fifo.count : 1;
        else
             return 0;
    } else {
    return !(s->lsr & UART_LSR_DR);
    }
}

static void serial_receive_break(SerialState *s)
{
    s->rbr = 0;
    /* When the LSR_DR is set a null byte is pushed into the fifo */
    fifo_put(s, RECV_FIFO, '\0');
    s->lsr |= UART_LSR_BI | UART_LSR_DR;
    serial_update_irq(s);
}

/* There's data in recv_fifo and s->rbr has not been read for 4 char transmit times */
static void fifo_timeout_int (void *opaque) {
    SerialState *s = opaque;
    if (s->recv_fifo.count) {
        s->timeout_ipending = 1;
        serial_update_irq(s);
    }
}

static int serial_can_receive1(void *opaque)
{
    SerialState *s = opaque;
    return serial_can_receive(s);
}

static void serial_receive1(void *opaque, const uint8_t *buf, int size)
{
    SerialState *s = opaque;
    if(s->fcr & UART_FCR_FE) {
        int i;
        for (i = 0; i < size; i++) {
            fifo_put(s, RECV_FIFO, buf[i]);
        }
        s->lsr |= UART_LSR_DR;
        /* call the timeout receive callback in 4 char transmit time */
        qemu_mod_timer(s->fifo_timeout_timer, qemu_get_clock_ns (vm_clock) + s->char_transmit_time * 4);
    } else {
        if (s->lsr & UART_LSR_DR)
            s->lsr |= UART_LSR_OE;
        s->rbr = buf[0];
        s->lsr |= UART_LSR_DR;
    }
    serial_update_irq(s);
}

static void serial_event(void *opaque, int event)
{
    SerialState *s = opaque;
    DPRINTF("event %x\n", event);
    if (event == CHR_EVENT_BREAK)
        serial_receive_break(s);
}

static void serial_pre_save(void *opaque)
{
    SerialState *s = opaque;
    s->fcr_vmstate = s->fcr;
}

static int serial_post_load(void *opaque, int version_id)
{
    SerialState *s = opaque;

    if (version_id < 3) {
        s->fcr_vmstate = 0;
    }
    /* Initialize fcr via setter to perform essential side-effects */
    serial_ioport_write(s, 0x02, s->fcr_vmstate);
    serial_update_parameters(s);
    return 0;
}

static const VMStateDescription vmstate_serial = {
    .name = "serial",
    .version_id = 3,
    .minimum_version_id = 2,
    .pre_save = serial_pre_save,
    .post_load = serial_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINT16_V(divider, SerialState, 2),
        VMSTATE_UINT8(rbr, SerialState),
        VMSTATE_UINT8(ier, SerialState),
        VMSTATE_UINT8(iir, SerialState),
        VMSTATE_UINT8(lcr, SerialState),
        VMSTATE_UINT8(mcr, SerialState),
        VMSTATE_UINT8(lsr, SerialState),
        VMSTATE_UINT8(msr, SerialState),
        VMSTATE_UINT8(scr, SerialState),
        VMSTATE_UINT8_V(fcr_vmstate, SerialState, 3),
        VMSTATE_END_OF_LIST()
    }
};

static void serial_reset(void *opaque)
{
    SerialState *s = opaque;

    s->rbr = 0;
    s->ier = 0;
    s->iir = UART_IIR_NO_INT;
    s->lcr = 0;
    s->lsr = UART_LSR_TEMT | UART_LSR_THRE;
    s->msr = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;
    /* Default to 9600 baud, 1 start bit, 8 data bits, 1 stop bit, no parity. */
    s->divider = 0x0C;
    s->mcr = UART_MCR_OUT2;
    s->scr = 0;
    s->tsr_retry = 0;
    s->char_transmit_time = (get_ticks_per_sec() / 9600) * 10;
    s->poll_msl = 0;

    fifo_clear(s,RECV_FIFO);
    fifo_clear(s,XMIT_FIFO);

    s->last_xmit_ts = qemu_get_clock_ns(vm_clock);

    s->thr_ipending = 0;
    s->last_break_enable = 0;
    qemu_irq_lower(s->irq);
}

static void serial_init_core(SerialState *s)
{
    if (!s->chr) {
        fprintf(stderr, "Can't create serial device, empty char device\n");
	exit(1);
    }

    s->modem_status_poll = qemu_new_timer_ns(vm_clock, (QEMUTimerCB *) serial_update_msl, s);

    s->fifo_timeout_timer = qemu_new_timer_ns(vm_clock, (QEMUTimerCB *) fifo_timeout_int, s);
    s->transmit_timer = qemu_new_timer_ns(vm_clock, (QEMUTimerCB *) serial_xmit, s);

    qemu_register_reset(serial_reset, s);

    qemu_chr_add_handlers(s->chr, serial_can_receive1, serial_receive1,
                          serial_event, s);
}

/* Change the main reference oscillator frequency. */
void serial_set_frequency(SerialState *s, uint32_t frequency)
{
    s->baudbase = frequency;
    serial_update_parameters(s);
}

static const int isa_serial_io[MAX_SERIAL_PORTS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static const int isa_serial_irq[MAX_SERIAL_PORTS] = { 4, 3, 4, 3 };

static const MemoryRegionPortio serial_portio[] = {
    { 0, 8, 1, .read = serial_ioport_read, .write = serial_ioport_write },
    PORTIO_END_OF_LIST()
};

static const MemoryRegionOps serial_io_ops = {
    .old_portio = serial_portio
};

static int serial_isa_initfn(ISADevice *dev)
{
    static int index;
    ISASerialState *isa = DO_UPCAST(ISASerialState, dev, dev);
    SerialState *s = &isa->state;

    if (isa->index == -1)
        isa->index = index;
    if (isa->index >= MAX_SERIAL_PORTS)
        return -1;
    if (isa->iobase == -1)
        isa->iobase = isa_serial_io[isa->index];
    if (isa->isairq == -1)
        isa->isairq = isa_serial_irq[isa->index];
    index++;

    s->baudbase = 115200;
    isa_init_irq(dev, &s->irq, isa->isairq);
    serial_init_core(s);
    qdev_set_legacy_instance_id(&dev->qdev, isa->iobase, 3);

    memory_region_init_io(&s->io, &serial_io_ops, s, "serial", 8);
    isa_register_ioport(dev, &s->io, isa->iobase);
    return 0;
}

static const VMStateDescription vmstate_isa_serial = {
    .name = "serial",
    .version_id = 3,
    .minimum_version_id = 2,
    .fields      = (VMStateField []) {
        VMSTATE_STRUCT(state, ISASerialState, 0, vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    }
};

SerialState *serial_init(int base, qemu_irq irq, int baudbase,
                         CharDriverState *chr)
{
    SerialState *s;

    s = g_malloc0(sizeof(SerialState));

    s->irq = irq;
    s->baudbase = baudbase;
    s->chr = chr;
    serial_init_core(s);

    vmstate_register(NULL, base, &vmstate_serial, s);

    register_ioport_write(base, 8, 1, serial_ioport_write, s);
    register_ioport_read(base, 8, 1, serial_ioport_read, s);
    return s;
}

/* Memory mapped interface */
static uint64_t serial_mm_read(void *opaque, target_phys_addr_t addr,
                               unsigned size)
{
    SerialState *s = opaque;
    return serial_ioport_read(s, addr >> s->it_shift);
}

static void serial_mm_write(void *opaque, target_phys_addr_t addr,
                            uint64_t value, unsigned size)
{
    SerialState *s = opaque;
    value &= ~0u >> (32 - (size * 8));
    serial_ioport_write(s, addr >> s->it_shift, value);
}

static const MemoryRegionOps serial_mm_ops[3] = {
    [DEVICE_NATIVE_ENDIAN] = {
        .read = serial_mm_read,
        .write = serial_mm_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
    },
    [DEVICE_LITTLE_ENDIAN] = {
        .read = serial_mm_read,
        .write = serial_mm_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
    },
    [DEVICE_BIG_ENDIAN] = {
        .read = serial_mm_read,
        .write = serial_mm_write,
        .endianness = DEVICE_BIG_ENDIAN,
    },
};

SerialState *serial_mm_init (target_phys_addr_t base, int it_shift,
                             qemu_irq irq, int baudbase,
                             CharDriverState *chr, int ioregister,
                             enum device_endian end)
{
    SerialState *s;

    s = g_malloc0(sizeof(SerialState));

    s->it_shift = it_shift;
    s->irq = irq;
    s->baudbase = baudbase;
    s->chr = chr;

    serial_init_core(s);
    vmstate_register(NULL, base, &vmstate_serial, s);

    memory_region_init_io(&s->io, &serial_mm_ops[end], s,
                          "serial", 8 << it_shift);
    if (ioregister) {
        memory_region_add_subregion(get_system_memory(), base, &s->io);
    }
    serial_update_msl(s);
    return s;
}

static ISADeviceInfo serial_isa_info = {
    .qdev.name  = "isa-serial",
    .qdev.size  = sizeof(ISASerialState),
    .qdev.vmsd  = &vmstate_isa_serial,
    .init       = serial_isa_initfn,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("index", ISASerialState, index,   -1),
        DEFINE_PROP_HEX32("iobase", ISASerialState, iobase,  -1),
        DEFINE_PROP_UINT32("irq",   ISASerialState, isairq,  -1),
        DEFINE_PROP_CHR("chardev",  ISASerialState, state.chr),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void serial_register_devices(void)
{
    isa_qdev_register(&serial_isa_info);
}

device_init(serial_register_devices)
