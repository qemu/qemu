/*
 * SH7750 device
 *
 * Copyright (c) 2005 Samuel Tardieu
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
#include <assert.h>
#include "vl.h"
#include "sh7750_regs.h"
#include "sh7750_regnames.h"

#define NB_DEVICES 4

typedef struct SH7750State {
    /* CPU */
    CPUSH4State *cpu;
    /* Peripheral frequency in Hz */
    uint32_t periph_freq;
    /* SDRAM controller */
    uint16_t rfcr;
    /* IO ports */
    uint16_t gpioic;
    uint32_t pctra;
    uint32_t pctrb;
    uint16_t portdira;		/* Cached */
    uint16_t portpullupa;	/* Cached */
    uint16_t portdirb;		/* Cached */
    uint16_t portpullupb;	/* Cached */
    uint16_t pdtra;
    uint16_t pdtrb;
    uint16_t periph_pdtra;	/* Imposed by the peripherals */
    uint16_t periph_portdira;	/* Direction seen from the peripherals */
    uint16_t periph_pdtrb;	/* Imposed by the peripherals */
    uint16_t periph_portdirb;	/* Direction seen from the peripherals */
    sh7750_io_device *devices[NB_DEVICES];	/* External peripherals */

    uint16_t icr;
    uint16_t ipra;
    uint16_t iprb;
    uint16_t iprc;
    uint16_t iprd;
    uint32_t intpri00;
    uint32_t intmsk00;
    /* Cache */
    uint32_t ccr;

} SH7750State;


/**********************************************************************
 I/O ports
**********************************************************************/

int sh7750_register_io_device(SH7750State * s, sh7750_io_device * device)
{
    int i;

    for (i = 0; i < NB_DEVICES; i++) {
	if (s->devices[i] == NULL) {
	    s->devices[i] = device;
	    return 0;
	}
    }
    return -1;
}

static uint16_t portdir(uint32_t v)
{
#define EVENPORTMASK(n) ((v & (1<<((n)<<1))) >> (n))
    return
	EVENPORTMASK(15) | EVENPORTMASK(14) | EVENPORTMASK(13) |
	EVENPORTMASK(12) | EVENPORTMASK(11) | EVENPORTMASK(10) |
	EVENPORTMASK(9) | EVENPORTMASK(8) | EVENPORTMASK(7) |
	EVENPORTMASK(6) | EVENPORTMASK(5) | EVENPORTMASK(4) |
	EVENPORTMASK(3) | EVENPORTMASK(2) | EVENPORTMASK(1) |
	EVENPORTMASK(0);
}

static uint16_t portpullup(uint32_t v)
{
#define ODDPORTMASK(n) ((v & (1<<(((n)<<1)+1))) >> (n))
    return
	ODDPORTMASK(15) | ODDPORTMASK(14) | ODDPORTMASK(13) |
	ODDPORTMASK(12) | ODDPORTMASK(11) | ODDPORTMASK(10) |
	ODDPORTMASK(9) | ODDPORTMASK(8) | ODDPORTMASK(7) | ODDPORTMASK(6) |
	ODDPORTMASK(5) | ODDPORTMASK(4) | ODDPORTMASK(3) | ODDPORTMASK(2) |
	ODDPORTMASK(1) | ODDPORTMASK(0);
}

static uint16_t porta_lines(SH7750State * s)
{
    return (s->portdira & s->pdtra) |	/* CPU */
	(s->periph_portdira & s->periph_pdtra) |	/* Peripherals */
	(~(s->portdira | s->periph_portdira) & s->portpullupa);	/* Pullups */
}

static uint16_t portb_lines(SH7750State * s)
{
    return (s->portdirb & s->pdtrb) |	/* CPU */
	(s->periph_portdirb & s->periph_pdtrb) |	/* Peripherals */
	(~(s->portdirb | s->periph_portdirb) & s->portpullupb);	/* Pullups */
}

static void gen_port_interrupts(SH7750State * s)
{
    /* XXXXX interrupts not generated */
}

static void porta_changed(SH7750State * s, uint16_t prev)
{
    uint16_t currenta, changes;
    int i, r = 0;

#if 0
    fprintf(stderr, "porta changed from 0x%04x to 0x%04x\n",
	    prev, porta_lines(s));
    fprintf(stderr, "pdtra=0x%04x, pctra=0x%08x\n", s->pdtra, s->pctra);
#endif
    currenta = porta_lines(s);
    if (currenta == prev)
	return;
    changes = currenta ^ prev;

    for (i = 0; i < NB_DEVICES; i++) {
	if (s->devices[i] && (s->devices[i]->portamask_trigger & changes)) {
	    r |= s->devices[i]->port_change_cb(currenta, portb_lines(s),
					       &s->periph_pdtra,
					       &s->periph_portdira,
					       &s->periph_pdtrb,
					       &s->periph_portdirb);
	}
    }

    if (r)
	gen_port_interrupts(s);
}

static void portb_changed(SH7750State * s, uint16_t prev)
{
    uint16_t currentb, changes;
    int i, r = 0;

    currentb = portb_lines(s);
    if (currentb == prev)
	return;
    changes = currentb ^ prev;

    for (i = 0; i < NB_DEVICES; i++) {
	if (s->devices[i] && (s->devices[i]->portbmask_trigger & changes)) {
	    r |= s->devices[i]->port_change_cb(portb_lines(s), currentb,
					       &s->periph_pdtra,
					       &s->periph_portdira,
					       &s->periph_pdtrb,
					       &s->periph_portdirb);
	}
    }

    if (r)
	gen_port_interrupts(s);
}

/**********************************************************************
 Memory
**********************************************************************/

static void error_access(const char *kind, target_phys_addr_t addr)
{
    fprintf(stderr, "%s to %s (0x%08x) not supported\n",
	    kind, regname(addr), addr);
}

static void ignore_access(const char *kind, target_phys_addr_t addr)
{
    fprintf(stderr, "%s to %s (0x%08x) ignored\n",
	    kind, regname(addr), addr);
}

static uint32_t sh7750_mem_readb(void *opaque, target_phys_addr_t addr)
{
    switch (addr) {
    default:
	error_access("byte read", addr);
	assert(0);
    }
}

static uint32_t sh7750_mem_readw(void *opaque, target_phys_addr_t addr)
{
    SH7750State *s = opaque;

    switch (addr) {
    case SH7750_FRQCR_A7:
	return 0;
    case SH7750_RFCR_A7:
	fprintf(stderr,
		"Read access to refresh count register, incrementing\n");
	return s->rfcr++;
    case SH7750_PDTRA_A7:
	return porta_lines(s);
    case SH7750_PDTRB_A7:
	return portb_lines(s);
    case 0x1fd00000:
        return s->icr;
    case 0x1fd00004:
        return s->ipra;
    case 0x1fd00008:
        return s->iprb;
    case 0x1fd0000c:
        return s->iprc;
    case 0x1fd00010:
        return s->iprd;
    default:
	error_access("word read", addr);
	assert(0);
    }
}

static uint32_t sh7750_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SH7750State *s = opaque;

    switch (addr) {
    case SH7750_MMUCR_A7:
	return s->cpu->mmucr;
    case SH7750_PTEH_A7:
	return s->cpu->pteh;
    case SH7750_PTEL_A7:
	return s->cpu->ptel;
    case SH7750_TTB_A7:
	return s->cpu->ttb;
    case SH7750_TEA_A7:
	return s->cpu->tea;
    case SH7750_TRA_A7:
	return s->cpu->tra;
    case SH7750_EXPEVT_A7:
	return s->cpu->expevt;
    case SH7750_INTEVT_A7:
	return s->cpu->intevt;
    case SH7750_CCR_A7:
	return s->ccr;
    case 0x1f000030:		/* Processor version PVR */
	return 0x00050000;	/* SH7750R */
    case 0x1f000040:		/* Processor version CVR */
	return 0x00110000;	/* Minimum caches */
    case 0x1f000044:		/* Processor version PRR */
	return 0x00000100;	/* SH7750R */
    case 0x1e080000:
        return s->intpri00;
    case 0x1e080020:
        return 0;
    case 0x1e080040:
        return s->intmsk00;
    case 0x1e080060:
        return 0;
    default:
	error_access("long read", addr);
	assert(0);
    }
}

static void sh7750_mem_writeb(void *opaque, target_phys_addr_t addr,
			      uint32_t mem_value)
{
    switch (addr) {
	/* PRECHARGE ? XXXXX */
    case SH7750_PRECHARGE0_A7:
    case SH7750_PRECHARGE1_A7:
	ignore_access("byte write", addr);
	return;
    default:
	error_access("byte write", addr);
	assert(0);
    }
}

static void sh7750_mem_writew(void *opaque, target_phys_addr_t addr,
			      uint32_t mem_value)
{
    SH7750State *s = opaque;
    uint16_t temp;

    switch (addr) {
	/* SDRAM controller */
    case SH7750_BCR2_A7:
    case SH7750_BCR3_A7:
    case SH7750_RTCOR_A7:
    case SH7750_RTCNT_A7:
    case SH7750_RTCSR_A7:
	ignore_access("word write", addr);
	return;
	/* IO ports */
    case SH7750_PDTRA_A7:
	temp = porta_lines(s);
	s->pdtra = mem_value;
	porta_changed(s, temp);
	return;
    case SH7750_PDTRB_A7:
	temp = portb_lines(s);
	s->pdtrb = mem_value;
	portb_changed(s, temp);
	return;
    case SH7750_RFCR_A7:
	fprintf(stderr, "Write access to refresh count register\n");
	s->rfcr = mem_value;
	return;
    case SH7750_GPIOIC_A7:
	s->gpioic = mem_value;
	if (mem_value != 0) {
	    fprintf(stderr, "I/O interrupts not implemented\n");
	    assert(0);
	}
	return;
    case 0x1fd00000:
        s->icr = mem_value;
	return;
    case 0x1fd00004:
        s->ipra = mem_value;
	return;
    case 0x1fd00008:
        s->iprb = mem_value;
	return;
    case 0x1fd0000c:
        s->iprc = mem_value;
	return;
    case 0x1fd00010:
        s->iprd = mem_value;
	return;
    default:
	error_access("word write", addr);
	assert(0);
    }
}

static void sh7750_mem_writel(void *opaque, target_phys_addr_t addr,
			      uint32_t mem_value)
{
    SH7750State *s = opaque;
    uint16_t temp;

    switch (addr) {
	/* SDRAM controller */
    case SH7750_BCR1_A7:
    case SH7750_BCR4_A7:
    case SH7750_WCR1_A7:
    case SH7750_WCR2_A7:
    case SH7750_WCR3_A7:
    case SH7750_MCR_A7:
	ignore_access("long write", addr);
	return;
	/* IO ports */
    case SH7750_PCTRA_A7:
	temp = porta_lines(s);
	s->pctra = mem_value;
	s->portdira = portdir(mem_value);
	s->portpullupa = portpullup(mem_value);
	porta_changed(s, temp);
	return;
    case SH7750_PCTRB_A7:
	temp = portb_lines(s);
	s->pctrb = mem_value;
	s->portdirb = portdir(mem_value);
	s->portpullupb = portpullup(mem_value);
	portb_changed(s, temp);
	return;
    case SH7750_MMUCR_A7:
	s->cpu->mmucr = mem_value;
	return;
    case SH7750_PTEH_A7:
	s->cpu->pteh = mem_value;
	return;
    case SH7750_PTEL_A7:
	s->cpu->ptel = mem_value;
	return;
    case SH7750_TTB_A7:
	s->cpu->ttb = mem_value;
	return;
    case SH7750_TEA_A7:
	s->cpu->tea = mem_value;
	return;
    case SH7750_TRA_A7:
	s->cpu->tra = mem_value & 0x000007ff;
	return;
    case SH7750_EXPEVT_A7:
	s->cpu->expevt = mem_value & 0x000007ff;
	return;
    case SH7750_INTEVT_A7:
	s->cpu->intevt = mem_value & 0x000007ff;
	return;
    case SH7750_CCR_A7:
	s->ccr = mem_value;
	return;
    case 0x1e080000:
        s->intpri00 = mem_value;
	return;
    case 0x1e080020:
        return;
    case 0x1e080040:
        s->intmsk00 = mem_value;
	return;
    case 0x1e080060:
        return;
    default:
	error_access("long write", addr);
	assert(0);
    }
}

static CPUReadMemoryFunc *sh7750_mem_read[] = {
    sh7750_mem_readb,
    sh7750_mem_readw,
    sh7750_mem_readl
};

static CPUWriteMemoryFunc *sh7750_mem_write[] = {
    sh7750_mem_writeb,
    sh7750_mem_writew,
    sh7750_mem_writel
};

SH7750State *sh7750_init(CPUSH4State * cpu)
{
    SH7750State *s;
    int sh7750_io_memory;

    s = qemu_mallocz(sizeof(SH7750State));
    s->cpu = cpu;
    s->periph_freq = 60000000;	/* 60MHz */
    sh7750_io_memory = cpu_register_io_memory(0,
					      sh7750_mem_read,
					      sh7750_mem_write, s);
    cpu_register_physical_memory(0x1c000000, 0x04000000, sh7750_io_memory);

    sh_serial_init(0x1fe00000, 0, s->periph_freq, serial_hds[0]);
    sh_serial_init(0x1fe80000, SH_SERIAL_FEAT_SCIF,
		   s->periph_freq, serial_hds[1]);

    tmu012_init(0x1fd80000,
		TMU012_FEAT_TOCR | TMU012_FEAT_3CHAN | TMU012_FEAT_EXTCLK,
		s->periph_freq);
    tmu012_init(0x1e100000, 0, s->periph_freq);
    return s;
}
