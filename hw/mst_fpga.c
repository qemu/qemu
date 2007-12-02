/*
 * PXA270-based Intel Mainstone platforms.
 * FPGA driver
 *
 * Copyright (c) 2007 by Armin Kuster <akuster@kama-aina.net> or
 *                                    <akuster@mvista.com>
 *
 * This code is licensed under the GNU GPL v2.
 */
#include "hw.h"
#include "pxa.h"
#include "mainstone.h"

/* Mainstone FPGA for extern irqs */
#define FPGA_GPIO_PIN	0
#define MST_NUM_IRQS	16
#define MST_BASE		MST_FPGA_PHYS
#define MST_LEDDAT1		0x10
#define MST_LEDDAT2		0x14
#define MST_LEDCTRL		0x40
#define MST_GPSWR		0x60
#define MST_MSCWR1		0x80
#define MST_MSCWR2		0x84
#define MST_MSCWR3		0x88
#define MST_MSCRD		0x90
#define MST_INTMSKENA	0xc0
#define MST_INTSETCLR	0xd0
#define MST_PCMCIA0		0xe0
#define MST_PCMCIA1		0xe4

typedef struct mst_irq_state{
	target_phys_addr_t target_base;
	qemu_irq *parent;
	qemu_irq *pins;

	uint32_t prev_level;
	uint32_t leddat1;
	uint32_t leddat2;
	uint32_t ledctrl;
	uint32_t gpswr;
	uint32_t mscwr1;
	uint32_t mscwr2;
	uint32_t mscwr3;
	uint32_t mscrd;
	uint32_t intmskena;
	uint32_t intsetclr;
	uint32_t pcmcia0;
	uint32_t pcmcia1;
}mst_irq_state;

static void
mst_fpga_update_gpio(mst_irq_state *s)
{
	uint32_t level, diff;
	int bit;
	level = s->prev_level ^ s->intsetclr;

	for (diff = s->prev_level ^ level; diff; diff ^= 1 << bit) {
		bit = ffs(diff) - 1;
		qemu_set_irq(s->pins[bit], (level >> bit) & 1 );
	}
	s->prev_level = level;
}

static void
mst_fpga_set_irq(void *opaque, int irq, int level)
{
	mst_irq_state *s = (mst_irq_state *)opaque;

	if (level)
		s->prev_level |= 1u << irq;
	else
		s->prev_level &= ~(1u << irq);

	if(s->intmskena & (1u << irq)) {
		s->intsetclr = 1u << irq;
		qemu_set_irq(s->parent[0], level);
	}
}


static uint32_t
mst_fpga_readb(void *opaque, target_phys_addr_t addr)
{
	mst_irq_state *s = (mst_irq_state *) opaque;
	addr -= s->target_base;

	switch (addr) {
	case MST_LEDDAT1:
		return s->leddat1;
	case MST_LEDDAT2:
		return s->leddat2;
	case MST_LEDCTRL:
		return s->ledctrl;
	case MST_GPSWR:
		return s->gpswr;
	case MST_MSCWR1:
		return s->mscwr1;
	case MST_MSCWR2:
		return s->mscwr2;
	case MST_MSCWR3:
		return s->mscwr3;
	case MST_MSCRD:
		return s->mscrd;
	case MST_INTMSKENA:
		return s->intmskena;
	case MST_INTSETCLR:
		return s->intsetclr;
	case MST_PCMCIA0:
		return s->pcmcia0;
	case MST_PCMCIA1:
		return s->pcmcia1;
	default:
		printf("Mainstone - mst_fpga_readb: Bad register offset "
			REG_FMT " \n", addr);
	}
	return 0;
}

static void
mst_fpga_writeb(void *opaque, target_phys_addr_t addr, uint32_t value)
{
	mst_irq_state *s = (mst_irq_state *) opaque;
	addr -= s->target_base;
	value &= 0xffffffff;

	switch (addr) {
	case MST_LEDDAT1:
		s->leddat1 = value;
		break;
	case MST_LEDDAT2:
		s->leddat2 = value;
		break;
	case MST_LEDCTRL:
		s->ledctrl = value;
		break;
	case MST_GPSWR:
		s->gpswr = value;
		break;
	case MST_MSCWR1:
		s->mscwr1 = value;
		break;
	case MST_MSCWR2:
		s->mscwr2 = value;
		break;
	case MST_MSCWR3:
		s->mscwr3 = value;
		break;
	case MST_MSCRD:
		s->mscrd =  value;
		break;
	case MST_INTMSKENA:	/* Mask interupt */
		s->intmskena = (value & 0xFEEFF);
		mst_fpga_update_gpio(s);
		break;
	case MST_INTSETCLR:	/* clear or set interrupt */
		s->intsetclr = (value & 0xFEEFF);
		break;
	case MST_PCMCIA0:
		s->pcmcia0 = value;
		break;
	case MST_PCMCIA1:
		s->pcmcia1 = value;
		break;
	default:
		printf("Mainstone - mst_fpga_writeb: Bad register offset "
			REG_FMT " \n", addr);
	}
}

CPUReadMemoryFunc *mst_fpga_readfn[] = {
	mst_fpga_readb,
	mst_fpga_readb,
	mst_fpga_readb,
};
CPUWriteMemoryFunc *mst_fpga_writefn[] = {
	mst_fpga_writeb,
	mst_fpga_writeb,
	mst_fpga_writeb,
};

static void
mst_fpga_save(QEMUFile *f, void *opaque)
{
	struct mst_irq_state *s = (mst_irq_state *) opaque;

	qemu_put_be32s(f, &s->prev_level);
	qemu_put_be32s(f, &s->leddat1);
	qemu_put_be32s(f, &s->leddat2);
	qemu_put_be32s(f, &s->ledctrl);
	qemu_put_be32s(f, &s->gpswr);
	qemu_put_be32s(f, &s->mscwr1);
	qemu_put_be32s(f, &s->mscwr2);
	qemu_put_be32s(f, &s->mscwr3);
	qemu_put_be32s(f, &s->mscrd);
	qemu_put_be32s(f, &s->intmskena);
	qemu_put_be32s(f, &s->intsetclr);
	qemu_put_be32s(f, &s->pcmcia0);
	qemu_put_be32s(f, &s->pcmcia1);
}

static int
mst_fpga_load(QEMUFile *f, void *opaque, int version_id)
{
	mst_irq_state *s = (mst_irq_state *) opaque;

	qemu_get_be32s(f, &s->prev_level);
	qemu_get_be32s(f, &s->leddat1);
	qemu_get_be32s(f, &s->leddat2);
	qemu_get_be32s(f, &s->ledctrl);
	qemu_get_be32s(f, &s->gpswr);
	qemu_get_be32s(f, &s->mscwr1);
	qemu_get_be32s(f, &s->mscwr2);
	qemu_get_be32s(f, &s->mscwr3);
	qemu_get_be32s(f, &s->mscrd);
	qemu_get_be32s(f, &s->intmskena);
	qemu_get_be32s(f, &s->intsetclr);
	qemu_get_be32s(f, &s->pcmcia0);
	qemu_get_be32s(f, &s->pcmcia1);
	return 0;
}

qemu_irq *mst_irq_init(struct pxa2xx_state_s *cpu, uint32_t base, int irq)
{
	mst_irq_state *s;
	int iomemtype;
	qemu_irq *qi;

	s = (mst_irq_state  *)
		qemu_mallocz(sizeof(mst_irq_state));

	if (!s)
		return NULL;
	s->target_base = base;
	s->parent = &cpu->pic[irq];

	/* alloc the external 16 irqs */
	qi  = qemu_allocate_irqs(mst_fpga_set_irq, s, MST_NUM_IRQS);
	s->pins = qi;

	iomemtype = cpu_register_io_memory(0, mst_fpga_readfn,
		mst_fpga_writefn, s);
	cpu_register_physical_memory(MST_BASE, 0x00100000, iomemtype);
	register_savevm("mainstone_fpga", 0, 0, mst_fpga_save, mst_fpga_load, s);
	return qi;
}
