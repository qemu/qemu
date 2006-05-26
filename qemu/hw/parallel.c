/*
 * QEMU Parallel PORT emulation
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

//#define DEBUG_PARALLEL

/*
 * These are the definitions for the Printer Status Register
 */
#define PARA_STS_BUSY	0x80	/* Busy complement */
#define PARA_STS_ACK	0x40	/* Acknowledge */
#define PARA_STS_PAPER	0x20	/* Out of paper */
#define PARA_STS_ONLINE	0x10	/* Online */
#define PARA_STS_ERROR	0x08	/* Error complement */

/*
 * These are the definitions for the Printer Control Register
 */
#define PARA_CTR_INTEN	0x10	/* IRQ Enable */
#define PARA_CTR_SELECT	0x08	/* Select In complement */
#define PARA_CTR_INIT	0x04	/* Initialize Printer complement */
#define PARA_CTR_AUTOLF	0x02	/* Auto linefeed complement */
#define PARA_CTR_STROBE	0x01	/* Strobe complement */

struct ParallelState {
    uint8_t data;
    uint8_t status; /* read only register */
    uint8_t control;
    int irq;
    int irq_pending;
    CharDriverState *chr;
    int hw_driver;
};

static void parallel_update_irq(ParallelState *s)
{
    if (s->irq_pending)
        pic_set_irq(s->irq, 1);
    else
        pic_set_irq(s->irq, 0);
}

static void parallel_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    ParallelState *s = opaque;
    
    addr &= 7;
#ifdef DEBUG_PARALLEL
    printf("parallel: write addr=0x%02x val=0x%02x\n", addr, val);
#endif
    switch(addr) {
    case 0:
        if (s->hw_driver) {
            s->data = val;
            qemu_chr_ioctl(s->chr, CHR_IOCTL_PP_WRITE_DATA, &s->data);
        } else {
            s->data = val;
            parallel_update_irq(s);
        }
        break;
    case 2:
        if (s->hw_driver) {
            s->control = val;
            qemu_chr_ioctl(s->chr, CHR_IOCTL_PP_WRITE_CONTROL, &s->control);
        } else {
            if ((val & PARA_CTR_INIT) == 0 ) {
                s->status = PARA_STS_BUSY;
                s->status |= PARA_STS_ACK;
                s->status |= PARA_STS_ONLINE;
                s->status |= PARA_STS_ERROR;
            }
            else if (val & PARA_CTR_SELECT) {
                if (val & PARA_CTR_STROBE) {
                    s->status &= ~PARA_STS_BUSY;
                    if ((s->control & PARA_CTR_STROBE) == 0)
                        qemu_chr_write(s->chr, &s->data, 1);
                } else {
                    if (s->control & PARA_CTR_INTEN) {
                        s->irq_pending = 1;
                    }
                }
            }
            parallel_update_irq(s);
            s->control = val;
        }
        break;
    }
}

static uint32_t parallel_ioport_read(void *opaque, uint32_t addr)
{
    ParallelState *s = opaque;
    uint32_t ret = 0xff;

    addr &= 7;
    switch(addr) {
    case 0:
        if (s->hw_driver) {
            qemu_chr_ioctl(s->chr, CHR_IOCTL_PP_READ_DATA, &s->data);
        } 
        ret = s->data; 
        break;
    case 1:
        if (s->hw_driver) {
            qemu_chr_ioctl(s->chr, CHR_IOCTL_PP_READ_STATUS, &s->status);
            ret = s->status; 
        } else {
            ret = s->status;
            s->irq_pending = 0;
            if ((s->status & PARA_STS_BUSY) == 0 && (s->control & PARA_CTR_STROBE) == 0) {
                /* XXX Fixme: wait 5 microseconds */
                if (s->status & PARA_STS_ACK)
                    s->status &= ~PARA_STS_ACK;
                else {
                    /* XXX Fixme: wait 5 microseconds */
                    s->status |= PARA_STS_ACK;
                    s->status |= PARA_STS_BUSY;
                }
            }
            parallel_update_irq(s);
        }
        break;
    case 2:
        if (s->hw_driver) {
            qemu_chr_ioctl(s->chr, CHR_IOCTL_PP_READ_CONTROL, &s->control);
        }
        ret = s->control;
        break;
    }
#ifdef DEBUG_PARALLEL
    printf("parallel: read addr=0x%02x val=0x%02x\n", addr, ret);
#endif
    return ret;
}

/* If fd is zero, it means that the parallel device uses the console */
ParallelState *parallel_init(int base, int irq, CharDriverState *chr)
{
    ParallelState *s;
    uint8_t dummy;

    s = qemu_mallocz(sizeof(ParallelState));
    if (!s)
        return NULL;
    s->chr = chr;
    s->hw_driver = 0;
    if (qemu_chr_ioctl(chr, CHR_IOCTL_PP_READ_STATUS, &dummy) == 0)
        s->hw_driver = 1;

    s->irq = irq;
    s->data = 0;
    s->status = PARA_STS_BUSY;
    s->status |= PARA_STS_ACK;
    s->status |= PARA_STS_ONLINE;
    s->status |= PARA_STS_ERROR;
    s->control = PARA_CTR_SELECT;
    s->control |= PARA_CTR_INIT;

    register_ioport_write(base, 8, 1, parallel_ioport_write, s);
    register_ioport_read(base, 8, 1, parallel_ioport_read, s);
    return s;
}
