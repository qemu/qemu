/*
 * QEMU UniCore32 CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */
#ifndef QEMU_UC32_CPU_QOM_H
#define QEMU_UC32_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_UNICORE32_CPU "unicore32-cpu"

OBJECT_DECLARE_TYPE(UniCore32CPU, UniCore32CPUClass,
                    UNICORE32_CPU)

/**
 * UniCore32CPUClass:
 * @parent_realize: The parent class' realize handler.
 *
 * A UniCore32 CPU model.
 */
struct UniCore32CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
};


#endif
