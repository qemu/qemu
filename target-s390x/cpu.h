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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CPU_S390X_H
#define CPU_S390X_H

#define TARGET_LONG_BITS 64

#define ELF_MACHINE	EM_S390

#define CPUState struct CPUS390XState

#include "cpu-defs.h"

#include "softfloat.h"

#define NB_MMU_MODES 2

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

#define MMU_MODE0_SUFFIX _kernel
#define MMU_MODE1_SUFFIX _user
#define MMU_USER_IDX 1
static inline int cpu_mmu_index (CPUState *env)
{
    /* XXX: Currently we don't implement virtual memory */
    return 0;
}

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

/* ??? This is certainly wrong for 64-bit s390x, but given that only KVM
   emulation actually works, this is good enough for a placeholder.  */
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#ifndef CONFIG_USER_ONLY
int s390_virtio_hypercall(CPUState *env);
void kvm_s390_virtio_irq(CPUState *env, int config_change, uint64_t token);
CPUState *s390_cpu_addr2state(uint16_t cpu_addr);
#endif


#define cpu_init cpu_s390x_init
#define cpu_exec cpu_s390x_exec
#define cpu_gen_code cpu_s390x_gen_code

#include "cpu-all.h"

#define EXCP_OPEX 1 /* operation exception (sigill) */
#define EXCP_SVC 2 /* supervisor call (syscall) */
#define EXCP_ADDR 5 /* addressing exception */
#define EXCP_EXECUTE_SVC 0xff00000 /* supervisor call via execute insn */

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

/* Program Status Word.  */
#define S390_PSWM_REGNUM 0
#define S390_PSWA_REGNUM 1
/* General Purpose Registers.  */
#define S390_R0_REGNUM 2
#define S390_R1_REGNUM 3
#define S390_R2_REGNUM 4
#define S390_R3_REGNUM 5
#define S390_R4_REGNUM 6
#define S390_R5_REGNUM 7
#define S390_R6_REGNUM 8
#define S390_R7_REGNUM 9
#define S390_R8_REGNUM 10
#define S390_R9_REGNUM 11
#define S390_R10_REGNUM 12
#define S390_R11_REGNUM 13
#define S390_R12_REGNUM 14
#define S390_R13_REGNUM 15
#define S390_R14_REGNUM 16
#define S390_R15_REGNUM 17
/* Access Registers.  */
#define S390_A0_REGNUM 18
#define S390_A1_REGNUM 19
#define S390_A2_REGNUM 20
#define S390_A3_REGNUM 21
#define S390_A4_REGNUM 22
#define S390_A5_REGNUM 23
#define S390_A6_REGNUM 24
#define S390_A7_REGNUM 25
#define S390_A8_REGNUM 26
#define S390_A9_REGNUM 27
#define S390_A10_REGNUM 28
#define S390_A11_REGNUM 29
#define S390_A12_REGNUM 30
#define S390_A13_REGNUM 31
#define S390_A14_REGNUM 32
#define S390_A15_REGNUM 33
/* Floating Point Control Word.  */
#define S390_FPC_REGNUM 34
/* Floating Point Registers.  */
#define S390_F0_REGNUM 35
#define S390_F1_REGNUM 36
#define S390_F2_REGNUM 37
#define S390_F3_REGNUM 38
#define S390_F4_REGNUM 39
#define S390_F5_REGNUM 40
#define S390_F6_REGNUM 41
#define S390_F7_REGNUM 42
#define S390_F8_REGNUM 43
#define S390_F9_REGNUM 44
#define S390_F10_REGNUM 45
#define S390_F11_REGNUM 46
#define S390_F12_REGNUM 47
#define S390_F13_REGNUM 48
#define S390_F14_REGNUM 49
#define S390_F15_REGNUM 50
/* Total.  */
#define S390_NUM_REGS 51

/* Pseudo registers -- PC and condition code.  */
#define S390_PC_REGNUM S390_NUM_REGS
#define S390_CC_REGNUM (S390_NUM_REGS+1)
#define S390_NUM_PSEUDO_REGS 2
#define S390_NUM_TOTAL_REGS (S390_NUM_REGS+2)



/* Program Status Word.  */
#define S390_PSWM_REGNUM 0
#define S390_PSWA_REGNUM 1
/* General Purpose Registers.  */
#define S390_R0_REGNUM 2
#define S390_R1_REGNUM 3
#define S390_R2_REGNUM 4
#define S390_R3_REGNUM 5
#define S390_R4_REGNUM 6
#define S390_R5_REGNUM 7
#define S390_R6_REGNUM 8
#define S390_R7_REGNUM 9
#define S390_R8_REGNUM 10
#define S390_R9_REGNUM 11
#define S390_R10_REGNUM 12
#define S390_R11_REGNUM 13
#define S390_R12_REGNUM 14
#define S390_R13_REGNUM 15
#define S390_R14_REGNUM 16
#define S390_R15_REGNUM 17
/* Access Registers.  */
#define S390_A0_REGNUM 18
#define S390_A1_REGNUM 19
#define S390_A2_REGNUM 20
#define S390_A3_REGNUM 21
#define S390_A4_REGNUM 22
#define S390_A5_REGNUM 23
#define S390_A6_REGNUM 24
#define S390_A7_REGNUM 25
#define S390_A8_REGNUM 26
#define S390_A9_REGNUM 27
#define S390_A10_REGNUM 28
#define S390_A11_REGNUM 29
#define S390_A12_REGNUM 30
#define S390_A13_REGNUM 31
#define S390_A14_REGNUM 32
#define S390_A15_REGNUM 33
/* Floating Point Control Word.  */
#define S390_FPC_REGNUM 34
/* Floating Point Registers.  */
#define S390_F0_REGNUM 35
#define S390_F1_REGNUM 36
#define S390_F2_REGNUM 37
#define S390_F3_REGNUM 38
#define S390_F4_REGNUM 39
#define S390_F5_REGNUM 40
#define S390_F6_REGNUM 41
#define S390_F7_REGNUM 42
#define S390_F8_REGNUM 43
#define S390_F9_REGNUM 44
#define S390_F10_REGNUM 45
#define S390_F11_REGNUM 46
#define S390_F12_REGNUM 47
#define S390_F13_REGNUM 48
#define S390_F14_REGNUM 49
#define S390_F15_REGNUM 50
/* Total.  */
#define S390_NUM_REGS 51

/* Pseudo registers -- PC and condition code.  */
#define S390_PC_REGNUM S390_NUM_REGS
#define S390_CC_REGNUM (S390_NUM_REGS+1)
#define S390_NUM_PSEUDO_REGS 2
#define S390_NUM_TOTAL_REGS (S390_NUM_REGS+2)


#endif
