/*
 * Generic ISA Super I/O
 *
 * Copyright (c) 2010-2012 Herve Poussineau
 * Copyright (c) 2011-2012 Andreas Färber
 * Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "sysemu/blockdev.h"
#include "chardev/char.h"
#include "hw/block/fdc.h"
#include "hw/isa/superio.h"
#include "hw/qdev-properties.h"
#include "hw/input/i8042.h"
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
    DriveInfo *fd[MAX_FD];
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
            if (chr == NULL) {
                name = g_strdup_printf("discarding-parallel%d", i);
                chr = qemu_chr_new(name, "null", NULL);
            } else {
                name = g_strdup_printf("parallel%d", i);
            }
            isa = isa_new("isa-parallel");
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
            object_property_add_child(OBJECT(dev), name, OBJECT(isa));
            isa_realize_and_unref(isa, bus, &error_fatal);
            sio->parallel[i] = isa;
            trace_superio_create_parallel(i,
                                          k->parallel.get_iobase ?
                                          k->parallel.get_iobase(sio, i) : -1,
                                          k->parallel.get_irq ?
                                          k->parallel.get_irq(sio, i) : -1);
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
            /* FIXME use a qdev chardev prop instead of serial_hd() */
            chr = serial_hd(i);
            if (chr == NULL) {
                name = g_strdup_printf("discarding-serial%d", i);
                chr = qemu_chr_new(name, "null", NULL);
            } else {
                name = g_strdup_printf("serial%d", i);
            }
            isa = isa_new(TYPE_ISA_SERIAL);
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
            object_property_add_child(OBJECT(dev), name, OBJECT(isa));
            isa_realize_and_unref(isa, bus, &error_fatal);
            sio->serial[i] = isa;
            trace_superio_create_serial(i,
                                        k->serial.get_iobase ?
                                        k->serial.get_iobase(sio, i) : -1,
                                        k->serial.get_irq ?
                                        k->serial.get_irq(sio, i) : -1);
            g_free(name);
        }
    }

    /* Floppy disc */
    if (!k->floppy.is_enabled || k->floppy.is_enabled(sio, 0)) {
        isa = isa_new(TYPE_ISA_FDC);
        d = DEVICE(isa);
        if (k->floppy.get_iobase) {
            qdev_prop_set_uint32(d, "iobase", k->floppy.get_iobase(sio, 0));
        }
        if (k->floppy.get_irq) {
            qdev_prop_set_uint32(d, "irq", k->floppy.get_irq(sio, 0));
        }
        /* FIXME use a qdev drive property instead of drive_get() */
        for (i = 0; i < MAX_FD; i++) {
            fd[i] = drive_get(IF_FLOPPY, 0, i);
        }
        object_property_add_child(OBJECT(sio), "isa-fdc", OBJECT(isa));
        isa_realize_and_unref(isa, bus, &error_fatal);
        isa_fdc_init_drives(isa, fd);
        sio->floppy = isa;
        trace_superio_create_floppy(0,
                                    k->floppy.get_iobase ?
                                    k->floppy.get_iobase(sio, 0) : -1,
                                    k->floppy.get_irq ?
                                    k->floppy.get_irq(sio, 0) : -1);
    }

    /* Keyboard, mouse */
    isa = isa_new(TYPE_I8042);
    object_property_add_child(OBJECT(sio), TYPE_I8042, OBJECT(isa));
    isa_realize_and_unref(isa, bus, &error_fatal);
    sio->kbc = isa;

    /* IDE */
    if (k->ide.count && (!k->ide.is_enabled || k->ide.is_enabled(sio, 0))) {
        isa = isa_new("isa-ide");
        d = DEVICE(isa);
        if (k->ide.get_iobase) {
            qdev_prop_set_uint32(d, "iobase", k->ide.get_iobase(sio, 0));
        }
        if (k->ide.get_iobase) {
            qdev_prop_set_uint32(d, "iobase2", k->ide.get_iobase(sio, 1));
        }
        if (k->ide.get_irq) {
            qdev_prop_set_uint32(d, "irq", k->ide.get_irq(sio, 0));
        }
        object_property_add_child(OBJECT(sio), "isa-ide", OBJECT(isa));
        isa_realize_and_unref(isa, bus, &error_fatal);
        sio->ide = isa;
        trace_superio_create_ide(0,
                                 k->ide.get_iobase ?
                                 k->ide.get_iobase(sio, 0) : -1,
                                 k->ide.get_irq ?
                                 k->ide.get_irq(sio, 0) : -1);
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

/* SMS FDC37M817 Super I/O */
static void fdc37m81x_class_init(ObjectClass *klass, void *data)
{
    ISASuperIOClass *sc = ISA_SUPERIO_CLASS(klass);

    sc->serial.count = 2; /* NS16C550A */
    sc->parallel.count = 1;
    sc->floppy.count = 1; /* SMSC 82077AA Compatible */
    sc->ide.count = 0;
}

static const TypeInfo fdc37m81x_type_info = {
    .name          = TYPE_FDC37M81X_SUPERIO,
    .parent        = TYPE_ISA_SUPERIO,
    .instance_size = sizeof(ISASuperIODevice),
    .class_init    = fdc37m81x_class_init,
};

static void isa_superio_register_types(void)
{
    type_register_static(&isa_superio_type_info);
    type_register_static(&fdc37m81x_type_info);
}

type_init(isa_superio_register_types)
