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

#include "hw/hw.h"
#include "hw/boards.h"

static const VMStateDescription vmstate_cpu = {
    .name = "cpu",
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

void cpu_save(QEMUFile *f, void *opaque)
{
    vmstate_save_state(f, &vmstate_cpu, opaque);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    return vmstate_load_state(f, &vmstate_cpu, opaque, version_id);
}
