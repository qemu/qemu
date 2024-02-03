/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_CPU_H
#define HEXAGON_CPU_H

#include "fpu/softfloat-types.h"

#include "cpu-qom.h"
#include "exec/cpu-defs.h"
#include "hex_regs.h"
#include "mmvec/mmvec.h"
#include "hw/registerfields.h"

#define NUM_PREGS 4
#define TOTAL_PER_THREAD_REGS 64

#define SLOTS_MAX 4
#define STORES_MAX 2
#define REG_WRITES_MAX 32
#define PRED_WRITES_MAX 5                   /* 4 insns + endloop */
#define VSTORES_MAX 2

#define CPU_RESOLVING_TYPE TYPE_HEXAGON_CPU

#define MMU_USER_IDX 0

typedef struct {
    target_ulong va;
    uint8_t width;
    uint32_t data32;
    uint64_t data64;
} MemLog;

typedef struct {
    target_ulong va;
    int size;
    DECLARE_BITMAP(mask, MAX_VEC_SIZE_BYTES) QEMU_ALIGNED(16);
    MMVector data QEMU_ALIGNED(16);
} VStoreLog;

#define EXEC_STATUS_OK          0x0000
#define EXEC_STATUS_STOP        0x0002
#define EXEC_STATUS_REPLAY      0x0010
#define EXEC_STATUS_LOCKED      0x0020
#define EXEC_STATUS_EXCEPTION   0x0100


#define EXCEPTION_DETECTED      (env->status & EXEC_STATUS_EXCEPTION)
#define REPLAY_DETECTED         (env->status & EXEC_STATUS_REPLAY)
#define CLEAR_EXCEPTION         (env->status &= (~EXEC_STATUS_EXCEPTION))
#define SET_EXCEPTION           (env->status |= EXEC_STATUS_EXCEPTION)

/* Maximum number of vector temps in a packet */
#define VECTOR_TEMPS_MAX            4

typedef struct CPUArchState {
    target_ulong gpr[TOTAL_PER_THREAD_REGS];
    target_ulong pred[NUM_PREGS];

    /* For comparing with LLDB on target - see adjust_stack_ptrs function */
    target_ulong last_pc_dumped;
    target_ulong stack_start;

    uint8_t slot_cancelled;
    target_ulong new_value_usr;

    /*
     * Only used when HEX_DEBUG is on, but unconditionally included
     * to reduce recompile time when turning HEX_DEBUG on/off.
     */
    target_ulong reg_written[TOTAL_PER_THREAD_REGS];

    MemLog mem_log_stores[STORES_MAX];

    float_status fp_status;

    target_ulong llsc_addr;
    target_ulong llsc_val;
    uint64_t     llsc_val_i64;

    MMVector VRegs[NUM_VREGS] QEMU_ALIGNED(16);
    MMVector future_VRegs[VECTOR_TEMPS_MAX] QEMU_ALIGNED(16);
    MMVector tmp_VRegs[VECTOR_TEMPS_MAX] QEMU_ALIGNED(16);

    MMQReg QRegs[NUM_QREGS] QEMU_ALIGNED(16);
    MMQReg future_QRegs[NUM_QREGS] QEMU_ALIGNED(16);

    /* Temporaries used within instructions */
    MMVectorPair VuuV QEMU_ALIGNED(16);
    MMVectorPair VvvV QEMU_ALIGNED(16);
    MMVectorPair VxxV QEMU_ALIGNED(16);
    MMVector     vtmp QEMU_ALIGNED(16);
    MMQReg       qtmp QEMU_ALIGNED(16);

    VStoreLog vstore[VSTORES_MAX];
    target_ulong vstore_pending[VSTORES_MAX];
    bool vtcm_pending;
    VTCMStoreLog vtcm_log;
} CPUHexagonState;

typedef struct HexagonCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
} HexagonCPUClass;

struct ArchCPU {
    CPUState parent_obj;

    CPUHexagonState env;

    bool lldb_compat;
    target_ulong lldb_stack_adjust;
    bool short_circuit;
};

#include "cpu_bits.h"

FIELD(TB_FLAGS, IS_TIGHT_LOOP, 0, 1)

static inline void cpu_get_tb_cpu_state(CPUHexagonState *env, vaddr *pc,
                                        uint64_t *cs_base, uint32_t *flags)
{
    uint32_t hex_flags = 0;
    *pc = env->gpr[HEX_REG_PC];
    *cs_base = 0;
    if (*pc == env->gpr[HEX_REG_SA0]) {
        hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, IS_TIGHT_LOOP, 1);
    }
    *flags = hex_flags;
}

typedef HexagonCPU ArchCPU;

void hexagon_translate_init(void);

#include "exec/cpu-all.h"

#endif /* HEXAGON_CPU_H */
