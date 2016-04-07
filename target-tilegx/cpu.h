/*
 *  TILE-Gx virtual CPU header
 *
 *  Copyright (c) 2015 Chen Gang
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
#ifndef CPU_TILEGX_H
#define CPU_TILEGX_H

#include "qemu-common.h"

#define TARGET_LONG_BITS 64

#define CPUArchState struct CPUTLGState

#include "exec/cpu-defs.h"


/* TILE-Gx common register alias */
#define TILEGX_R_RE    0   /*  0 register, for function/syscall return value */
#define TILEGX_R_ERR   1   /*  1 register, for syscall errno flag */
#define TILEGX_R_NR    10  /* 10 register, for syscall number */
#define TILEGX_R_BP    52  /* 52 register, optional frame pointer */
#define TILEGX_R_TP    53  /* TP register, thread local storage data */
#define TILEGX_R_SP    54  /* SP register, stack pointer */
#define TILEGX_R_LR    55  /* LR register, may save pc, but it is not pc */
#define TILEGX_R_COUNT 56  /* Only 56 registers are really useful */
#define TILEGX_R_SN    56  /* SN register, obsoleted, it likes zero register */
#define TILEGX_R_IDN0  57  /* IDN0 register, cause IDN_ACCESS exception */
#define TILEGX_R_IDN1  58  /* IDN1 register, cause IDN_ACCESS exception */
#define TILEGX_R_UDN0  59  /* UDN0 register, cause UDN_ACCESS exception */
#define TILEGX_R_UDN1  60  /* UDN1 register, cause UDN_ACCESS exception */
#define TILEGX_R_UDN2  61  /* UDN2 register, cause UDN_ACCESS exception */
#define TILEGX_R_UDN3  62  /* UDN3 register, cause UDN_ACCESS exception */
#define TILEGX_R_ZERO  63  /* Zero register, always zero */
#define TILEGX_R_NOREG 255 /* Invalid register value */

/* TILE-Gx special registers used by outside */
enum {
    TILEGX_SPR_CMPEXCH = 0,
    TILEGX_SPR_CRITICAL_SEC = 1,
    TILEGX_SPR_SIM_CONTROL = 2,
    TILEGX_SPR_EX_CONTEXT_0_0 = 3,
    TILEGX_SPR_EX_CONTEXT_0_1 = 4,
    TILEGX_SPR_COUNT
};

/* Exception numbers */
typedef enum {
    TILEGX_EXCP_NONE = 0,
    TILEGX_EXCP_SYSCALL = 1,
    TILEGX_EXCP_SIGNAL = 2,
    TILEGX_EXCP_OPCODE_UNKNOWN = 0x101,
    TILEGX_EXCP_OPCODE_UNIMPLEMENTED = 0x102,
    TILEGX_EXCP_OPCODE_CMPEXCH = 0x103,
    TILEGX_EXCP_OPCODE_CMPEXCH4 = 0x104,
    TILEGX_EXCP_OPCODE_EXCH = 0x105,
    TILEGX_EXCP_OPCODE_EXCH4 = 0x106,
    TILEGX_EXCP_OPCODE_FETCHADD = 0x107,
    TILEGX_EXCP_OPCODE_FETCHADD4 = 0x108,
    TILEGX_EXCP_OPCODE_FETCHADDGEZ = 0x109,
    TILEGX_EXCP_OPCODE_FETCHADDGEZ4 = 0x10a,
    TILEGX_EXCP_OPCODE_FETCHAND = 0x10b,
    TILEGX_EXCP_OPCODE_FETCHAND4 = 0x10c,
    TILEGX_EXCP_OPCODE_FETCHOR = 0x10d,
    TILEGX_EXCP_OPCODE_FETCHOR4 = 0x10e,
    TILEGX_EXCP_REG_IDN_ACCESS = 0x181,
    TILEGX_EXCP_REG_UDN_ACCESS = 0x182,
    TILEGX_EXCP_UNALIGNMENT = 0x201,
    TILEGX_EXCP_DBUG_BREAK = 0x301
} TileExcp;

typedef struct CPUTLGState {
    uint64_t regs[TILEGX_R_COUNT];     /* Common used registers by outside */
    uint64_t spregs[TILEGX_SPR_COUNT]; /* Special used registers by outside */
    uint64_t pc;                       /* Current pc */

#if defined(CONFIG_USER_ONLY)
    uint64_t excaddr;                  /* exception address */
    uint64_t atomic_srca;              /* Arguments to atomic "exceptions" */
    uint64_t atomic_srcb;
    uint32_t atomic_dstr;
    uint32_t signo;                    /* Signal number */
    uint32_t sigcode;                  /* Signal code */
#endif

    CPU_COMMON
} CPUTLGState;

#include "qom/cpu.h"

#define TYPE_TILEGX_CPU "tilegx-cpu"

#define TILEGX_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(TileGXCPUClass, (klass), TYPE_TILEGX_CPU)
#define TILEGX_CPU(obj) \
    OBJECT_CHECK(TileGXCPU, (obj), TYPE_TILEGX_CPU)
#define TILEGX_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(TileGXCPUClass, (obj), TYPE_TILEGX_CPU)

/**
 * TileGXCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A Tile-Gx CPU model.
 */
typedef struct TileGXCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} TileGXCPUClass;

/**
 * TileGXCPU:
 * @env: #CPUTLGState
 *
 * A Tile-GX CPU.
 */
typedef struct TileGXCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUTLGState env;
} TileGXCPU;

static inline TileGXCPU *tilegx_env_get_cpu(CPUTLGState *env)
{
    return container_of(env, TileGXCPU, env);
}

#define ENV_GET_CPU(e) CPU(tilegx_env_get_cpu(e))

#define ENV_OFFSET offsetof(TileGXCPU, env)

/* TILE-Gx memory attributes */
#define TARGET_PAGE_BITS 16  /* TILE-Gx uses 64KB page size */
#define TARGET_PHYS_ADDR_SPACE_BITS 42
#define TARGET_VIRT_ADDR_SPACE_BITS 64
#define MMU_USER_IDX    0  /* Current memory operation is in user mode */

#include "exec/cpu-all.h"

void tilegx_tcg_init(void);
int cpu_tilegx_exec(CPUState *s);
int cpu_tilegx_signal_handler(int host_signum, void *pinfo, void *puc);

TileGXCPU *cpu_tilegx_init(const char *cpu_model);

#define cpu_init(cpu_model) CPU(cpu_tilegx_init(cpu_model))

#define cpu_exec cpu_tilegx_exec
#define cpu_signal_handler cpu_tilegx_signal_handler

static inline void cpu_get_tb_cpu_state(CPUTLGState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = 0;
}

#include "exec/exec-all.h"

#endif
