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
#include "pci_host.h"
#include "isa.h"
#include "sysbus.h"
#include "range.h"
#include "xen.h"

/*
 * I440FX chipset data sheet.
 * http://download.intel.com/design/chipsets/datashts/29054901.pdf
 */

typedef PCIHostState I440FXState;

#define PIIX_NUM_PIC_IRQS       16      /* i8259 * 2 */
#define PIIX_NUM_PIRQS          4ULL    /* PIRQ[A-D] */
#define XEN_PIIX_NUM_PIRQS      128ULL
#define PIIX_PIRQC              0x60

typedef struct PIIX3State {
    PCIDevice dev;

    /*
     * bitmap to track pic levels.
     * The pic level is the logical OR of all the PCI irqs mapped to it
     * So one PIC level is tracked by PIIX_NUM_PIRQS bits.
     *
     * PIRQ is mapped to PIC pins, we track it by
     * PIIX_NUM_PIRQS * PIIX_NUM_PIC_IRQS = 64 bits with
     * pic_irq * PIIX_NUM_PIRQS + pirq
     */
#if PIIX_NUM_PIC_IRQS * PIIX_NUM_PIRQS > 64
#error "unable to encode pic state in 64bit in pic_levels."
#endif
    uint64_t pic_levels;

    qemu_irq *pic;

    /* This member isn't used. Just for save/load compatibility */
    int32_t pci_irq_levels_vmstate[PIIX_NUM_PIRQS];
} PIIX3State;

struct PCII440FXState {
    PCIDevice dev;
    target_phys_addr_t isa_page_descs[384 / 4];
    uint8_t smm_enabled;
    PIIX3State *piix3;
};


#define I440FX_PAM      0x59
#define I440FX_PAM_SIZE 7
#define I440FX_SMRAM    0x72

static void piix3_set_irq(void *opaque, int pirq, int level);
static void piix3_write_config_xen(PCIDevice *dev,
                               uint32_t address, uint32_t val, int len);

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. */
static int pci_slot_get_pirq(PCIDevice *pci_dev, int pci_intx)
{
    int slot_addend;
    slot_addend = (pci_dev->devfn >> 3) - 1;
    return (pci_intx + slot_addend) & 3;
}

static void update_pam(PCII440FXState *d, uint32_t start, uint32_t end, int r)
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
                                         d->isa_page_descs[(addr - 0xa0000) >> 12]);
        }
        break;
    }
}

static void i440fx_update_memory_mappings(PCII440FXState *d)
{
    int i, r;
    uint32_t smram, addr;

    update_pam(d, 0xf0000, 0x100000, (d->dev.config[I440FX_PAM] >> 4) & 3);
    for(i = 0; i < 12; i++) {
        r = (d->dev.config[(i >> 1) + (I440FX_PAM + 1)] >> ((i & 1) * 4)) & 3;
        update_pam(d, 0xc0000 + 0x4000 * i, 0xc0000 + 0x4000 * (i + 1), r);
    }
    smram = d->dev.config[I440FX_SMRAM];
    if ((d->smm_enabled && (smram & 0x08)) || (smram & 0x40)) {
        cpu_register_physical_memory(0xa0000, 0x20000, 0xa0000);
    } else {
        for(addr = 0xa0000; addr < 0xc0000; addr += 4096) {
            cpu_register_physical_memory(addr, 4096,
                                         d->isa_page_descs[(addr - 0xa0000) >> 12]);
        }
    }
}

static void i440fx_set_smm(int val, void *arg)
{
    PCII440FXState *d = arg;

    val = (val != 0);
    if (d->smm_enabled != val) {
        d->smm_enabled = val;
        i440fx_update_memory_mappings(d);
    }
}


/* XXX: suppress when better memory API. We make the assumption that
   no device (in particular the VGA) changes the memory mappings in
   the 0xa0000-0x100000 range */
void i440fx_init_memory_mappings(PCII440FXState *d)
{
    int i;
    for(i = 0; i < 96; i++) {
        d->isa_page_descs[i] = cpu_get_physical_page_desc(0xa0000 + i * 0x1000);
    }
}

static void i440fx_write_config(PCIDevice *dev,
                                uint32_t address, uint32_t val, int len)
{
    PCII440FXState *d = DO_UPCAST(PCII440FXState, dev, dev);

    /* XXX: implement SMRAM.D_LOCK */
    pci_default_write_config(dev, address, val, len);
    if (ranges_overlap(address, len, I440FX_PAM, I440FX_PAM_SIZE) ||
        range_covers_byte(address, len, I440FX_SMRAM)) {
        i440fx_update_memory_mappings(d);
    }
}

static int i440fx_load_old(QEMUFile* f, void *opaque, int version_id)
{
    PCII440FXState *d = opaque;
    int ret, i;

    ret = pci_device_load(&d->dev, f);
    if (ret < 0)
        return ret;
    i440fx_update_memory_mappings(d);
    qemu_get_8s(f, &d->smm_enabled);

    if (version_id == 2) {
        for (i = 0; i < PIIX_NUM_PIRQS; i++) {
            qemu_get_be32(f); /* dummy load for compatibility */
        }
    }

    return 0;
}

static int i440fx_post_load(void *opaque, int version_id)
{
    PCII440FXState *d = opaque;

    i440fx_update_memory_mappings(d);
    return 0;
}

static const VMStateDescription vmstate_i440fx = {
    .name = "I440FX",
    .version_id = 3,
    .minimum_version_id = 3,
    .minimum_version_id_old = 1,
    .load_state_old = i440fx_load_old,
    .post_load = i440fx_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, PCII440FXState),
        VMSTATE_UINT8(smm_enabled, PCII440FXState),
        VMSTATE_END_OF_LIST()
    }
};

static int i440fx_pcihost_initfn(SysBusDevice *dev)
{
    I440FXState *s = FROM_SYSBUS(I440FXState, dev);

    pci_host_conf_register_ioport(0xcf8, s);

    pci_host_data_register_ioport(0xcfc, s);
    return 0;
}

static int i440fx_initfn(PCIDevice *dev)
{
    PCII440FXState *d = DO_UPCAST(PCII440FXState, dev, dev);

    d->dev.config[I440FX_SMRAM] = 0x02;

    cpu_smm_register(&i440fx_set_smm, d);
    return 0;
}

static PCIBus *i440fx_common_init(const char *device_name,
                                  PCII440FXState **pi440fx_state,
                                  int *piix3_devfn,
                                  qemu_irq *pic, ram_addr_t ram_size)
{
    DeviceState *dev;
    PCIBus *b;
    PCIDevice *d;
    I440FXState *s;
    PIIX3State *piix3;

    dev = qdev_create(NULL, "i440FX-pcihost");
    s = FROM_SYSBUS(I440FXState, sysbus_from_qdev(dev));
    b = pci_bus_new(&s->busdev.qdev, NULL, 0);
    s->bus = b;
    qdev_init_nofail(dev);

    d = pci_create_simple(b, 0, device_name);
    *pi440fx_state = DO_UPCAST(PCII440FXState, dev, d);

    /* Xen supports additional interrupt routes from the PCI devices to
     * the IOAPIC: the four pins of each PCI device on the bus are also
     * connected to the IOAPIC directly.
     * These additional routes can be discovered through ACPI. */
    if (xen_enabled()) {
        piix3 = DO_UPCAST(PIIX3State, dev,
                pci_create_simple_multifunction(b, -1, true, "PIIX3-xen"));
        pci_bus_irqs(b, xen_piix3_set_irq, xen_pci_slot_get_pirq,
                piix3, XEN_PIIX_NUM_PIRQS);
    } else {
        piix3 = DO_UPCAST(PIIX3State, dev,
                pci_create_simple_multifunction(b, -1, true, "PIIX3"));
        pci_bus_irqs(b, piix3_set_irq, pci_slot_get_pirq, piix3,
                PIIX_NUM_PIRQS);
    }
    piix3->pic = pic;

    (*pi440fx_state)->piix3 = piix3;

    *piix3_devfn = piix3->dev.devfn;

    ram_size = ram_size / 8 / 1024 / 1024;
    if (ram_size > 255)
        ram_size = 255;
    (*pi440fx_state)->dev.config[0x57]=ram_size;

    return b;
}

PCIBus *i440fx_init(PCII440FXState **pi440fx_state, int *piix3_devfn,
                    qemu_irq *pic, ram_addr_t ram_size)
{
    PCIBus *b;

    b = i440fx_common_init("i440FX", pi440fx_state, piix3_devfn, pic, ram_size);
    return b;
}

/* PIIX3 PCI to ISA bridge */
static void piix3_set_irq_pic(PIIX3State *piix3, int pic_irq)
{
    qemu_set_irq(piix3->pic[pic_irq],
                 !!(piix3->pic_levels &
                    (((1ULL << PIIX_NUM_PIRQS) - 1) <<
                     (pic_irq * PIIX_NUM_PIRQS))));
}

static void piix3_set_irq_level(PIIX3State *piix3, int pirq, int level)
{
    int pic_irq;
    uint64_t mask;

    pic_irq = piix3->dev.config[PIIX_PIRQC + pirq];
    if (pic_irq >= PIIX_NUM_PIC_IRQS) {
        return;
    }

    mask = 1ULL << ((pic_irq * PIIX_NUM_PIRQS) + pirq);
    piix3->pic_levels &= ~mask;
    piix3->pic_levels |= mask * !!level;

    piix3_set_irq_pic(piix3, pic_irq);
}

static void piix3_set_irq(void *opaque, int pirq, int level)
{
    PIIX3State *piix3 = opaque;
    piix3_set_irq_level(piix3, pirq, level);
}

/* irq routing is changed. so rebuild bitmap */
static void piix3_update_irq_levels(PIIX3State *piix3)
{
    int pirq;

    piix3->pic_levels = 0;
    for (pirq = 0; pirq < PIIX_NUM_PIRQS; pirq++) {
        piix3_set_irq_level(piix3, pirq,
                            pci_bus_get_irq_level(piix3->dev.bus, pirq));
    }
}

static void piix3_write_config(PCIDevice *dev,
                               uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(dev, address, val, len);
    if (ranges_overlap(address, len, PIIX_PIRQC, 4)) {
        PIIX3State *piix3 = DO_UPCAST(PIIX3State, dev, dev);
        int pic_irq;
        piix3_update_irq_levels(piix3);
        for (pic_irq = 0; pic_irq < PIIX_NUM_PIC_IRQS; pic_irq++) {
            piix3_set_irq_pic(piix3, pic_irq);
        }
    }
}

static void piix3_write_config_xen(PCIDevice *dev,
                               uint32_t address, uint32_t val, int len)
{
    xen_piix_pci_write_config_client(address, val, len);
    piix3_write_config(dev, address, val, len);
}

static void piix3_reset(void *opaque)
{
    PIIX3State *d = opaque;
    uint8_t *pci_conf = d->dev.config;

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

    d->pic_levels = 0;
}

static int piix3_post_load(void *opaque, int version_id)
{
    PIIX3State *piix3 = opaque;
    piix3_update_irq_levels(piix3);
    return 0;
}

static void piix3_pre_save(void *opaque)
{
    int i;
    PIIX3State *piix3 = opaque;

    for (i = 0; i < ARRAY_SIZE(piix3->pci_irq_levels_vmstate); i++) {
        piix3->pci_irq_levels_vmstate[i] =
            pci_bus_get_irq_level(piix3->dev.bus, i);
    }
}

static const VMStateDescription vmstate_piix3 = {
    .name = "PIIX3",
    .version_id = 3,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .post_load = piix3_post_load,
    .pre_save = piix3_pre_save,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, PIIX3State),
        VMSTATE_INT32_ARRAY_V(pci_irq_levels_vmstate, PIIX3State,
                              PIIX_NUM_PIRQS, 3),
        VMSTATE_END_OF_LIST()
    }
};

static int piix3_initfn(PCIDevice *dev)
{
    PIIX3State *d = DO_UPCAST(PIIX3State, dev, dev);

    isa_bus_new(&d->dev.qdev);
    qemu_register_reset(piix3_reset, d);
    return 0;
}

static PCIDeviceInfo i440fx_info[] = {
    {
        .qdev.name    = "i440FX",
        .qdev.desc    = "Host bridge",
        .qdev.size    = sizeof(PCII440FXState),
        .qdev.vmsd    = &vmstate_i440fx,
        .qdev.no_user = 1,
        .no_hotplug   = 1,
        .init         = i440fx_initfn,
        .config_write = i440fx_write_config,
        .vendor_id    = PCI_VENDOR_ID_INTEL,
        .device_id    = PCI_DEVICE_ID_INTEL_82441,
        .revision     = 0x02,
        .class_id     = PCI_CLASS_BRIDGE_HOST,
    },{
        .qdev.name    = "PIIX3",
        .qdev.desc    = "ISA bridge",
        .qdev.size    = sizeof(PIIX3State),
        .qdev.vmsd    = &vmstate_piix3,
        .qdev.no_user = 1,
        .no_hotplug   = 1,
        .init         = piix3_initfn,
        .config_write = piix3_write_config,
        .vendor_id    = PCI_VENDOR_ID_INTEL,
        .device_id    = PCI_DEVICE_ID_INTEL_82371SB_0, // 82371SB PIIX3 PCI-to-ISA bridge (Step A1)
        .class_id     = PCI_CLASS_BRIDGE_ISA,
    },{
        .qdev.name    = "PIIX3-xen",
        .qdev.desc    = "ISA bridge",
        .qdev.size    = sizeof(PIIX3State),
        .qdev.vmsd    = &vmstate_piix3,
        .qdev.no_user = 1,
        .no_hotplug   = 1,
        .init         = piix3_initfn,
        .config_write = piix3_write_config_xen,
        .vendor_id    = PCI_VENDOR_ID_INTEL,
        .device_id    = PCI_DEVICE_ID_INTEL_82371SB_0, // 82371SB PIIX3 PCI-to-ISA bridge (Step A1)
        .class_id     = PCI_CLASS_BRIDGE_ISA,
    },{
        /* end of list */
    }
};

static SysBusDeviceInfo i440fx_pcihost_info = {
    .init         = i440fx_pcihost_initfn,
    .qdev.name    = "i440FX-pcihost",
    .qdev.fw_name = "pci",
    .qdev.size    = sizeof(I440FXState),
    .qdev.no_user = 1,
};

static void i440fx_register(void)
{
    sysbus_register_withprop(&i440fx_pcihost_info);
    pci_qdev_register_many(i440fx_info);
}
device_init(i440fx_register);
