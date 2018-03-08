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
#include "hw/isa/superio.h"
#include "trace.h"

static const TypeInfo isa_superio_type_info = {
    .name = TYPE_ISA_SUPERIO,
    .parent = TYPE_ISA_DEVICE,
    .abstract = true,
    .class_size = sizeof(ISASuperIOClass),
};

static void isa_superio_register_types(void)
{
    type_register_static(&isa_superio_type_info);
}

type_init(isa_superio_register_types)
