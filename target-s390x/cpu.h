/*
 * S/390 virtual CPU header
 *
 *  Copyright (c) 2009 Ulrich Hecht
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#ifndef CPU_S390X_H
#define CPU_S390X_H

#define TARGET_LONG_BITS 64

#define ELF_MACHINE	EM_S390

#define CPUState struct CPUS390XState

#include "cpu-defs.h"

#include "softfloat.h"

#define NB_MMU_MODES 2 // guess
#define MMU_USER_IDX 0 // guess

typedef union FPReg {
    struct {
#ifdef WORDS_BIGENDIAN
        float32 e;
        int32_t __pad;
#else
        int32_t __pad;
        float32 e;
#endif
    };
    float64 d;
    uint64_t i;
} FPReg;

typedef struct CPUS390XState {
    uint64_t regs[16];	/* GP registers */

    uint32_t aregs[16];	/* access registers */

    uint32_t fpc;	/* floating-point control register */
    FPReg fregs[16]; /* FP registers */
    float_status fpu_status; /* passed to softfloat lib */

    struct {
        uint64_t mask;
        uint64_t addr;
    } psw;

    int cc; /* condition code (0-3) */

    uint64_t __excp_addr;

    CPU_COMMON
} CPUS390XState;

#if defined(CONFIG_USER_ONLY)
static inline void cpu_clone_regs(CPUState *env, target_ulong newsp)
{
    if (newsp)
        env->regs[15] = newsp;
    env->regs[0] = 0;
}
#endif

CPUS390XState *cpu_s390x_init(const char *cpu_model);
int cpu_s390x_exec(CPUS390XState *s);
void cpu_s390x_close(CPUS390XState *s);

/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_s390x_signal_handler(int host_signum, void *pinfo,
                           void *puc);
int cpu_s390x_handle_mmu_fault (CPUS390XState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmuu);
#define cpu_handle_mmu_fault cpu_s390x_handle_mmu_fault

#define TARGET_PAGE_BITS 12

#define cpu_init cpu_s390x_init
#define cpu_exec cpu_s390x_exec
#define cpu_gen_code cpu_s390x_gen_code

#include "cpu-all.h"
#include "exec-all.h"

#define EXCP_OPEX 1 /* operation exception (sigill) */
#define EXCP_SVC 2 /* supervisor call (syscall) */
#define EXCP_ADDR 5 /* addressing exception */
#define EXCP_EXECUTE_SVC 0xff00000 /* supervisor call via execute insn */

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock* tb)
{
    env->psw.addr = tb->pc;
}

static inline void cpu_get_tb_cpu_state(CPUState* env, target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    *pc = env->psw.addr;
    /* XXX this is correct for user-mode emulation, but needs
     *     the asce register information as well when softmmu
     *     is implemented in the future */
    *cs_base = 0;
    *flags = env->psw.mask;
}
#endif
