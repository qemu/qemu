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
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(gpr, CPUOpenRISCState, 32),
        VMSTATE_UINTTL(pc, CPUOpenRISCState),
        VMSTATE_UINTTL(npc, CPUOpenRISCState),
        VMSTATE_UINTTL(ppc, CPUOpenRISCState),
        VMSTATE_UINTTL(jmp_pc, CPUOpenRISCState),
        VMSTATE_UINTTL(lock_addr, CPUOpenRISCState),
        VMSTATE_UINTTL(lock_value, CPUOpenRISCState),
        VMSTATE_UINTTL(epcr, CPUOpenRISCState),
        VMSTATE_UINTTL(eear, CPUOpenRISCState),
        VMSTATE_UINT32(sr, CPUOpenRISCState),
        VMSTATE_UINT32(vr, CPUOpenRISCState),
        VMSTATE_UINT32(upr, CPUOpenRISCState),
        VMSTATE_UINT32(cpucfgr, CPUOpenRISCState),
        VMSTATE_UINT32(dmmucfgr, CPUOpenRISCState),
        VMSTATE_UINT32(immucfgr, CPUOpenRISCState),
        VMSTATE_UINT32(esr, CPUOpenRISCState),
        VMSTATE_UINT32(fpcsr, CPUOpenRISCState),
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
