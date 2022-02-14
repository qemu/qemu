/*
 * QEMU SuperH CPU
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
#ifndef QEMU_SUPERH_CPU_QOM_H
#define QEMU_SUPERH_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_SUPERH_CPU "superh-cpu"

#define TYPE_SH7750R_CPU SUPERH_CPU_TYPE_NAME("sh7750r")
#define TYPE_SH7751R_CPU SUPERH_CPU_TYPE_NAME("sh7751r")
#define TYPE_SH7785_CPU  SUPERH_CPU_TYPE_NAME("sh7785")

OBJECT_DECLARE_CPU_TYPE(SuperHCPU, SuperHCPUClass, SUPERH_CPU)

/**
 * SuperHCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 * @pvr: Processor Version Register
 * @prr: Processor Revision Register
 * @cvr: Cache Version Register
 *
 * A SuperH CPU model.
 */
struct SuperHCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    DeviceReset parent_reset;

    uint32_t pvr;
    uint32_t prr;
    uint32_t cvr;
};


#endif
