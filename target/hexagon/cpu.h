/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

/* Forward declaration needed by some of the header files */
typedef struct CPUHexagonState CPUHexagonState;

#include <fenv.h>
#include "hex_regs.h"
#include "mmvec/mmvec.h"

#define TARGET_PAGE_BITS 16     /* 64K pages */
#define TARGET_LONG_BITS 32

#include "qemu/compiler.h"
#include "qemu-common.h"
#include "exec/cpu-defs.h"

#define NUM_PREGS 4
#ifdef CONFIG_USER_ONLY
#define TOTAL_PER_THREAD_REGS 64
#else
#error System mode not implemented
#endif

#define SLOTS_MAX 4
#define STORES_MAX 2
#define REG_WRITES_MAX 32
#define PRED_WRITES_MAX 5                   /* 4 insns + endloop */

#define TYPE_HEXAGON_CPU "hexagon-cpu"

#define HEXAGON_CPU_TYPE_SUFFIX "-" TYPE_HEXAGON_CPU
#define HEXAGON_CPU_TYPE_NAME(name) (name HEXAGON_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_HEXAGON_CPU

#define TYPE_HEXAGON_CPU_V67 HEXAGON_CPU_TYPE_NAME("v67")

#define MMU_USER_IDX 0

struct MemLog {
    vaddr_t va;
    size1u_t width;
    size4u_t data32;
    size8u_t data64;
};

typedef struct {
    target_ulong va;
    int size;
    mmvector_t mask;
    mmvector_t data;
} vstorelog_t;

typedef struct {
    unsigned char cdata[256];
    size4u_t range;
    size1u_t format;
} mem_access_info_t;

#define EXEC_STATUS_OK          0x0000
#define EXEC_STATUS_STOP        0x0002
#define EXEC_STATUS_REPLAY      0x0010
#define EXEC_STATUS_LOCKED      0x0020
#define EXEC_STATUS_EXCEPTION   0x0100


#define EXCEPTION_DETECTED      (env->status & EXEC_STATUS_EXCEPTION)
#define REPLAY_DETECTED         (env->status & EXEC_STATUS_REPLAY)
#define CLEAR_EXCEPTION         (env->status &= (~EXEC_STATUS_EXCEPTION))
#define SET_EXCEPTION           (env->status |= EXEC_STATUS_EXCEPTION)

/* This needs to be large enough for all the reads and writes in a packet */
#define TEMP_VECTORS_MAX        25

struct CPUHexagonState {
    target_ulong gpr[TOTAL_PER_THREAD_REGS];
    target_ulong pred[NUM_PREGS];
    target_ulong branch_taken;
    target_ulong this_PC;
    target_ulong next_PC;

    /* For comparing with LLDB on target - see hack_stack_ptrs function */
    target_ulong stack_start;
    target_ulong stack_adjust;

    size1u_t slot_cancelled;
    target_ulong new_value[TOTAL_PER_THREAD_REGS];

    /*
     * Only used when HEX_DEBUG is on, but unconditionally included
     * to reduce recompile time when turning it on/off.
     */
    target_ulong reg_written[TOTAL_PER_THREAD_REGS];

    target_ulong new_pred_value[NUM_PREGS];
    target_ulong pred_written[NUM_PREGS];

    struct MemLog mem_log_stores[STORES_MAX];

    target_ulong dczero_addr;

    fenv_t fenv;

    target_ulong llsc_addr;
    target_ulong llsc_val;
    uint64_t     llsc_val_i64;
    target_ulong llsc_newval;
    uint64_t     llsc_newval_i64;
    target_ulong llsc_reg;

    mmvector_t VRegs[NUM_VREGS];
    mmvector_t future_VRegs[NUM_VREGS];
    mmvector_t tmp_VRegs[NUM_VREGS];

    VRegMask VRegs_updated_tmp;
    VRegMask VRegs_updated;
    VRegMask VRegs_select;

    mmqreg_t QRegs[NUM_QREGS];
    mmqreg_t future_QRegs[NUM_QREGS];
    QRegMask QRegs_updated;

    vstorelog_t vstore[2];
    uint8_t store_pending[2];
    uint8_t vstore_pending[2];
    target_ulong is_gather_store_insn;
    target_ulong gather_issued;
    uint8_t vtcm_pending;
    vtcm_storelog_t vtcm_log;
    mem_access_info_t mem_access[SLOTS_MAX];

    int status;

    mmvector_t temp_vregs[TEMP_VECTORS_MAX];
    mmqreg_t temp_qregs[TEMP_VECTORS_MAX];
};

#define HEXAGON_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(HexagonCPUClass, (klass), TYPE_HEXAGON_CPU)
#define HEXAGON_CPU(obj) \
    OBJECT_CHECK(HexagonCPU, (obj), TYPE_HEXAGON_CPU)
#define HEXAGON_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(HexagonCPUClass, (obj), TYPE_HEXAGON_CPU)

typedef struct HexagonCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} HexagonCPUClass;

typedef struct HexagonCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/
    CPUNegativeOffsetState neg;
    CPUHexagonState env;
} HexagonCPU;

static inline HexagonCPU *hexagon_env_get_cpu(CPUHexagonState *env)
{
    return container_of(env, HexagonCPU, env);
}

#include "cpu_bits.h"

#define cpu_signal_handler cpu_hexagon_signal_handler
extern int cpu_hexagon_signal_handler(int host_signum, void *pinfo, void *puc);

static inline void cpu_get_tb_cpu_state(CPUHexagonState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->gpr[HEX_REG_PC];
    *cs_base = 0;
#ifdef CONFIG_USER_ONLY
    *flags = 0;
#else
#error System mode not supported on Hexagon yet
#endif
}

typedef struct CPUHexagonState CPUArchState;
typedef HexagonCPU ArchCPU;

#include "exec/cpu-all.h"

#endif /* HEXAGON_CPU_H */
