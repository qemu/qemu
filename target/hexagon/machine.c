/*
 * Copyright(c) 2023-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "migration/cpu.h"
#include "cpu.h"
#include "hex_mmu.h"

static int get_hex_tlb_ptr(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field)
{
    CPUHexagonTLBContext *tlb = pv;
    for (int i = 0; i < ARRAY_SIZE(tlb->entries); i++) {
        tlb->entries[i] = qemu_get_be64(f);
    }
    return 0;
}

static int put_hex_tlb_ptr(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, JSONWriter *vmdesc)
{
    CPUHexagonTLBContext *tlb = pv;
    for (int i = 0; i < ARRAY_SIZE(tlb->entries); i++) {
        qemu_put_be64(f,  tlb->entries[i]);
    }
    return 0;
}

const VMStateInfo vmstate_info_hex_tlb_ptr = {
    .name = "hex_tlb_pointer",
    .get  = get_hex_tlb_ptr,
    .put  = put_hex_tlb_ptr,
};


const VMStateDescription vmstate_hexagon_cpu = {
    .name = "cpu",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_CPU(),
        VMSTATE_UINTTL_ARRAY(env.gpr, HexagonCPU, TOTAL_PER_THREAD_REGS),
        VMSTATE_UINTTL_ARRAY(env.pred, HexagonCPU, NUM_PREGS),
        VMSTATE_UINTTL_ARRAY(env.t_sreg, HexagonCPU, NUM_SREGS),
        VMSTATE_UINTTL_ARRAY(env.greg, HexagonCPU, NUM_GREGS),
        VMSTATE_UINTTL(env.next_PC, HexagonCPU),
        VMSTATE_UINTTL(env.tlb_lock_state, HexagonCPU),
        VMSTATE_UINTTL(env.k0_lock_state, HexagonCPU),
        VMSTATE_UINTTL(env.tlb_lock_count, HexagonCPU),
        VMSTATE_UINTTL(env.k0_lock_count, HexagonCPU),
        VMSTATE_UINTTL(env.threadId, HexagonCPU),
        VMSTATE_UINTTL(env.cause_code, HexagonCPU),
        VMSTATE_UINTTL(env.wait_next_pc, HexagonCPU),
        VMSTATE_POINTER(env.hex_tlb, HexagonCPU, 0,
                        vmstate_info_hex_tlb_ptr, CPUHexagonTLBContext *),

        VMSTATE_END_OF_LIST()
    },
};

