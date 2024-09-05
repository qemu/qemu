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

#define NUM_GREGS 32
#define GREG_WRITES_MAX 32
#define NUM_SREGS 64
#define SREG_WRITES_MAX 64

#include "cpu-qom.h"
#include "exec/cpu-defs.h"
#include "exec/cpu-common.h"
#include "hex_regs.h"
#include "mmvec/mmvec.h"
#include "hw/registerfields.h"

#ifndef CONFIG_USER_ONLY
#include "reg_fields.h"
typedef struct CPUHexagonTLBContext CPUHexagonTLBContext;
#endif

#define NUM_PREGS 4
#define TOTAL_PER_THREAD_REGS 64

#define SLOTS_MAX 4
#define STORES_MAX 2
#define REG_WRITES_MAX 32
#define PRED_WRITES_MAX 5                   /* 4 insns + endloop */
#define VSTORES_MAX 2
#define VECTOR_UNIT_MAX 8

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

#define HEXAGON_CPU_IRQ_0 0
#define HEXAGON_CPU_IRQ_1 1
#define HEXAGON_CPU_IRQ_2 2
#define HEXAGON_CPU_IRQ_3 3
#define HEXAGON_CPU_IRQ_4 4
#define HEXAGON_CPU_IRQ_5 5
#define HEXAGON_CPU_IRQ_6 6
#define HEXAGON_CPU_IRQ_7 7

typedef enum {
    HEX_LOCK_UNLOCKED       = 0,
    HEX_LOCK_WAITING        = 1,
    HEX_LOCK_OWNER          = 2,
    HEX_LOCK_QUEUED        = 3
} hex_lock_state_t;
#endif

#define CPU_RESOLVING_TYPE TYPE_HEXAGON_CPU

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
    target_ulong cause_code;

    /* For comparing with LLDB on target - see adjust_stack_ptrs function */
    target_ulong last_pc_dumped;
    target_ulong stack_start;

    uint8_t slot_cancelled;
    uint64_t t_cycle_count;
    uint64_t *g_pcycle_base;
#ifndef CONFIG_USER_ONLY
    /* Some system registers are per thread and some are global. */
    target_ulong t_sreg[NUM_SREGS];
    target_ulong t_sreg_written[NUM_SREGS];
    target_ulong *g_sreg;

    target_ulong greg[NUM_GREGS];
    target_ulong greg_written[NUM_GREGS];
    target_ulong wait_next_pc;

    /* This alias of CPUState.cpu_index is used by imported sources: */
    target_ulong threadId;
    hex_lock_state_t tlb_lock_state;
    hex_lock_state_t k0_lock_state;
    target_ulong tlb_lock_count;
    target_ulong k0_lock_count;
    target_ulong next_PC;
    CPUHexagonTLBContext *hex_tlb;
#endif
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
} HexagonCPUClass;

struct ArchCPU {
    CPUState parent_obj;

    CPUHexagonState env;

    bool lldb_compat;
    target_ulong lldb_stack_adjust;
    bool short_circuit;
#ifndef CONFIG_USER_ONLY
    uint32_t num_tlbs;
    uint32_t l2vic_base_addr;
    uint32_t hvx_contexts;
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
uint32_t hexagon_greg_read(CPUHexagonState *env, uint32_t reg);
uint32_t hexagon_sreg_read(CPUHexagonState *env, uint32_t reg);
void hexagon_gdb_sreg_write(CPUHexagonState *env, uint32_t reg, uint32_t val);
void hexagon_cpu_soft_reset(CPUHexagonState *env);
#endif

#include "exec/cpu-all.h"

#ifndef CONFIG_USER_ONLY
#include "cpu_helper.h"
#endif

static inline void cpu_get_tb_cpu_state(CPUHexagonState *env, vaddr *pc,
                                        uint64_t *cs_base, uint32_t *flags)
{
    uint32_t hex_flags = 0;
    *pc = env->gpr[HEX_REG_PC];
    *cs_base = 0;
    if (*pc == env->gpr[HEX_REG_SA0]) {
        hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, IS_TIGHT_LOOP, 1);
    }
    if (*pc & PCALIGN_MASK) {
        hexagon_raise_exception_err(env, HEX_CAUSE_PC_NOT_ALIGNED, 0);
    }
#ifndef CONFIG_USER_ONLY
    target_ulong syscfg = ARCH_GET_SYSTEM_REG(env, HEX_SREG_SYSCFG);

    bool pcycle_enabled = extract32(syscfg,
                                    reg_field_info[SYSCFG_PCYCLEEN].offset,
                                    reg_field_info[SYSCFG_PCYCLEEN].width);

    hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, MMU_INDEX,
                           cpu_mmu_index(env_cpu(env), false));

    if (pcycle_enabled) {
        hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, PCYCLE_ENABLED, 1);
    }
#else
    hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, PCYCLE_ENABLED, true);
    hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, MMU_INDEX, MMU_USER_IDX);
#endif
    *flags = hex_flags;
}

typedef HexagonCPU ArchCPU;

void hexagon_translate_init(void);
#ifndef CONFIG_USER_ONLY
void hexagon_cpu_do_interrupt(CPUState *cpu);
void register_trap_exception(CPUHexagonState *env, int type, int imm,
                             target_ulong PC);
#endif

#endif /* HEXAGON_CPU_H */
