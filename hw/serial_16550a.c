/*
 * QEMU 16450 UART emulation
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

#include "hw.h"
#include "qemu-char.h"
#include "isa.h"
#include "pc.h"

//#define DEBUG_SERIAL

#define CONFIG_16550A

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

struct SerialFifo {
    char *data;             /* bytes contained by the fifo */
    unsigned int count;     /* number of byte in the fifo */
    unsigned int length;    /* length of the fifo */
    unsigned int trigger;   /* trigger level of the fifo */
} typedef SerialFifo;

/* initialize a FIFO */
SerialFifo * fifo_init(unsigned int length, unsigned int trigger) {
    SerialFifo *f;
    if(!length)
        return NULL;
    if(trigger>length)
        return NULL;
    f = qemu_mallocz(sizeof(SerialFifo));
    if(f == NULL)
        return NULL;
    f->data = qemu_mallocz(length);
    if(f->data == NULL) {
        qemu_free(f);
        return NULL;
    }
    f->length = length;
    f->count = 0;
    f->trigger = trigger;
    return f;
}

/* set the trigger level of a FIFO */
int fifo_set_trigger(SerialFifo *f, unsigned int trigger) {
    f->trigger = trigger;
    return 1;
}

/* clear a FIFO */
int fifo_clear(SerialFifo *f) {
    f->count = 0;
    return 1;
}


/* free the memory of the FIFO - unused */
int fifo_free(SerialFifo *f) {
    if(f->data != NULL)
        qemu_free(f->data);
    qemu_free(f);
    return 1;
}

/* put a character in the FIFO */
int fifo_put(SerialFifo *f, const uint8_t *buf, int size) {
    if(f->count >= f->length)
        return 0;
    /* Do the fifo moving */
    memmove(f->data+f->count, buf, size);
    f->count += size;
    return 1;
}

/* get the FIFO triggering level */
unsigned int fifo_get_trigger(SerialFifo *f) {
    return f->trigger;
}


/* get the first char of the FIFO */
char fifo_get(SerialFifo *f) {
    char c;
    if(f->count == 0)
        return (char) 0;
    f->count -= 1;
    c = f->data[0];
    memmove(f->data, f->data+1, f->count);
    return c;
}

unsigned int fifo_count(SerialFifo *f) {
    return f->count;
}

unsigned int fifo_is_full(SerialFifo *f) {
    if(f->count >= f->length)
        return 1;
    else
        return 0;
}

/* Used to test if the FIFO trigger level is reached */
unsigned int fifo_is_triggered(SerialFifo *f) {
    if(f->count >= f->trigger)
        return 1;
    else
        return 0;
}

#endif

struct SerialState {
    uint8_t divider;
    uint8_t rbr; /* receive register */
    uint8_t ier;
    uint8_t iir; /* read only */
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr; /* read only */
    uint8_t msr; /* read only */
    uint8_t scr;
#if defined(CONFIG_16550A)
    uint8_t fcr;
#endif
    /* NOTE: this hidden state is necessary for tx irq generation as
       it can be reset while reading iir */
    int thr_ipending;
    SetIRQFunc *set_irq;
    void *irq_opaque;
    int irq;
    CharDriverState *chr;
    int last_break_enable;
    target_ulong base;
#if defined(CONFIG_16550A)
    int output_fifo_count;                  /* number of byte in the simulated XMIT FIFO */
    int64_t output_start_time;              /* Time when the first byte has been put in the XMIT FIFO */
    SerialFifo *input_fifo;
    float char_transmit_time;               /* time to transmit a char */
    struct QEMUTimer *fifo_timeout_timer;
    int timeout_ipending;                   /* timeout interrupt pending state */
    struct QEMUTimer *fifo_transmit_timer;
#endif
    int it_shift;
};

static void serial_update_irq(SerialState *s)
{
    if ((s->lsr & UART_LSR_OE) && (s->ier & UART_IER_RLSI)) {
            s->iir = (s->iir& UART_IIR_FE) | UART_IIR_RLSI;
    } else if ((s->lsr & UART_LSR_DR) && (s->ier & UART_IER_RDI) && (s->fcr & UART_FCR_FE) && fifo_is_triggered(s->input_fifo) ) {
            s->iir = (s->iir& UART_IIR_FE) | UART_IIR_RDI;
    } else if ((s->lsr & UART_LSR_DR) && (s->ier & UART_IER_RDI) && !((s->fcr & UART_FCR_FE) != 0) ) {
            s->iir = (s->iir& UART_IIR_FE) | UART_IIR_RDI;
    } else if (s->timeout_ipending) {
         s->iir = (s->iir& UART_IIR_FE) | UART_IIR_CTI;
    } else if (s->thr_ipending && (s->ier & UART_IER_THRI)) {
        s->iir = (s->iir& UART_IIR_FE) | UART_IIR_THRI;
    } else {
        s->iir = (s->iir& UART_IIR_FE) | UART_IIR_NO_INT;
    }
    if (s->set_irq == 0) {
	static uint8_t iir = 255;
	if (iir != s->iir) {
	  iir = s->iir;
	}
    } else if (s->iir != UART_IIR_NO_INT) {
        s->set_irq(s->irq_opaque, s->irq, 1);
    } else {
        s->set_irq(s->irq_opaque, s->irq, 0);
    }
}

static void serial_update_parameters(SerialState *s)
{
    int speed, parity, data_bits, stop_bits, bit_count;
    QEMUSerialSetParams ssp;

    bit_count = 1;

    if (s->lcr & 0x08) {
        bit_count += 1;
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
    speed = 115200 / s->divider;
    ssp.speed = speed;
    ssp.parity = parity;
    ssp.data_bits = data_bits;
    ssp.stop_bits = stop_bits;
    qemu_chr_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
    bit_count += stop_bits + data_bits;
    s->char_transmit_time = ( (float) bit_count * 1000) / speed;
#if 0
    printf("speed=%d parity=%c data=%d stop=%d\n", 
           speed, parity, data_bits, stop_bits);
#endif
}

static void serial_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    SerialState *s = opaque;
    unsigned char ch;
    
    addr >>= s->it_shift;
    addr &= 7;
#ifdef DEBUG_SERIAL
    printf("serial: write addr=0x%02x val=0x%02x\n", addr, val);
#endif
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0xff00) | val;
            serial_update_parameters(s);
        } else {
            ch = val;
#if defined(CONFIG_16550A)
            if(s->fcr & UART_FCR_FE) {
                s->thr_ipending = 0;
                s->lsr &= ~UART_LSR_TEMT;
                s->lsr &= ~UART_LSR_THRE;
                qemu_chr_write(s->chr, &ch, 1);
                /* get the vm time when the first byte is put in the output FIFO */
                if(s->output_fifo_count == 0) {
                    s->output_start_time = qemu_get_clock (vm_clock);
                }
                int64_t ticks;
                /* call the XMIT ending FIFO emit when all byte are supposed to be send by the output fifo */
                ticks = (int64_t) ((ticks_per_sec * s->char_transmit_time * s->output_fifo_count )/1000);
                qemu_mod_timer(s->fifo_transmit_timer  ,  s->output_start_time + ticks);
                s->output_fifo_count += 1;
            } else {
                s->thr_ipending = 0;
                s->lsr &= ~UART_LSR_THRE;
                serial_update_irq(s);
                qemu_chr_write(s->chr, &ch, 1);
                s->thr_ipending = 1;
                s->lsr |= UART_LSR_THRE;
                s->lsr |= UART_LSR_TEMT;
            }
#endif
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
#if defined(CONFIG_16550A)
        {
            /* FIFO Control Register - ignores the DMA bit */
            unsigned int trigger;
            int fifo_enable_changed;

            trigger = 0;
            fifo_enable_changed = 0;


            /* Compute RCVR FIFO trigger Level */
            ch = val & UART_FCR_ITL_MASQ;
            if(ch == (unsigned char) UART_FCR_ITL_1) 
                trigger = 1;
            if(ch == (unsigned char) UART_FCR_ITL_4)
                trigger = 4;
            if(ch == (unsigned char) UART_FCR_ITL_8)
                trigger = 8;
            if(ch == (unsigned char) UART_FCR_ITL_14)
                trigger = 14;

            ch = val;
            /* Detect FIFO mode changes */
            if(ch & UART_FCR_FE) {
                if( !(s->fcr & UART_FCR_FE) )
                        fifo_enable_changed = 1;
                s->iir |= UART_IIR_FE;
            } else {
                if(s->fcr & UART_FCR_FE)
                        fifo_enable_changed = 1;
                s->iir &= ~(unsigned char) UART_IIR_FE;
            }

            s->fcr = val;

            /* Clear FIFOs if FIFO mode has been changed */
            if(fifo_enable_changed) {
                fifo_clear(s->input_fifo);
            }
            
            /* If FIFO mode is enabled config RCVR FIFO trigger level */
            if(s->fcr & UART_FCR_FE) {
                fifo_set_trigger(s->input_fifo, trigger);
            }

            /* Manage Fifo Control Register clearing bits*/
            if(s->fcr & UART_FCR_FE) {
                if(s->fcr & UART_FCR_RFR)
                    fifo_clear(s->input_fifo);
            }

        }
#endif
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

static uint32_t serial_ioport_read(void *opaque, uint32_t addr)
{
    SerialState *s = opaque;
    uint32_t ret;

    addr >>= s->it_shift;
    addr &= 7;
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            ret = s->divider & 0xff; 
        } else {
#if defined(CONFIG_16550A)
            if(s->fcr & UART_FCR_FE) {
                int64_t ticks;
                ticks = (int64_t) ( (ticks_per_sec * s->char_transmit_time * 4) / 1000 );
                ret = fifo_get(s->input_fifo);
                if(fifo_count(s->input_fifo) == 0)
                    s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
                else
                    /* call the RCVR FIFO timeout routine in 4 char transmit time */
                    qemu_mod_timer(s->fifo_timeout_timer  ,  qemu_get_clock (vm_clock) + ticks);
                s->timeout_ipending = 0;
                /* break interrupt */
#endif
            } else {
                ret = s->rbr;
                s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
            }
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
#ifdef DEBUG_SERIAL
    printf("serial: read addr=0x%02x val=0x%02x\n", addr, ret);
#endif
    return ret;
}

#if defined(CONFIG_16550A)

/* callback used to trigger the Transmit Holding Register Empty interrupt when all byte are transmited from the simulated XMIT buffer */
static void output_bytes_transmited_cb (void *opaque) {
    SerialState *s = opaque;
    s->output_fifo_count = 0;
    s->thr_ipending = 1;
    s->lsr |= UART_LSR_THRE;
    s->lsr |= UART_LSR_TEMT;
    serial_update_irq(s);
}

/* callback called when no new char has been received for 4 char transmit times */
static void timeout_timer_cb (void *opaque) {
    SerialState *s = opaque;

    if(fifo_count(s->input_fifo) > 0) {
        s->timeout_ipending = 1;
    } else {
        s->timeout_ipending = 0;
    }
    serial_update_irq(s);
}

#endif

static int serial_can_receive(SerialState *s)
{
    if(s->fcr & UART_FCR_FE) {
        if(fifo_count(s->input_fifo) < UART_FIFO_LENGTH)
            return UART_FIFO_LENGTH - fifo_count(s->input_fifo);
        else
            return 0;
    } else {
        return !(s->lsr & UART_LSR_DR);
    }
}

static void serial_receive_bytes(SerialState *s, const uint8_t *buf, int size) {
    if(s->fcr & UART_FCR_FE) {
        if( fifo_is_full(s->input_fifo) ) {
            s->lsr |= UART_LSR_OE;
        } else {
            /* call the timeout receive callback in 4 char transmit time */
            int64_t ticks;
            ticks = (int64_t) ((ticks_per_sec * s->char_transmit_time * 4) / 1000 );
            fifo_put(s->input_fifo, buf, size);
            s->lsr |= UART_LSR_DR;
            qemu_mod_timer(s->fifo_timeout_timer  ,  qemu_get_clock (vm_clock) + ticks);
        }
    } else {
        s->rbr = buf[0];
        s->lsr |= UART_LSR_DR;
    }
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
    serial_receive_bytes(s, buf, size);
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

    qemu_put_8s(f,&s->divider);
    qemu_put_8s(f,&s->rbr);
    qemu_put_8s(f,&s->ier);
    qemu_put_8s(f,&s->iir);
    qemu_put_8s(f,&s->lcr);
    qemu_put_8s(f,&s->mcr);
    qemu_put_8s(f,&s->lsr);
    qemu_put_8s(f,&s->msr);
    qemu_put_8s(f,&s->scr);
    qemu_put_8s(f,&s->fcr);
}

static int serial_load(QEMUFile *f, void *opaque, int version_id)
{
    SerialState *s = opaque;

    if(version_id != 1)
        return -EINVAL;

    qemu_get_8s(f,&s->divider);
    qemu_get_8s(f,&s->rbr);
    qemu_get_8s(f,&s->ier);
    qemu_get_8s(f,&s->iir);
    qemu_get_8s(f,&s->lcr);
    qemu_get_8s(f,&s->mcr);
    qemu_get_8s(f,&s->lsr);
    qemu_get_8s(f,&s->msr);
    qemu_get_8s(f,&s->scr);
    qemu_get_8s(f,&s->fcr);

    return 0;
}

/* If fd is zero, it means that the serial device uses the console */
SerialState *serial_init(SetIRQFunc *set_irq, void *opaque,
                         int base, int it_shift, int irq, CharDriverState *chr)
{
    SerialState *s;

    s = qemu_mallocz(sizeof(SerialState));
    if (!s)
        return NULL;
    s->set_irq = set_irq;
    s->irq_opaque = opaque;
    s->irq = irq;
    s->lsr = UART_LSR_TEMT | UART_LSR_THRE;
    s->iir = UART_IIR_NO_INT;
    s->it_shift = it_shift;
    s->msr = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;

#if defined(CONFIG_16550A)
    /* Init fifo structures */
    s->input_fifo = fifo_init(UART_FIFO_LENGTH , 0);
    s->fifo_timeout_timer = qemu_new_timer(vm_clock, timeout_timer_cb, s);
    s->fifo_transmit_timer = qemu_new_timer(vm_clock, output_bytes_transmited_cb, s);

    s->output_fifo_count = 0;
    s->output_start_time = 0;
#endif

    register_savevm("serial", base, 1, serial_save, serial_load, s);

    register_ioport_write(base, 8 << it_shift, 1, serial_ioport_write, s);
    register_ioport_read(base, 8 << it_shift, 1, serial_ioport_read, s);
    s->chr = chr;
    qemu_chr_add_handlers(chr, serial_can_receive1, serial_receive1,
                          serial_event, s);
    return s;
}
