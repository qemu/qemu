/*
 * PowerMac MacIO device emulation
 *
 * Copyright (c) 2005-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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
#include "hw/hw.h"
#include "hw/ppc/mac.h"
#include "hw/pci/pci.h"
#include "hw/ppc/mac_dbdma.h"
#include "hw/char/escc.h"

#define TYPE_MACIO "macio"
#define MACIO(obj) OBJECT_CHECK(MacIOState, (obj), TYPE_MACIO)

typedef struct MacIOState
{
    /*< private >*/
    PCIDevice parent;
    /*< public >*/

    MemoryRegion bar;
    CUDAState cuda;
    void *dbdma;
    MemoryRegion *pic_mem;
    MemoryRegion *escc_mem;
} MacIOState;

#define OLDWORLD_MACIO(obj) \
    OBJECT_CHECK(OldWorldMacIOState, (obj), TYPE_OLDWORLD_MACIO)

typedef struct OldWorldMacIOState {
    /*< private >*/
    MacIOState parent_obj;
    /*< public >*/

    qemu_irq irqs[3];

    MacIONVRAMState nvram;
    MACIOIDEState ide;
} OldWorldMacIOState;

#define NEWWORLD_MACIO(obj) \
    OBJECT_CHECK(NewWorldMacIOState, (obj), TYPE_NEWWORLD_MACIO)

typedef struct NewWorldMacIOState {
    /*< private >*/
    MacIOState parent_obj;
    /*< public >*/
    qemu_irq irqs[5];
    MACIOIDEState ide[2];
} NewWorldMacIOState;

static void macio_bar_setup(MacIOState *macio_state)
{
    MemoryRegion *bar = &macio_state->bar;

    if (macio_state->escc_mem) {
        memory_region_add_subregion(bar, 0x13000, macio_state->escc_mem);
    }
}

static int macio_common_initfn(PCIDevice *d)
{
    MacIOState *s = MACIO(d);
    SysBusDevice *sysbus_dev;
    int ret;

    d->config[0x3d] = 0x01; // interrupt on pin 1

    ret = qdev_init(DEVICE(&s->cuda));
    if (ret < 0) {
        return ret;
    }
    sysbus_dev = SYS_BUS_DEVICE(&s->cuda);
    memory_region_add_subregion(&s->bar, 0x16000,
                                sysbus_mmio_get_region(sysbus_dev, 0));

    macio_bar_setup(s);
    pci_register_bar(d, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar);

    return 0;
}

static int macio_oldworld_initfn(PCIDevice *d)
{
    MacIOState *s = MACIO(d);
    OldWorldMacIOState *os = OLDWORLD_MACIO(d);
    SysBusDevice *sysbus_dev;
    int ret = macio_common_initfn(d);
    if (ret < 0) {
        return ret;
    }

    sysbus_dev = SYS_BUS_DEVICE(&s->cuda);
    sysbus_connect_irq(sysbus_dev, 0, os->irqs[0]);

    ret = qdev_init(DEVICE(&os->nvram));
    if (ret < 0) {
        return ret;
    }
    sysbus_dev = SYS_BUS_DEVICE(&os->nvram);
    memory_region_add_subregion(&s->bar, 0x60000,
                                sysbus_mmio_get_region(sysbus_dev, 0));
    pmac_format_nvram_partition(&os->nvram, os->nvram.size);

    if (s->pic_mem) {
        /* Heathrow PIC */
        memory_region_add_subregion(&s->bar, 0x00000, s->pic_mem);
    }

    sysbus_dev = SYS_BUS_DEVICE(&os->ide);
    sysbus_connect_irq(sysbus_dev, 0, os->irqs[1]);
    sysbus_connect_irq(sysbus_dev, 1, os->irqs[2]);
    macio_ide_register_dma(&os->ide, s->dbdma, 0x16);
    ret = qdev_init(DEVICE(&os->ide));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void macio_oldworld_init(Object *obj)
{
    MacIOState *s = MACIO(obj);
    OldWorldMacIOState *os = OLDWORLD_MACIO(obj);
    DeviceState *dev;

    qdev_init_gpio_out(DEVICE(obj), os->irqs, ARRAY_SIZE(os->irqs));

    object_initialize(&os->nvram, TYPE_MACIO_NVRAM);
    dev = DEVICE(&os->nvram);
    qdev_prop_set_uint32(dev, "size", 0x2000);
    qdev_prop_set_uint32(dev, "it_shift", 4);

    object_initialize(&os->ide, TYPE_MACIO_IDE);
    qdev_set_parent_bus(DEVICE(&os->ide), sysbus_get_default());
    memory_region_add_subregion(&s->bar, 0x1f000 + (1 * 0x1000), &os->ide.mem);
    object_property_add_child(obj, "ide", OBJECT(&os->ide), NULL);
}

static int macio_newworld_initfn(PCIDevice *d)
{
    MacIOState *s = MACIO(d);
    NewWorldMacIOState *ns = NEWWORLD_MACIO(d);
    SysBusDevice *sysbus_dev;
    int ret = macio_common_initfn(d);
    if (ret < 0) {
        return ret;
    }

    sysbus_dev = SYS_BUS_DEVICE(&s->cuda);
    sysbus_connect_irq(sysbus_dev, 0, ns->irqs[0]);

    if (s->pic_mem) {
        /* OpenPIC */
        memory_region_add_subregion(&s->bar, 0x40000, s->pic_mem);
    }

    sysbus_dev = SYS_BUS_DEVICE(&ns->ide[0]);
    sysbus_connect_irq(sysbus_dev, 0, ns->irqs[1]);
    sysbus_connect_irq(sysbus_dev, 1, ns->irqs[2]);
    macio_ide_register_dma(&ns->ide[0], s->dbdma, 0x16);
    ret = qdev_init(DEVICE(&ns->ide[0]));
    if (ret < 0) {
        return ret;
    }

    sysbus_dev = SYS_BUS_DEVICE(&ns->ide[1]);
    sysbus_connect_irq(sysbus_dev, 0, ns->irqs[3]);
    sysbus_connect_irq(sysbus_dev, 1, ns->irqs[4]);
    macio_ide_register_dma(&ns->ide[1], s->dbdma, 0x1a);
    ret = qdev_init(DEVICE(&ns->ide[1]));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void macio_newworld_init(Object *obj)
{
    MacIOState *s = MACIO(obj);
    NewWorldMacIOState *ns = NEWWORLD_MACIO(obj);
    int i;
    gchar *name;

    qdev_init_gpio_out(DEVICE(obj), ns->irqs, ARRAY_SIZE(ns->irqs));

    for (i = 0; i < 2; i++) {
        object_initialize(&ns->ide[i], TYPE_MACIO_IDE);
        qdev_set_parent_bus(DEVICE(&ns->ide[i]), sysbus_get_default());
        memory_region_add_subregion(&s->bar, 0x1f000 + ((i + 1) * 0x1000),
                                    &ns->ide[i].mem);
        name = g_strdup_printf("ide[%i]", i);
        object_property_add_child(obj, name, OBJECT(&ns->ide[i]), NULL);
        g_free(name);
    }
}

static void macio_instance_init(Object *obj)
{
    MacIOState *s = MACIO(obj);
    MemoryRegion *dbdma_mem;

    memory_region_init(&s->bar, "macio", 0x80000);

    object_initialize(&s->cuda, TYPE_CUDA);
    qdev_set_parent_bus(DEVICE(&s->cuda), sysbus_get_default());
    object_property_add_child(obj, "cuda", OBJECT(&s->cuda), NULL);

    s->dbdma = DBDMA_init(&dbdma_mem);
    memory_region_add_subregion(&s->bar, 0x08000, dbdma_mem);
}

static void macio_oldworld_class_init(ObjectClass *oc, void *data)
{
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(oc);

    pdc->init = macio_oldworld_initfn;
    pdc->device_id = PCI_DEVICE_ID_APPLE_343S1201;
}

static void macio_newworld_class_init(ObjectClass *oc, void *data)
{
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(oc);

    pdc->init = macio_newworld_initfn;
    pdc->device_id = PCI_DEVICE_ID_APPLE_UNI_N_KEYL;
}

static void macio_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->class_id = PCI_CLASS_OTHERS << 8;
}

static const TypeInfo macio_oldworld_type_info = {
    .name          = TYPE_OLDWORLD_MACIO,
    .parent        = TYPE_MACIO,
    .instance_size = sizeof(OldWorldMacIOState),
    .instance_init = macio_oldworld_init,
    .class_init    = macio_oldworld_class_init,
};

static const TypeInfo macio_newworld_type_info = {
    .name          = TYPE_NEWWORLD_MACIO,
    .parent        = TYPE_MACIO,
    .instance_size = sizeof(NewWorldMacIOState),
    .instance_init = macio_newworld_init,
    .class_init    = macio_newworld_class_init,
};

static const TypeInfo macio_type_info = {
    .name          = TYPE_MACIO,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MacIOState),
    .instance_init = macio_instance_init,
    .abstract      = true,
    .class_init    = macio_class_init,
};

static void macio_register_types(void)
{
    type_register_static(&macio_type_info);
    type_register_static(&macio_oldworld_type_info);
    type_register_static(&macio_newworld_type_info);
}

type_init(macio_register_types)

void macio_init(PCIDevice *d,
                MemoryRegion *pic_mem,
                MemoryRegion *escc_mem)
{
    MacIOState *macio_state = MACIO(d);

    macio_state->pic_mem = pic_mem;
    macio_state->escc_mem = escc_mem;
    /* Note: this code is strongly inspirated from the corresponding code
       in PearPC */

    qdev_init_nofail(DEVICE(d));
}
