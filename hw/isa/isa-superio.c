/*
 * Generic ISA Super I/O
 *
 * Copyright (c) 2010-2012 Herve Poussineau
 * Copyright (c) 2011-2012 Andreas Färber
 * Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This code is licensed under the GNU GPLv2 and later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "chardev/char.h"
#include "hw/isa/superio.h"
#include "hw/char/serial.h"
#include "trace.h"

static void isa_superio_realize(DeviceState *dev, Error **errp)
{
    ISASuperIODevice *sio = ISA_SUPERIO(dev);
    ISASuperIOClass *k = ISA_SUPERIO_GET_CLASS(sio);
    ISABus *bus = isa_bus_from_device(ISA_DEVICE(dev));
    ISADevice *isa;
    DeviceState *d;
    Chardev *chr;
    char *name;
    int i;

    /* Parallel port */
    for (i = 0; i < k->parallel.count; i++) {
        if (i >= ARRAY_SIZE(sio->parallel)) {
            warn_report("superio: ignoring %td parallel controllers",
                        k->parallel.count - ARRAY_SIZE(sio->parallel));
            break;
        }
        if (!k->parallel.is_enabled || k->parallel.is_enabled(sio, i)) {
            /* FIXME use a qdev chardev prop instead of parallel_hds[] */
            chr = parallel_hds[i];
            if (chr == NULL || chr->be) {
                name = g_strdup_printf("discarding-parallel%d", i);
                chr = qemu_chr_new(name, "null");
            } else {
                name = g_strdup_printf("parallel%d", i);
            }
            isa = isa_create(bus, "isa-parallel");
            d = DEVICE(isa);
            qdev_prop_set_uint32(d, "index", i);
            if (k->parallel.get_iobase) {
                qdev_prop_set_uint32(d, "iobase",
                                     k->parallel.get_iobase(sio, i));
            }
            if (k->parallel.get_irq) {
                qdev_prop_set_uint32(d, "irq", k->parallel.get_irq(sio, i));
            }
            qdev_prop_set_chr(d, "chardev", chr);
            qdev_init_nofail(d);
            sio->parallel[i] = isa;
            trace_superio_create_parallel(i,
                                          k->parallel.get_iobase ?
                                          k->parallel.get_iobase(sio, i) : -1,
                                          k->parallel.get_irq ?
                                          k->parallel.get_irq(sio, i) : -1);
            object_property_add_child(OBJECT(dev), name,
                                      OBJECT(sio->parallel[i]), NULL);
            g_free(name);
        }
    }

    /* Serial */
    for (i = 0; i < k->serial.count; i++) {
        if (i >= ARRAY_SIZE(sio->serial)) {
            warn_report("superio: ignoring %td serial controllers",
                        k->serial.count - ARRAY_SIZE(sio->serial));
            break;
        }
        if (!k->serial.is_enabled || k->serial.is_enabled(sio, i)) {
            /* FIXME use a qdev chardev prop instead of serial_hds[] */
            chr = serial_hds[i];
            if (chr == NULL || chr->be) {
                name = g_strdup_printf("discarding-serial%d", i);
                chr = qemu_chr_new(name, "null");
            } else {
                name = g_strdup_printf("serial%d", i);
            }
            isa = isa_create(bus, TYPE_ISA_SERIAL);
            d = DEVICE(isa);
            qdev_prop_set_uint32(d, "index", i);
            if (k->serial.get_iobase) {
                qdev_prop_set_uint32(d, "iobase",
                                     k->serial.get_iobase(sio, i));
            }
            if (k->serial.get_irq) {
                qdev_prop_set_uint32(d, "irq", k->serial.get_irq(sio, i));
            }
            qdev_prop_set_chr(d, "chardev", chr);
            qdev_init_nofail(d);
            sio->serial[i] = isa;
            trace_superio_create_serial(i,
                                        k->serial.get_iobase ?
                                        k->serial.get_iobase(sio, i) : -1,
                                        k->serial.get_irq ?
                                        k->serial.get_irq(sio, i) : -1);
            object_property_add_child(OBJECT(dev), name,
                                      OBJECT(sio->serial[0]), NULL);
            g_free(name);
        }
    }
}

static void isa_superio_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = isa_superio_realize;
    /* Reason: Uses parallel_hds[0] in realize(), so it can't be used twice */
    dc->user_creatable = false;
}

static const TypeInfo isa_superio_type_info = {
    .name = TYPE_ISA_SUPERIO,
    .parent = TYPE_ISA_DEVICE,
    .abstract = true,
    .class_size = sizeof(ISASuperIOClass),
    .class_init = isa_superio_class_init,
};

static void isa_superio_register_types(void)
{
    type_register_static(&isa_superio_type_info);
}

type_init(isa_superio_register_types)
