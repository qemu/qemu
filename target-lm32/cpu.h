/*
 *  LatticeMico32 virtual CPU header.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
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

#ifndef CPU_LM32_H
#define CPU_LM32_H

#define TARGET_LONG_BITS 32

#define CPUState struct CPULM32State

#include "qemu-common.h"
#include "cpu-defs.h"
struct CPULM32State;

#define TARGET_HAS_ICE 1

#define ELF_MACHINE EM_LATTICEMICO32

#define NB_MMU_MODES 1
#define TARGET_PAGE_BITS 12
static inline int cpu_mmu_index(CPUState *env)
{
    return 0;
}

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

/* Exceptions indices */
enum {
    EXCP_RESET = 0,
    EXCP_BREAKPOINT,
    EXCP_INSN_BUS_ERROR,
    EXCP_WATCHPOINT,
    EXCP_DATA_BUS_ERROR,
    EXCP_DIVIDE_BY_ZERO,
    EXCP_IRQ,
    EXCP_SYSTEMCALL
};

/* Registers */
enum {
    R_R0 = 0, R_R1, R_R2, R_R3, R_R4, R_R5, R_R6, R_R7, R_R8, R_R9, R_R10,
    R_R11, R_R12, R_R13, R_R14, R_R15, R_R16, R_R17, R_R18, R_R19, R_R20,
    R_R21, R_R22, R_R23, R_R24, R_R25, R_R26, R_R27, R_R28, R_R29, R_R30,
    R_R31
};

/* Register aliases */
enum {
    R_GP = R_R26,
    R_FP = R_R27,
    R_SP = R_R28,
    R_RA = R_R29,
    R_EA = R_R30,
    R_BA = R_R31
};

/* IE flags */
enum {
    IE_IE  = (1<<0),
    IE_EIE = (1<<1),
    IE_BIE = (1<<2),
};

/* DC flags */
enum {
    DC_SS  = (1<<0),
    DC_RE  = (1<<1),
    DC_C0  = (1<<2),
    DC_C1  = (1<<3),
    DC_C2  = (1<<4),
    DC_C3  = (1<<5),
};

/* CFG mask */
enum {
    CFG_M         = (1<<0),
    CFG_D         = (1<<1),
    CFG_S         = (1<<2),
    CFG_U         = (1<<3),
    CFG_X         = (1<<4),
    CFG_CC        = (1<<5),
    CFG_IC        = (1<<6),
    CFG_DC        = (1<<7),
    CFG_G         = (1<<8),
    CFG_H         = (1<<9),
    CFG_R         = (1<<10),
    CFG_J         = (1<<11),
    CFG_INT_SHIFT = 12,
    CFG_BP_SHIFT  = 18,
    CFG_WP_SHIFT  = 22,
    CFG_REV_SHIFT = 26,
};

/* CSRs */
enum {
    CSR_IE   = 0x00,
    CSR_IM   = 0x01,
    CSR_IP   = 0x02,
    CSR_ICC  = 0x03,
    CSR_DCC  = 0x04,
    CSR_CC   = 0x05,
    CSR_CFG  = 0x06,
    CSR_EBA  = 0x07,
    CSR_DC   = 0x08,
    CSR_DEBA = 0x09,
    CSR_JTX  = 0x0e,
    CSR_JRX  = 0x0f,
    CSR_BP0  = 0x10,
    CSR_BP1  = 0x11,
    CSR_BP2  = 0x12,
    CSR_BP3  = 0x13,
    CSR_WP0  = 0x18,
    CSR_WP1  = 0x19,
    CSR_WP2  = 0x1a,
    CSR_WP3  = 0x1b,
};

enum {
    LM32_FEATURE_MULTIPLY     =  1,
    LM32_FEATURE_DIVIDE       =  2,
    LM32_FEATURE_SHIFT        =  4,
    LM32_FEATURE_SIGN_EXTEND  =  8,
    LM32_FEATURE_I_CACHE      = 16,
    LM32_FEATURE_D_CACHE      = 32,
    LM32_FEATURE_CYCLE_COUNT  = 64,
};

enum {
    LM32_FLAG_IGNORE_MSB = 1,
};

typedef struct CPULM32State {
    /* general registers */
    uint32_t regs[32];

    /* special registers */
    uint32_t pc;        /* program counter */
    uint32_t ie;        /* interrupt enable */
    uint32_t icc;       /* instruction cache control */
    uint32_t dcc;       /* data cache control */
    uint32_t cc;        /* cycle counter */
    uint32_t cfg;       /* configuration */

    /* debug registers */
    uint32_t dc;        /* debug control */
    uint32_t bp[4];     /* breakpoint addresses */
    uint32_t wp[4];     /* watchpoint addresses */

    CPU_COMMON

    uint32_t eba;       /* exception base address */
    uint32_t deba;      /* debug exception base address */

    /* interrupt controller handle for callbacks */
    DeviceState *pic_state;
    /* JTAG UART handle for callbacks */
    DeviceState *juart_state;

    /* processor core features */
    uint32_t features;
    uint32_t flags;
    uint8_t num_bps;
    uint8_t num_wps;

} CPULM32State;


CPUState *cpu_lm32_init(const char *cpu_model);
void cpu_lm32_list(FILE *f, fprintf_function cpu_fprintf);
int cpu_lm32_exec(CPUState *s);
void cpu_lm32_close(CPUState *s);
void do_interrupt(CPUState *env);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_lm32_signal_handler(int host_signum, void *pinfo,
                          void *puc);
void lm32_translate_init(void);
void cpu_lm32_set_phys_msb_ignore(CPUState *env, int value);

#define cpu_list cpu_lm32_list
#define cpu_init cpu_lm32_init
#define cpu_exec cpu_lm32_exec
#define cpu_gen_code cpu_lm32_gen_code
#define cpu_signal_handler cpu_lm32_signal_handler

#define CPU_SAVE_VERSION 1

int cpu_lm32_handle_mmu_fault(CPUState *env, target_ulong address, int rw,
                            int mmu_idx, int is_softmmu);
#define cpu_handle_mmu_fault cpu_lm32_handle_mmu_fault

#if defined(CONFIG_USER_ONLY)
static inline void cpu_clone_regs(CPUState *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[R_SP] = newsp;
    }
    env->regs[R_R1] = 0;
}
#endif

static inline void cpu_set_tls(CPUState *env, target_ulong newtls)
{
}

static inline int cpu_interrupts_enabled(CPUState *env)
{
    return env->ie & IE_IE;
}

#include "cpu-all.h"

static inline target_ulong cpu_get_pc(CPUState *env)
{
    return env->pc;
}

static inline void cpu_get_tb_cpu_state(CPUState *env, target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    *pc = env->pc;
    *cs_base = 0;
    *flags = 0;
}

static inline bool cpu_has_work(CPUState *env)
{
    return env->interrupt_request & CPU_INTERRUPT_HARD;
}

#include "exec-all.h"

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
}

#endif
