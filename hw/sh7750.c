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

typedef struct {
    uint8_t data[16];
    uint8_t length;		/* Number of characters in the FIFO */
    uint8_t write_idx;		/* Index of first character to write */
    uint8_t read_idx;		/* Index of first character to read */
} fifo;

#define NB_DEVICES 4

typedef struct SH7750State {
    /* CPU */
    CPUSH4State *cpu;
    /* Peripheral frequency in Hz */
    uint32_t periph_freq;
    /* SDRAM controller */
    uint16_t rfcr;
    /* First serial port */
    CharDriverState *serial1;
    uint8_t scscr1;
    uint8_t scsmr1;
    uint8_t scbrr1;
    uint8_t scssr1;
    uint8_t scssr1_read;
    uint8_t sctsr1;
    uint8_t sctsr1_loaded;
    uint8_t sctdr1;
    uint8_t scrdr1;
    /* Second serial port */
    CharDriverState *serial2;
    uint16_t sclsr2;
    uint16_t scscr2;
    uint16_t scfcr2;
    uint16_t scfsr2;
    uint16_t scsmr2;
    uint8_t scbrr2;
    fifo serial2_receive_fifo;
    fifo serial2_transmit_fifo;
    /* Timers */
    uint8_t tstr;
    /* Timer 0 */
    QEMUTimer *timer0;
    uint16_t tcr0;
    uint32_t tcor0;
    uint32_t tcnt0;
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
    /* Cache */
    uint32_t ccr;
} SH7750State;

/**********************************************************************
 Timers
**********************************************************************/

/* XXXXX At this time, timer0 works in underflow only mode, that is
   the value of tcnt0 is read at alarm computation time and cannot
   be read back by the guest OS */

static void start_timer0(SH7750State * s)
{
    uint64_t now, next, prescaler;

    if ((s->tcr0 & 6) == 6) {
	fprintf(stderr, "rtc clock for timer 0 not supported\n");
	assert(0);
    }

    if ((s->tcr0 & 7) == 5) {
	fprintf(stderr, "timer 0 configuration not supported\n");
	assert(0);
    }

    if ((s->tcr0 & 4) == 4)
	prescaler = 1024;
    else
	prescaler = 4 << (s->tcr0 & 3);

    now = qemu_get_clock(vm_clock);
    /* XXXXX */
    next =
	now + muldiv64(prescaler * s->tcnt0, ticks_per_sec,
		       s->periph_freq);
    if (next == now)
	next = now + 1;
    fprintf(stderr, "now=%016" PRIx64 ", next=%016" PRIx64 "\n", now, next);
    fprintf(stderr, "timer will underflow in %f seconds\n",
	    (float) (next - now) / (float) ticks_per_sec);

    qemu_mod_timer(s->timer0, next);
}

static void timer_start_changed(SH7750State * s)
{
    if (s->tstr & SH7750_TSTR_STR0) {
	start_timer0(s);
    } else {
	fprintf(stderr, "timer 0 is stopped\n");
	qemu_del_timer(s->timer0);
    }
}

static void timer0_cb(void *opaque)
{
    SH7750State *s = opaque;

    s->tcnt0 = (uint32_t) 0;	/* XXXXX */
    if (--s->tcnt0 == (uint32_t) - 1) {
	fprintf(stderr, "timer 0 underflow\n");
	s->tcnt0 = s->tcor0;
	s->tcr0 |= SH7750_TCR_UNF;
	if (s->tcr0 & SH7750_TCR_UNIE) {
	    fprintf(stderr,
		    "interrupt generation for timer 0 not supported\n");
	    assert(0);
	}
    }
    start_timer0(s);
}

static void init_timers(SH7750State * s)
{
    s->tcor0 = 0xffffffff;
    s->tcnt0 = 0xffffffff;
    s->timer0 = qemu_new_timer(vm_clock, &timer0_cb, s);
}

/**********************************************************************
 First serial port
**********************************************************************/

static int serial1_can_receive(void *opaque)
{
    SH7750State *s = opaque;

    return s->scscr1 & SH7750_SCSCR_RE;
}

static void serial1_receive_char(SH7750State * s, uint8_t c)
{
    if (s->scssr1 & SH7750_SCSSR1_RDRF) {
	s->scssr1 |= SH7750_SCSSR1_ORER;
	return;
    }

    s->scrdr1 = c;
    s->scssr1 |= SH7750_SCSSR1_RDRF;
}

static void serial1_receive(void *opaque, const uint8_t * buf, int size)
{
    SH7750State *s = opaque;
    int i;

    for (i = 0; i < size; i++) {
	serial1_receive_char(s, buf[i]);
    }
}

static void serial1_event(void *opaque, int event)
{
    assert(0);
}

static void serial1_maybe_send(SH7750State * s)
{
    uint8_t c;

    if (s->scssr1 & SH7750_SCSSR1_TDRE)
	return;
    c = s->sctdr1;
    s->scssr1 |= SH7750_SCSSR1_TDRE | SH7750_SCSSR1_TEND;
    if (s->scscr1 & SH7750_SCSCR_TIE) {
	fprintf(stderr, "interrupts for serial port 1 not implemented\n");
	assert(0);
    }
    /* XXXXX Check for errors in write */
    qemu_chr_write(s->serial1, &c, 1);
}

static void serial1_change_scssr1(SH7750State * s, uint8_t mem_value)
{
    uint8_t new_flags;

    /* If transmit disable, TDRE and TEND stays up */
    if ((s->scscr1 & SH7750_SCSCR_TE) == 0) {
	mem_value |= SH7750_SCSSR1_TDRE | SH7750_SCSSR1_TEND;
    }

    /* Only clear bits which have been read before and do not set any bit
       in the flags */
    new_flags = s->scssr1 & ~s->scssr1_read;	/* Preserve unread flags */
    new_flags &= mem_value | ~s->scssr1_read;	/* Clear read flags */

    s->scssr1 = (new_flags & 0xf8) | (mem_value & 1);
    s->scssr1_read &= mem_value;

    /* If TDRE has been cleared, TEND will also be cleared */
    if ((s->scssr1 & SH7750_SCSSR1_TDRE) == 0) {
	s->scssr1 &= ~SH7750_SCSSR1_TEND;
    }

    /* Check for transmission to start */
    serial1_maybe_send(s);
}

static void serial1_update_parameters(SH7750State * s)
{
    QEMUSerialSetParams ssp;

    if (s->scsmr1 & SH7750_SCSMR_CHR_7)
	ssp.data_bits = 7;
    else
	ssp.data_bits = 8;
    if (s->scsmr1 & SH7750_SCSMR_PE) {
	if (s->scsmr1 & SH7750_SCSMR_PM_ODD)
	    ssp.parity = 'O';
	else
	    ssp.parity = 'E';
    } else
	ssp.parity = 'N';
    if (s->scsmr1 & SH7750_SCSMR_STOP_2)
	ssp.stop_bits = 2;
    else
	ssp.stop_bits = 1;
    fprintf(stderr, "SCSMR1=%04x SCBRR1=%02x\n", s->scsmr1, s->scbrr1);
    ssp.speed = s->periph_freq /
	(32 * s->scbrr1 * (1 << (2 * (s->scsmr1 & 3)))) - 1;
    fprintf(stderr, "data bits=%d, stop bits=%d, parity=%c, speed=%d\n",
	    ssp.data_bits, ssp.stop_bits, ssp.parity, ssp.speed);
    qemu_chr_ioctl(s->serial1, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
}

static void scscr1_changed(SH7750State * s)
{
    if (s->scscr1 & (SH7750_SCSCR_TE | SH7750_SCSCR_RE)) {
	if (!s->serial1) {
	    fprintf(stderr, "serial port 1 not bound to anything\n");
	    assert(0);
	}
	serial1_update_parameters(s);
    }
    if ((s->scscr1 & SH7750_SCSCR_RE) == 0) {
	s->scssr1 |= SH7750_SCSSR1_TDRE;
    }
}

static void init_serial1(SH7750State * s, int serial_nb)
{
    CharDriverState *chr;

    s->scssr1 = 0x84;
    chr = serial_hds[serial_nb];
    if (!chr) {
	fprintf(stderr,
		"no serial port associated to SH7750 first serial port\n");
	return;
    }

    s->serial1 = chr;
    qemu_chr_add_handlers(chr, serial1_can_receive,
			  serial1_receive, serial1_event, s);
}

/**********************************************************************
 Second serial port
**********************************************************************/

static int serial2_can_receive(void *opaque)
{
    SH7750State *s = opaque;
    static uint8_t max_fifo_size[] = { 15, 1, 4, 6, 8, 10, 12, 14 };

    return s->serial2_receive_fifo.length <
	max_fifo_size[(s->scfcr2 >> 9) & 7];
}

static void serial2_adjust_receive_flags(SH7750State * s)
{
    static uint8_t max_fifo_size[] = { 1, 4, 8, 14 };

    /* XXXXX Add interrupt generation */
    if (s->serial2_receive_fifo.length >=
	max_fifo_size[(s->scfcr2 >> 7) & 3]) {
	s->scfsr2 |= SH7750_SCFSR2_RDF;
	s->scfsr2 &= ~SH7750_SCFSR2_DR;
    } else {
	s->scfsr2 &= ~SH7750_SCFSR2_RDF;
	if (s->serial2_receive_fifo.length > 0)
	    s->scfsr2 |= SH7750_SCFSR2_DR;
	else
	    s->scfsr2 &= ~SH7750_SCFSR2_DR;
    }
}

static void serial2_append_char(SH7750State * s, uint8_t c)
{
    if (s->serial2_receive_fifo.length == 16) {
	/* Overflow */
	s->sclsr2 |= SH7750_SCLSR2_ORER;
	return;
    }

    s->serial2_receive_fifo.data[s->serial2_receive_fifo.write_idx++] = c;
    s->serial2_receive_fifo.length++;
    serial2_adjust_receive_flags(s);
}

static void serial2_receive(void *opaque, const uint8_t * buf, int size)
{
    SH7750State *s = opaque;
    int i;

    for (i = 0; i < size; i++)
	serial2_append_char(s, buf[i]);
}

static void serial2_event(void *opaque, int event)
{
    /* XXXXX */
    assert(0);
}

static void serial2_update_parameters(SH7750State * s)
{
    QEMUSerialSetParams ssp;

    if (s->scsmr2 & SH7750_SCSMR_CHR_7)
	ssp.data_bits = 7;
    else
	ssp.data_bits = 8;
    if (s->scsmr2 & SH7750_SCSMR_PE) {
	if (s->scsmr2 & SH7750_SCSMR_PM_ODD)
	    ssp.parity = 'O';
	else
	    ssp.parity = 'E';
    } else
	ssp.parity = 'N';
    if (s->scsmr2 & SH7750_SCSMR_STOP_2)
	ssp.stop_bits = 2;
    else
	ssp.stop_bits = 1;
    fprintf(stderr, "SCSMR2=%04x SCBRR2=%02x\n", s->scsmr2, s->scbrr2);
    ssp.speed = s->periph_freq /
	(32 * s->scbrr2 * (1 << (2 * (s->scsmr2 & 3)))) - 1;
    fprintf(stderr, "data bits=%d, stop bits=%d, parity=%c, speed=%d\n",
	    ssp.data_bits, ssp.stop_bits, ssp.parity, ssp.speed);
    qemu_chr_ioctl(s->serial2, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
}

static void scscr2_changed(SH7750State * s)
{
    if (s->scscr2 & (SH7750_SCSCR_TE | SH7750_SCSCR_RE)) {
	if (!s->serial2) {
	    fprintf(stderr, "serial port 2 not bound to anything\n");
	    assert(0);
	}
	serial2_update_parameters(s);
    }
}

static void init_serial2(SH7750State * s, int serial_nb)
{
    CharDriverState *chr;

    s->scfsr2 = 0x0060;

    chr = serial_hds[serial_nb];
    if (!chr) {
	fprintf(stderr,
		"no serial port associated to SH7750 second serial port\n");
	return;
    }

    s->serial2 = chr;
    qemu_chr_add_handlers(chr, serial2_can_receive,
			  serial2_receive, serial1_event, s);
}

static void init_serial_ports(SH7750State * s)
{
    init_serial1(s, 0);
    init_serial2(s, 1);
}

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
    SH7750State *s = opaque;
    uint8_t r;

    switch (addr) {
    case SH7750_SCSSR1_A7:
	r = s->scssr1;
	s->scssr1_read |= r;
	return s->scssr1;
    case SH7750_SCRDR1_A7:
	s->scssr1 &= ~SH7750_SCSSR1_RDRF;
	return s->scrdr1;
    default:
	error_access("byte read", addr);
	assert(0);
    }
}

static uint32_t sh7750_mem_readw(void *opaque, target_phys_addr_t addr)
{
    SH7750State *s = opaque;
    uint16_t r;

    switch (addr) {
    case SH7750_RFCR_A7:
	fprintf(stderr,
		"Read access to refresh count register, incrementing\n");
	return s->rfcr++;
    case SH7750_TCR0_A7:
	return s->tcr0;
    case SH7750_SCLSR2_A7:
	/* Read and clear overflow bit */
	r = s->sclsr2;
	s->sclsr2 = 0;
	return r;
    case SH7750_SCSFR2_A7:
	return s->scfsr2;
    case SH7750_PDTRA_A7:
	return porta_lines(s);
    case SH7750_PDTRB_A7:
	return portb_lines(s);
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
    default:
	error_access("long read", addr);
	assert(0);
    }
}

static void sh7750_mem_writeb(void *opaque, target_phys_addr_t addr,
			      uint32_t mem_value)
{
    SH7750State *s = opaque;

    switch (addr) {
	/* PRECHARGE ? XXXXX */
    case SH7750_PRECHARGE0_A7:
    case SH7750_PRECHARGE1_A7:
	ignore_access("byte write", addr);
	return;
    case SH7750_SCBRR2_A7:
	s->scbrr2 = mem_value;
	return;
    case SH7750_TSTR_A7:
	s->tstr = mem_value;
	timer_start_changed(s);
	return;
    case SH7750_SCSCR1_A7:
	s->scscr1 = mem_value;
	scscr1_changed(s);
	return;
    case SH7750_SCSMR1_A7:
	s->scsmr1 = mem_value;
	return;
    case SH7750_SCBRR1_A7:
	s->scbrr1 = mem_value;
	return;
    case SH7750_SCTDR1_A7:
	s->scssr1 &= ~SH7750_SCSSR1_TEND;
	s->sctdr1 = mem_value;
	return;
    case SH7750_SCSSR1_A7:
	serial1_change_scssr1(s, mem_value);
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
    case SH7750_SCBRR1_A7:
    case SH7750_SCBRR2_A7:
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
    case SH7750_SCLSR2_A7:
	s->sclsr2 = mem_value;
	return;
    case SH7750_SCSCR2_A7:
	s->scscr2 = mem_value;
	scscr2_changed(s);
	return;
    case SH7750_SCFCR2_A7:
	s->scfcr2 = mem_value;
	return;
    case SH7750_SCSMR2_A7:
	s->scsmr2 = mem_value;
	return;
    case SH7750_TCR0_A7:
	s->tcr0 = mem_value;
	return;
    case SH7750_GPIOIC_A7:
	s->gpioic = mem_value;
	if (mem_value != 0) {
	    fprintf(stderr, "I/O interrupts not implemented\n");
	    assert(0);
	}
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
    case SH7750_TCNT0_A7:
	s->tcnt0 = mem_value & 0xf;
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
    init_timers(s);
    init_serial_ports(s);
    return s;
}
