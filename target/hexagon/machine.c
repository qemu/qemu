/*
 * Copyright(c) 2023-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "migration/cpu.h"
#include "cpu.h"


const VMStateDescription vmstate_hexagon_cpu = {
    .name = "cpu",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_CPU(),
        VMSTATE_UINTTL_ARRAY(env.gpr, HexagonCPU, TOTAL_PER_THREAD_REGS),
        VMSTATE_UINTTL_ARRAY(env.pred, HexagonCPU, NUM_PREGS),
        VMSTATE_UINTTL_ARRAY(env.t_sreg, HexagonCPU, NUM_SREGS),
        VMSTATE_UINTTL_ARRAY(env.t_sreg_written, HexagonCPU, NUM_SREGS),
        VMSTATE_UINTTL_ARRAY(env.greg, HexagonCPU, NUM_GREGS),
        VMSTATE_UINTTL_ARRAY(env.greg_written, HexagonCPU, NUM_GREGS),
        VMSTATE_UINTTL(env.next_PC, HexagonCPU),
        VMSTATE_UINTTL(env.tlb_lock_state, HexagonCPU),
        VMSTATE_UINTTL(env.k0_lock_state, HexagonCPU),
        VMSTATE_UINTTL(env.tlb_lock_count, HexagonCPU),
        VMSTATE_UINTTL(env.k0_lock_count, HexagonCPU),
        VMSTATE_UINTTL(env.threadId, HexagonCPU),
        VMSTATE_UINTTL(env.cause_code, HexagonCPU),
        VMSTATE_UINTTL(env.wait_next_pc, HexagonCPU),
        VMSTATE_END_OF_LIST()
    },
};

