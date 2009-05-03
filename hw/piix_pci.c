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

#include "hw.h"
#include "pc.h"
#include "pci.h"

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

static void piix3_set_irq(qemu_irq *pic, int irq_num, int level);

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. */
static int pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    int slot_addend;
    slot_addend = (pci_dev->devfn >> 3) - 1;
    return (irq_num + slot_addend) & 3;
}

static target_phys_addr_t isa_page_descs[384 / 4];
static uint8_t smm_enabled;
static int pci_irq_levels[4];

static void update_pam(PCIDevice *d, uint32_t start, uint32_t end, int r)
{
    uint32_t addr;

    //    printf("ISA mapping %08x-0x%08x: %d\n", start, end, r);
    switch(r) {
    case 3:
        /* RAM */
        cpu_register_physical_memory(start, end - start,
                                     start);
        break;
    case 1:
        /* ROM (XXX: not quite correct) */
        cpu_register_physical_memory(start, end - start,
                                     start | IO_MEM_ROM);
        break;
    case 2:
    case 0:
        /* XXX: should distinguish read/write cases */
        for(addr = start; addr < end; addr += 4096) {
            cpu_register_physical_memory(addr, 4096,
                                         isa_page_descs[(addr - 0xa0000) >> 12]);
        }
        break;
    }
}

static void i440fx_update_memory_mappings(PCIDevice *d)
{
    int i, r;
    uint32_t smram, addr;

    update_pam(d, 0xf0000, 0x100000, (d->config[0x59] >> 4) & 3);
    for(i = 0; i < 12; i++) {
        r = (d->config[(i >> 1) + 0x5a] >> ((i & 1) * 4)) & 3;
        update_pam(d, 0xc0000 + 0x4000 * i, 0xc0000 + 0x4000 * (i + 1), r);
    }
    smram = d->config[0x72];
    if ((smm_enabled && (smram & 0x08)) || (smram & 0x40)) {
        cpu_register_physical_memory(0xa0000, 0x20000, 0xa0000);
    } else {
        for(addr = 0xa0000; addr < 0xc0000; addr += 4096) {
            cpu_register_physical_memory(addr, 4096,
                                         isa_page_descs[(addr - 0xa0000) >> 12]);
        }
    }
}

void i440fx_set_smm(PCIDevice *d, int val)
{
    val = (val != 0);
    if (smm_enabled != val) {
        smm_enabled = val;
        i440fx_update_memory_mappings(d);
    }
}


/* XXX: suppress when better memory API. We make the assumption that
   no device (in particular the VGA) changes the memory mappings in
   the 0xa0000-0x100000 range */
void i440fx_init_memory_mappings(PCIDevice *d)
{
    int i;
    for(i = 0; i < 96; i++) {
        isa_page_descs[i] = cpu_get_physical_page_desc(0xa0000 + i * 0x1000);
    }
}

static void i440fx_write_config(PCIDevice *d,
                                uint32_t address, uint32_t val, int len)
{
    /* XXX: implement SMRAM.D_LOCK */
    pci_default_write_config(d, address, val, len);
    if ((address >= 0x59 && address <= 0x5f) || address == 0x72)
        i440fx_update_memory_mappings(d);
}

static void i440fx_save(QEMUFile* f, void *opaque)
{
    PCIDevice *d = opaque;
    int i;

    pci_device_save(d, f);
    qemu_put_8s(f, &smm_enabled);

    for (i = 0; i < 4; i++)
        qemu_put_be32(f, pci_irq_levels[i]);
}

static int i440fx_load(QEMUFile* f, void *opaque, int version_id)
{
    PCIDevice *d = opaque;
    int ret, i;

    if (version_id > 2)
        return -EINVAL;
    ret = pci_device_load(d, f);
    if (ret < 0)
        return ret;
    i440fx_update_memory_mappings(d);
    qemu_get_8s(f, &smm_enabled);

    if (version_id >= 2)
        for (i = 0; i < 4; i++)
            pci_irq_levels[i] = qemu_get_be32(f);

    return 0;
}

PCIBus *i440fx_init(PCIDevice **pi440fx_state, qemu_irq *pic)
{
    PCIBus *b;
    PCIDevice *d;
    I440FXState *s;

    s = qemu_mallocz(sizeof(I440FXState));
    b = pci_register_bus(piix3_set_irq, pci_slot_get_pirq, pic, 0, 4);
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
                            NULL, i440fx_write_config);

    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_INTEL_82441);
    d->config[0x08] = 0x02; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type

    d->config[0x72] = 0x02; /* SMRAM */

    register_savevm("I440FX", 0, 2, i440fx_save, i440fx_load, d);
    *pi440fx_state = d;
    return b;
}

/* PIIX3 PCI to ISA bridge */

static PCIDevice *piix3_dev;
PCIDevice *piix4_dev;

static void piix3_set_irq(qemu_irq *pic, int irq_num, int level)
{
    int i, pic_irq, pic_level;

    pci_irq_levels[irq_num] = level;

    /* now we change the pic irq level according to the piix irq mappings */
    /* XXX: optimize */
    pic_irq = piix3_dev->config[0x60 + irq_num];
    if (pic_irq < 16) {
        /* The pic level is the logical OR of all the PCI irqs mapped
           to it */
        pic_level = 0;
        for (i = 0; i < 4; i++) {
            if (pic_irq == piix3_dev->config[0x60 + i])
                pic_level |= pci_irq_levels[i];
        }
        qemu_set_irq(pic[pic_irq], pic_level);
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
    pci_conf[0x61] = 0x80;
    pci_conf[0x62] = 0x80;
    pci_conf[0x63] = 0x80;
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
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

static void piix4_reset(PCIDevice *d)
{
    uint8_t *pci_conf = d->config;

    pci_conf[0x04] = 0x07; // master, memory and I/O
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; // PCI_status_devsel_medium
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[0x60] = 0x0a; // PCI A -> IRQ 10
    pci_conf[0x61] = 0x0a; // PCI B -> IRQ 10
    pci_conf[0x62] = 0x0b; // PCI C -> IRQ 11
    pci_conf[0x63] = 0x0b; // PCI D -> IRQ 11
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
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

static void piix_save(QEMUFile* f, void *opaque)
{
    PCIDevice *d = opaque;
    pci_device_save(d, f);
}

static int piix_load(QEMUFile* f, void *opaque, int version_id)
{
    PCIDevice *d = opaque;
    if (version_id != 2)
        return -EINVAL;
    return pci_device_load(d, f);
}

int piix3_init(PCIBus *bus, int devfn)
{
    PCIDevice *d;
    uint8_t *pci_conf;

    d = pci_register_device(bus, "PIIX3", sizeof(PCIDevice),
                                    devfn, NULL, NULL);
    register_savevm("PIIX3", 0, 2, piix_save, piix_load, d);

    piix3_dev = d;
    pci_conf = d->config;

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82371SB_0); // 82371SB PIIX3 PCI-to-ISA bridge (Step A1)
    pci_config_set_class(pci_conf, PCI_CLASS_BRIDGE_ISA);
    pci_conf[PCI_HEADER_TYPE] =
        PCI_HEADER_TYPE_NORMAL | PCI_HEADER_TYPE_MULTI_FUNCTION; // header_type = PCI_multifunction, generic

    piix3_reset(d);
    return d->devfn;
}

int piix4_init(PCIBus *bus, int devfn)
{
    PCIDevice *d;
    uint8_t *pci_conf;

    d = pci_register_device(bus, "PIIX4", sizeof(PCIDevice),
                                    devfn, NULL, NULL);
    register_savevm("PIIX4", 0, 2, piix_save, piix_load, d);

    piix4_dev = d;
    pci_conf = d->config;

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82371AB_0); // 82371AB/EB/MB PIIX4 PCI-to-ISA bridge
    pci_config_set_class(pci_conf, PCI_CLASS_BRIDGE_ISA);
    pci_conf[PCI_HEADER_TYPE] =
        PCI_HEADER_TYPE_NORMAL | PCI_HEADER_TYPE_MULTI_FUNCTION; // header_type = PCI_multifunction, generic


    piix4_reset(d);
    return d->devfn;
}
