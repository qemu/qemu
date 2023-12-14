/*
 * CTU CAN FD PCI device emulation
 * http://canbus.pages.fel.cvut.cz/
 *
 * Copyright (c) 2019 Jan Charvat (jancharvat.charvat@gmail.com)
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
#include "qemu/event_notifier.h"
#include "qemu/module.h"
#include "qemu/thread.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"

#include "ctucan_core.h"

#define TYPE_CTUCAN_PCI_DEV "ctucan_pci"

typedef struct CtuCanPCIState CtuCanPCIState;
DECLARE_INSTANCE_CHECKER(CtuCanPCIState, CTUCAN_PCI_DEV,
                         TYPE_CTUCAN_PCI_DEV)

#define CTUCAN_PCI_CORE_COUNT     2
#define CTUCAN_PCI_CORE_RANGE     0x10000

#define CTUCAN_PCI_BAR_COUNT      2

#define CTUCAN_PCI_BYTES_PER_CORE 0x4000

#ifndef PCI_VENDOR_ID_TEDIA
#define PCI_VENDOR_ID_TEDIA 0x1760
#endif

#define PCI_DEVICE_ID_TEDIA_CTUCAN_VER21 0xff00

#define CTUCAN_BAR0_RANGE 0x8000
#define CTUCAN_BAR0_CTUCAN_ID 0x0000
#define CTUCAN_BAR0_CRA_BASE  0x4000
#define CYCLONE_IV_CRA_A2P_IE (0x0050)

#define CTUCAN_WITHOUT_CTUCAN_ID  0
#define CTUCAN_WITH_CTUCAN_ID     1

struct CtuCanPCIState {
    /*< private >*/
    PCIDevice       dev;
    /*< public >*/
    MemoryRegion    ctucan_io[CTUCAN_PCI_BAR_COUNT];

    CtuCanCoreState ctucan_state[CTUCAN_PCI_CORE_COUNT];
    qemu_irq        irq;

    char            *model; /* The model that support, only SJA1000 now. */
    CanBusState     *canbus[CTUCAN_PCI_CORE_COUNT];
};

static void ctucan_pci_reset(DeviceState *dev)
{
    CtuCanPCIState *d = CTUCAN_PCI_DEV(dev);
    int i;

    for (i = 0 ; i < CTUCAN_PCI_CORE_COUNT; i++) {
        ctucan_hardware_reset(&d->ctucan_state[i]);
    }
}

static uint64_t ctucan_pci_id_cra_io_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    if (addr >= 4) {
        return 0;
    }

    uint64_t tmp = 0xC0000000 + CTUCAN_PCI_CORE_COUNT;
    tmp >>= ((addr & 3) << 3);
    if (size < 8) {
        tmp &= ((uint64_t)1 << (size << 3)) - 1;
    }
    return tmp;
}

static void ctucan_pci_id_cra_io_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{

}

static uint64_t ctucan_pci_cores_io_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    CtuCanPCIState *d = opaque;
    CtuCanCoreState *s;
    hwaddr core_num = addr / CTUCAN_PCI_BYTES_PER_CORE;

    if (core_num >= CTUCAN_PCI_CORE_COUNT) {
        return 0;
    }

    s = &d->ctucan_state[core_num];

    return ctucan_mem_read(s, addr % CTUCAN_PCI_BYTES_PER_CORE, size);
}

static void ctucan_pci_cores_io_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    CtuCanPCIState *d = opaque;
    CtuCanCoreState *s;
    hwaddr core_num = addr / CTUCAN_PCI_BYTES_PER_CORE;

    if (core_num >= CTUCAN_PCI_CORE_COUNT) {
        return;
    }

    s = &d->ctucan_state[core_num];

    return ctucan_mem_write(s, addr % CTUCAN_PCI_BYTES_PER_CORE, data, size);
}

static const MemoryRegionOps ctucan_pci_id_cra_io_ops = {
    .read = ctucan_pci_id_cra_io_read,
    .write = ctucan_pci_id_cra_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static const MemoryRegionOps ctucan_pci_cores_io_ops = {
    .read = ctucan_pci_cores_io_read,
    .write = ctucan_pci_cores_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void ctucan_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    CtuCanPCIState *d = CTUCAN_PCI_DEV(pci_dev);
    uint8_t *pci_conf;
    int i;

    pci_conf = pci_dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin A */

    d->irq = pci_allocate_irq(&d->dev);

    for (i = 0 ; i < CTUCAN_PCI_CORE_COUNT; i++) {
        ctucan_init(&d->ctucan_state[i], d->irq);
    }

    for (i = 0 ; i < CTUCAN_PCI_CORE_COUNT; i++) {
        if (ctucan_connect_to_bus(&d->ctucan_state[i], d->canbus[i]) < 0) {
            error_setg(errp, "ctucan_connect_to_bus failed");
            return;
        }
    }

    memory_region_init_io(&d->ctucan_io[0], OBJECT(d),
                          &ctucan_pci_id_cra_io_ops, d,
                          "ctucan_pci-core0", CTUCAN_BAR0_RANGE);
    memory_region_init_io(&d->ctucan_io[1], OBJECT(d),
                          &ctucan_pci_cores_io_ops, d,
                          "ctucan_pci-core1", CTUCAN_PCI_CORE_RANGE);

    for (i = 0 ; i < CTUCAN_PCI_BAR_COUNT; i++) {
        pci_register_bar(&d->dev, i, PCI_BASE_ADDRESS_MEM_MASK & 0,
                         &d->ctucan_io[i]);
    }
}

static void ctucan_pci_exit(PCIDevice *pci_dev)
{
    CtuCanPCIState *d = CTUCAN_PCI_DEV(pci_dev);
    int i;

    for (i = 0 ; i < CTUCAN_PCI_CORE_COUNT; i++) {
        ctucan_disconnect(&d->ctucan_state[i]);
    }

    qemu_free_irq(d->irq);
}

static const VMStateDescription vmstate_ctucan_pci = {
    .name = "ctucan_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, CtuCanPCIState),
        VMSTATE_STRUCT(ctucan_state[0], CtuCanPCIState, 0, vmstate_ctucan,
                       CtuCanCoreState),
#if CTUCAN_PCI_CORE_COUNT >= 2
        VMSTATE_STRUCT(ctucan_state[1], CtuCanPCIState, 0, vmstate_ctucan,
                       CtuCanCoreState),
#endif
        VMSTATE_END_OF_LIST()
    }
};

static void ctucan_pci_instance_init(Object *obj)
{
    CtuCanPCIState *d = CTUCAN_PCI_DEV(obj);

    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&d->canbus[0],
                             qdev_prop_allow_set_link_before_realize, 0);
#if CTUCAN_PCI_CORE_COUNT >= 2
    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&d->canbus[1],
                             qdev_prop_allow_set_link_before_realize, 0);
#endif
}

static void ctucan_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = ctucan_pci_realize;
    k->exit = ctucan_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_TEDIA;
    k->device_id = PCI_DEVICE_ID_TEDIA_CTUCAN_VER21;
    k->revision = 0x00;
    k->class_id = 0x000c09;
    k->subsystem_vendor_id = PCI_VENDOR_ID_TEDIA;
    k->subsystem_id = PCI_DEVICE_ID_TEDIA_CTUCAN_VER21;
    dc->desc = "CTU CAN PCI";
    dc->vmsd = &vmstate_ctucan_pci;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = ctucan_pci_reset;
}

static const TypeInfo ctucan_pci_info = {
    .name          = TYPE_CTUCAN_PCI_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(CtuCanPCIState),
    .class_init    = ctucan_pci_class_init,
    .instance_init = ctucan_pci_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void ctucan_pci_register_types(void)
{
    type_register_static(&ctucan_pci_info);
}

type_init(ctucan_pci_register_types)
