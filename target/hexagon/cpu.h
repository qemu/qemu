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

#ifndef CONFIG_USER_ONLY
#define NUM_GREGS 32
#define GREG_WRITES_MAX 2
#define NUM_SREGS 64
#define SREG_WRITES_MAX 2
#endif

typedef struct HexagonTLBState HexagonTLBState;
typedef struct HexagonGlobalRegState HexagonGlobalRegState;

#include "cpu-qom.h"
#include "exec/cpu-common.h"
#include "exec/target_long.h"
#include "hex_regs.h"
#include "mmvec/mmvec.h"
#include "hw/core/registerfields.h"
#include "qemu/bitmap.h"

#ifndef CONFIG_USER_ONLY
#error "Hexagon does not support system emulation"
#endif


#define NUM_PREGS 4
#define TOTAL_PER_THREAD_REGS 64

#define SLOTS_MAX 4
#define STORES_MAX 2
#define REG_WRITES_MAX 32
#define PRED_WRITES_MAX 5                   /* 4 insns + endloop */
#define VSTORES_MAX 2
#define MAX_TLB_ENTRIES 1024
#define THREADS_MAX 8

#define CPU_RESOLVING_TYPE TYPE_HEXAGON_CPU
#ifndef CONFIG_USER_ONLY
#define CPU_INTERRUPT_SWI      CPU_INTERRUPT_TGT_INT_0
#define CPU_INTERRUPT_K0_UNLOCK CPU_INTERRUPT_TGT_INT_1
#define CPU_INTERRUPT_TLB_UNLOCK CPU_INTERRUPT_TGT_INT_2

#define HEX_CPU_MODE_USER    1
#define HEX_CPU_MODE_GUEST   2
#define HEX_CPU_MODE_MONITOR 3

#define HEX_EXE_MODE_OFF     1
#define HEX_EXE_MODE_RUN     2
#define HEX_EXE_MODE_WAIT    3
#define HEX_EXE_MODE_DEBUG   4
#endif

#define MMU_USER_IDX         0
#ifndef CONFIG_USER_ONLY
#define MMU_GUEST_IDX        1
#define MMU_KERNEL_IDX       2

typedef enum {
    HEX_LOCK_UNLOCKED       = 0,
    HEX_LOCK_WAITING        = 1,
    HEX_LOCK_OWNER          = 2,
    HEX_LOCK_QUEUED        = 3
} hex_lock_state_t;
#endif


#define HEXAGON_CPU_IRQ_0 0
#define HEXAGON_CPU_IRQ_1 1
#define HEXAGON_CPU_IRQ_2 2
#define HEXAGON_CPU_IRQ_3 3
#define HEXAGON_CPU_IRQ_4 4
#define HEXAGON_CPU_IRQ_5 5
#define HEXAGON_CPU_IRQ_6 6
#define HEXAGON_CPU_IRQ_7 7

typedef struct {
    target_ulong va;
    uint32_t width;
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
    uint32_t cause_code;

    /* For comparing with LLDB on target - see adjust_stack_ptrs function */
    target_ulong last_pc_dumped;
    target_ulong stack_start;

    uint8_t slot_cancelled;

#ifndef CONFIG_USER_ONLY
    /* Some system registers are per thread and some are global. */
    uint32_t t_sreg[NUM_SREGS];

    uint32_t greg[NUM_GREGS];
    uint32_t wait_next_pc;

    /* This alias of CPUState.cpu_index is used by imported sources: */
    uint32_t threadId;
    hex_lock_state_t tlb_lock_state;
    hex_lock_state_t k0_lock_state;
    uint32_t tlb_lock_count;
    uint32_t k0_lock_count;
    uint64_t t_cycle_count;
#endif
    uint32_t next_PC;
    target_ulong new_value_usr;

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

    const HexagonCPUDef *hex_def;
} HexagonCPUClass;

struct ArchCPU {
    CPUState parent_obj;

    CPUHexagonState env;

    bool lldb_compat;
    target_ulong lldb_stack_adjust;
    bool short_circuit;
#ifndef CONFIG_USER_ONLY
    HexagonTLBState *tlb;
    uint32_t boot_addr;
    HexagonGlobalRegState *globalregs;
    uint32_t htid;
#endif
};

#include "cpu_bits.h"

FIELD(TB_FLAGS, IS_TIGHT_LOOP, 0, 1)
FIELD(TB_FLAGS, MMU_INDEX, 1, 3)
FIELD(TB_FLAGS, PCYCLE_ENABLED, 4, 1)

G_NORETURN void hexagon_raise_exception_err(CPUHexagonState *env,
                                            uint32_t exception,
                                            uintptr_t pc);

#ifndef CONFIG_USER_ONLY
/*
 * @return true if the @a thread_env hardware thread is
 * not stopped.
 */
bool hexagon_thread_is_enabled(CPUHexagonState *thread_env);
void hexagon_cpu_soft_reset(CPUHexagonState *env);
#endif

typedef HexagonCPU ArchCPU;

void hexagon_translate_init(void);
void hexagon_translate_code(CPUState *cs, TranslationBlock *tb,
                            int *max_insns, vaddr pc, void *host_pc);

#endif /* HEXAGON_CPU_H */
