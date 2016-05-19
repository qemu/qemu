/*
 * SH7750 device
 *
 * Copyright (c) 2007 Magnus Damm
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
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sh4/sh.h"
#include "sysemu/sysemu.h"
#include "sh7750_regs.h"
#include "sh7750_regnames.h"
#include "hw/sh4/sh_intc.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/address-spaces.h"

#define NB_DEVICES 4

typedef struct SH7750State {
    MemoryRegion iomem;
    MemoryRegion iomem_1f0;
    MemoryRegion iomem_ff0;
    MemoryRegion iomem_1f8;
    MemoryRegion iomem_ff8;
    MemoryRegion iomem_1fc;
    MemoryRegion iomem_ffc;
    MemoryRegion mmct_iomem;
    /* CPU */
    SuperHCPU *cpu;
    /* Peripheral frequency in Hz */
    uint32_t periph_freq;
    /* SDRAM controller */
    uint32_t bcr1;
    uint16_t bcr2;
    uint16_t bcr3;
    uint32_t bcr4;
    uint16_t rfcr;
    /* PCMCIA controller */
    uint16_t pcr;
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

    struct intc_desc intc;
} SH7750State;

static inline int has_bcr3_and_bcr4(SH7750State * s)
{
    return s->cpu->env.features & SH_FEATURE_BCR3_AND_BCR4;
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

static void error_access(const char *kind, hwaddr addr)
{
    fprintf(stderr, "%s to %s (0x" TARGET_FMT_plx ") not supported\n",
	    kind, regname(addr), addr);
}

static void ignore_access(const char *kind, hwaddr addr)
{
    fprintf(stderr, "%s to %s (0x" TARGET_FMT_plx ") ignored\n",
	    kind, regname(addr), addr);
}

static uint32_t sh7750_mem_readb(void *opaque, hwaddr addr)
{
    switch (addr) {
    default:
	error_access("byte read", addr);
        abort();
    }
}

static uint32_t sh7750_mem_readw(void *opaque, hwaddr addr)
{
    SH7750State *s = opaque;

    switch (addr) {
    case SH7750_BCR2_A7:
	return s->bcr2;
    case SH7750_BCR3_A7:
	if(!has_bcr3_and_bcr4(s))
	    error_access("word read", addr);
	return s->bcr3;
    case SH7750_FRQCR_A7:
	return 0;
    case SH7750_PCR_A7:
	return s->pcr;
    case SH7750_RFCR_A7:
	fprintf(stderr,
		"Read access to refresh count register, incrementing\n");
	return s->rfcr++;
    case SH7750_PDTRA_A7:
	return porta_lines(s);
    case SH7750_PDTRB_A7:
	return portb_lines(s);
    case SH7750_RTCOR_A7:
    case SH7750_RTCNT_A7:
    case SH7750_RTCSR_A7:
	ignore_access("word read", addr);
	return 0;
    default:
	error_access("word read", addr);
        abort();
    }
}

static uint32_t sh7750_mem_readl(void *opaque, hwaddr addr)
{
    SH7750State *s = opaque;
    SuperHCPUClass *scc;

    switch (addr) {
    case SH7750_BCR1_A7:
	return s->bcr1;
    case SH7750_BCR4_A7:
	if(!has_bcr3_and_bcr4(s))
	    error_access("long read", addr);
	return s->bcr4;
    case SH7750_WCR1_A7:
    case SH7750_WCR2_A7:
    case SH7750_WCR3_A7:
    case SH7750_MCR_A7:
        ignore_access("long read", addr);
        return 0;
    case SH7750_MMUCR_A7:
        return s->cpu->env.mmucr;
    case SH7750_PTEH_A7:
        return s->cpu->env.pteh;
    case SH7750_PTEL_A7:
        return s->cpu->env.ptel;
    case SH7750_TTB_A7:
        return s->cpu->env.ttb;
    case SH7750_TEA_A7:
        return s->cpu->env.tea;
    case SH7750_TRA_A7:
        return s->cpu->env.tra;
    case SH7750_EXPEVT_A7:
        return s->cpu->env.expevt;
    case SH7750_INTEVT_A7:
        return s->cpu->env.intevt;
    case SH7750_CCR_A7:
	return s->ccr;
    case 0x1f000030:		/* Processor version */
        scc = SUPERH_CPU_GET_CLASS(s->cpu);
        return scc->pvr;
    case 0x1f000040:		/* Cache version */
        scc = SUPERH_CPU_GET_CLASS(s->cpu);
        return scc->cvr;
    case 0x1f000044:		/* Processor revision */
        scc = SUPERH_CPU_GET_CLASS(s->cpu);
        return scc->prr;
    default:
	error_access("long read", addr);
        abort();
    }
}

#define is_in_sdrmx(a, x) (a >= SH7750_SDMR ## x ## _A7 \
			&& a <= (SH7750_SDMR ## x ## _A7 + SH7750_SDMR ## x ## _REGNB))
static void sh7750_mem_writeb(void *opaque, hwaddr addr,
			      uint32_t mem_value)
{

    if (is_in_sdrmx(addr, 2) || is_in_sdrmx(addr, 3)) {
	ignore_access("byte write", addr);
	return;
    }

    error_access("byte write", addr);
    abort();
}

static void sh7750_mem_writew(void *opaque, hwaddr addr,
			      uint32_t mem_value)
{
    SH7750State *s = opaque;
    uint16_t temp;

    switch (addr) {
	/* SDRAM controller */
    case SH7750_BCR2_A7:
        s->bcr2 = mem_value;
        return;
    case SH7750_BCR3_A7:
	if(!has_bcr3_and_bcr4(s))
	    error_access("word write", addr);
	s->bcr3 = mem_value;
	return;
    case SH7750_PCR_A7:
	s->pcr = mem_value;
	return;
    case SH7750_RTCNT_A7:
    case SH7750_RTCOR_A7:
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
            abort();
	}
	return;
    default:
	error_access("word write", addr);
        abort();
    }
}

static void sh7750_mem_writel(void *opaque, hwaddr addr,
			      uint32_t mem_value)
{
    SH7750State *s = opaque;
    uint16_t temp;

    switch (addr) {
	/* SDRAM controller */
    case SH7750_BCR1_A7:
        s->bcr1 = mem_value;
        return;
    case SH7750_BCR4_A7:
	if(!has_bcr3_and_bcr4(s))
	    error_access("long write", addr);
	s->bcr4 = mem_value;
	return;
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
        if (mem_value & MMUCR_TI) {
            cpu_sh4_invalidate_tlb(&s->cpu->env);
        }
        s->cpu->env.mmucr = mem_value & ~MMUCR_TI;
        return;
    case SH7750_PTEH_A7:
        /* If asid changes, clear all registered tlb entries. */
        if ((s->cpu->env.pteh & 0xff) != (mem_value & 0xff)) {
            tlb_flush(CPU(s->cpu), 1);
        }
        s->cpu->env.pteh = mem_value;
        return;
    case SH7750_PTEL_A7:
        s->cpu->env.ptel = mem_value;
        return;
    case SH7750_PTEA_A7:
        s->cpu->env.ptea = mem_value & 0x0000000f;
        return;
    case SH7750_TTB_A7:
        s->cpu->env.ttb = mem_value;
        return;
    case SH7750_TEA_A7:
        s->cpu->env.tea = mem_value;
        return;
    case SH7750_TRA_A7:
        s->cpu->env.tra = mem_value & 0x000007ff;
        return;
    case SH7750_EXPEVT_A7:
        s->cpu->env.expevt = mem_value & 0x000007ff;
        return;
    case SH7750_INTEVT_A7:
        s->cpu->env.intevt = mem_value & 0x000007ff;
        return;
    case SH7750_CCR_A7:
	s->ccr = mem_value;
	return;
    default:
	error_access("long write", addr);
        abort();
    }
}

static const MemoryRegionOps sh7750_mem_ops = {
    .old_mmio = {
        .read = {sh7750_mem_readb,
                 sh7750_mem_readw,
                 sh7750_mem_readl },
        .write = {sh7750_mem_writeb,
                  sh7750_mem_writew,
                  sh7750_mem_writel },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* sh775x interrupt controller tables for sh_intc.c
 * stolen from linux/arch/sh/kernel/cpu/sh4/setup-sh7750.c
 */

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRL_0, IRL_1, IRL_2, IRL_3, IRL_4, IRL_5, IRL_6, IRL_7,
	IRL_8, IRL_9, IRL_A, IRL_B, IRL_C, IRL_D, IRL_E,
	IRL0, IRL1, IRL2, IRL3,
	HUDI, GPIOI,
	DMAC_DMTE0, DMAC_DMTE1, DMAC_DMTE2, DMAC_DMTE3,
	DMAC_DMTE4, DMAC_DMTE5, DMAC_DMTE6, DMAC_DMTE7,
	DMAC_DMAE,
	PCIC0_PCISERR, PCIC1_PCIERR, PCIC1_PCIPWDWN, PCIC1_PCIPWON,
	PCIC1_PCIDMA0, PCIC1_PCIDMA1, PCIC1_PCIDMA2, PCIC1_PCIDMA3,
	TMU3, TMU4, TMU0, TMU1, TMU2_TUNI, TMU2_TICPI,
	RTC_ATI, RTC_PRI, RTC_CUI,
	SCI1_ERI, SCI1_RXI, SCI1_TXI, SCI1_TEI,
	SCIF_ERI, SCIF_RXI, SCIF_BRI, SCIF_TXI,
	WDT,
	REF_RCMI, REF_ROVI,

	/* interrupt groups */
	DMAC, PCIC1, TMU2, RTC, SCI1, SCIF, REF,
	/* irl bundle */
	IRL,

	NR_SOURCES,
};

static struct intc_vect vectors[] = {
	INTC_VECT(HUDI, 0x600), INTC_VECT(GPIOI, 0x620),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2_TUNI, 0x440), INTC_VECT(TMU2_TICPI, 0x460),
	INTC_VECT(RTC_ATI, 0x480), INTC_VECT(RTC_PRI, 0x4a0),
	INTC_VECT(RTC_CUI, 0x4c0),
	INTC_VECT(SCI1_ERI, 0x4e0), INTC_VECT(SCI1_RXI, 0x500),
	INTC_VECT(SCI1_TXI, 0x520), INTC_VECT(SCI1_TEI, 0x540),
	INTC_VECT(SCIF_ERI, 0x700), INTC_VECT(SCIF_RXI, 0x720),
	INTC_VECT(SCIF_BRI, 0x740), INTC_VECT(SCIF_TXI, 0x760),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(REF_RCMI, 0x580), INTC_VECT(REF_ROVI, 0x5a0),
};

static struct intc_group groups[] = {
	INTC_GROUP(TMU2, TMU2_TUNI, TMU2_TICPI),
	INTC_GROUP(RTC, RTC_ATI, RTC_PRI, RTC_CUI),
	INTC_GROUP(SCI1, SCI1_ERI, SCI1_RXI, SCI1_TXI, SCI1_TEI),
	INTC_GROUP(SCIF, SCIF_ERI, SCIF_RXI, SCIF_BRI, SCIF_TXI),
	INTC_GROUP(REF, REF_RCMI, REF_ROVI),
};

static struct intc_prio_reg prio_registers[] = {
	{ 0xffd00004, 0, 16, 4, /* IPRA */ { TMU0, TMU1, TMU2, RTC } },
	{ 0xffd00008, 0, 16, 4, /* IPRB */ { WDT, REF, SCI1, 0 } },
	{ 0xffd0000c, 0, 16, 4, /* IPRC */ { GPIOI, DMAC, SCIF, HUDI } },
	{ 0xffd00010, 0, 16, 4, /* IPRD */ { IRL0, IRL1, IRL2, IRL3 } },
	{ 0xfe080000, 0, 32, 4, /* INTPRI00 */ { 0, 0, 0, 0,
						 TMU4, TMU3,
						 PCIC1, PCIC0_PCISERR } },
};

/* SH7750, SH7750S, SH7751 and SH7091 all have 4-channel DMA controllers */

static struct intc_vect vectors_dma4[] = {
	INTC_VECT(DMAC_DMTE0, 0x640), INTC_VECT(DMAC_DMTE1, 0x660),
	INTC_VECT(DMAC_DMTE2, 0x680), INTC_VECT(DMAC_DMTE3, 0x6a0),
	INTC_VECT(DMAC_DMAE, 0x6c0),
};

static struct intc_group groups_dma4[] = {
	INTC_GROUP(DMAC, DMAC_DMTE0, DMAC_DMTE1, DMAC_DMTE2,
		   DMAC_DMTE3, DMAC_DMAE),
};

/* SH7750R and SH7751R both have 8-channel DMA controllers */

static struct intc_vect vectors_dma8[] = {
	INTC_VECT(DMAC_DMTE0, 0x640), INTC_VECT(DMAC_DMTE1, 0x660),
	INTC_VECT(DMAC_DMTE2, 0x680), INTC_VECT(DMAC_DMTE3, 0x6a0),
	INTC_VECT(DMAC_DMTE4, 0x780), INTC_VECT(DMAC_DMTE5, 0x7a0),
	INTC_VECT(DMAC_DMTE6, 0x7c0), INTC_VECT(DMAC_DMTE7, 0x7e0),
	INTC_VECT(DMAC_DMAE, 0x6c0),
};

static struct intc_group groups_dma8[] = {
	INTC_GROUP(DMAC, DMAC_DMTE0, DMAC_DMTE1, DMAC_DMTE2,
		   DMAC_DMTE3, DMAC_DMTE4, DMAC_DMTE5,
		   DMAC_DMTE6, DMAC_DMTE7, DMAC_DMAE),
};

/* SH7750R, SH7751 and SH7751R all have two extra timer channels */

static struct intc_vect vectors_tmu34[] = {
	INTC_VECT(TMU3, 0xb00), INTC_VECT(TMU4, 0xb80),
};

static struct intc_mask_reg mask_registers[] = {
	{ 0xfe080040, 0xfe080060, 32, /* INTMSK00 / INTMSKCLR00 */
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, TMU4, TMU3,
	    PCIC1_PCIERR, PCIC1_PCIPWDWN, PCIC1_PCIPWON,
	    PCIC1_PCIDMA0, PCIC1_PCIDMA1, PCIC1_PCIDMA2,
	    PCIC1_PCIDMA3, PCIC0_PCISERR } },
};

/* SH7750S, SH7750R, SH7751 and SH7751R all have IRLM priority registers */

static struct intc_vect vectors_irlm[] = {
	INTC_VECT(IRL0, 0x240), INTC_VECT(IRL1, 0x2a0),
	INTC_VECT(IRL2, 0x300), INTC_VECT(IRL3, 0x360),
};

/* SH7751 and SH7751R both have PCI */

static struct intc_vect vectors_pci[] = {
	INTC_VECT(PCIC0_PCISERR, 0xa00), INTC_VECT(PCIC1_PCIERR, 0xae0),
	INTC_VECT(PCIC1_PCIPWDWN, 0xac0), INTC_VECT(PCIC1_PCIPWON, 0xaa0),
	INTC_VECT(PCIC1_PCIDMA0, 0xa80), INTC_VECT(PCIC1_PCIDMA1, 0xa60),
	INTC_VECT(PCIC1_PCIDMA2, 0xa40), INTC_VECT(PCIC1_PCIDMA3, 0xa20),
};

static struct intc_group groups_pci[] = {
	INTC_GROUP(PCIC1, PCIC1_PCIERR, PCIC1_PCIPWDWN, PCIC1_PCIPWON,
		   PCIC1_PCIDMA0, PCIC1_PCIDMA1, PCIC1_PCIDMA2, PCIC1_PCIDMA3),
};

static struct intc_vect vectors_irl[] = {
	INTC_VECT(IRL_0, 0x200),
	INTC_VECT(IRL_1, 0x220),
	INTC_VECT(IRL_2, 0x240),
	INTC_VECT(IRL_3, 0x260),
	INTC_VECT(IRL_4, 0x280),
	INTC_VECT(IRL_5, 0x2a0),
	INTC_VECT(IRL_6, 0x2c0),
	INTC_VECT(IRL_7, 0x2e0),
	INTC_VECT(IRL_8, 0x300),
	INTC_VECT(IRL_9, 0x320),
	INTC_VECT(IRL_A, 0x340),
	INTC_VECT(IRL_B, 0x360),
	INTC_VECT(IRL_C, 0x380),
	INTC_VECT(IRL_D, 0x3a0),
	INTC_VECT(IRL_E, 0x3c0),
};

static struct intc_group groups_irl[] = {
	INTC_GROUP(IRL, IRL_0, IRL_1, IRL_2, IRL_3, IRL_4, IRL_5, IRL_6,
		IRL_7, IRL_8, IRL_9, IRL_A, IRL_B, IRL_C, IRL_D, IRL_E),
};

/**********************************************************************
 Memory mapped cache and TLB
**********************************************************************/

#define MM_REGION_MASK   0x07000000
#define MM_ICACHE_ADDR   (0)
#define MM_ICACHE_DATA   (1)
#define MM_ITLB_ADDR     (2)
#define MM_ITLB_DATA     (3)
#define MM_OCACHE_ADDR   (4)
#define MM_OCACHE_DATA   (5)
#define MM_UTLB_ADDR     (6)
#define MM_UTLB_DATA     (7)
#define MM_REGION_TYPE(addr)  ((addr & MM_REGION_MASK) >> 24)

static uint64_t invalid_read(void *opaque, hwaddr addr)
{
    abort();

    return 0;
}

static uint64_t sh7750_mmct_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    SH7750State *s = opaque;
    uint32_t ret = 0;

    if (size != 4) {
        return invalid_read(opaque, addr);
    }

    switch (MM_REGION_TYPE(addr)) {
    case MM_ICACHE_ADDR:
    case MM_ICACHE_DATA:
        /* do nothing */
	break;
    case MM_ITLB_ADDR:
        ret = cpu_sh4_read_mmaped_itlb_addr(&s->cpu->env, addr);
        break;
    case MM_ITLB_DATA:
        ret = cpu_sh4_read_mmaped_itlb_data(&s->cpu->env, addr);
        break;
    case MM_OCACHE_ADDR:
    case MM_OCACHE_DATA:
        /* do nothing */
	break;
    case MM_UTLB_ADDR:
        ret = cpu_sh4_read_mmaped_utlb_addr(&s->cpu->env, addr);
        break;
    case MM_UTLB_DATA:
        ret = cpu_sh4_read_mmaped_utlb_data(&s->cpu->env, addr);
        break;
    default:
        abort();
    }

    return ret;
}

static void invalid_write(void *opaque, hwaddr addr,
                          uint64_t mem_value)
{
    abort();
}

static void sh7750_mmct_write(void *opaque, hwaddr addr,
                              uint64_t mem_value, unsigned size)
{
    SH7750State *s = opaque;

    if (size != 4) {
        invalid_write(opaque, addr, mem_value);
    }

    switch (MM_REGION_TYPE(addr)) {
    case MM_ICACHE_ADDR:
    case MM_ICACHE_DATA:
        /* do nothing */
	break;
    case MM_ITLB_ADDR:
        cpu_sh4_write_mmaped_itlb_addr(&s->cpu->env, addr, mem_value);
        break;
    case MM_ITLB_DATA:
        cpu_sh4_write_mmaped_itlb_data(&s->cpu->env, addr, mem_value);
        abort();
	break;
    case MM_OCACHE_ADDR:
    case MM_OCACHE_DATA:
        /* do nothing */
	break;
    case MM_UTLB_ADDR:
        cpu_sh4_write_mmaped_utlb_addr(&s->cpu->env, addr, mem_value);
	break;
    case MM_UTLB_DATA:
        cpu_sh4_write_mmaped_utlb_data(&s->cpu->env, addr, mem_value);
	break;
    default:
        abort();
	break;
    }
}

static const MemoryRegionOps sh7750_mmct_ops = {
    .read = sh7750_mmct_read,
    .write = sh7750_mmct_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

SH7750State *sh7750_init(SuperHCPU *cpu, MemoryRegion *sysmem)
{
    SH7750State *s;

    s = g_malloc0(sizeof(SH7750State));
    s->cpu = cpu;
    s->periph_freq = 60000000;	/* 60MHz */
    memory_region_init_io(&s->iomem, NULL, &sh7750_mem_ops, s,
                          "memory", 0x1fc01000);

    memory_region_init_alias(&s->iomem_1f0, NULL, "memory-1f0",
                             &s->iomem, 0x1f000000, 0x1000);
    memory_region_add_subregion(sysmem, 0x1f000000, &s->iomem_1f0);

    memory_region_init_alias(&s->iomem_ff0, NULL, "memory-ff0",
                             &s->iomem, 0x1f000000, 0x1000);
    memory_region_add_subregion(sysmem, 0xff000000, &s->iomem_ff0);

    memory_region_init_alias(&s->iomem_1f8, NULL, "memory-1f8",
                             &s->iomem, 0x1f800000, 0x1000);
    memory_region_add_subregion(sysmem, 0x1f800000, &s->iomem_1f8);

    memory_region_init_alias(&s->iomem_ff8, NULL, "memory-ff8",
                             &s->iomem, 0x1f800000, 0x1000);
    memory_region_add_subregion(sysmem, 0xff800000, &s->iomem_ff8);

    memory_region_init_alias(&s->iomem_1fc, NULL, "memory-1fc",
                             &s->iomem, 0x1fc00000, 0x1000);
    memory_region_add_subregion(sysmem, 0x1fc00000, &s->iomem_1fc);

    memory_region_init_alias(&s->iomem_ffc, NULL, "memory-ffc",
                             &s->iomem, 0x1fc00000, 0x1000);
    memory_region_add_subregion(sysmem, 0xffc00000, &s->iomem_ffc);

    memory_region_init_io(&s->mmct_iomem, NULL, &sh7750_mmct_ops, s,
                          "cache-and-tlb", 0x08000000);
    memory_region_add_subregion(sysmem, 0xf0000000, &s->mmct_iomem);

    sh_intc_init(sysmem, &s->intc, NR_SOURCES,
		 _INTC_ARRAY(mask_registers),
		 _INTC_ARRAY(prio_registers));

    sh_intc_register_sources(&s->intc,
			     _INTC_ARRAY(vectors),
			     _INTC_ARRAY(groups));

    cpu->env.intc_handle = &s->intc;

    sh_serial_init(sysmem, 0x1fe00000,
                   0, s->periph_freq, serial_hds[0],
                   s->intc.irqs[SCI1_ERI],
                   s->intc.irqs[SCI1_RXI],
                   s->intc.irqs[SCI1_TXI],
                   s->intc.irqs[SCI1_TEI],
                   NULL);
    sh_serial_init(sysmem, 0x1fe80000,
                   SH_SERIAL_FEAT_SCIF,
                   s->periph_freq, serial_hds[1],
                   s->intc.irqs[SCIF_ERI],
                   s->intc.irqs[SCIF_RXI],
                   s->intc.irqs[SCIF_TXI],
                   NULL,
                   s->intc.irqs[SCIF_BRI]);

    tmu012_init(sysmem, 0x1fd80000,
		TMU012_FEAT_TOCR | TMU012_FEAT_3CHAN | TMU012_FEAT_EXTCLK,
		s->periph_freq,
		s->intc.irqs[TMU0],
		s->intc.irqs[TMU1],
		s->intc.irqs[TMU2_TUNI],
		s->intc.irqs[TMU2_TICPI]);

    if (cpu->env.id & (SH_CPU_SH7750 | SH_CPU_SH7750S | SH_CPU_SH7751)) {
        sh_intc_register_sources(&s->intc,
				 _INTC_ARRAY(vectors_dma4),
				 _INTC_ARRAY(groups_dma4));
    }

    if (cpu->env.id & (SH_CPU_SH7750R | SH_CPU_SH7751R)) {
        sh_intc_register_sources(&s->intc,
				 _INTC_ARRAY(vectors_dma8),
				 _INTC_ARRAY(groups_dma8));
    }

    if (cpu->env.id & (SH_CPU_SH7750R | SH_CPU_SH7751 | SH_CPU_SH7751R)) {
        sh_intc_register_sources(&s->intc,
				 _INTC_ARRAY(vectors_tmu34),
				 NULL, 0);
        tmu012_init(sysmem, 0x1e100000, 0, s->periph_freq,
		    s->intc.irqs[TMU3],
		    s->intc.irqs[TMU4],
		    NULL, NULL);
    }

    if (cpu->env.id & (SH_CPU_SH7751_ALL)) {
        sh_intc_register_sources(&s->intc,
				 _INTC_ARRAY(vectors_pci),
				 _INTC_ARRAY(groups_pci));
    }

    if (cpu->env.id & (SH_CPU_SH7750S | SH_CPU_SH7750R | SH_CPU_SH7751_ALL)) {
        sh_intc_register_sources(&s->intc,
				 _INTC_ARRAY(vectors_irlm),
				 NULL, 0);
    }

    sh_intc_register_sources(&s->intc,
				_INTC_ARRAY(vectors_irl),
				_INTC_ARRAY(groups_irl));
    return s;
}

qemu_irq sh7750_irl(SH7750State *s)
{
    sh_intc_toggle_source(sh_intc_source(&s->intc, IRL), 1, 0); /* enable */
    return qemu_allocate_irq(sh_intc_set_irl, sh_intc_source(&s->intc, IRL), 0);
}
