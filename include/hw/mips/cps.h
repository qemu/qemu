/*
 * Coherent Processing System emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MIPS_CPS_H
#define MIPS_CPS_H

#include "hw/sysbus.h"
#include "hw/clock.h"
#include "hw/misc/mips_cmgcr.h"
#include "hw/intc/mips_gic.h"
#include "hw/misc/mips_cpc.h"
#include "hw/misc/mips_itu.h"
#include "target/mips/cpu.h"
#include "qom/object.h"

#define TYPE_MIPS_CPS "mips-cps"
OBJECT_DECLARE_SIMPLE_TYPE(MIPSCPSState, MIPS_CPS)

struct MIPSCPSState {
    SysBusDevice parent_obj;

    uint32_t num_vp;
    uint32_t num_irq;
    char *cpu_type;
    bool cpu_is_bigendian;

    MemoryRegion container;
    MIPSGCRState gcr;
    MIPSGICState gic;
    MIPSCPCState cpc;
    MIPSITUState itu;
    Clock *clock;
};

qemu_irq get_cps_irq(MIPSCPSState *cps, int pin_number);

#endif
