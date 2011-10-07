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
#define TARGET_PAGE_BITS 12

#define TARGET_PHYS_ADDR_SPACE_BITS 64
#define TARGET_VIRT_ADDR_SPACE_BITS 64

#include "cpu-all.h"

#include "softfloat.h"

#define NB_MMU_MODES 3

#define MMU_MODE0_SUFFIX _primary
#define MMU_MODE1_SUFFIX _secondary
#define MMU_MODE2_SUFFIX _home

#define MMU_USER_IDX 1

#define MAX_EXT_QUEUE 16

typedef struct PSW {
    uint64_t mask;
    uint64_t addr;
} PSW;

typedef struct ExtQueue {
    uint32_t code;
    uint32_t param;
    uint32_t param64;
} ExtQueue;

typedef struct CPUS390XState {
    uint64_t regs[16];	/* GP registers */

    uint32_t aregs[16];	/* access registers */

    uint32_t fpc;	/* floating-point control register */
    CPU_DoubleU fregs[16]; /* FP registers */
    float_status fpu_status; /* passed to softfloat lib */

    PSW psw;

    uint32_t cc_op;
    uint64_t cc_src;
    uint64_t cc_dst;
    uint64_t cc_vr;

    uint64_t __excp_addr;
    uint64_t psa;

    uint32_t int_pgm_code;
    uint32_t int_pgm_ilc;

    uint32_t int_svc_code;
    uint32_t int_svc_ilc;

    uint64_t cregs[16]; /* control registers */

    int pending_int;
    ExtQueue ext_queue[MAX_EXT_QUEUE];

    int ext_index;

    CPU_COMMON

    /* reset does memset(0) up to here */

    int cpu_num;
    uint8_t *storage_keys;

    uint64_t tod_offset;
    uint64_t tod_basetime;
    QEMUTimer *tod_timer;

    QEMUTimer *cpu_timer;
} CPUS390XState;

#if defined(CONFIG_USER_ONLY)
static inline void cpu_clone_regs(CPUState *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[15] = newsp;
    }
    env->regs[0] = 0;
}
#endif

/* Interrupt Codes */
/* Program Interrupts */
#define PGM_OPERATION                   0x0001
#define PGM_PRIVILEGED                  0x0002
#define PGM_EXECUTE                     0x0003
#define PGM_PROTECTION                  0x0004
#define PGM_ADDRESSING                  0x0005
#define PGM_SPECIFICATION               0x0006
#define PGM_DATA                        0x0007
#define PGM_FIXPT_OVERFLOW              0x0008
#define PGM_FIXPT_DIVIDE                0x0009
#define PGM_DEC_OVERFLOW                0x000a
#define PGM_DEC_DIVIDE                  0x000b
#define PGM_HFP_EXP_OVERFLOW            0x000c
#define PGM_HFP_EXP_UNDERFLOW           0x000d
#define PGM_HFP_SIGNIFICANCE            0x000e
#define PGM_HFP_DIVIDE                  0x000f
#define PGM_SEGMENT_TRANS               0x0010
#define PGM_PAGE_TRANS                  0x0011
#define PGM_TRANS_SPEC                  0x0012
#define PGM_SPECIAL_OP                  0x0013
#define PGM_OPERAND                     0x0015
#define PGM_TRACE_TABLE                 0x0016
#define PGM_SPACE_SWITCH                0x001c
#define PGM_HFP_SQRT                    0x001d
#define PGM_PC_TRANS_SPEC               0x001f
#define PGM_AFX_TRANS                   0x0020
#define PGM_ASX_TRANS                   0x0021
#define PGM_LX_TRANS                    0x0022
#define PGM_EX_TRANS                    0x0023
#define PGM_PRIM_AUTH                   0x0024
#define PGM_SEC_AUTH                    0x0025
#define PGM_ALET_SPEC                   0x0028
#define PGM_ALEN_SPEC                   0x0029
#define PGM_ALE_SEQ                     0x002a
#define PGM_ASTE_VALID                  0x002b
#define PGM_ASTE_SEQ                    0x002c
#define PGM_EXT_AUTH                    0x002d
#define PGM_STACK_FULL                  0x0030
#define PGM_STACK_EMPTY                 0x0031
#define PGM_STACK_SPEC                  0x0032
#define PGM_STACK_TYPE                  0x0033
#define PGM_STACK_OP                    0x0034
#define PGM_ASCE_TYPE                   0x0038
#define PGM_REG_FIRST_TRANS             0x0039
#define PGM_REG_SEC_TRANS               0x003a
#define PGM_REG_THIRD_TRANS             0x003b
#define PGM_MONITOR                     0x0040
#define PGM_PER                         0x0080
#define PGM_CRYPTO                      0x0119

/* External Interrupts */
#define EXT_INTERRUPT_KEY               0x0040
#define EXT_CLOCK_COMP                  0x1004
#define EXT_CPU_TIMER                   0x1005
#define EXT_MALFUNCTION                 0x1200
#define EXT_EMERGENCY                   0x1201
#define EXT_EXTERNAL_CALL               0x1202
#define EXT_ETR                         0x1406
#define EXT_SERVICE                     0x2401
#define EXT_VIRTIO                      0x2603

/* PSW defines */
#undef PSW_MASK_PER
#undef PSW_MASK_DAT
#undef PSW_MASK_IO
#undef PSW_MASK_EXT
#undef PSW_MASK_KEY
#undef PSW_SHIFT_KEY
#undef PSW_MASK_MCHECK
#undef PSW_MASK_WAIT
#undef PSW_MASK_PSTATE
#undef PSW_MASK_ASC
#undef PSW_MASK_CC
#undef PSW_MASK_PM
#undef PSW_MASK_64

#define PSW_MASK_PER            0x4000000000000000ULL
#define PSW_MASK_DAT            0x0400000000000000ULL
#define PSW_MASK_IO             0x0200000000000000ULL
#define PSW_MASK_EXT            0x0100000000000000ULL
#define PSW_MASK_KEY            0x00F0000000000000ULL
#define PSW_SHIFT_KEY           56
#define PSW_MASK_MCHECK         0x0004000000000000ULL
#define PSW_MASK_WAIT           0x0002000000000000ULL
#define PSW_MASK_PSTATE         0x0001000000000000ULL
#define PSW_MASK_ASC            0x0000C00000000000ULL
#define PSW_MASK_CC             0x0000300000000000ULL
#define PSW_MASK_PM             0x00000F0000000000ULL
#define PSW_MASK_64             0x0000000100000000ULL
#define PSW_MASK_32             0x0000000080000000ULL

#undef PSW_ASC_PRIMARY
#undef PSW_ASC_ACCREG
#undef PSW_ASC_SECONDARY
#undef PSW_ASC_HOME

#define PSW_ASC_PRIMARY         0x0000000000000000ULL
#define PSW_ASC_ACCREG          0x0000400000000000ULL
#define PSW_ASC_SECONDARY       0x0000800000000000ULL
#define PSW_ASC_HOME            0x0000C00000000000ULL

/* tb flags */

#define FLAG_MASK_PER           (PSW_MASK_PER    >> 32)
#define FLAG_MASK_DAT           (PSW_MASK_DAT    >> 32)
#define FLAG_MASK_IO            (PSW_MASK_IO     >> 32)
#define FLAG_MASK_EXT           (PSW_MASK_EXT    >> 32)
#define FLAG_MASK_KEY           (PSW_MASK_KEY    >> 32)
#define FLAG_MASK_MCHECK        (PSW_MASK_MCHECK >> 32)
#define FLAG_MASK_WAIT          (PSW_MASK_WAIT   >> 32)
#define FLAG_MASK_PSTATE        (PSW_MASK_PSTATE >> 32)
#define FLAG_MASK_ASC           (PSW_MASK_ASC    >> 32)
#define FLAG_MASK_CC            (PSW_MASK_CC     >> 32)
#define FLAG_MASK_PM            (PSW_MASK_PM     >> 32)
#define FLAG_MASK_64            (PSW_MASK_64     >> 32)
#define FLAG_MASK_32            0x00001000

static inline int cpu_mmu_index (CPUState *env)
{
    if (env->psw.mask & PSW_MASK_PSTATE) {
        return 1;
    }

    return 0;
}

static inline void cpu_get_tb_cpu_state(CPUState* env, target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    *pc = env->psw.addr;
    *cs_base = 0;
    *flags = ((env->psw.mask >> 32) & ~FLAG_MASK_CC) |
             ((env->psw.mask & PSW_MASK_32) ? FLAG_MASK_32 : 0);
}

static inline int get_ilc(uint8_t opc)
{
    switch (opc >> 6) {
    case 0:
        return 1;
    case 1:
    case 2:
        return 2;
    case 3:
        return 3;
    }

    return 0;
}

#define ILC_LATER       0x20
#define ILC_LATER_INC   0x21
#define ILC_LATER_INC_2 0x22


CPUS390XState *cpu_s390x_init(const char *cpu_model);
void s390x_translate_init(void);
int cpu_s390x_exec(CPUS390XState *s);
void cpu_s390x_close(CPUS390XState *s);
void do_interrupt (CPUState *env);

/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_s390x_signal_handler(int host_signum, void *pinfo,
                           void *puc);
int cpu_s390x_handle_mmu_fault (CPUS390XState *env, target_ulong address, int rw,
                                int mmu_idx);
#define cpu_handle_mmu_fault cpu_s390x_handle_mmu_fault


#ifndef CONFIG_USER_ONLY
int s390_virtio_hypercall(CPUState *env, uint64_t mem, uint64_t hypercall);

#ifdef CONFIG_KVM
void kvm_s390_interrupt(CPUState *env, int type, uint32_t code);
void kvm_s390_virtio_irq(CPUState *env, int config_change, uint64_t token);
void kvm_s390_interrupt_internal(CPUState *env, int type, uint32_t parm,
                                 uint64_t parm64, int vm);
#else
static inline void kvm_s390_interrupt(CPUState *env, int type, uint32_t code)
{
}

static inline void kvm_s390_virtio_irq(CPUState *env, int config_change,
                                       uint64_t token)
{
}

static inline void kvm_s390_interrupt_internal(CPUState *env, int type,
                                               uint32_t parm, uint64_t parm64,
                                               int vm)
{
}
#endif
CPUState *s390_cpu_addr2state(uint16_t cpu_addr);
void s390_add_running_cpu(CPUState *env);
unsigned s390_del_running_cpu(CPUState *env);

/* from s390-virtio-bus */
extern const target_phys_addr_t virtio_size;

#else
static inline void s390_add_running_cpu(CPUState *env)
{
}

static inline unsigned s390_del_running_cpu(CPUState *env)
{
    return 0;
}
#endif
void cpu_lock(void);
void cpu_unlock(void);

static inline void cpu_set_tls(CPUS390XState *env, target_ulong newtls)
{
    env->aregs[0] = newtls >> 32;
    env->aregs[1] = newtls & 0xffffffffULL;
}

#define cpu_init cpu_s390x_init
#define cpu_exec cpu_s390x_exec
#define cpu_gen_code cpu_s390x_gen_code
#define cpu_signal_handler cpu_s390x_signal_handler

#include "exec-all.h"

#ifdef CONFIG_USER_ONLY

#define EXCP_OPEX 1 /* operation exception (sigill) */
#define EXCP_SVC 2 /* supervisor call (syscall) */
#define EXCP_ADDR 5 /* addressing exception */
#define EXCP_SPEC 6 /* specification exception */

#else

#define EXCP_EXT 1 /* external interrupt */
#define EXCP_SVC 2 /* supervisor call (syscall) */
#define EXCP_PGM 3 /* program interruption */

#endif /* CONFIG_USER_ONLY */

#define INTERRUPT_EXT        (1 << 0)
#define INTERRUPT_TOD        (1 << 1)
#define INTERRUPT_CPUTIMER   (1 << 2)

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

/* CC optimization */

enum cc_op {
    CC_OP_CONST0 = 0,           /* CC is 0 */
    CC_OP_CONST1,               /* CC is 1 */
    CC_OP_CONST2,               /* CC is 2 */
    CC_OP_CONST3,               /* CC is 3 */

    CC_OP_DYNAMIC,              /* CC calculation defined by env->cc_op */
    CC_OP_STATIC,               /* CC value is env->cc_op */

    CC_OP_NZ,                   /* env->cc_dst != 0 */
    CC_OP_LTGT_32,              /* signed less/greater than (32bit) */
    CC_OP_LTGT_64,              /* signed less/greater than (64bit) */
    CC_OP_LTUGTU_32,            /* unsigned less/greater than (32bit) */
    CC_OP_LTUGTU_64,            /* unsigned less/greater than (64bit) */
    CC_OP_LTGT0_32,             /* signed less/greater than 0 (32bit) */
    CC_OP_LTGT0_64,             /* signed less/greater than 0 (64bit) */

    CC_OP_ADD_64,               /* overflow on add (64bit) */
    CC_OP_ADDU_64,              /* overflow on unsigned add (64bit) */
    CC_OP_SUB_64,               /* overflow on substraction (64bit) */
    CC_OP_SUBU_64,              /* overflow on unsigned substraction (64bit) */
    CC_OP_ABS_64,               /* sign eval on abs (64bit) */
    CC_OP_NABS_64,              /* sign eval on nabs (64bit) */

    CC_OP_ADD_32,               /* overflow on add (32bit) */
    CC_OP_ADDU_32,              /* overflow on unsigned add (32bit) */
    CC_OP_SUB_32,               /* overflow on substraction (32bit) */
    CC_OP_SUBU_32,              /* overflow on unsigned substraction (32bit) */
    CC_OP_ABS_32,               /* sign eval on abs (64bit) */
    CC_OP_NABS_32,              /* sign eval on nabs (64bit) */

    CC_OP_COMP_32,              /* complement */
    CC_OP_COMP_64,              /* complement */

    CC_OP_TM_32,                /* test under mask (32bit) */
    CC_OP_TM_64,                /* test under mask (64bit) */

    CC_OP_LTGT_F32,             /* FP compare (32bit) */
    CC_OP_LTGT_F64,             /* FP compare (64bit) */

    CC_OP_NZ_F32,               /* FP dst != 0 (32bit) */
    CC_OP_NZ_F64,               /* FP dst != 0 (64bit) */

    CC_OP_ICM,                  /* insert characters under mask */
    CC_OP_SLAG,                 /* Calculate shift left signed */
    CC_OP_MAX
};

static const char *cc_names[] = {
    [CC_OP_CONST0]    = "CC_OP_CONST0",
    [CC_OP_CONST1]    = "CC_OP_CONST1",
    [CC_OP_CONST2]    = "CC_OP_CONST2",
    [CC_OP_CONST3]    = "CC_OP_CONST3",
    [CC_OP_DYNAMIC]   = "CC_OP_DYNAMIC",
    [CC_OP_STATIC]    = "CC_OP_STATIC",
    [CC_OP_NZ]        = "CC_OP_NZ",
    [CC_OP_LTGT_32]   = "CC_OP_LTGT_32",
    [CC_OP_LTGT_64]   = "CC_OP_LTGT_64",
    [CC_OP_LTUGTU_32] = "CC_OP_LTUGTU_32",
    [CC_OP_LTUGTU_64] = "CC_OP_LTUGTU_64",
    [CC_OP_LTGT0_32]  = "CC_OP_LTGT0_32",
    [CC_OP_LTGT0_64]  = "CC_OP_LTGT0_64",
    [CC_OP_ADD_64]    = "CC_OP_ADD_64",
    [CC_OP_ADDU_64]   = "CC_OP_ADDU_64",
    [CC_OP_SUB_64]    = "CC_OP_SUB_64",
    [CC_OP_SUBU_64]   = "CC_OP_SUBU_64",
    [CC_OP_ABS_64]    = "CC_OP_ABS_64",
    [CC_OP_NABS_64]   = "CC_OP_NABS_64",
    [CC_OP_ADD_32]    = "CC_OP_ADD_32",
    [CC_OP_ADDU_32]   = "CC_OP_ADDU_32",
    [CC_OP_SUB_32]    = "CC_OP_SUB_32",
    [CC_OP_SUBU_32]   = "CC_OP_SUBU_32",
    [CC_OP_ABS_32]    = "CC_OP_ABS_32",
    [CC_OP_NABS_32]   = "CC_OP_NABS_32",
    [CC_OP_COMP_32]   = "CC_OP_COMP_32",
    [CC_OP_COMP_64]   = "CC_OP_COMP_64",
    [CC_OP_TM_32]     = "CC_OP_TM_32",
    [CC_OP_TM_64]     = "CC_OP_TM_64",
    [CC_OP_LTGT_F32]  = "CC_OP_LTGT_F32",
    [CC_OP_LTGT_F64]  = "CC_OP_LTGT_F64",
    [CC_OP_NZ_F32]    = "CC_OP_NZ_F32",
    [CC_OP_NZ_F64]    = "CC_OP_NZ_F64",
    [CC_OP_ICM]       = "CC_OP_ICM",
    [CC_OP_SLAG]      = "CC_OP_SLAG",
};

static inline const char *cc_name(int cc_op)
{
    return cc_names[cc_op];
}

/* SCLP PV interface defines */
#define SCLP_CMDW_READ_SCP_INFO         0x00020001
#define SCLP_CMDW_READ_SCP_INFO_FORCED  0x00120001

#define SCP_LENGTH                      0x00
#define SCP_FUNCTION_CODE               0x02
#define SCP_CONTROL_MASK                0x03
#define SCP_RESPONSE_CODE               0x06
#define SCP_MEM_CODE                    0x08
#define SCP_INCREMENT                   0x0a

typedef struct LowCore
{
    /* prefix area: defined by architecture */
    uint32_t        ccw1[2];                  /* 0x000 */
    uint32_t        ccw2[4];                  /* 0x008 */
    uint8_t         pad1[0x80-0x18];          /* 0x018 */
    uint32_t        ext_params;               /* 0x080 */
    uint16_t        cpu_addr;                 /* 0x084 */
    uint16_t        ext_int_code;             /* 0x086 */
    uint16_t        svc_ilc;                  /* 0x088 */
    uint16_t        svc_code;                 /* 0x08a */
    uint16_t        pgm_ilc;                  /* 0x08c */
    uint16_t        pgm_code;                 /* 0x08e */
    uint32_t        data_exc_code;            /* 0x090 */
    uint16_t        mon_class_num;            /* 0x094 */
    uint16_t        per_perc_atmid;           /* 0x096 */
    uint64_t        per_address;              /* 0x098 */
    uint8_t         exc_access_id;            /* 0x0a0 */
    uint8_t         per_access_id;            /* 0x0a1 */
    uint8_t         op_access_id;             /* 0x0a2 */
    uint8_t         ar_access_id;             /* 0x0a3 */
    uint8_t         pad2[0xA8-0xA4];          /* 0x0a4 */
    uint64_t        trans_exc_code;           /* 0x0a8 */
    uint64_t        monitor_code;             /* 0x0b0 */
    uint16_t        subchannel_id;            /* 0x0b8 */
    uint16_t        subchannel_nr;            /* 0x0ba */
    uint32_t        io_int_parm;              /* 0x0bc */
    uint32_t        io_int_word;              /* 0x0c0 */
    uint8_t         pad3[0xc8-0xc4];          /* 0x0c4 */
    uint32_t        stfl_fac_list;            /* 0x0c8 */
    uint8_t         pad4[0xe8-0xcc];          /* 0x0cc */
    uint32_t        mcck_interruption_code[2]; /* 0x0e8 */
    uint8_t         pad5[0xf4-0xf0];          /* 0x0f0 */
    uint32_t        external_damage_code;     /* 0x0f4 */
    uint64_t        failing_storage_address;  /* 0x0f8 */
    uint8_t         pad6[0x120-0x100];        /* 0x100 */
    PSW             restart_old_psw;          /* 0x120 */
    PSW             external_old_psw;         /* 0x130 */
    PSW             svc_old_psw;              /* 0x140 */
    PSW             program_old_psw;          /* 0x150 */
    PSW             mcck_old_psw;             /* 0x160 */
    PSW             io_old_psw;               /* 0x170 */
    uint8_t         pad7[0x1a0-0x180];        /* 0x180 */
    PSW             restart_psw;              /* 0x1a0 */
    PSW             external_new_psw;         /* 0x1b0 */
    PSW             svc_new_psw;              /* 0x1c0 */
    PSW             program_new_psw;          /* 0x1d0 */
    PSW             mcck_new_psw;             /* 0x1e0 */
    PSW             io_new_psw;               /* 0x1f0 */
    PSW             return_psw;               /* 0x200 */
    uint8_t         irb[64];                  /* 0x210 */
    uint64_t        sync_enter_timer;         /* 0x250 */
    uint64_t        async_enter_timer;        /* 0x258 */
    uint64_t        exit_timer;               /* 0x260 */
    uint64_t        last_update_timer;        /* 0x268 */
    uint64_t        user_timer;               /* 0x270 */
    uint64_t        system_timer;             /* 0x278 */
    uint64_t        last_update_clock;        /* 0x280 */
    uint64_t        steal_clock;              /* 0x288 */
    PSW             return_mcck_psw;          /* 0x290 */
    uint8_t         pad8[0xc00-0x2a0];        /* 0x2a0 */
    /* System info area */
    uint64_t        save_area[16];            /* 0xc00 */
    uint8_t         pad9[0xd40-0xc80];        /* 0xc80 */
    uint64_t        kernel_stack;             /* 0xd40 */
    uint64_t        thread_info;              /* 0xd48 */
    uint64_t        async_stack;              /* 0xd50 */
    uint64_t        kernel_asce;              /* 0xd58 */
    uint64_t        user_asce;                /* 0xd60 */
    uint64_t        panic_stack;              /* 0xd68 */
    uint64_t        user_exec_asce;           /* 0xd70 */
    uint8_t         pad10[0xdc0-0xd78];       /* 0xd78 */

    /* SMP info area: defined by DJB */
    uint64_t        clock_comparator;         /* 0xdc0 */
    uint64_t        ext_call_fast;            /* 0xdc8 */
    uint64_t        percpu_offset;            /* 0xdd0 */
    uint64_t        current_task;             /* 0xdd8 */
    uint32_t        softirq_pending;          /* 0xde0 */
    uint32_t        pad_0x0de4;               /* 0xde4 */
    uint64_t        int_clock;                /* 0xde8 */
    uint8_t         pad12[0xe00-0xdf0];       /* 0xdf0 */

    /* 0xe00 is used as indicator for dump tools */
    /* whether the kernel died with panic() or not */
    uint32_t        panic_magic;              /* 0xe00 */

    uint8_t         pad13[0x11b8-0xe04];      /* 0xe04 */

    /* 64 bit extparam used for pfault, diag 250 etc  */
    uint64_t        ext_params2;               /* 0x11B8 */

    uint8_t         pad14[0x1200-0x11C0];      /* 0x11C0 */

    /* System info area */

    uint64_t        floating_pt_save_area[16]; /* 0x1200 */
    uint64_t        gpregs_save_area[16];      /* 0x1280 */
    uint32_t        st_status_fixed_logout[4]; /* 0x1300 */
    uint8_t         pad15[0x1318-0x1310];      /* 0x1310 */
    uint32_t        prefixreg_save_area;       /* 0x1318 */
    uint32_t        fpt_creg_save_area;        /* 0x131c */
    uint8_t         pad16[0x1324-0x1320];      /* 0x1320 */
    uint32_t        tod_progreg_save_area;     /* 0x1324 */
    uint32_t        cpu_timer_save_area[2];    /* 0x1328 */
    uint32_t        clock_comp_save_area[2];   /* 0x1330 */
    uint8_t         pad17[0x1340-0x1338];      /* 0x1338 */
    uint32_t        access_regs_save_area[16]; /* 0x1340 */
    uint64_t        cregs_save_area[16];       /* 0x1380 */

    /* align to the top of the prefix area */

    uint8_t         pad18[0x2000-0x1400];      /* 0x1400 */
} QEMU_PACKED LowCore;

/* STSI */
#define STSI_LEVEL_MASK         0x00000000f0000000ULL
#define STSI_LEVEL_CURRENT      0x0000000000000000ULL
#define STSI_LEVEL_1            0x0000000010000000ULL
#define STSI_LEVEL_2            0x0000000020000000ULL
#define STSI_LEVEL_3            0x0000000030000000ULL
#define STSI_R0_RESERVED_MASK   0x000000000fffff00ULL
#define STSI_R0_SEL1_MASK       0x00000000000000ffULL
#define STSI_R1_RESERVED_MASK   0x00000000ffff0000ULL
#define STSI_R1_SEL2_MASK       0x000000000000ffffULL

/* Basic Machine Configuration */
struct sysib_111 {
    uint32_t res1[8];
    uint8_t  manuf[16];
    uint8_t  type[4];
    uint8_t  res2[12];
    uint8_t  model[16];
    uint8_t  sequence[16];
    uint8_t  plant[4];
    uint8_t  res3[156];
};

/* Basic Machine CPU */
struct sysib_121 {
    uint32_t res1[80];
    uint8_t  sequence[16];
    uint8_t  plant[4];
    uint8_t  res2[2];
    uint16_t cpu_addr;
    uint8_t  res3[152];
};

/* Basic Machine CPUs */
struct sysib_122 {
    uint8_t res1[32];
    uint32_t capability;
    uint16_t total_cpus;
    uint16_t active_cpus;
    uint16_t standby_cpus;
    uint16_t reserved_cpus;
    uint16_t adjustments[2026];
};

/* LPAR CPU */
struct sysib_221 {
    uint32_t res1[80];
    uint8_t  sequence[16];
    uint8_t  plant[4];
    uint16_t cpu_id;
    uint16_t cpu_addr;
    uint8_t  res3[152];
};

/* LPAR CPUs */
struct sysib_222 {
    uint32_t res1[32];
    uint16_t lpar_num;
    uint8_t  res2;
    uint8_t  lcpuc;
    uint16_t total_cpus;
    uint16_t conf_cpus;
    uint16_t standby_cpus;
    uint16_t reserved_cpus;
    uint8_t  name[8];
    uint32_t caf;
    uint8_t  res3[16];
    uint16_t dedicated_cpus;
    uint16_t shared_cpus;
    uint8_t  res4[180];
};

/* VM CPUs */
struct sysib_322 {
    uint8_t  res1[31];
    uint8_t  count;
    struct {
        uint8_t  res2[4];
        uint16_t total_cpus;
        uint16_t conf_cpus;
        uint16_t standby_cpus;
        uint16_t reserved_cpus;
        uint8_t  name[8];
        uint32_t caf;
        uint8_t  cpi[16];
        uint8_t  res3[24];
    } vm[8];
    uint8_t res4[3552];
};

/* MMU defines */
#define _ASCE_ORIGIN            ~0xfffULL /* segment table origin             */
#define _ASCE_SUBSPACE          0x200     /* subspace group control           */
#define _ASCE_PRIVATE_SPACE     0x100     /* private space control            */
#define _ASCE_ALT_EVENT         0x80      /* storage alteration event control */
#define _ASCE_SPACE_SWITCH      0x40      /* space switch event               */
#define _ASCE_REAL_SPACE        0x20      /* real space control               */
#define _ASCE_TYPE_MASK         0x0c      /* asce table type mask             */
#define _ASCE_TYPE_REGION1      0x0c      /* region first table type          */
#define _ASCE_TYPE_REGION2      0x08      /* region second table type         */
#define _ASCE_TYPE_REGION3      0x04      /* region third table type          */
#define _ASCE_TYPE_SEGMENT      0x00      /* segment table type               */
#define _ASCE_TABLE_LENGTH      0x03      /* region table length              */

#define _REGION_ENTRY_ORIGIN    ~0xfffULL /* region/segment table origin      */
#define _REGION_ENTRY_INV       0x20      /* invalid region table entry       */
#define _REGION_ENTRY_TYPE_MASK 0x0c      /* region/segment table type mask   */
#define _REGION_ENTRY_TYPE_R1   0x0c      /* region first table type          */
#define _REGION_ENTRY_TYPE_R2   0x08      /* region second table type         */
#define _REGION_ENTRY_TYPE_R3   0x04      /* region third table type          */
#define _REGION_ENTRY_LENGTH    0x03      /* region third length              */

#define _SEGMENT_ENTRY_ORIGIN   ~0x7ffULL /* segment table origin             */
#define _SEGMENT_ENTRY_RO       0x200     /* page protection bit              */
#define _SEGMENT_ENTRY_INV      0x20      /* invalid segment table entry      */

#define _PAGE_RO        0x200            /* HW read-only bit  */
#define _PAGE_INVALID   0x400            /* HW invalid bit    */

#define SK_C                    (0x1 << 1)
#define SK_R                    (0x1 << 2)
#define SK_F                    (0x1 << 3)
#define SK_ACC_MASK             (0xf << 4)


/* EBCDIC handling */
static const uint8_t ebcdic2ascii[] = {
    0x00, 0x01, 0x02, 0x03, 0x07, 0x09, 0x07, 0x7F,
    0x07, 0x07, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x07, 0x0A, 0x08, 0x07,
    0x18, 0x19, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x1C, 0x07, 0x07, 0x0A, 0x17, 0x1B,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x06, 0x07,
    0x07, 0x07, 0x16, 0x07, 0x07, 0x07, 0x07, 0x04,
    0x07, 0x07, 0x07, 0x07, 0x14, 0x15, 0x07, 0x1A,
    0x20, 0xFF, 0x83, 0x84, 0x85, 0xA0, 0x07, 0x86,
    0x87, 0xA4, 0x5B, 0x2E, 0x3C, 0x28, 0x2B, 0x21,
    0x26, 0x82, 0x88, 0x89, 0x8A, 0xA1, 0x8C, 0x07,
    0x8D, 0xE1, 0x5D, 0x24, 0x2A, 0x29, 0x3B, 0x5E,
    0x2D, 0x2F, 0x07, 0x8E, 0x07, 0x07, 0x07, 0x8F,
    0x80, 0xA5, 0x07, 0x2C, 0x25, 0x5F, 0x3E, 0x3F,
    0x07, 0x90, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x70, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22,
    0x07, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0xAE, 0xAF, 0x07, 0x07, 0x07, 0xF1,
    0xF8, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,
    0x71, 0x72, 0xA6, 0xA7, 0x91, 0x07, 0x92, 0x07,
    0xE6, 0x7E, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0xAD, 0xAB, 0x07, 0x07, 0x07, 0x07,
    0x9B, 0x9C, 0x9D, 0xFA, 0x07, 0x07, 0x07, 0xAC,
    0xAB, 0x07, 0xAA, 0x7C, 0x07, 0x07, 0x07, 0x07,
    0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x07, 0x93, 0x94, 0x95, 0xA2, 0x07,
    0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
    0x51, 0x52, 0x07, 0x96, 0x81, 0x97, 0xA3, 0x98,
    0x5C, 0xF6, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0xFD, 0x07, 0x99, 0x07, 0x07, 0x07,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x07, 0x07, 0x9A, 0x07, 0x07, 0x07,
};

static const uint8_t ascii2ebcdic [] = {
    0x00, 0x01, 0x02, 0x03, 0x37, 0x2D, 0x2E, 0x2F,
    0x16, 0x05, 0x15, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x3C, 0x3D, 0x32, 0x26,
    0x18, 0x19, 0x3F, 0x27, 0x22, 0x1D, 0x1E, 0x1F,
    0x40, 0x5A, 0x7F, 0x7B, 0x5B, 0x6C, 0x50, 0x7D,
    0x4D, 0x5D, 0x5C, 0x4E, 0x6B, 0x60, 0x4B, 0x61,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0x7A, 0x5E, 0x4C, 0x7E, 0x6E, 0x6F,
    0x7C, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
    0xD7, 0xD8, 0xD9, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
    0xE7, 0xE8, 0xE9, 0xBA, 0xE0, 0xBB, 0xB0, 0x6D,
    0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xC0, 0x4F, 0xD0, 0xA1, 0x07,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x59, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x90, 0x3F, 0x3F, 0x3F, 0x3F, 0xEA, 0x3F, 0xFF
};

static inline void ebcdic_put(uint8_t *p, const char *ascii, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        p[i] = ascii2ebcdic[(int)ascii[i]];
    }
}

#define SIGP_SENSE             0x01
#define SIGP_EXTERNAL_CALL     0x02
#define SIGP_EMERGENCY         0x03
#define SIGP_START             0x04
#define SIGP_STOP              0x05
#define SIGP_RESTART           0x06
#define SIGP_STOP_STORE_STATUS 0x09
#define SIGP_INITIAL_CPU_RESET 0x0b
#define SIGP_CPU_RESET         0x0c
#define SIGP_SET_PREFIX        0x0d
#define SIGP_STORE_STATUS_ADDR 0x0e
#define SIGP_SET_ARCH          0x12

/* cpu status bits */
#define SIGP_STAT_EQUIPMENT_CHECK   0x80000000UL
#define SIGP_STAT_INCORRECT_STATE   0x00000200UL
#define SIGP_STAT_INVALID_PARAMETER 0x00000100UL
#define SIGP_STAT_EXT_CALL_PENDING  0x00000080UL
#define SIGP_STAT_STOPPED           0x00000040UL
#define SIGP_STAT_OPERATOR_INTERV   0x00000020UL
#define SIGP_STAT_CHECK_STOP        0x00000010UL
#define SIGP_STAT_INOPERATIVE       0x00000004UL
#define SIGP_STAT_INVALID_ORDER     0x00000002UL
#define SIGP_STAT_RECEIVER_CHECK    0x00000001UL

void load_psw(CPUState *env, uint64_t mask, uint64_t addr);
int mmu_translate(CPUState *env, target_ulong vaddr, int rw, uint64_t asc,
                  target_ulong *raddr, int *flags);
int sclp_service_call(CPUState *env, uint32_t sccb, uint64_t code);
uint32_t calc_cc(CPUState *env, uint32_t cc_op, uint64_t src, uint64_t dst,
                 uint64_t vr);

#define TARGET_HAS_ICE 1

/* The value of the TOD clock for 1.1.1970. */
#define TOD_UNIX_EPOCH 0x7d91048bca000000ULL

/* Converts ns to s390's clock format */
static inline uint64_t time2tod(uint64_t ns) {
    return (ns << 9) / 125;
}

static inline void cpu_inject_ext(CPUState *env, uint32_t code, uint32_t param,
                                  uint64_t param64)
{
    if (env->ext_index == MAX_EXT_QUEUE - 1) {
        /* ugh - can't queue anymore. Let's drop. */
        return;
    }

    env->ext_index++;
    assert(env->ext_index < MAX_EXT_QUEUE);

    env->ext_queue[env->ext_index].code = code;
    env->ext_queue[env->ext_index].param = param;
    env->ext_queue[env->ext_index].param64 = param64;

    env->pending_int |= INTERRUPT_EXT;
    cpu_interrupt(env, CPU_INTERRUPT_HARD);
}

static inline bool cpu_has_work(CPUState *env)
{
    return (env->interrupt_request & CPU_INTERRUPT_HARD) &&
        (env->psw.mask & PSW_MASK_EXT);
}

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock* tb)
{
    env->psw.addr = tb->pc;
}

#endif
