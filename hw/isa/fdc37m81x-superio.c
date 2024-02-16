/*
 * SMS FDC37M817 Super I/O
 *
 * Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/isa/superio.h"

static void fdc37m81x_class_init(ObjectClass *klass, void *data)
{
    ISASuperIOClass *sc = ISA_SUPERIO_CLASS(klass);

    sc->serial.count = 2; /* NS16C550A */
    sc->parallel.count = 1;
    sc->floppy.count = 1; /* SMSC 82077AA Compatible */
    sc->ide.count = 0;
}

static const TypeInfo types[] = {
    {
        .name          = TYPE_FDC37M81X_SUPERIO,
        .parent        = TYPE_ISA_SUPERIO,
        .class_init    = fdc37m81x_class_init,
    },
};

DEFINE_TYPES(types)
