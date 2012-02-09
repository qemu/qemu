/*
 * PXA270-based Intel Mainstone platforms.
 * FPGA driver
 *
 * Copyright (c) 2007 by Armin Kuster <akuster@kama-aina.net> or
 *                                    <akuster@mvista.com>
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "hw.h"
#include "sysbus.h"

/* Mainstone FPGA for extern irqs */
#define FPGA_GPIO_PIN	0
#define MST_NUM_IRQS	16
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

#define MST_PCMCIAx_READY	(1 << 10)
#define MST_PCMCIAx_nCD		(1 << 5)

#define MST_PCMCIA_CD0_IRQ	9
#define MST_PCMCIA_CD1_IRQ	13

typedef struct mst_irq_state{
	SysBusDevice busdev;
	MemoryRegion iomem;

	qemu_irq parent;

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
mst_fpga_set_irq(void *opaque, int irq, int level)
{
	mst_irq_state *s = (mst_irq_state *)opaque;
	uint32_t oldint = s->intsetclr & s->intmskena;

	if (level)
		s->prev_level |= 1u << irq;
	else
		s->prev_level &= ~(1u << irq);

	switch(irq) {
	case MST_PCMCIA_CD0_IRQ:
		if (level)
			s->pcmcia0 &= ~MST_PCMCIAx_nCD;
		else
			s->pcmcia0 |=  MST_PCMCIAx_nCD;
		break;
	case MST_PCMCIA_CD1_IRQ:
		if (level)
			s->pcmcia1 &= ~MST_PCMCIAx_nCD;
		else
			s->pcmcia1 |=  MST_PCMCIAx_nCD;
		break;
	}

	if ((s->intmskena & (1u << irq)) && level)
		s->intsetclr |= 1u << irq;

	if (oldint != (s->intsetclr & s->intmskena))
		qemu_set_irq(s->parent, s->intsetclr & s->intmskena);
}


static uint64_t
mst_fpga_readb(void *opaque, target_phys_addr_t addr, unsigned size)
{
	mst_irq_state *s = (mst_irq_state *) opaque;

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
			"0x" TARGET_FMT_plx "\n", addr);
	}
	return 0;
}

static void
mst_fpga_writeb(void *opaque, target_phys_addr_t addr, uint64_t value,
		unsigned size)
{
	mst_irq_state *s = (mst_irq_state *) opaque;
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
	case MST_INTMSKENA:	/* Mask interrupt */
		s->intmskena = (value & 0xFEEFF);
		qemu_set_irq(s->parent, s->intsetclr & s->intmskena);
		break;
	case MST_INTSETCLR:	/* clear or set interrupt */
		s->intsetclr = (value & 0xFEEFF);
		qemu_set_irq(s->parent, s->intsetclr & s->intmskena);
		break;
		/* For PCMCIAx allow the to change only power and reset */
	case MST_PCMCIA0:
		s->pcmcia0 = (value & 0x1f) | (s->pcmcia0 & ~0x1f);
		break;
	case MST_PCMCIA1:
		s->pcmcia1 = (value & 0x1f) | (s->pcmcia1 & ~0x1f);
		break;
	default:
		printf("Mainstone - mst_fpga_writeb: Bad register offset "
			"0x" TARGET_FMT_plx "\n", addr);
	}
}

static const MemoryRegionOps mst_fpga_ops = {
	.read = mst_fpga_readb,
	.write = mst_fpga_writeb,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static int mst_fpga_post_load(void *opaque, int version_id)
{
	mst_irq_state *s = (mst_irq_state *) opaque;

	qemu_set_irq(s->parent, s->intsetclr & s->intmskena);
	return 0;
}

static int mst_fpga_init(SysBusDevice *dev)
{
	mst_irq_state *s;

	s = FROM_SYSBUS(mst_irq_state, dev);

	s->pcmcia0 = MST_PCMCIAx_READY | MST_PCMCIAx_nCD;
	s->pcmcia1 = MST_PCMCIAx_READY | MST_PCMCIAx_nCD;

	sysbus_init_irq(dev, &s->parent);

	/* alloc the external 16 irqs */
	qdev_init_gpio_in(&dev->qdev, mst_fpga_set_irq, MST_NUM_IRQS);

	memory_region_init_io(&s->iomem, &mst_fpga_ops, s,
			    "fpga", 0x00100000);
	sysbus_init_mmio(dev, &s->iomem);
	return 0;
}

static VMStateDescription vmstate_mst_fpga_regs = {
	.name = "mainstone_fpga",
	.version_id = 0,
	.minimum_version_id = 0,
	.minimum_version_id_old = 0,
	.post_load = mst_fpga_post_load,
	.fields = (VMStateField []) {
		VMSTATE_UINT32(prev_level, mst_irq_state),
		VMSTATE_UINT32(leddat1, mst_irq_state),
		VMSTATE_UINT32(leddat2, mst_irq_state),
		VMSTATE_UINT32(ledctrl, mst_irq_state),
		VMSTATE_UINT32(gpswr, mst_irq_state),
		VMSTATE_UINT32(mscwr1, mst_irq_state),
		VMSTATE_UINT32(mscwr2, mst_irq_state),
		VMSTATE_UINT32(mscwr3, mst_irq_state),
		VMSTATE_UINT32(mscrd, mst_irq_state),
		VMSTATE_UINT32(intmskena, mst_irq_state),
		VMSTATE_UINT32(intsetclr, mst_irq_state),
		VMSTATE_UINT32(pcmcia0, mst_irq_state),
		VMSTATE_UINT32(pcmcia1, mst_irq_state),
		VMSTATE_END_OF_LIST(),
	},
};

static void mst_fpga_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = mst_fpga_init;
    dc->desc = "Mainstone II FPGA";
    dc->vmsd = &vmstate_mst_fpga_regs;
}

static TypeInfo mst_fpga_info = {
    .name          = "mainstone-fpga",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mst_irq_state),
    .class_init    = mst_fpga_class_init,
};

static void mst_fpga_register_types(void)
{
    type_register_static(&mst_fpga_info);
}

type_init(mst_fpga_register_types)
