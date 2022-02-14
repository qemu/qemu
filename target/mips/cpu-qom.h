/*
 * QEMU MIPS CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */
#ifndef QEMU_MIPS_CPU_QOM_H
#define QEMU_MIPS_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#ifdef TARGET_MIPS64
#define TYPE_MIPS_CPU "mips64-cpu"
#else
#define TYPE_MIPS_CPU "mips-cpu"
#endif

OBJECT_DECLARE_CPU_TYPE(MIPSCPU, MIPSCPUClass, MIPS_CPU)

/**
 * MIPSCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A MIPS CPU model.
 */
struct MIPSCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    DeviceReset parent_reset;
    const struct mips_def_t *cpu_def;

    /* Used for the jazz board to modify mips_cpu_do_transaction_failed. */
    bool no_data_aborts;
};


#endif
