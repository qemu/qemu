/*
 *  CRIS virtual CPU state save/load support
 *
 *  Copyright (c) 2012 Red Hat, Inc.
 *  Written by Juan Quintela <quintela@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "migration/cpu.h"

static const VMStateDescription vmstate_tlbset = {
    .name = "cpu/tlbset",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(lo, TLBSet),
        VMSTATE_UINT32(hi, TLBSet),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_cris_env = {
    .name = "env",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CPUCRISState, 16),
        VMSTATE_UINT32_ARRAY(pregs, CPUCRISState, 16),
        VMSTATE_UINT32(pc, CPUCRISState),
        VMSTATE_UINT32(ksp, CPUCRISState),
        VMSTATE_INT32(dslot, CPUCRISState),
        VMSTATE_INT32(btaken, CPUCRISState),
        VMSTATE_UINT32(btarget, CPUCRISState),
        VMSTATE_UINT32(cc_op, CPUCRISState),
        VMSTATE_UINT32(cc_mask, CPUCRISState),
        VMSTATE_UINT32(cc_dest, CPUCRISState),
        VMSTATE_UINT32(cc_src, CPUCRISState),
        VMSTATE_UINT32(cc_result, CPUCRISState),
        VMSTATE_INT32(cc_size, CPUCRISState),
        VMSTATE_INT32(cc_x, CPUCRISState),
        VMSTATE_INT32(locked_irq, CPUCRISState),
        VMSTATE_INT32(interrupt_vector, CPUCRISState),
        VMSTATE_INT32(fault_vector, CPUCRISState),
        VMSTATE_INT32(trap_vector, CPUCRISState),
        VMSTATE_UINT32_ARRAY(sregs[0], CPUCRISState, 16),
        VMSTATE_UINT32_ARRAY(sregs[1], CPUCRISState, 16),
        VMSTATE_UINT32_ARRAY(sregs[2], CPUCRISState, 16),
        VMSTATE_UINT32_ARRAY(sregs[3], CPUCRISState, 16),
        VMSTATE_UINT32(mmu_rand_lfsr, CPUCRISState),
        VMSTATE_STRUCT_ARRAY(tlbsets[0][0], CPUCRISState, 16, 0,
                             vmstate_tlbset, TLBSet),
        VMSTATE_STRUCT_ARRAY(tlbsets[0][1], CPUCRISState, 16, 0,
                             vmstate_tlbset, TLBSet),
        VMSTATE_STRUCT_ARRAY(tlbsets[0][2], CPUCRISState, 16, 0,
                             vmstate_tlbset, TLBSet),
        VMSTATE_STRUCT_ARRAY(tlbsets[0][3], CPUCRISState, 16, 0,
                             vmstate_tlbset, TLBSet),
        VMSTATE_STRUCT_ARRAY(tlbsets[1][0], CPUCRISState, 16, 0,
                             vmstate_tlbset, TLBSet),
        VMSTATE_STRUCT_ARRAY(tlbsets[1][1], CPUCRISState, 16, 0,
                             vmstate_tlbset, TLBSet),
        VMSTATE_STRUCT_ARRAY(tlbsets[1][2], CPUCRISState, 16, 0,
                             vmstate_tlbset, TLBSet),
        VMSTATE_STRUCT_ARRAY(tlbsets[1][3], CPUCRISState, 16, 0,
                             vmstate_tlbset, TLBSet),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_cris_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_CPU(),
        VMSTATE_STRUCT(env, CRISCPU, 1, vmstate_cris_env, CPUCRISState),
        VMSTATE_END_OF_LIST()
    }
};
