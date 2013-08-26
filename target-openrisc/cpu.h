/*
 * OpenRISC virtual CPU header.
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

#ifndef CPU_OPENRISC_H
#define CPU_OPENRISC_H

#define TARGET_LONG_BITS 32
#define ELF_MACHINE    EM_OPENRISC

#define CPUArchState struct CPUOpenRISCState

/* cpu_openrisc_map_address_* in CPUOpenRISCTLBContext need this decl.  */
struct OpenRISCCPU;

#include "config.h"
#include "qemu-common.h"
#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"
#include "qom/cpu.h"

#define TYPE_OPENRISC_CPU "or32-cpu"

#define OPENRISC_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(OpenRISCCPUClass, (klass), TYPE_OPENRISC_CPU)
#define OPENRISC_CPU(obj) \
    OBJECT_CHECK(OpenRISCCPU, (obj), TYPE_OPENRISC_CPU)
#define OPENRISC_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(OpenRISCCPUClass, (obj), TYPE_OPENRISC_CPU)

/**
 * OpenRISCCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A OpenRISC CPU model.
 */
typedef struct OpenRISCCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} OpenRISCCPUClass;

#define NB_MMU_MODES    3

enum {
    MMU_NOMMU_IDX = 0,
    MMU_SUPERVISOR_IDX = 1,
    MMU_USER_IDX = 2,
};

#define TARGET_PAGE_BITS 13

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#define SET_FP_CAUSE(reg, v)    do {\
                                    (reg) = ((reg) & ~(0x3f << 12)) | \
                                            ((v & 0x3f) << 12);\
                                } while (0)
#define GET_FP_ENABLE(reg)       (((reg) >>  7) & 0x1f)
#define UPDATE_FP_FLAGS(reg, v)   do {\
                                      (reg) |= ((v & 0x1f) << 2);\
                                  } while (0)

/* Version Register */
#define SPR_VR 0xFFFF003F

/* Internal flags, delay slot flag */
#define D_FLAG    1

/* Interrupt */
#define NR_IRQS  32

/* Unit presece register */
enum {
    UPR_UP = (1 << 0),
    UPR_DCP = (1 << 1),
    UPR_ICP = (1 << 2),
    UPR_DMP = (1 << 3),
    UPR_IMP = (1 << 4),
    UPR_MP = (1 << 5),
    UPR_DUP = (1 << 6),
    UPR_PCUR = (1 << 7),
    UPR_PMP = (1 << 8),
    UPR_PICP = (1 << 9),
    UPR_TTP = (1 << 10),
    UPR_CUP = (255 << 24),
};

/* CPU configure register */
enum {
    CPUCFGR_NSGF = (15 << 0),
    CPUCFGR_CGF = (1 << 4),
    CPUCFGR_OB32S = (1 << 5),
    CPUCFGR_OB64S = (1 << 6),
    CPUCFGR_OF32S = (1 << 7),
    CPUCFGR_OF64S = (1 << 8),
    CPUCFGR_OV64S = (1 << 9),
};

/* DMMU configure register */
enum {
    DMMUCFGR_NTW = (3 << 0),
    DMMUCFGR_NTS = (7 << 2),
    DMMUCFGR_NAE = (7 << 5),
    DMMUCFGR_CRI = (1 << 8),
    DMMUCFGR_PRI = (1 << 9),
    DMMUCFGR_TEIRI = (1 << 10),
    DMMUCFGR_HTR = (1 << 11),
};

/* IMMU configure register */
enum {
    IMMUCFGR_NTW = (3 << 0),
    IMMUCFGR_NTS = (7 << 2),
    IMMUCFGR_NAE = (7 << 5),
    IMMUCFGR_CRI = (1 << 8),
    IMMUCFGR_PRI = (1 << 9),
    IMMUCFGR_TEIRI = (1 << 10),
    IMMUCFGR_HTR = (1 << 11),
};

/* Float point control status register */
enum {
    FPCSR_FPEE = 1,
    FPCSR_RM = (3 << 1),
    FPCSR_OVF = (1 << 3),
    FPCSR_UNF = (1 << 4),
    FPCSR_SNF = (1 << 5),
    FPCSR_QNF = (1 << 6),
    FPCSR_ZF = (1 << 7),
    FPCSR_IXF = (1 << 8),
    FPCSR_IVF = (1 << 9),
    FPCSR_INF = (1 << 10),
    FPCSR_DZF = (1 << 11),
};

/* Exceptions indices */
enum {
    EXCP_RESET    = 0x1,
    EXCP_BUSERR   = 0x2,
    EXCP_DPF      = 0x3,
    EXCP_IPF      = 0x4,
    EXCP_TICK     = 0x5,
    EXCP_ALIGN    = 0x6,
    EXCP_ILLEGAL  = 0x7,
    EXCP_INT      = 0x8,
    EXCP_DTLBMISS = 0x9,
    EXCP_ITLBMISS = 0xa,
    EXCP_RANGE    = 0xb,
    EXCP_SYSCALL  = 0xc,
    EXCP_FPE      = 0xd,
    EXCP_TRAP     = 0xe,
    EXCP_NR,
};

/* Supervisor register */
enum {
    SR_SM = (1 << 0),
    SR_TEE = (1 << 1),
    SR_IEE = (1 << 2),
    SR_DCE = (1 << 3),
    SR_ICE = (1 << 4),
    SR_DME = (1 << 5),
    SR_IME = (1 << 6),
    SR_LEE = (1 << 7),
    SR_CE  = (1 << 8),
    SR_F   = (1 << 9),
    SR_CY  = (1 << 10),
    SR_OV  = (1 << 11),
    SR_OVE = (1 << 12),
    SR_DSX = (1 << 13),
    SR_EPH = (1 << 14),
    SR_FO  = (1 << 15),
    SR_SUMRA = (1 << 16),
    SR_SCE = (1 << 17),
};

/* OpenRISC Hardware Capabilities */
enum {
    OPENRISC_FEATURE_NSGF = (15 << 0),
    OPENRISC_FEATURE_CGF = (1 << 4),
    OPENRISC_FEATURE_OB32S = (1 << 5),
    OPENRISC_FEATURE_OB64S = (1 << 6),
    OPENRISC_FEATURE_OF32S = (1 << 7),
    OPENRISC_FEATURE_OF64S = (1 << 8),
    OPENRISC_FEATURE_OV64S = (1 << 9),
};

/* Tick Timer Mode Register */
enum {
    TTMR_TP = (0xfffffff),
    TTMR_IP = (1 << 28),
    TTMR_IE = (1 << 29),
    TTMR_M  = (3 << 30),
};

/* Timer Mode */
enum {
    TIMER_NONE = (0 << 30),
    TIMER_INTR = (1 << 30),
    TIMER_SHOT = (2 << 30),
    TIMER_CONT = (3 << 30),
};

/* TLB size */
enum {
    DTLB_WAYS = 1,
    DTLB_SIZE = 64,
    DTLB_MASK = (DTLB_SIZE-1),
    ITLB_WAYS = 1,
    ITLB_SIZE = 64,
    ITLB_MASK = (ITLB_SIZE-1),
};

/* TLB prot */
enum {
    URE = (1 << 6),
    UWE = (1 << 7),
    SRE = (1 << 8),
    SWE = (1 << 9),

    SXE = (1 << 6),
    UXE = (1 << 7),
};

/* check if tlb available */
enum {
    TLBRET_INVALID = -3,
    TLBRET_NOMATCH = -2,
    TLBRET_BADADDR = -1,
    TLBRET_MATCH = 0
};

typedef struct OpenRISCTLBEntry {
    uint32_t mr;
    uint32_t tr;
} OpenRISCTLBEntry;

#ifndef CONFIG_USER_ONLY
typedef struct CPUOpenRISCTLBContext {
    OpenRISCTLBEntry itlb[ITLB_WAYS][ITLB_SIZE];
    OpenRISCTLBEntry dtlb[DTLB_WAYS][DTLB_SIZE];

    int (*cpu_openrisc_map_address_code)(struct OpenRISCCPU *cpu,
                                         hwaddr *physical,
                                         int *prot,
                                         target_ulong address, int rw);
    int (*cpu_openrisc_map_address_data)(struct OpenRISCCPU *cpu,
                                         hwaddr *physical,
                                         int *prot,
                                         target_ulong address, int rw);
} CPUOpenRISCTLBContext;
#endif

typedef struct CPUOpenRISCState {
    target_ulong gpr[32];     /* General registers */
    target_ulong pc;          /* Program counter */
    target_ulong npc;         /* Next PC */
    target_ulong ppc;         /* Prev PC */
    target_ulong jmp_pc;      /* Jump PC */

    target_ulong machi;       /* Multiply register MACHI */
    target_ulong maclo;       /* Multiply register MACLO */

    target_ulong fpmaddhi;    /* Multiply and add float register FPMADDHI */
    target_ulong fpmaddlo;    /* Multiply and add float register FPMADDLO */

    target_ulong epcr;        /* Exception PC register */
    target_ulong eear;        /* Exception EA register */

    uint32_t sr;              /* Supervisor register */
    uint32_t vr;              /* Version register */
    uint32_t upr;             /* Unit presence register */
    uint32_t cpucfgr;         /* CPU configure register */
    uint32_t dmmucfgr;        /* DMMU configure register */
    uint32_t immucfgr;        /* IMMU configure register */
    uint32_t esr;             /* Exception supervisor register */
    uint32_t fpcsr;           /* Float register */
    float_status fp_status;

    uint32_t flags;           /* cpu_flags, we only use it for exception
                                 in solt so far.  */
    uint32_t btaken;          /* the SR_F bit */

    CPU_COMMON

    /* Fields from here on are preserved across CPU reset. */
#ifndef CONFIG_USER_ONLY
    CPUOpenRISCTLBContext * tlb;

    QEMUTimer *timer;
    uint32_t ttmr;          /* Timer tick mode register */
    uint32_t ttcr;          /* Timer tick count register */

    uint32_t picmr;         /* Interrupt mask register */
    uint32_t picsr;         /* Interrupt contrl register*/
#endif
    void *irq[32];          /* Interrupt irq input */
} CPUOpenRISCState;

/**
 * OpenRISCCPU:
 * @env: #CPUOpenRISCState
 *
 * A OpenRISC CPU.
 */
typedef struct OpenRISCCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUOpenRISCState env;

    uint32_t feature;       /* CPU Capabilities */
} OpenRISCCPU;

static inline OpenRISCCPU *openrisc_env_get_cpu(CPUOpenRISCState *env)
{
    return container_of(env, OpenRISCCPU, env);
}

#define ENV_GET_CPU(e) CPU(openrisc_env_get_cpu(e))

#define ENV_OFFSET offsetof(OpenRISCCPU, env)

OpenRISCCPU *cpu_openrisc_init(const char *cpu_model);

void cpu_openrisc_list(FILE *f, fprintf_function cpu_fprintf);
int cpu_openrisc_exec(CPUOpenRISCState *s);
void openrisc_cpu_do_interrupt(CPUState *cpu);
void openrisc_cpu_dump_state(CPUState *cpu, FILE *f,
                             fprintf_function cpu_fprintf, int flags);
hwaddr openrisc_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int openrisc_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int openrisc_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void openrisc_translate_init(void);
int openrisc_cpu_handle_mmu_fault(CPUState *cpu, vaddr address,
                                  int rw, int mmu_idx);
int cpu_openrisc_signal_handler(int host_signum, void *pinfo, void *puc);

#define cpu_list cpu_openrisc_list
#define cpu_exec cpu_openrisc_exec
#define cpu_gen_code cpu_openrisc_gen_code
#define cpu_signal_handler cpu_openrisc_signal_handler

#ifndef CONFIG_USER_ONLY
extern const struct VMStateDescription vmstate_openrisc_cpu;

/* hw/openrisc_pic.c */
void cpu_openrisc_pic_init(OpenRISCCPU *cpu);

/* hw/openrisc_timer.c */
void cpu_openrisc_clock_init(OpenRISCCPU *cpu);
void cpu_openrisc_count_update(OpenRISCCPU *cpu);
void cpu_openrisc_timer_update(OpenRISCCPU *cpu);
void cpu_openrisc_count_start(OpenRISCCPU *cpu);
void cpu_openrisc_count_stop(OpenRISCCPU *cpu);

void cpu_openrisc_mmu_init(OpenRISCCPU *cpu);
int cpu_openrisc_get_phys_nommu(OpenRISCCPU *cpu,
                                hwaddr *physical,
                                int *prot, target_ulong address, int rw);
int cpu_openrisc_get_phys_code(OpenRISCCPU *cpu,
                               hwaddr *physical,
                               int *prot, target_ulong address, int rw);
int cpu_openrisc_get_phys_data(OpenRISCCPU *cpu,
                               hwaddr *physical,
                               int *prot, target_ulong address, int rw);
#endif

static inline CPUOpenRISCState *cpu_init(const char *cpu_model)
{
    OpenRISCCPU *cpu = cpu_openrisc_init(cpu_model);
    if (cpu) {
        return &cpu->env;
    }
    return NULL;
}

#include "exec/cpu-all.h"

static inline void cpu_get_tb_cpu_state(CPUOpenRISCState *env,
                                        target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    /* D_FLAG -- branch instruction exception */
    *flags = (env->flags & D_FLAG);
}

static inline int cpu_mmu_index(CPUOpenRISCState *env)
{
    if (!(env->sr & SR_IME)) {
        return MMU_NOMMU_IDX;
    }
    return (env->sr & SR_SM) == 0 ? MMU_USER_IDX : MMU_SUPERVISOR_IDX;
}

#define CPU_INTERRUPT_TIMER   CPU_INTERRUPT_TGT_INT_0

#include "exec/exec-all.h"

static inline target_ulong cpu_get_pc(CPUOpenRISCState *env)
{
    return env->pc;
}

#endif /* CPU_OPENRISC_H */
