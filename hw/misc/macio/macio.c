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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/ppc/mac.h"
#include "hw/misc/macio/cuda.h"
#include "hw/pci/pci.h"
#include "hw/ppc/mac_dbdma.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/char/escc.h"
#include "hw/misc/macio/macio.h"
#include "hw/intc/heathrow_pic.h"
#include "sysemu/sysemu.h"
#include "trace.h"

/* Note: this code is strongly inspirated from the corresponding code
 * in PearPC */

/*
 * The mac-io has two interfaces to the ESCC. One is called "escc-legacy",
 * while the other one is the normal, current ESCC interface.
 *
 * The magic below creates memory aliases to spawn the escc-legacy device
 * purely by rerouting the respective registers to our escc region. This
 * works because the only difference between the two memory regions is the
 * register layout, not their semantics.
 *
 * Reference: ftp://ftp.software.ibm.com/rs6000/technology/spec/chrp/inwork/CHRP_IORef_1.0.pdf
 */
static void macio_escc_legacy_setup(MacIOState *s)
{
    ESCCState *escc = ESCC(&s->escc);
    SysBusDevice *sbd = SYS_BUS_DEVICE(escc);
    MemoryRegion *escc_legacy = g_new(MemoryRegion, 1);
    MemoryRegion *bar = &s->bar;
    int i;
    static const int maps[] = {
        0x00, 0x00, /* Command B */
        0x02, 0x20, /* Command A */
        0x04, 0x10, /* Data B */
        0x06, 0x30, /* Data A */
        0x08, 0x40, /* Enhancement B */
        0x0A, 0x50, /* Enhancement A */
        0x80, 0x80, /* Recovery count */
        0x90, 0x90, /* Start A */
        0xa0, 0xa0, /* Start B */
        0xb0, 0xb0, /* Detect AB */
    };

    memory_region_init(escc_legacy, OBJECT(s), "escc-legacy", 256);
    for (i = 0; i < ARRAY_SIZE(maps); i += 2) {
        MemoryRegion *port = g_new(MemoryRegion, 1);
        memory_region_init_alias(port, OBJECT(s), "escc-legacy-port",
                                 sysbus_mmio_get_region(sbd, 0),
                                 maps[i + 1], 0x2);
        memory_region_add_subregion(escc_legacy, maps[i], port);
    }

    memory_region_add_subregion(bar, 0x12000, escc_legacy);
}

static void macio_bar_setup(MacIOState *s)
{
    ESCCState *escc = ESCC(&s->escc);
    SysBusDevice *sbd = SYS_BUS_DEVICE(escc);
    MemoryRegion *bar = &s->bar;

    memory_region_add_subregion(bar, 0x13000, sysbus_mmio_get_region(sbd, 0));
    macio_escc_legacy_setup(s);
}

static void macio_common_realize(PCIDevice *d, Error **errp)
{
    MacIOState *s = MACIO(d);
    SysBusDevice *sysbus_dev;

    if (!qdev_realize(DEVICE(&s->dbdma), BUS(&s->macio_bus), errp)) {
        return;
    }
    sysbus_dev = SYS_BUS_DEVICE(&s->dbdma);
    memory_region_add_subregion(&s->bar, 0x08000,
                                sysbus_mmio_get_region(sysbus_dev, 0));

    qdev_prop_set_uint32(DEVICE(&s->escc), "disabled", 0);
    qdev_prop_set_uint32(DEVICE(&s->escc), "frequency", ESCC_CLOCK);
    qdev_prop_set_uint32(DEVICE(&s->escc), "it_shift", 4);
    qdev_prop_set_chr(DEVICE(&s->escc), "chrA", serial_hd(0));
    qdev_prop_set_chr(DEVICE(&s->escc), "chrB", serial_hd(1));
    qdev_prop_set_uint32(DEVICE(&s->escc), "chnBtype", escc_serial);
    qdev_prop_set_uint32(DEVICE(&s->escc), "chnAtype", escc_serial);
    if (!qdev_realize(DEVICE(&s->escc), BUS(&s->macio_bus), errp)) {
        return;
    }

    macio_bar_setup(s);
    pci_register_bar(d, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar);
}

static void macio_realize_ide(MacIOState *s, MACIOIDEState *ide,
                              qemu_irq irq0, qemu_irq irq1, int dmaid,
                              Error **errp)
{
    SysBusDevice *sysbus_dev;

    sysbus_dev = SYS_BUS_DEVICE(ide);
    sysbus_connect_irq(sysbus_dev, 0, irq0);
    sysbus_connect_irq(sysbus_dev, 1, irq1);
    qdev_prop_set_uint32(DEVICE(ide), "channel", dmaid);
    object_property_set_link(OBJECT(ide), "dbdma", OBJECT(&s->dbdma),
                             &error_abort);
    macio_ide_register_dma(ide);

    qdev_realize(DEVICE(ide), BUS(&s->macio_bus), errp);
}

static void macio_oldworld_realize(PCIDevice *d, Error **errp)
{
    MacIOState *s = MACIO(d);
    OldWorldMacIOState *os = OLDWORLD_MACIO(d);
    DeviceState *pic_dev = DEVICE(os->pic);
    Error *err = NULL;
    SysBusDevice *sysbus_dev;

    macio_common_realize(d, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    qdev_prop_set_uint64(DEVICE(&s->cuda), "timebase-frequency",
                         s->frequency);
    if (!qdev_realize(DEVICE(&s->cuda), BUS(&s->macio_bus), errp)) {
        return;
    }
    sysbus_dev = SYS_BUS_DEVICE(&s->cuda);
    memory_region_add_subregion(&s->bar, 0x16000,
                                sysbus_mmio_get_region(sysbus_dev, 0));
    sysbus_connect_irq(sysbus_dev, 0, qdev_get_gpio_in(pic_dev,
                                                       OLDWORLD_CUDA_IRQ));

    sysbus_dev = SYS_BUS_DEVICE(&s->escc);
    sysbus_connect_irq(sysbus_dev, 0, qdev_get_gpio_in(pic_dev,
                                                       OLDWORLD_ESCCB_IRQ));
    sysbus_connect_irq(sysbus_dev, 1, qdev_get_gpio_in(pic_dev,
                                                       OLDWORLD_ESCCA_IRQ));

    if (!qdev_realize(DEVICE(&os->nvram), BUS(&s->macio_bus), errp)) {
        return;
    }
    sysbus_dev = SYS_BUS_DEVICE(&os->nvram);
    memory_region_add_subregion(&s->bar, 0x60000,
                                sysbus_mmio_get_region(sysbus_dev, 0));
    pmac_format_nvram_partition(&os->nvram, os->nvram.size);

    /* Heathrow PIC */
    sysbus_dev = SYS_BUS_DEVICE(os->pic);
    memory_region_add_subregion(&s->bar, 0x0,
                                sysbus_mmio_get_region(sysbus_dev, 0));

    /* IDE buses */
    macio_realize_ide(s, &os->ide[0],
                      qdev_get_gpio_in(pic_dev, OLDWORLD_IDE0_IRQ),
                      qdev_get_gpio_in(pic_dev, OLDWORLD_IDE0_DMA_IRQ),
                      0x16, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    macio_realize_ide(s, &os->ide[1],
                      qdev_get_gpio_in(pic_dev, OLDWORLD_IDE1_IRQ),
                      qdev_get_gpio_in(pic_dev, OLDWORLD_IDE1_DMA_IRQ),
                      0x1a, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
}

static void macio_init_ide(MacIOState *s, MACIOIDEState *ide, int index)
{
    gchar *name = g_strdup_printf("ide[%i]", index);
    uint32_t addr = 0x1f000 + ((index + 1) * 0x1000);

    object_initialize_child(OBJECT(s), name, ide, TYPE_MACIO_IDE);
    qdev_prop_set_uint32(DEVICE(ide), "addr", addr);
    memory_region_add_subregion(&s->bar, addr, &ide->mem);
    g_free(name);
}

static void macio_oldworld_init(Object *obj)
{
    MacIOState *s = MACIO(obj);
    OldWorldMacIOState *os = OLDWORLD_MACIO(obj);
    DeviceState *dev;
    int i;

    object_property_add_link(obj, "pic", TYPE_HEATHROW,
                             (Object **) &os->pic,
                             qdev_prop_allow_set_link_before_realize,
                             0);

    object_initialize_child(OBJECT(s), "cuda", &s->cuda, TYPE_CUDA);

    object_initialize_child(OBJECT(s), "nvram", &os->nvram, TYPE_MACIO_NVRAM);
    dev = DEVICE(&os->nvram);
    qdev_prop_set_uint32(dev, "size", 0x2000);
    qdev_prop_set_uint32(dev, "it_shift", 4);

    for (i = 0; i < 2; i++) {
        macio_init_ide(s, &os->ide[i], i);
    }
}

static void timer_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned size)
{
    trace_macio_timer_write(addr, size, value);
}

static uint64_t timer_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t value = 0;
    uint64_t systime = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t kltime;

    kltime = muldiv64(systime, 4194300, NANOSECONDS_PER_SECOND * 4);
    kltime = muldiv64(kltime, 18432000, 1048575);

    switch (addr) {
    case 0x38:
        value = kltime;
        break;
    case 0x3c:
        value = kltime >> 32;
        break;
    }

    trace_macio_timer_read(addr, size, value);
    return value;
}

static const MemoryRegionOps timer_ops = {
    .read = timer_read,
    .write = timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void macio_newworld_realize(PCIDevice *d, Error **errp)
{
    MacIOState *s = MACIO(d);
    NewWorldMacIOState *ns = NEWWORLD_MACIO(d);
    DeviceState *pic_dev = DEVICE(ns->pic);
    Error *err = NULL;
    SysBusDevice *sysbus_dev;
    MemoryRegion *timer_memory = NULL;

    macio_common_realize(d, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    sysbus_dev = SYS_BUS_DEVICE(&s->escc);
    sysbus_connect_irq(sysbus_dev, 0, qdev_get_gpio_in(pic_dev,
                                                       NEWWORLD_ESCCB_IRQ));
    sysbus_connect_irq(sysbus_dev, 1, qdev_get_gpio_in(pic_dev,
                                                       NEWWORLD_ESCCA_IRQ));

    /* OpenPIC */
    sysbus_dev = SYS_BUS_DEVICE(ns->pic);
    memory_region_add_subregion(&s->bar, 0x40000,
                                sysbus_mmio_get_region(sysbus_dev, 0));

    /* IDE buses */
    macio_realize_ide(s, &ns->ide[0],
                      qdev_get_gpio_in(pic_dev, NEWWORLD_IDE0_IRQ),
                      qdev_get_gpio_in(pic_dev, NEWWORLD_IDE0_DMA_IRQ),
                      0x16, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    macio_realize_ide(s, &ns->ide[1],
                      qdev_get_gpio_in(pic_dev, NEWWORLD_IDE1_IRQ),
                      qdev_get_gpio_in(pic_dev, NEWWORLD_IDE1_DMA_IRQ),
                      0x1a, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* Timer */
    timer_memory = g_new(MemoryRegion, 1);
    memory_region_init_io(timer_memory, OBJECT(s), &timer_ops, NULL, "timer",
                          0x1000);
    memory_region_add_subregion(&s->bar, 0x15000, timer_memory);

    if (ns->has_pmu) {
        /* GPIOs */
        sysbus_dev = SYS_BUS_DEVICE(&ns->gpio);
        object_property_set_link(OBJECT(&ns->gpio), "pic", OBJECT(pic_dev),
                                 &error_abort);
        memory_region_add_subregion(&s->bar, 0x50,
                                    sysbus_mmio_get_region(sysbus_dev, 0));
        if (!qdev_realize(DEVICE(&ns->gpio), BUS(&s->macio_bus), errp)) {
            return;
        }

        /* PMU */
        object_initialize_child(OBJECT(s), "pmu", &s->pmu, TYPE_VIA_PMU);
        object_property_set_link(OBJECT(&s->pmu), "gpio", OBJECT(sysbus_dev),
                                 &error_abort);
        qdev_prop_set_bit(DEVICE(&s->pmu), "has-adb", ns->has_adb);
        if (!qdev_realize(DEVICE(&s->pmu), BUS(&s->macio_bus), errp)) {
            return;
        }
        sysbus_dev = SYS_BUS_DEVICE(&s->pmu);
        sysbus_connect_irq(sysbus_dev, 0, qdev_get_gpio_in(pic_dev,
                                                           NEWWORLD_PMU_IRQ));
        memory_region_add_subregion(&s->bar, 0x16000,
                                    sysbus_mmio_get_region(sysbus_dev, 0));
    } else {
        object_unparent(OBJECT(&ns->gpio));

        /* CUDA */
        object_initialize_child(OBJECT(s), "cuda", &s->cuda, TYPE_CUDA);
        qdev_prop_set_uint64(DEVICE(&s->cuda), "timebase-frequency",
                             s->frequency);

        if (!qdev_realize(DEVICE(&s->cuda), BUS(&s->macio_bus), errp)) {
            return;
        }
        sysbus_dev = SYS_BUS_DEVICE(&s->cuda);
        sysbus_connect_irq(sysbus_dev, 0, qdev_get_gpio_in(pic_dev,
                                                           NEWWORLD_CUDA_IRQ));
        memory_region_add_subregion(&s->bar, 0x16000,
                                    sysbus_mmio_get_region(sysbus_dev, 0));
    }
}

static void macio_newworld_init(Object *obj)
{
    MacIOState *s = MACIO(obj);
    NewWorldMacIOState *ns = NEWWORLD_MACIO(obj);
    int i;

    object_property_add_link(obj, "pic", TYPE_OPENPIC,
                             (Object **) &ns->pic,
                             qdev_prop_allow_set_link_before_realize,
                             0);

    object_initialize_child(OBJECT(s), "gpio", &ns->gpio, TYPE_MACIO_GPIO);

    for (i = 0; i < 2; i++) {
        macio_init_ide(s, &ns->ide[i], i);
    }
}

static void macio_instance_init(Object *obj)
{
    MacIOState *s = MACIO(obj);

    memory_region_init(&s->bar, obj, "macio", 0x80000);

    qbus_create_inplace(&s->macio_bus, sizeof(s->macio_bus), TYPE_MACIO_BUS,
                        DEVICE(obj), "macio.0");

    object_initialize_child(OBJECT(s), "dbdma", &s->dbdma, TYPE_MAC_DBDMA);

    object_initialize_child(OBJECT(s), "escc", &s->escc, TYPE_ESCC);
}

static const VMStateDescription vmstate_macio_oldworld = {
    .name = "macio-oldworld",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent, OldWorldMacIOState),
        VMSTATE_END_OF_LIST()
    }
};

static void macio_oldworld_class_init(ObjectClass *oc, void *data)
{
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    pdc->realize = macio_oldworld_realize;
    pdc->device_id = PCI_DEVICE_ID_APPLE_343S1201;
    dc->vmsd = &vmstate_macio_oldworld;
}

static const VMStateDescription vmstate_macio_newworld = {
    .name = "macio-newworld",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent, NewWorldMacIOState),
        VMSTATE_END_OF_LIST()
    }
};

static Property macio_newworld_properties[] = {
    DEFINE_PROP_BOOL("has-pmu", NewWorldMacIOState, has_pmu, false),
    DEFINE_PROP_BOOL("has-adb", NewWorldMacIOState, has_adb, false),
    DEFINE_PROP_END_OF_LIST()
};

static void macio_newworld_class_init(ObjectClass *oc, void *data)
{
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    pdc->realize = macio_newworld_realize;
    pdc->device_id = PCI_DEVICE_ID_APPLE_UNI_N_KEYL;
    dc->vmsd = &vmstate_macio_newworld;
    device_class_set_props(dc, macio_newworld_properties);
}

static Property macio_properties[] = {
    DEFINE_PROP_UINT64("frequency", MacIOState, frequency, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void macio_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->class_id = PCI_CLASS_OTHERS << 8;
    device_class_set_props(dc, macio_properties);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    /* Reason: Uses serial_hds in macio_instance_init */
    dc->user_creatable = false;
}

static const TypeInfo macio_bus_info = {
    .name = TYPE_MACIO_BUS,
    .parent = TYPE_SYSTEM_BUS,
    .instance_size = sizeof(MacIOBusState),
};

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
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void macio_register_types(void)
{
    type_register_static(&macio_bus_info);
    type_register_static(&macio_type_info);
    type_register_static(&macio_oldworld_type_info);
    type_register_static(&macio_newworld_type_info);
}

type_init(macio_register_types)
