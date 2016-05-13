/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CPU_XTENSA_H
#define CPU_XTENSA_H

#define ALIGNED_ONLY
#define TARGET_LONG_BITS 32

#define CPUArchState struct CPUXtensaState

#include "qemu-common.h"
#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"

#define NB_MMU_MODES 4

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#define TARGET_PAGE_BITS 12

enum {
    /* Additional instructions */
    XTENSA_OPTION_CODE_DENSITY,
    XTENSA_OPTION_LOOP,
    XTENSA_OPTION_EXTENDED_L32R,
    XTENSA_OPTION_16_BIT_IMUL,
    XTENSA_OPTION_32_BIT_IMUL,
    XTENSA_OPTION_32_BIT_IMUL_HIGH,
    XTENSA_OPTION_32_BIT_IDIV,
    XTENSA_OPTION_MAC16,
    XTENSA_OPTION_MISC_OP_NSA,
    XTENSA_OPTION_MISC_OP_MINMAX,
    XTENSA_OPTION_MISC_OP_SEXT,
    XTENSA_OPTION_MISC_OP_CLAMPS,
    XTENSA_OPTION_COPROCESSOR,
    XTENSA_OPTION_BOOLEAN,
    XTENSA_OPTION_FP_COPROCESSOR,
    XTENSA_OPTION_MP_SYNCHRO,
    XTENSA_OPTION_CONDITIONAL_STORE,
    XTENSA_OPTION_ATOMCTL,
    XTENSA_OPTION_DEPBITS,

    /* Interrupts and exceptions */
    XTENSA_OPTION_EXCEPTION,
    XTENSA_OPTION_RELOCATABLE_VECTOR,
    XTENSA_OPTION_UNALIGNED_EXCEPTION,
    XTENSA_OPTION_INTERRUPT,
    XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
    XTENSA_OPTION_TIMER_INTERRUPT,

    /* Local memory */
    XTENSA_OPTION_ICACHE,
    XTENSA_OPTION_ICACHE_TEST,
    XTENSA_OPTION_ICACHE_INDEX_LOCK,
    XTENSA_OPTION_DCACHE,
    XTENSA_OPTION_DCACHE_TEST,
    XTENSA_OPTION_DCACHE_INDEX_LOCK,
    XTENSA_OPTION_IRAM,
    XTENSA_OPTION_IROM,
    XTENSA_OPTION_DRAM,
    XTENSA_OPTION_DROM,
    XTENSA_OPTION_XLMI,
    XTENSA_OPTION_HW_ALIGNMENT,
    XTENSA_OPTION_MEMORY_ECC_PARITY,

    /* Memory protection and translation */
    XTENSA_OPTION_REGION_PROTECTION,
    XTENSA_OPTION_REGION_TRANSLATION,
    XTENSA_OPTION_MMU,
    XTENSA_OPTION_CACHEATTR,

    /* Other */
    XTENSA_OPTION_WINDOWED_REGISTER,
    XTENSA_OPTION_PROCESSOR_INTERFACE,
    XTENSA_OPTION_MISC_SR,
    XTENSA_OPTION_THREAD_POINTER,
    XTENSA_OPTION_PROCESSOR_ID,
    XTENSA_OPTION_DEBUG,
    XTENSA_OPTION_TRACE_PORT,
};

enum {
    THREADPTR = 231,
    FCR = 232,
    FSR = 233,
};

enum {
    LBEG = 0,
    LEND = 1,
    LCOUNT = 2,
    SAR = 3,
    BR = 4,
    LITBASE = 5,
    SCOMPARE1 = 12,
    ACCLO = 16,
    ACCHI = 17,
    MR = 32,
    WINDOW_BASE = 72,
    WINDOW_START = 73,
    PTEVADDR = 83,
    RASID = 90,
    ITLBCFG = 91,
    DTLBCFG = 92,
    IBREAKENABLE = 96,
    CACHEATTR = 98,
    ATOMCTL = 99,
    IBREAKA = 128,
    DBREAKA = 144,
    DBREAKC = 160,
    CONFIGID0 = 176,
    EPC1 = 177,
    DEPC = 192,
    EPS2 = 194,
    CONFIGID1 = 208,
    EXCSAVE1 = 209,
    CPENABLE = 224,
    INTSET = 226,
    INTCLEAR = 227,
    INTENABLE = 228,
    PS = 230,
    VECBASE = 231,
    EXCCAUSE = 232,
    DEBUGCAUSE = 233,
    CCOUNT = 234,
    PRID = 235,
    ICOUNT = 236,
    ICOUNTLEVEL = 237,
    EXCVADDR = 238,
    CCOMPARE = 240,
    MISC = 244,
};

#define PS_INTLEVEL 0xf
#define PS_INTLEVEL_SHIFT 0

#define PS_EXCM 0x10
#define PS_UM 0x20

#define PS_RING 0xc0
#define PS_RING_SHIFT 6

#define PS_OWB 0xf00
#define PS_OWB_SHIFT 8

#define PS_CALLINC 0x30000
#define PS_CALLINC_SHIFT 16
#define PS_CALLINC_LEN 2

#define PS_WOE 0x40000

#define DEBUGCAUSE_IC 0x1
#define DEBUGCAUSE_IB 0x2
#define DEBUGCAUSE_DB 0x4
#define DEBUGCAUSE_BI 0x8
#define DEBUGCAUSE_BN 0x10
#define DEBUGCAUSE_DI 0x20
#define DEBUGCAUSE_DBNUM 0xf00
#define DEBUGCAUSE_DBNUM_SHIFT 8

#define DBREAKC_SB 0x80000000
#define DBREAKC_LB 0x40000000
#define DBREAKC_SB_LB (DBREAKC_SB | DBREAKC_LB)
#define DBREAKC_MASK 0x3f

#define MAX_NAREG 64
#define MAX_NINTERRUPT 32
#define MAX_NLEVEL 6
#define MAX_NNMI 1
#define MAX_NCCOMPARE 3
#define MAX_TLB_WAY_SIZE 8
#define MAX_NDBREAK 2

#define REGION_PAGE_MASK 0xe0000000

#define PAGE_CACHE_MASK    0x700
#define PAGE_CACHE_SHIFT   8
#define PAGE_CACHE_INVALID 0x000
#define PAGE_CACHE_BYPASS  0x100
#define PAGE_CACHE_WT      0x200
#define PAGE_CACHE_WB      0x400
#define PAGE_CACHE_ISOLATE 0x600

enum {
    /* Static vectors */
    EXC_RESET,
    EXC_MEMORY_ERROR,

    /* Dynamic vectors */
    EXC_WINDOW_OVERFLOW4,
    EXC_WINDOW_UNDERFLOW4,
    EXC_WINDOW_OVERFLOW8,
    EXC_WINDOW_UNDERFLOW8,
    EXC_WINDOW_OVERFLOW12,
    EXC_WINDOW_UNDERFLOW12,
    EXC_IRQ,
    EXC_KERNEL,
    EXC_USER,
    EXC_DOUBLE,
    EXC_DEBUG,
    EXC_MAX
};

enum {
    ILLEGAL_INSTRUCTION_CAUSE = 0,
    SYSCALL_CAUSE,
    INSTRUCTION_FETCH_ERROR_CAUSE,
    LOAD_STORE_ERROR_CAUSE,
    LEVEL1_INTERRUPT_CAUSE,
    ALLOCA_CAUSE,
    INTEGER_DIVIDE_BY_ZERO_CAUSE,
    PRIVILEGED_CAUSE = 8,
    LOAD_STORE_ALIGNMENT_CAUSE,

    INSTR_PIF_DATA_ERROR_CAUSE = 12,
    LOAD_STORE_PIF_DATA_ERROR_CAUSE,
    INSTR_PIF_ADDR_ERROR_CAUSE,
    LOAD_STORE_PIF_ADDR_ERROR_CAUSE,

    INST_TLB_MISS_CAUSE,
    INST_TLB_MULTI_HIT_CAUSE,
    INST_FETCH_PRIVILEGE_CAUSE,
    INST_FETCH_PROHIBITED_CAUSE = 20,
    LOAD_STORE_TLB_MISS_CAUSE = 24,
    LOAD_STORE_TLB_MULTI_HIT_CAUSE,
    LOAD_STORE_PRIVILEGE_CAUSE,
    LOAD_PROHIBITED_CAUSE = 28,
    STORE_PROHIBITED_CAUSE,

    COPROCESSOR0_DISABLED = 32,
};

typedef enum {
    INTTYPE_LEVEL,
    INTTYPE_EDGE,
    INTTYPE_NMI,
    INTTYPE_SOFTWARE,
    INTTYPE_TIMER,
    INTTYPE_DEBUG,
    INTTYPE_WRITE_ERR,
    INTTYPE_PROFILING,
    INTTYPE_MAX
} interrupt_type;

typedef struct xtensa_tlb_entry {
    uint32_t vaddr;
    uint32_t paddr;
    uint8_t asid;
    uint8_t attr;
    bool variable;
} xtensa_tlb_entry;

typedef struct xtensa_tlb {
    unsigned nways;
    const unsigned way_size[10];
    bool varway56;
    unsigned nrefillentries;
} xtensa_tlb;

typedef struct XtensaGdbReg {
    int targno;
    int type;
    int group;
    unsigned size;
} XtensaGdbReg;

typedef struct XtensaGdbRegmap {
    int num_regs;
    int num_core_regs;
    /* PC + a + ar + sr + ur */
    XtensaGdbReg reg[1 + 16 + 64 + 256 + 256];
} XtensaGdbRegmap;

typedef struct XtensaConfig {
    const char *name;
    uint64_t options;
    XtensaGdbRegmap gdb_regmap;
    unsigned nareg;
    int excm_level;
    int ndepc;
    uint32_t vecbase;
    uint32_t exception_vector[EXC_MAX];
    unsigned ninterrupt;
    unsigned nlevel;
    uint32_t interrupt_vector[MAX_NLEVEL + MAX_NNMI + 1];
    uint32_t level_mask[MAX_NLEVEL + MAX_NNMI + 1];
    uint32_t inttype_mask[INTTYPE_MAX];
    struct {
        uint32_t level;
        interrupt_type inttype;
    } interrupt[MAX_NINTERRUPT];
    unsigned nccompare;
    uint32_t timerint[MAX_NCCOMPARE];
    unsigned nextint;
    unsigned extint[MAX_NINTERRUPT];

    unsigned debug_level;
    unsigned nibreak;
    unsigned ndbreak;

    uint32_t configid[2];

    uint32_t clock_freq_khz;

    xtensa_tlb itlb;
    xtensa_tlb dtlb;
} XtensaConfig;

typedef struct XtensaConfigList {
    const XtensaConfig *config;
    struct XtensaConfigList *next;
} XtensaConfigList;

#ifdef HOST_WORDS_BIGENDIAN
enum {
    FP_F32_HIGH,
    FP_F32_LOW,
};
#else
enum {
    FP_F32_LOW,
    FP_F32_HIGH,
};
#endif

typedef struct CPUXtensaState {
    const XtensaConfig *config;
    uint32_t regs[16];
    uint32_t pc;
    uint32_t sregs[256];
    uint32_t uregs[256];
    uint32_t phys_regs[MAX_NAREG];
    union {
        float32 f32[2];
        float64 f64;
    } fregs[16];
    float_status fp_status;

    xtensa_tlb_entry itlb[7][MAX_TLB_WAY_SIZE];
    xtensa_tlb_entry dtlb[10][MAX_TLB_WAY_SIZE];
    unsigned autorefill_idx;

    int pending_irq_level; /* level of last raised IRQ */
    void **irq_inputs;
    QEMUTimer *ccompare_timer;
    uint32_t wake_ccount;
    int64_t halt_clock;

    int exception_taken;

    /* Watchpoints for DBREAK registers */
    struct CPUWatchpoint *cpu_watchpoint[MAX_NDBREAK];

    CPU_COMMON
} CPUXtensaState;

#include "cpu-qom.h"

#define cpu_exec cpu_xtensa_exec
#define cpu_signal_handler cpu_xtensa_signal_handler
#define cpu_list xtensa_cpu_list

#ifdef TARGET_WORDS_BIGENDIAN
#define XTENSA_DEFAULT_CPU_MODEL "fsf"
#else
#define XTENSA_DEFAULT_CPU_MODEL "dc232b"
#endif

XtensaCPU *cpu_xtensa_init(const char *cpu_model);

#define cpu_init(cpu_model) CPU(cpu_xtensa_init(cpu_model))

void xtensa_translate_init(void);
void xtensa_breakpoint_handler(CPUState *cs);
int cpu_xtensa_exec(CPUState *cpu);
void xtensa_finalize_config(XtensaConfig *config);
void xtensa_register_core(XtensaConfigList *node);
void check_interrupts(CPUXtensaState *s);
void xtensa_irq_init(CPUXtensaState *env);
void *xtensa_get_extint(CPUXtensaState *env, unsigned extint);
void xtensa_advance_ccount(CPUXtensaState *env, uint32_t d);
void xtensa_timer_irq(CPUXtensaState *env, uint32_t id, uint32_t active);
void xtensa_rearm_ccompare_timer(CPUXtensaState *env);
int cpu_xtensa_signal_handler(int host_signum, void *pinfo, void *puc);
void xtensa_cpu_list(FILE *f, fprintf_function cpu_fprintf);
void xtensa_sync_window_from_phys(CPUXtensaState *env);
void xtensa_sync_phys_from_window(CPUXtensaState *env);
uint32_t xtensa_tlb_get_addr_mask(const CPUXtensaState *env, bool dtlb, uint32_t way);
void split_tlb_entry_spec_way(const CPUXtensaState *env, uint32_t v, bool dtlb,
        uint32_t *vpn, uint32_t wi, uint32_t *ei);
int xtensa_tlb_lookup(const CPUXtensaState *env, uint32_t addr, bool dtlb,
        uint32_t *pwi, uint32_t *pei, uint8_t *pring);
void xtensa_tlb_set_entry_mmu(const CPUXtensaState *env,
        xtensa_tlb_entry *entry, bool dtlb,
        unsigned wi, unsigned ei, uint32_t vpn, uint32_t pte);
void xtensa_tlb_set_entry(CPUXtensaState *env, bool dtlb,
        unsigned wi, unsigned ei, uint32_t vpn, uint32_t pte);
int xtensa_get_physical_addr(CPUXtensaState *env, bool update_tlb,
        uint32_t vaddr, int is_write, int mmu_idx,
        uint32_t *paddr, uint32_t *page_size, unsigned *access);
void reset_mmu(CPUXtensaState *env);
void dump_mmu(FILE *f, fprintf_function cpu_fprintf, CPUXtensaState *env);
void debug_exception_env(CPUXtensaState *new_env, uint32_t cause);


#define XTENSA_OPTION_BIT(opt) (((uint64_t)1) << (opt))
#define XTENSA_OPTION_ALL (~(uint64_t)0)

static inline bool xtensa_option_bits_enabled(const XtensaConfig *config,
        uint64_t opt)
{
    return (config->options & opt) != 0;
}

static inline bool xtensa_option_enabled(const XtensaConfig *config, int opt)
{
    return xtensa_option_bits_enabled(config, XTENSA_OPTION_BIT(opt));
}

static inline int xtensa_get_cintlevel(const CPUXtensaState *env)
{
    int level = (env->sregs[PS] & PS_INTLEVEL) >> PS_INTLEVEL_SHIFT;
    if ((env->sregs[PS] & PS_EXCM) && env->config->excm_level > level) {
        level = env->config->excm_level;
    }
    return level;
}

static inline int xtensa_get_ring(const CPUXtensaState *env)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        return (env->sregs[PS] & PS_RING) >> PS_RING_SHIFT;
    } else {
        return 0;
    }
}

static inline int xtensa_get_cring(const CPUXtensaState *env)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU) &&
            (env->sregs[PS] & PS_EXCM) == 0) {
        return (env->sregs[PS] & PS_RING) >> PS_RING_SHIFT;
    } else {
        return 0;
    }
}

static inline xtensa_tlb_entry *xtensa_tlb_get_entry(CPUXtensaState *env,
        bool dtlb, unsigned wi, unsigned ei)
{
    return dtlb ?
        env->dtlb[wi] + ei :
        env->itlb[wi] + ei;
}

static inline uint32_t xtensa_replicate_windowstart(CPUXtensaState *env)
{
    return env->sregs[WINDOW_START] |
        (env->sregs[WINDOW_START] << env->config->nareg / 4);
}

/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _ring0
#define MMU_MODE1_SUFFIX _ring1
#define MMU_MODE2_SUFFIX _ring2
#define MMU_MODE3_SUFFIX _ring3

static inline int cpu_mmu_index(CPUXtensaState *env, bool ifetch)
{
    return xtensa_get_cring(env);
}

#define XTENSA_TBFLAG_RING_MASK 0x3
#define XTENSA_TBFLAG_EXCM 0x4
#define XTENSA_TBFLAG_LITBASE 0x8
#define XTENSA_TBFLAG_DEBUG 0x10
#define XTENSA_TBFLAG_ICOUNT 0x20
#define XTENSA_TBFLAG_CPENABLE_MASK 0x3fc0
#define XTENSA_TBFLAG_CPENABLE_SHIFT 6
#define XTENSA_TBFLAG_EXCEPTION 0x4000
#define XTENSA_TBFLAG_WINDOW_MASK 0x18000
#define XTENSA_TBFLAG_WINDOW_SHIFT 15

static inline void cpu_get_tb_cpu_state(CPUXtensaState *env, target_ulong *pc,
        target_ulong *cs_base, uint32_t *flags)
{
    CPUState *cs = CPU(xtensa_env_get_cpu(env));

    *pc = env->pc;
    *cs_base = 0;
    *flags = 0;
    *flags |= xtensa_get_ring(env);
    if (env->sregs[PS] & PS_EXCM) {
        *flags |= XTENSA_TBFLAG_EXCM;
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_EXTENDED_L32R) &&
            (env->sregs[LITBASE] & 1)) {
        *flags |= XTENSA_TBFLAG_LITBASE;
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_DEBUG)) {
        if (xtensa_get_cintlevel(env) < env->config->debug_level) {
            *flags |= XTENSA_TBFLAG_DEBUG;
        }
        if (xtensa_get_cintlevel(env) < env->sregs[ICOUNTLEVEL]) {
            *flags |= XTENSA_TBFLAG_ICOUNT;
        }
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_COPROCESSOR)) {
        *flags |= env->sregs[CPENABLE] << XTENSA_TBFLAG_CPENABLE_SHIFT;
    }
    if (cs->singlestep_enabled && env->exception_taken) {
        *flags |= XTENSA_TBFLAG_EXCEPTION;
    }
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_WINDOWED_REGISTER) &&
        (env->sregs[PS] & (PS_WOE | PS_EXCM)) == PS_WOE) {
        uint32_t windowstart = xtensa_replicate_windowstart(env) >>
            (env->sregs[WINDOW_BASE] + 1);
        uint32_t w = ctz32(windowstart | 0x8);

        *flags |= w << XTENSA_TBFLAG_WINDOW_SHIFT;
    } else {
        *flags |= 3 << XTENSA_TBFLAG_WINDOW_SHIFT;
    }
}

#include "exec/cpu-all.h"
#include "exec/exec-all.h"

#endif
