/*
 * OpenRISC virtual CPU header.
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
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

#ifndef OPENRISC_CPU_H
#define OPENRISC_CPU_H

#include "exec/cpu-defs.h"
#include "hw/core/cpu.h"
#include "qom/object.h"

/* cpu_openrisc_map_address_* in CPUOpenRISCTLBContext need this decl.  */
struct OpenRISCCPU;

#define TYPE_OPENRISC_CPU "or1k-cpu"

OBJECT_DECLARE_TYPE(OpenRISCCPU, OpenRISCCPUClass,
                    OPENRISC_CPU)

/**
 * OpenRISCCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A OpenRISC CPU model.
 */
struct OpenRISCCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    DeviceReset parent_reset;
};

#define TARGET_INSN_START_EXTRA_WORDS 1

enum {
    MMU_NOMMU_IDX = 0,
    MMU_SUPERVISOR_IDX = 1,
    MMU_USER_IDX = 2,
};

#define SET_FP_CAUSE(reg, v)    do {\
                                    (reg) = ((reg) & ~(0x3f << 12)) | \
                                            ((v & 0x3f) << 12);\
                                } while (0)
#define GET_FP_ENABLE(reg)       (((reg) >>  7) & 0x1f)
#define UPDATE_FP_FLAGS(reg, v)   do {\
                                      (reg) |= ((v & 0x1f) << 2);\
                                  } while (0)

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
    CPUCFGR_ND = (1 << 10),
    CPUCFGR_AVRP = (1 << 11),
    CPUCFGR_EVBARP = (1 << 12),
    CPUCFGR_ISRP = (1 << 13),
    CPUCFGR_AECSRP = (1 << 14),
    CPUCFGR_OF64A32S = (1 << 15),
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

/* Power management register */
enum {
    PMR_SDF = (15 << 0),
    PMR_DME = (1 << 4),
    PMR_SME = (1 << 5),
    PMR_DCGE = (1 << 6),
    PMR_SUME = (1 << 7),
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
    TLB_SIZE = 128,
    TLB_MASK = TLB_SIZE - 1,
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

typedef struct OpenRISCTLBEntry {
    uint32_t mr;
    uint32_t tr;
} OpenRISCTLBEntry;

#ifndef CONFIG_USER_ONLY
typedef struct CPUOpenRISCTLBContext {
    OpenRISCTLBEntry itlb[TLB_SIZE];
    OpenRISCTLBEntry dtlb[TLB_SIZE];

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
    target_ulong shadow_gpr[16][32]; /* Shadow registers */

    target_ulong pc;          /* Program counter */
    target_ulong ppc;         /* Prev PC */
    target_ulong jmp_pc;      /* Jump PC */

    uint64_t mac;             /* Multiply registers MACHI:MACLO */

    target_ulong epcr;        /* Exception PC register */
    target_ulong eear;        /* Exception EA register */

    target_ulong sr_f;        /* the SR_F bit, values 0, 1.  */
    target_ulong sr_cy;       /* the SR_CY bit, values 0, 1.  */
    target_long  sr_ov;       /* the SR_OV bit (in the sign bit only) */
    uint32_t sr;              /* Supervisor register, without SR_{F,CY,OV} */
    uint32_t esr;             /* Exception supervisor register */
    uint32_t evbar;           /* Exception vector base address register */
    uint32_t pmr;             /* Power Management Register */
    uint32_t fpcsr;           /* Float register */
    float_status fp_status;

    target_ulong lock_addr;
    target_ulong lock_value;

    uint32_t dflag;           /* In delay slot (boolean) */

#ifndef CONFIG_USER_ONLY
    CPUOpenRISCTLBContext tlb;
#endif

    /* Fields up to this point are cleared by a CPU reset */
    struct {} end_reset_fields;

    /* Fields from here on are preserved across CPU reset. */
    uint32_t vr;              /* Version register */
    uint32_t vr2;             /* Version register 2 */
    uint32_t avr;             /* Architecture version register */
    uint32_t upr;             /* Unit presence register */
    uint32_t cpucfgr;         /* CPU configure register */
    uint32_t dmmucfgr;        /* DMMU configure register */
    uint32_t immucfgr;        /* IMMU configure register */

#ifndef CONFIG_USER_ONLY
    QEMUTimer *timer;
    uint32_t ttmr;          /* Timer tick mode register */
    int is_counting;

    uint32_t picmr;         /* Interrupt mask register */
    uint32_t picsr;         /* Interrupt contrl register*/
#endif
} CPUOpenRISCState;

/**
 * OpenRISCCPU:
 * @env: #CPUOpenRISCState
 *
 * A OpenRISC CPU.
 */
struct OpenRISCCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPUOpenRISCState env;
};


void cpu_openrisc_list(void);
void openrisc_cpu_dump_state(CPUState *cpu, FILE *f, int flags);
hwaddr openrisc_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int openrisc_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int openrisc_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void openrisc_translate_init(void);
int print_insn_or1k(bfd_vma addr, disassemble_info *info);

#define cpu_list cpu_openrisc_list

#ifndef CONFIG_USER_ONLY
bool openrisc_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                           MMUAccessType access_type, int mmu_idx,
                           bool probe, uintptr_t retaddr);

extern const VMStateDescription vmstate_openrisc_cpu;

void openrisc_cpu_do_interrupt(CPUState *cpu);
bool openrisc_cpu_exec_interrupt(CPUState *cpu, int int_req);

/* hw/openrisc_pic.c */
void cpu_openrisc_pic_init(OpenRISCCPU *cpu);

/* hw/openrisc_timer.c */
void cpu_openrisc_clock_init(OpenRISCCPU *cpu);
uint32_t cpu_openrisc_count_get(OpenRISCCPU *cpu);
void cpu_openrisc_count_set(OpenRISCCPU *cpu, uint32_t val);
void cpu_openrisc_count_update(OpenRISCCPU *cpu);
void cpu_openrisc_timer_update(OpenRISCCPU *cpu);
void cpu_openrisc_count_start(OpenRISCCPU *cpu);
void cpu_openrisc_count_stop(OpenRISCCPU *cpu);
#endif

#define OPENRISC_CPU_TYPE_SUFFIX "-" TYPE_OPENRISC_CPU
#define OPENRISC_CPU_TYPE_NAME(model) model OPENRISC_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_OPENRISC_CPU

typedef CPUOpenRISCState CPUArchState;
typedef OpenRISCCPU ArchCPU;

#include "exec/cpu-all.h"

#define TB_FLAGS_SM    SR_SM
#define TB_FLAGS_DME   SR_DME
#define TB_FLAGS_IME   SR_IME
#define TB_FLAGS_OVE   SR_OVE
#define TB_FLAGS_DFLAG 2      /* reuse SR_TEE */
#define TB_FLAGS_R0_0  4      /* reuse SR_IEE */

static inline uint32_t cpu_get_gpr(const CPUOpenRISCState *env, int i)
{
    return env->shadow_gpr[0][i];
}

static inline void cpu_set_gpr(CPUOpenRISCState *env, int i, uint32_t val)
{
    env->shadow_gpr[0][i] = val;
}

static inline void cpu_get_tb_cpu_state(CPUOpenRISCState *env,
                                        target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = (env->dflag ? TB_FLAGS_DFLAG : 0)
           | (cpu_get_gpr(env, 0) ? 0 : TB_FLAGS_R0_0)
           | (env->sr & (SR_SM | SR_DME | SR_IME | SR_OVE));
}

static inline int cpu_mmu_index(CPUOpenRISCState *env, bool ifetch)
{
    int ret = MMU_NOMMU_IDX;  /* mmu is disabled */

    if (env->sr & (ifetch ? SR_IME : SR_DME)) {
        /* The mmu is enabled; test supervisor state.  */
        ret = env->sr & SR_SM ? MMU_SUPERVISOR_IDX : MMU_USER_IDX;
    }

    return ret;
}

static inline uint32_t cpu_get_sr(const CPUOpenRISCState *env)
{
    return (env->sr
            + env->sr_f * SR_F
            + env->sr_cy * SR_CY
            + (env->sr_ov < 0) * SR_OV);
}

static inline void cpu_set_sr(CPUOpenRISCState *env, uint32_t val)
{
    env->sr_f = (val & SR_F) != 0;
    env->sr_cy = (val & SR_CY) != 0;
    env->sr_ov = (val & SR_OV ? -1 : 0);
    env->sr = (val & ~(SR_F | SR_CY | SR_OV)) | SR_FO;
}

void cpu_set_fpcsr(CPUOpenRISCState *env, uint32_t val);

#define CPU_INTERRUPT_TIMER   CPU_INTERRUPT_TGT_INT_0

#endif /* OPENRISC_CPU_H */
