/*
 *  Microblaze VMState for qemu.
 *
 *  Copyright (c) 2020 Linaro, Ltd.
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "migration/cpu.h"


static const VMStateField vmstate_mmu_fields[] = {
    VMSTATE_UINT64_2DARRAY(rams, MicroBlazeMMU, 2, TLB_ENTRIES),
    VMSTATE_UINT8_ARRAY(tids, MicroBlazeMMU, TLB_ENTRIES),
    VMSTATE_UINT32_ARRAY(regs, MicroBlazeMMU, 3),
    VMSTATE_END_OF_LIST()
};

static const VMStateDescription vmstate_mmu = {
    .name = "mmu",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = vmstate_mmu_fields,
};

static int get_msr(QEMUFile *f, void *opaque, size_t size,
                   const VMStateField *field)
{
    CPUMBState *env = container_of(opaque, CPUMBState, msr);

    mb_cpu_write_msr(env, qemu_get_be32(f));
    return 0;
}

static int put_msr(QEMUFile *f, void *opaque, size_t size,
                   const VMStateField *field, JSONWriter *vmdesc)
{
    CPUMBState *env = container_of(opaque, CPUMBState, msr);

    qemu_put_be32(f, mb_cpu_read_msr(env));
    return 0;
}

static const VMStateInfo vmstate_msr = {
    .name = "msr",
    .get = get_msr,
    .put = put_msr,
};

static const VMStateField vmstate_env_fields[] = {
    VMSTATE_UINT32_ARRAY(regs, CPUMBState, 32),

    VMSTATE_UINT32(pc, CPUMBState),
    VMSTATE_SINGLE(msr, CPUMBState, 0, vmstate_msr, uint32_t),
    VMSTATE_UINT32(esr, CPUMBState),
    VMSTATE_UINT32(fsr, CPUMBState),
    VMSTATE_UINT32(btr, CPUMBState),
    VMSTATE_UINT32(edr, CPUMBState),
    VMSTATE_UINT32(slr, CPUMBState),
    VMSTATE_UINT32(shr, CPUMBState),
    VMSTATE_UINT64(ear, CPUMBState),

    VMSTATE_UINT32(btarget, CPUMBState),
    VMSTATE_UINT32(imm, CPUMBState),
    VMSTATE_UINT32(iflags, CPUMBState),

    VMSTATE_UINT32(res_val, CPUMBState),
    VMSTATE_UINTTL(res_addr, CPUMBState),

    VMSTATE_STRUCT(mmu, CPUMBState, 0, vmstate_mmu, MicroBlazeMMU),

    VMSTATE_END_OF_LIST()
};

static const VMStateDescription vmstate_env = {
    .name = "env",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = vmstate_env_fields,
};

static const VMStateField vmstate_cpu_fields[] = {
    VMSTATE_CPU(),
    VMSTATE_STRUCT(env, MicroBlazeCPU, 1, vmstate_env, CPUMBState),
    VMSTATE_END_OF_LIST()
};

const VMStateDescription vmstate_mb_cpu = {
    .name = "cpu",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = vmstate_cpu_fields,
};
