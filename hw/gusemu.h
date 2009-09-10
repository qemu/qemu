/*
 * GUSEMU32 - API
 *
 * Copyright (C) 2000-2007 Tibor "TS" Schütz
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

#ifndef GUSEMU_H
#define GUSEMU_H

/* data types (need to be adjusted if neither a VC6 nor a C99 compatible compiler is used) */

#if defined _WIN32 && defined _MSC_VER /* doesnt support other win32 compilers yet, do it yourself... */
 typedef unsigned char GUSbyte;
 typedef unsigned short GUSword;
 typedef unsigned int GUSdword;
 typedef signed char GUSchar;
 typedef signed short GUSsample;
#else
 #include <stdint.h>
 typedef int8_t GUSchar;
 typedef uint8_t GUSbyte;
 typedef uint16_t GUSword;
 typedef uint32_t GUSdword;
 typedef int16_t GUSsample;
#endif

typedef struct _GUSEmuState
{
 GUSbyte *himemaddr; /* 1024*1024 bytes used for storing uploaded samples (+32 additional bytes for read padding) */
 GUSbyte *gusdatapos; /* (gusdataend-gusdata) bytes used for storing emulated GF1/mixer register states (32*32+4 bytes in initial GUSemu32 version) */
 uint32_t gusirq;
 uint32_t gusdma;
 unsigned int timer1fraction;
 unsigned int timer2fraction;
 void *opaque;
} GUSEmuState;

/* ** Callback functions needed: */
/* NMI is defined as hwirq=-1 (not supported (yet?)) */
/* GUS_irqrequest returns the number of IRQs actually scheduled into the virtual machine */
/* Level triggered IRQ simulations normally return 1 */
/* Event triggered IRQ simulation can safely ignore GUS_irqclear calls */
int  GUS_irqrequest(GUSEmuState *state, int hwirq, int num);/* needed in both mixer and bus emulation functions. */
void GUS_irqclear(  GUSEmuState *state, int hwirq); /* used by gus_write() only - can be left empty for mixer functions */
void GUS_dmarequest(GUSEmuState *state);            /* used by gus_write() only - can be left empty for mixer functions */

/* ** ISA bus interface functions: */

/* Port I/O handlers */
/* support the following ports: */
/* 2x0,2x6,2x8...2xF,3x0...3x7;  */
/* optional: 388,389 (at least writes should be forwarded or some GUS detection algorithms will fail) */
/* data is passed in host byte order */
unsigned int gus_read( GUSEmuState *state, int port, int size);
void         gus_write(GUSEmuState *state, int port, int size, unsigned int data);
/* size is given in bytes (1 for byte, 2 for word) */

/* DMA data transfer function */
/* data pointed to is passed in native x86 order */
void gus_dma_transferdata(GUSEmuState *state, char *dma_addr, unsigned int count, int TC);
/* Called back by GUS_start_DMA as soon as the emulated DMA controller is ready for a transfer to or from GUS */
/* (might be immediately if the DMA controller was programmed first) */
/* dma_addr is an already translated address directly pointing to the beginning of the memory block */
/* do not forget to update DMA states after the call, including the DREQ and TC flags */
/* it is possible to break down a single transfer into multiple ones, but take care that: */
/* -dma_count is actually count-1 */
/* -before and during a transfer, DREQ is set and TC cleared */
/* -when calling gus_dma_transferdata(), TC is only set true for call transfering the last byte */
/* -after the last transfer, DREQ is cleared and TC is set */

/* ** GF1 mixer emulation functions: */
/* Usually, gus_irqgen should be called directly after gus_mixvoices if you can meet the recommended ranges. */
/* If the interrupts are executed immediately (i.e., are synchronous), it may be useful to break this */
/* down into a sequence of gus_mixvoice();gus_irqgen(); calls while mixing an audio block. */
/* If the interrupts are asynchronous, it may be needed to use a separate thread mixing into a temporary */
/* audio buffer in order to avoid quality loss caused by large numsamples and elapsed_time values. */

void gus_mixvoices(GUSEmuState *state, unsigned int playback_freq, unsigned int numsamples, GUSsample *bufferpos);
/* recommended range: 10 < numsamples < 100 */
/* lower values may result in increased rounding error, higher values often cause audible timing delays */

void gus_irqgen(GUSEmuState *state, unsigned int elapsed_time);
/* recommended range: 80us < elapsed_time < max(1000us, numsamples/playback_freq) */
/* lower values won´t provide any benefit at all, higher values can cause audible timing delays */
/* note: masked timers are also calculated by this function, thus it might be needed even without any IRQs in use! */

#endif  /* gusemu.h */
