/*
 * QEMU Macintosh Nubus Virtio MMIO card
 *
 * Copyright (c) 2024 Mark Cave-Ayland <mark.cave-ayland@ilande.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/nubus/nubus-virtio-mmio.h"


#define NUBUS_VIRTIO_MMIO_PIC_OFFSET   0
#define NUBUS_VIRTIO_MMIO_DEV_OFFSET   0x200


static void nubus_virtio_mmio_set_input_irq(void *opaque, int n, int level)
{
    NubusDevice *nd = NUBUS_DEVICE(opaque);

    nubus_set_irq(nd, level);
}

static void nubus_virtio_mmio_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    NubusVirtioMMIODeviceClass *nvmdc = NUBUS_VIRTIO_MMIO_GET_CLASS(dev);
    NubusVirtioMMIO *s = NUBUS_VIRTIO_MMIO(dev);
    NubusDevice *nd = NUBUS_DEVICE(dev);
    SysBusDevice *sbd;
    int i, offset;

    nvmdc->parent_realize(dev, errp);
    if (*errp) {
        return;
    }

    /* Goldfish PIC */
    sbd = SYS_BUS_DEVICE(&s->pic);
    if (!sysbus_realize(sbd, errp)) {
        return;
    }
    memory_region_add_subregion(&nd->slot_mem, NUBUS_VIRTIO_MMIO_PIC_OFFSET,
                                sysbus_mmio_get_region(sbd, 0));
    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in_named(dev, "pic-input-irq", 0));

    /* virtio-mmio devices */
    offset = NUBUS_VIRTIO_MMIO_DEV_OFFSET;
    for (i = 0; i < NUBUS_VIRTIO_MMIO_NUM_DEVICES; i++) {
        sbd = SYS_BUS_DEVICE(&s->virtio_mmio[i]);
        qdev_prop_set_bit(DEVICE(sbd), "force-legacy", false);
        if (!sysbus_realize_and_unref(sbd, errp)) {
            return;
        }

        memory_region_add_subregion(&nd->slot_mem, offset,
                                    sysbus_mmio_get_region(sbd, 0));
        offset += 0x200;

        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(DEVICE(&s->pic), i));
    }
}

static void nubus_virtio_mmio_init(Object *obj)
{
    NubusVirtioMMIO *s = NUBUS_VIRTIO_MMIO(obj);
    int i;

    object_initialize_child(obj, "pic", &s->pic, TYPE_GOLDFISH_PIC);
    for (i = 0; i < NUBUS_VIRTIO_MMIO_NUM_DEVICES; i++) {
        char *name = g_strdup_printf("virtio-mmio[%d]", i);
        object_initialize_child(obj, name, &s->virtio_mmio[i],
                                TYPE_VIRTIO_MMIO);
        g_free(name);
    }

    /* Input from goldfish PIC */
    qdev_init_gpio_in_named(DEVICE(obj), nubus_virtio_mmio_set_input_irq,
                            "pic-input-irq", 1);
}

static void nubus_virtio_mmio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    NubusVirtioMMIODeviceClass *nvmdc = NUBUS_VIRTIO_MMIO_CLASS(oc);

    device_class_set_parent_realize(dc, nubus_virtio_mmio_realize,
                                    &nvmdc->parent_realize);
}

static const TypeInfo nubus_virtio_mmio_types[] = {
    {
        .name = TYPE_NUBUS_VIRTIO_MMIO,
        .parent = TYPE_NUBUS_DEVICE,
        .instance_init = nubus_virtio_mmio_init,
        .instance_size = sizeof(NubusVirtioMMIO),
        .class_init = nubus_virtio_mmio_class_init,
        .class_size = sizeof(NubusVirtioMMIODeviceClass),
    },
};

DEFINE_TYPES(nubus_virtio_mmio_types)
