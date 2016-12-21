/*
 * OpenRISC Machine
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "migration/cpu.h"

static const VMStateDescription vmstate_env = {
    .name = "env",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(gpr, CPUOpenRISCState, 32),
        VMSTATE_UINT32(sr, CPUOpenRISCState),
        VMSTATE_UINT32(epcr, CPUOpenRISCState),
        VMSTATE_UINT32(eear, CPUOpenRISCState),
        VMSTATE_UINT32(esr, CPUOpenRISCState),
        VMSTATE_UINT32(fpcsr, CPUOpenRISCState),
        VMSTATE_UINT32(pc, CPUOpenRISCState),
        VMSTATE_UINT32(npc, CPUOpenRISCState),
        VMSTATE_UINT32(ppc, CPUOpenRISCState),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_openrisc_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_CPU(),
        VMSTATE_STRUCT(env, OpenRISCCPU, 1, vmstate_env, CPUOpenRISCState),
        VMSTATE_END_OF_LIST()
    }
};
