/*
 * QEMU i440FX/PIIX3 PCI Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
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
typedef uint32_t pci_addr_t;
#include "pci_host.h"

typedef PCIHostState I440FXState;

static void i440fx_addr_writel(void* opaque, uint32_t addr, uint32_t val)
{
    I440FXState *s = opaque;
    s->config_reg = val;
}

static uint32_t i440fx_addr_readl(void* opaque, uint32_t addr)
{
    I440FXState *s = opaque;
    return s->config_reg;
}

static void piix3_set_irq(PCIDevice *pci_dev, void *pic, int irq_num, int level);

PCIBus *i440fx_init(void)
{
    PCIBus *b;
    PCIDevice *d;
    I440FXState *s;

    s = qemu_mallocz(sizeof(I440FXState));
    b = pci_register_bus(piix3_set_irq, NULL, 0);
    s->bus = b;

    register_ioport_write(0xcf8, 4, 4, i440fx_addr_writel, s);
    register_ioport_read(0xcf8, 4, 4, i440fx_addr_readl, s);

    register_ioport_write(0xcfc, 4, 1, pci_host_data_writeb, s);
    register_ioport_write(0xcfc, 4, 2, pci_host_data_writew, s);
    register_ioport_write(0xcfc, 4, 4, pci_host_data_writel, s);
    register_ioport_read(0xcfc, 4, 1, pci_host_data_readb, s);
    register_ioport_read(0xcfc, 4, 2, pci_host_data_readw, s);
    register_ioport_read(0xcfc, 4, 4, pci_host_data_readl, s);

    d = pci_register_device(b, "i440FX", sizeof(PCIDevice), 0, 
                            NULL, NULL);

    d->config[0x00] = 0x86; // vendor_id
    d->config[0x01] = 0x80;
    d->config[0x02] = 0x37; // device_id
    d->config[0x03] = 0x12;
    d->config[0x08] = 0x02; // revision
    d->config[0x0a] = 0x00; // class_sub = host2pci
    d->config[0x0b] = 0x06; // class_base = PCI_bridge
    d->config[0x0e] = 0x00; // header_type
    return b;
}

/* PIIX3 PCI to ISA bridge */

static PCIDevice *piix3_dev;

/* just used for simpler irq handling. */
#define PCI_IRQ_WORDS   ((PCI_DEVICES_MAX + 31) / 32)

static uint32_t pci_irq_levels[4][PCI_IRQ_WORDS];

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. */
static inline int pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    int slot_addend;
    slot_addend = (pci_dev->devfn >> 3) - 1;
    return (irq_num + slot_addend) & 3;
}

static inline int get_pci_irq_level(int irq_num)
{
    int pic_level;
#if (PCI_IRQ_WORDS == 2)
    pic_level = ((pci_irq_levels[irq_num][0] | 
                  pci_irq_levels[irq_num][1]) != 0);
#else
    {
        int i;
        pic_level = 0;
        for(i = 0; i < PCI_IRQ_WORDS; i++) {
            if (pci_irq_levels[irq_num][i]) {
                pic_level = 1;
                break;
            }
        }
    }
#endif
    return pic_level;
}

static void piix3_set_irq(PCIDevice *pci_dev, void *pic, int irq_num, int level)
{
    int irq_index, shift, pic_irq, pic_level;
    uint32_t *p;

    irq_num = pci_slot_get_pirq(pci_dev, irq_num);
    irq_index = pci_dev->irq_index;
    p = &pci_irq_levels[irq_num][irq_index >> 5];
    shift = (irq_index & 0x1f);
    *p = (*p & ~(1 << shift)) | (level << shift);

    /* now we change the pic irq level according to the piix irq mappings */
    /* XXX: optimize */
    pic_irq = piix3_dev->config[0x60 + irq_num];
    if (pic_irq < 16) {
        /* the pic level is the logical OR of all the PCI irqs mapped
           to it */
        pic_level = 0;
        if (pic_irq == piix3_dev->config[0x60])
            pic_level |= get_pci_irq_level(0);
        if (pic_irq == piix3_dev->config[0x61])
            pic_level |= get_pci_irq_level(1);
        if (pic_irq == piix3_dev->config[0x62])
            pic_level |= get_pci_irq_level(2);
        if (pic_irq == piix3_dev->config[0x63])
            pic_level |= get_pci_irq_level(3);
        pic_set_irq(pic_irq, pic_level);
    }
}

static void piix3_reset(PCIDevice *d)
{
    uint8_t *pci_conf = d->config;

    pci_conf[0x04] = 0x07; // master, memory and I/O
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; // PCI_status_devsel_medium
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[0x60] = 0x80;
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa2] = 0x00;
    pci_conf[0xa3] = 0x00;
    pci_conf[0xa4] = 0x00;
    pci_conf[0xa5] = 0x00;
    pci_conf[0xa6] = 0x00;
    pci_conf[0xa7] = 0x00;
    pci_conf[0xa8] = 0x0f;
    pci_conf[0xaa] = 0x00;
    pci_conf[0xab] = 0x00;
    pci_conf[0xac] = 0x00;
    pci_conf[0xae] = 0x00;
}

int piix3_init(PCIBus *bus)
{
    PCIDevice *d;
    uint8_t *pci_conf;

    d = pci_register_device(bus, "PIIX3", sizeof(PCIDevice),
                                    -1, NULL, NULL);
    register_savevm("PIIX3", 0, 1, generic_pci_save, generic_pci_load, d);

    piix3_dev = d;
    pci_conf = d->config;

    pci_conf[0x00] = 0x86; // Intel
    pci_conf[0x01] = 0x80;
    pci_conf[0x02] = 0x00; // 82371SB PIIX3 PCI-to-ISA bridge (Step A1)
    pci_conf[0x03] = 0x70;
    pci_conf[0x0a] = 0x01; // class_sub = PCI_ISA
    pci_conf[0x0b] = 0x06; // class_base = PCI_bridge
    pci_conf[0x0e] = 0x80; // header_type = PCI_multifunction, generic

    piix3_reset(d);
    return d->devfn;
}

/***********************************************************/
/* XXX: the following should be moved to the PC BIOS */

static __attribute__((unused)) uint32_t isa_inb(uint32_t addr)
{
    return cpu_inb(NULL, addr);
}

static void isa_outb(uint32_t val, uint32_t addr)
{
    cpu_outb(NULL, addr, val);
}

static __attribute__((unused)) uint32_t isa_inw(uint32_t addr)
{
    return cpu_inw(NULL, addr);
}

static __attribute__((unused)) void isa_outw(uint32_t val, uint32_t addr)
{
    cpu_outw(NULL, addr, val);
}

static __attribute__((unused)) uint32_t isa_inl(uint32_t addr)
{
    return cpu_inl(NULL, addr);
}

static __attribute__((unused)) void isa_outl(uint32_t val, uint32_t addr)
{
    cpu_outl(NULL, addr, val);
}

static uint32_t pci_bios_io_addr;
static uint32_t pci_bios_mem_addr;
/* host irqs corresponding to PCI irqs A-D */
static uint8_t pci_irqs[4] = { 11, 9, 11, 9 };

static void pci_config_writel(PCIDevice *d, uint32_t addr, uint32_t val)
{
    PCIBus *s = d->bus;
    addr |= (pci_bus_num(s) << 16) | (d->devfn << 8);
    pci_data_write(s, addr, val, 4);
}

static void pci_config_writew(PCIDevice *d, uint32_t addr, uint32_t val)
{
    PCIBus *s = d->bus;
    addr |= (pci_bus_num(s) << 16) | (d->devfn << 8);
    pci_data_write(s, addr, val, 2);
}

static void pci_config_writeb(PCIDevice *d, uint32_t addr, uint32_t val)
{
    PCIBus *s = d->bus;
    addr |= (pci_bus_num(s) << 16) | (d->devfn << 8);
    pci_data_write(s, addr, val, 1);
}

static __attribute__((unused)) uint32_t pci_config_readl(PCIDevice *d, uint32_t addr)
{
    PCIBus *s = d->bus;
    addr |= (pci_bus_num(s) << 16) | (d->devfn << 8);
    return pci_data_read(s, addr, 4);
}

static uint32_t pci_config_readw(PCIDevice *d, uint32_t addr)
{
    PCIBus *s = d->bus;
    addr |= (pci_bus_num(s) << 16) | (d->devfn << 8);
    return pci_data_read(s, addr, 2);
}

static uint32_t pci_config_readb(PCIDevice *d, uint32_t addr)
{
    PCIBus *s = d->bus;
    addr |= (pci_bus_num(s) << 16) | (d->devfn << 8);
    return pci_data_read(s, addr, 1);
}

static void pci_set_io_region_addr(PCIDevice *d, int region_num, uint32_t addr)
{
    PCIIORegion *r;
    uint16_t cmd;
    uint32_t ofs;

    if ( region_num == PCI_ROM_SLOT ) {
        ofs = 0x30;
    }else{
        ofs = 0x10 + region_num * 4;
    }

    pci_config_writel(d, ofs, addr);
    r = &d->io_regions[region_num];

    /* enable memory mappings */
    cmd = pci_config_readw(d, PCI_COMMAND);
    if ( region_num == PCI_ROM_SLOT )
        cmd |= 2;
    else if (r->type & PCI_ADDRESS_SPACE_IO)
        cmd |= 1;
    else
        cmd |= 2;
    pci_config_writew(d, PCI_COMMAND, cmd);
}

static void pci_bios_init_device(PCIDevice *d)
{
    int class;
    PCIIORegion *r;
    uint32_t *paddr;
    int i, pin, pic_irq, vendor_id, device_id;

    class = pci_config_readw(d, PCI_CLASS_DEVICE);
    vendor_id = pci_config_readw(d, PCI_VENDOR_ID);
    device_id = pci_config_readw(d, PCI_DEVICE_ID);
    switch(class) {
    case 0x0101:
        if (vendor_id == 0x8086 && device_id == 0x7010) {
            /* PIIX3 IDE */
            pci_config_writew(d, 0x40, 0x8000); // enable IDE0
            pci_config_writew(d, 0x42, 0x8000); // enable IDE1
            goto default_map;
        } else {
            /* IDE: we map it as in ISA mode */
            pci_set_io_region_addr(d, 0, 0x1f0);
            pci_set_io_region_addr(d, 1, 0x3f4);
            pci_set_io_region_addr(d, 2, 0x170);
            pci_set_io_region_addr(d, 3, 0x374);
        }
        break;
    case 0x0300:
        if (vendor_id != 0x1234)
            goto default_map;
        /* VGA: map frame buffer to default Bochs VBE address */
        pci_set_io_region_addr(d, 0, 0xE0000000);
        break;
    case 0x0800:
        /* PIC */
        vendor_id = pci_config_readw(d, PCI_VENDOR_ID);
        device_id = pci_config_readw(d, PCI_DEVICE_ID);
        if (vendor_id == 0x1014) {
            /* IBM */
            if (device_id == 0x0046 || device_id == 0xFFFF) {
                /* MPIC & MPIC2 */
                pci_set_io_region_addr(d, 0, 0x80800000 + 0x00040000);
            }
        }
        break;
    case 0xff00:
        if (vendor_id == 0x0106b &&
            (device_id == 0x0017 || device_id == 0x0022)) {
            /* macio bridge */
            pci_set_io_region_addr(d, 0, 0x80800000);
        }
        break;
    default:
    default_map:
        /* default memory mappings */
        for(i = 0; i < PCI_NUM_REGIONS; i++) {
            r = &d->io_regions[i];
            if (r->size) {
                if (r->type & PCI_ADDRESS_SPACE_IO)
                    paddr = &pci_bios_io_addr;
                else
                    paddr = &pci_bios_mem_addr;
                *paddr = (*paddr + r->size - 1) & ~(r->size - 1);
                pci_set_io_region_addr(d, i, *paddr);
                *paddr += r->size;
            }
        }
        break;
    }

    /* map the interrupt */
    pin = pci_config_readb(d, PCI_INTERRUPT_PIN);
    if (pin != 0) {
        pin = pci_slot_get_pirq(d, pin - 1);
        pic_irq = pci_irqs[pin];
        pci_config_writeb(d, PCI_INTERRUPT_LINE, pic_irq);
    }
}

/*
 * This function initializes the PCI devices as a normal PCI BIOS
 * would do. It is provided just in case the BIOS has no support for
 * PCI.
 */
void pci_bios_init(void)
{
    int i, irq;
    uint8_t elcr[2];

    pci_bios_io_addr = 0xc000;
    pci_bios_mem_addr = 0xf0000000;

    /* activate IRQ mappings */
    elcr[0] = 0x00;
    elcr[1] = 0x00;
    for(i = 0; i < 4; i++) {
        irq = pci_irqs[i];
        /* set to trigger level */
        elcr[irq >> 3] |= (1 << (irq & 7));
        /* activate irq remapping in PIIX */
        pci_config_writeb(piix3_dev, 0x60 + i, irq);
    }
    isa_outb(elcr[0], 0x4d0);
    isa_outb(elcr[1], 0x4d1);

    pci_for_each_device(pci_bios_init_device);
}

