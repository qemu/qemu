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

#define TARGET_LONG_BITS 32
#define ELF_MACHINE EM_XTENSA

#define CPUState struct CPUXtensaState

#include "config.h"
#include "qemu-common.h"
#include "cpu-defs.h"

#define TARGET_HAS_ICE 1

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
    XTENSA_OPTION_32_BIT_IDIV,
    XTENSA_OPTION_MAC16,
    XTENSA_OPTION_MISC_OP,
    XTENSA_OPTION_COPROCESSOR,
    XTENSA_OPTION_BOOLEAN,
    XTENSA_OPTION_FP_COPROCESSOR,
    XTENSA_OPTION_MP_SYNCHRO,
    XTENSA_OPTION_CONDITIONAL_STORE,

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
    SAR = 3,
    SCOMPARE1 = 12,
    WINDOW_BASE = 72,
    WINDOW_START = 73,
    EPC1 = 177,
    DEPC = 192,
    EXCSAVE1 = 209,
    PS = 230,
    EXCCAUSE = 232,
    EXCVADDR = 238,
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

#define MAX_NAREG 64

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

typedef struct XtensaConfig {
    const char *name;
    uint64_t options;
    unsigned nareg;
    int excm_level;
    int ndepc;
    uint32_t exception_vector[EXC_MAX];
} XtensaConfig;

typedef struct CPUXtensaState {
    const XtensaConfig *config;
    uint32_t regs[16];
    uint32_t pc;
    uint32_t sregs[256];
    uint32_t uregs[256];
    uint32_t phys_regs[MAX_NAREG];

    int exception_taken;

    CPU_COMMON
} CPUXtensaState;

#define cpu_init cpu_xtensa_init
#define cpu_exec cpu_xtensa_exec
#define cpu_gen_code cpu_xtensa_gen_code
#define cpu_signal_handler cpu_xtensa_signal_handler
#define cpu_list xtensa_cpu_list

CPUXtensaState *cpu_xtensa_init(const char *cpu_model);
void xtensa_translate_init(void);
int cpu_xtensa_exec(CPUXtensaState *s);
void do_interrupt(CPUXtensaState *s);
int cpu_xtensa_signal_handler(int host_signum, void *pinfo, void *puc);
void xtensa_cpu_list(FILE *f, fprintf_function cpu_fprintf);
void xtensa_sync_window_from_phys(CPUState *env);
void xtensa_sync_phys_from_window(CPUState *env);

#define XTENSA_OPTION_BIT(opt) (((uint64_t)1) << (opt))

static inline bool xtensa_option_enabled(const XtensaConfig *config, int opt)
{
    return (config->options & XTENSA_OPTION_BIT(opt)) != 0;
}

static inline int xtensa_get_cintlevel(const CPUState *env)
{
    int level = (env->sregs[PS] & PS_INTLEVEL) >> PS_INTLEVEL_SHIFT;
    if ((env->sregs[PS] & PS_EXCM) && env->config->excm_level > level) {
        level = env->config->excm_level;
    }
    return level;
}

static inline int xtensa_get_ring(const CPUState *env)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        return (env->sregs[PS] & PS_RING) >> PS_RING_SHIFT;
    } else {
        return 0;
    }
}

static inline int xtensa_get_cring(const CPUState *env)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU) &&
            (env->sregs[PS] & PS_EXCM) == 0) {
        return (env->sregs[PS] & PS_RING) >> PS_RING_SHIFT;
    } else {
        return 0;
    }
}

/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _ring0
#define MMU_MODE1_SUFFIX _ring1
#define MMU_MODE2_SUFFIX _ring2
#define MMU_MODE3_SUFFIX _ring3

static inline int cpu_mmu_index(CPUState *env)
{
    return xtensa_get_cring(env);
}

#define XTENSA_TBFLAG_RING_MASK 0x3
#define XTENSA_TBFLAG_EXCM 0x4

static inline void cpu_get_tb_cpu_state(CPUState *env, target_ulong *pc,
        target_ulong *cs_base, int *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = 0;
    *flags |= xtensa_get_ring(env);
    if (env->sregs[PS] & PS_EXCM) {
        *flags |= XTENSA_TBFLAG_EXCM;
    }
}

#include "cpu-all.h"
#include "exec-all.h"

static inline int cpu_has_work(CPUState *env)
{
    return 1;
}

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
}

#endif
