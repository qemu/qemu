/*
 * QEMU binary/target API (QOM types)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "hw/arm/machines-qom.h"

static const TypeInfo target_info_types[] = {
    {
        .name           = TYPE_TARGET_ARM_MACHINE,
        .parent         = TYPE_INTERFACE,
    },
    {
        .name           = TYPE_TARGET_AARCH64_MACHINE,
        .parent         = TYPE_INTERFACE,
    },
};

DEFINE_TYPES(target_info_types)
