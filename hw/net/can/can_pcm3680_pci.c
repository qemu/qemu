/*
 * PCM-3680i PCI CAN device (SJA1000 based) emulation
 *
 * Copyright (c) 2016 Deniz Eren (deniz.eren@icloud.com)
 *
 * Based on Kvaser PCI CAN device (SJA1000 based) emulation implemented by
 * Jin Yang and Pavel Pisa
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
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"

#include "can_sja1000.h"
#include "qom/object.h"

#define TYPE_CAN_PCI_DEV "pcm3680_pci"

typedef struct Pcm3680iPCIState Pcm3680iPCIState;
DECLARE_INSTANCE_CHECKER(Pcm3680iPCIState, PCM3680i_PCI_DEV,
                         TYPE_CAN_PCI_DEV)

/* the PCI device and vendor IDs */
#ifndef PCM3680i_PCI_VENDOR_ID1
#define PCM3680i_PCI_VENDOR_ID1     0x13fe
#endif

#ifndef PCM3680i_PCI_DEVICE_ID1
#define PCM3680i_PCI_DEVICE_ID1     0xc002
#endif

#define PCM3680i_PCI_SJA_COUNT     2
#define PCM3680i_PCI_SJA_RANGE     0x100

#define PCM3680i_PCI_BYTES_PER_SJA 0x20

struct Pcm3680iPCIState {
    /*< private >*/
    PCIDevice       dev;
    /*< public >*/
    MemoryRegion    sja_io[PCM3680i_PCI_SJA_COUNT];

    CanSJA1000State sja_state[PCM3680i_PCI_SJA_COUNT];
    qemu_irq        irq;

    char            *model; /* The model that support, only SJA1000 now. */
    CanBusState     *canbus[PCM3680i_PCI_SJA_COUNT];
};

static void pcm3680i_pci_reset(DeviceState *dev)
{
    Pcm3680iPCIState *d = PCM3680i_PCI_DEV(dev);
    int i;

    for (i = 0; i < PCM3680i_PCI_SJA_COUNT; i++) {
        can_sja_hardware_reset(&d->sja_state[i]);
    }
}

static uint64_t pcm3680i_pci_sja1_io_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    Pcm3680iPCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state[0];

    if (addr >= PCM3680i_PCI_BYTES_PER_SJA) {
        return 0;
    }

    return can_sja_mem_read(s, addr, size);
}

static void pcm3680i_pci_sja1_io_write(void *opaque, hwaddr addr,
                                       uint64_t data, unsigned size)
{
    Pcm3680iPCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state[0];

    if (addr >= PCM3680i_PCI_BYTES_PER_SJA) {
        return;
    }

    can_sja_mem_write(s, addr, data, size);
}

static uint64_t pcm3680i_pci_sja2_io_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    Pcm3680iPCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state[1];

    if (addr >= PCM3680i_PCI_BYTES_PER_SJA) {
        return 0;
    }

    return can_sja_mem_read(s, addr, size);
}

static void pcm3680i_pci_sja2_io_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    Pcm3680iPCIState *d = opaque;
    CanSJA1000State *s = &d->sja_state[1];

    if (addr >= PCM3680i_PCI_BYTES_PER_SJA) {
        return;
    }

    can_sja_mem_write(s, addr, data, size);
}

static const MemoryRegionOps pcm3680i_pci_sja1_io_ops = {
    .read = pcm3680i_pci_sja1_io_read,
    .write = pcm3680i_pci_sja1_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .max_access_size = 1,
    },
};

static const MemoryRegionOps pcm3680i_pci_sja2_io_ops = {
    .read = pcm3680i_pci_sja2_io_read,
    .write = pcm3680i_pci_sja2_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .max_access_size = 1,
    },
};

static void pcm3680i_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    Pcm3680iPCIState *d = PCM3680i_PCI_DEV(pci_dev);
    uint8_t *pci_conf;
    int i;

    pci_conf = pci_dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin A */

    d->irq = pci_allocate_irq(&d->dev);

    for (i = 0; i < PCM3680i_PCI_SJA_COUNT; i++) {
        can_sja_init(&d->sja_state[i], d->irq);
    }

    for (i = 0; i < PCM3680i_PCI_SJA_COUNT; i++) {
        if (can_sja_connect_to_bus(&d->sja_state[i], d->canbus[i]) < 0) {
            error_setg(errp, "can_sja_connect_to_bus failed");
            return;
        }
    }

    memory_region_init_io(&d->sja_io[0], OBJECT(d), &pcm3680i_pci_sja1_io_ops,
                          d, "pcm3680i_pci-sja1", PCM3680i_PCI_SJA_RANGE);

    memory_region_init_io(&d->sja_io[1], OBJECT(d), &pcm3680i_pci_sja2_io_ops,
                          d, "pcm3680i_pci-sja2", PCM3680i_PCI_SJA_RANGE);

    for (i = 0; i < PCM3680i_PCI_SJA_COUNT; i++) {
        pci_register_bar(&d->dev, /*BAR*/ i, PCI_BASE_ADDRESS_SPACE_IO,
                         &d->sja_io[i]);
    }
}

static void pcm3680i_pci_exit(PCIDevice *pci_dev)
{
    Pcm3680iPCIState *d = PCM3680i_PCI_DEV(pci_dev);
    int i;

    for (i = 0; i < PCM3680i_PCI_SJA_COUNT; i++) {
        can_sja_disconnect(&d->sja_state[i]);
    }

    qemu_free_irq(d->irq);
}

static const VMStateDescription vmstate_pcm3680i_pci = {
    .name = "pcm3680i_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, Pcm3680iPCIState),
        VMSTATE_STRUCT(sja_state[0], Pcm3680iPCIState, 0,
                       vmstate_can_sja, CanSJA1000State),
        VMSTATE_STRUCT(sja_state[1], Pcm3680iPCIState, 0,
                       vmstate_can_sja, CanSJA1000State),
        VMSTATE_END_OF_LIST()
    }
};

static void pcm3680i_pci_instance_init(Object *obj)
{
    Pcm3680iPCIState *d = PCM3680i_PCI_DEV(obj);

    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&d->canbus[0],
                             qdev_prop_allow_set_link_before_realize,
                             0);
    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&d->canbus[1],
                             qdev_prop_allow_set_link_before_realize,
                             0);
}

static void pcm3680i_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcm3680i_pci_realize;
    k->exit = pcm3680i_pci_exit;
    k->vendor_id = PCM3680i_PCI_VENDOR_ID1;
    k->device_id = PCM3680i_PCI_DEVICE_ID1;
    k->revision = 0x00;
    k->class_id = 0x000c09;
    k->subsystem_vendor_id = PCM3680i_PCI_VENDOR_ID1;
    k->subsystem_id = PCM3680i_PCI_DEVICE_ID1;
    dc->desc = "Pcm3680i PCICANx";
    dc->vmsd = &vmstate_pcm3680i_pci;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_legacy_reset(dc, pcm3680i_pci_reset);
}

static const TypeInfo pcm3680i_pci_info = {
    .name          = TYPE_CAN_PCI_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(Pcm3680iPCIState),
    .class_init    = pcm3680i_pci_class_init,
    .instance_init = pcm3680i_pci_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcm3680i_pci_register_types(void)
{
    type_register_static(&pcm3680i_pci_info);
}

type_init(pcm3680i_pci_register_types)
