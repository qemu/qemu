/*
 * i386 virtual CPU header
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef CPU_I386_H
#define CPU_I386_H

#include "cpu-defs.h"

#define R_EAX 0
#define R_ECX 1
#define R_EDX 2
#define R_EBX 3
#define R_ESP 4
#define R_EBP 5
#define R_ESI 6
#define R_EDI 7

#define R_AL 0
#define R_CL 1
#define R_DL 2
#define R_BL 3
#define R_AH 4
#define R_CH 5
#define R_DH 6
#define R_BH 7

#define R_ES 0
#define R_CS 1
#define R_SS 2
#define R_DS 3
#define R_FS 4
#define R_GS 5

/* segment descriptor fields */
#define DESC_G_MASK     (1 << 23)
#define DESC_B_SHIFT    22
#define DESC_B_MASK     (1 << DESC_B_SHIFT)
#define DESC_AVL_MASK   (1 << 20)
#define DESC_P_MASK     (1 << 15)
#define DESC_DPL_SHIFT  13
#define DESC_S_MASK     (1 << 12)
#define DESC_TYPE_SHIFT 8
#define DESC_A_MASK     (1 << 8)

#define DESC_CS_MASK    (1 << 11)
#define DESC_C_MASK     (1 << 10)
#define DESC_R_MASK     (1 << 9)

#define DESC_E_MASK     (1 << 10)
#define DESC_W_MASK     (1 << 9)

/* eflags masks */
#define CC_C   	0x0001
#define CC_P 	0x0004
#define CC_A	0x0010
#define CC_Z	0x0040
#define CC_S    0x0080
#define CC_O    0x0800

#define TF_SHIFT   8
#define IOPL_SHIFT 12
#define VM_SHIFT   17

#define TF_MASK 		0x00000100
#define IF_MASK 		0x00000200
#define DF_MASK 		0x00000400
#define IOPL_MASK		0x00003000
#define NT_MASK	         	0x00004000
#define RF_MASK			0x00010000
#define VM_MASK			0x00020000
#define AC_MASK			0x00040000 
#define VIF_MASK                0x00080000
#define VIP_MASK                0x00100000
#define ID_MASK                 0x00200000

/* hidden flags - used internally by qemu to represent additionnal cpu
   states. Only the CPL and INHIBIT_IRQ are not redundant. We avoid
   using the IOPL_MASK, TF_MASK and VM_MASK bit position to ease oring
   with eflags. */
/* current cpl */
#define HF_CPL_SHIFT         0
/* true if soft mmu is being used */
#define HF_SOFTMMU_SHIFT     2
/* true if hardware interrupts must be disabled for next instruction */
#define HF_INHIBIT_IRQ_SHIFT 3
/* 16 or 32 segments */
#define HF_CS32_SHIFT        4
#define HF_SS32_SHIFT        5
/* zero base for DS, ES and SS */
#define HF_ADDSEG_SHIFT      6

#define HF_CPL_MASK          (3 << HF_CPL_SHIFT)
#define HF_SOFTMMU_MASK      (1 << HF_SOFTMMU_SHIFT)
#define HF_INHIBIT_IRQ_MASK  (1 << HF_INHIBIT_IRQ_SHIFT)
#define HF_CS32_MASK         (1 << HF_CS32_SHIFT)
#define HF_SS32_MASK         (1 << HF_SS32_SHIFT)
#define HF_ADDSEG_MASK       (1 << HF_ADDSEG_SHIFT)

#define CR0_PE_MASK  (1 << 0)
#define CR0_TS_MASK  (1 << 3)
#define CR0_WP_MASK  (1 << 16)
#define CR0_AM_MASK  (1 << 18)
#define CR0_PG_MASK  (1 << 31)

#define CR4_VME_MASK  (1 << 0)
#define CR4_PVI_MASK  (1 << 1)
#define CR4_TSD_MASK  (1 << 2)
#define CR4_DE_MASK   (1 << 3)
#define CR4_PSE_MASK  (1 << 4)

#define PG_PRESENT_BIT	0
#define PG_RW_BIT	1
#define PG_USER_BIT	2
#define PG_PWT_BIT	3
#define PG_PCD_BIT	4
#define PG_ACCESSED_BIT	5
#define PG_DIRTY_BIT	6
#define PG_PSE_BIT	7
#define PG_GLOBAL_BIT	8

#define PG_PRESENT_MASK  (1 << PG_PRESENT_BIT)
#define PG_RW_MASK	 (1 << PG_RW_BIT)
#define PG_USER_MASK	 (1 << PG_USER_BIT)
#define PG_PWT_MASK	 (1 << PG_PWT_BIT)
#define PG_PCD_MASK	 (1 << PG_PCD_BIT)
#define PG_ACCESSED_MASK (1 << PG_ACCESSED_BIT)
#define PG_DIRTY_MASK	 (1 << PG_DIRTY_BIT)
#define PG_PSE_MASK	 (1 << PG_PSE_BIT)
#define PG_GLOBAL_MASK	 (1 << PG_GLOBAL_BIT)

#define PG_ERROR_W_BIT     1

#define PG_ERROR_P_MASK    0x01
#define PG_ERROR_W_MASK    (1 << PG_ERROR_W_BIT)
#define PG_ERROR_U_MASK    0x04
#define PG_ERROR_RSVD_MASK 0x08

#define MSR_IA32_APICBASE               0x1b
#define MSR_IA32_APICBASE_BSP           (1<<8)
#define MSR_IA32_APICBASE_ENABLE        (1<<11)
#define MSR_IA32_APICBASE_BASE          (0xfffff<<12)

#define MSR_IA32_SYSENTER_CS            0x174
#define MSR_IA32_SYSENTER_ESP           0x175
#define MSR_IA32_SYSENTER_EIP           0x176

#define EXCP00_DIVZ	0
#define EXCP01_SSTP	1
#define EXCP02_NMI	2
#define EXCP03_INT3	3
#define EXCP04_INTO	4
#define EXCP05_BOUND	5
#define EXCP06_ILLOP	6
#define EXCP07_PREX	7
#define EXCP08_DBLE	8
#define EXCP09_XERR	9
#define EXCP0A_TSS	10
#define EXCP0B_NOSEG	11
#define EXCP0C_STACK	12
#define EXCP0D_GPF	13
#define EXCP0E_PAGE	14
#define EXCP10_COPR	16
#define EXCP11_ALGN	17
#define EXCP12_MCHK	18

enum {
    CC_OP_DYNAMIC, /* must use dynamic code to get cc_op */
    CC_OP_EFLAGS,  /* all cc are explicitely computed, CC_SRC = flags */
    CC_OP_MUL, /* modify all flags, C, O = (CC_SRC != 0) */

    CC_OP_ADDB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_ADDW,
    CC_OP_ADDL,

    CC_OP_ADCB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_ADCW,
    CC_OP_ADCL,

    CC_OP_SUBB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_SUBW,
    CC_OP_SUBL,

    CC_OP_SBBB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_SBBW,
    CC_OP_SBBL,

    CC_OP_LOGICB, /* modify all flags, CC_DST = res */
    CC_OP_LOGICW,
    CC_OP_LOGICL,

    CC_OP_INCB, /* modify all flags except, CC_DST = res, CC_SRC = C */
    CC_OP_INCW,
    CC_OP_INCL,

    CC_OP_DECB, /* modify all flags except, CC_DST = res, CC_SRC = C  */
    CC_OP_DECW,
    CC_OP_DECL,

    CC_OP_SHLB, /* modify all flags, CC_DST = res, CC_SRC.lsb = C */
    CC_OP_SHLW,
    CC_OP_SHLL,

    CC_OP_SARB, /* modify all flags, CC_DST = res, CC_SRC.lsb = C */
    CC_OP_SARW,
    CC_OP_SARL,

    CC_OP_NB,
};

#ifdef __i386__
#define USE_X86LDOUBLE
#endif

#ifdef USE_X86LDOUBLE
typedef long double CPU86_LDouble;
#else
typedef double CPU86_LDouble;
#endif

typedef struct SegmentCache {
    uint32_t selector;
    uint8_t *base;
    uint32_t limit;
    uint32_t flags;
} SegmentCache;

typedef struct CPUX86State {
    /* standard registers */
    uint32_t regs[8];
    uint32_t eip;
    uint32_t eflags; /* eflags register. During CPU emulation, CC
                        flags and DF are set to zero because they are
                        stored elsewhere */

    /* emulator internal eflags handling */
    uint32_t cc_src;
    uint32_t cc_dst;
    uint32_t cc_op;
    int32_t df; /* D flag : 1 if D = 0, -1 if D = 1 */
    uint32_t hflags; /* hidden flags, see HF_xxx constants */

    /* FPU state */
    unsigned int fpstt; /* top of stack index */
    unsigned int fpus;
    unsigned int fpuc;
    uint8_t fptags[8];   /* 0 = valid, 1 = empty */
    CPU86_LDouble fpregs[8];    

    /* emulator internal variables */
    CPU86_LDouble ft0;
    union {
	float f;
        double d;
	int i32;
        int64_t i64;
    } fp_convert;
    
    /* segments */
    SegmentCache segs[6]; /* selector values */
    SegmentCache ldt;
    SegmentCache tr;
    SegmentCache gdt; /* only base and limit are used */
    SegmentCache idt; /* only base and limit are used */
    
    /* sysenter registers */
    uint32_t sysenter_cs;
    uint32_t sysenter_esp;
    uint32_t sysenter_eip;
    
    /* exception/interrupt handling */
    jmp_buf jmp_env;
    int exception_index;
    int error_code;
    int exception_is_int;
    int exception_next_eip;
    struct TranslationBlock *current_tb; /* currently executing TB */
    uint32_t cr[5]; /* NOTE: cr1 is unused */
    uint32_t dr[8]; /* debug registers */
    int interrupt_request; 
    int user_mode_only; /* user mode only simulation */

    /* soft mmu support */
    /* 0 = kernel, 1 = user */
    CPUTLBEntry tlb_read[2][CPU_TLB_SIZE];
    CPUTLBEntry tlb_write[2][CPU_TLB_SIZE];
    
    /* ice debug support */
    uint32_t breakpoints[MAX_BREAKPOINTS];
    int nb_breakpoints;
    int singlestep_enabled;

    /* user data */
    void *opaque;
} CPUX86State;

#ifndef IN_OP_I386
void cpu_x86_outb(CPUX86State *env, int addr, int val);
void cpu_x86_outw(CPUX86State *env, int addr, int val);
void cpu_x86_outl(CPUX86State *env, int addr, int val);
int cpu_x86_inb(CPUX86State *env, int addr);
int cpu_x86_inw(CPUX86State *env, int addr);
int cpu_x86_inl(CPUX86State *env, int addr);
#endif

CPUX86State *cpu_x86_init(void);
int cpu_x86_exec(CPUX86State *s);
void cpu_x86_close(CPUX86State *s);
int cpu_x86_get_pic_interrupt(CPUX86State *s);

/* this function must always be used to load data in the segment
   cache: it synchronizes the hflags with the segment cache values */
static inline void cpu_x86_load_seg_cache(CPUX86State *env, 
                                          int seg_reg, unsigned int selector,
                                          uint8_t *base, unsigned int limit, 
                                          unsigned int flags)
{
    SegmentCache *sc;
    unsigned int new_hflags;
    
    sc = &env->segs[seg_reg];
    sc->selector = selector;
    sc->base = base;
    sc->limit = limit;
    sc->flags = flags;

    /* update the hidden flags */
    new_hflags = (env->segs[R_CS].flags & DESC_B_MASK)
        >> (DESC_B_SHIFT - HF_CS32_SHIFT);
    new_hflags |= (env->segs[R_SS].flags & DESC_B_MASK)
        >> (DESC_B_SHIFT - HF_SS32_SHIFT);
    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
        /* XXX: try to avoid this test. The problem comes from the
           fact that is real mode or vm86 mode we only modify the
           'base' and 'selector' fields of the segment cache to go
           faster. A solution may be to force addseg to one in
           translate-i386.c. */
        new_hflags |= HF_ADDSEG_MASK;
    } else {
        new_hflags |= (((unsigned long)env->segs[R_DS].base | 
                        (unsigned long)env->segs[R_ES].base |
                        (unsigned long)env->segs[R_SS].base) != 0) << 
            HF_ADDSEG_SHIFT;
    }
    env->hflags = (env->hflags & 
                   ~(HF_CS32_MASK | HF_SS32_MASK | HF_ADDSEG_MASK)) | new_hflags;
}

/* wrapper, just in case memory mappings must be changed */
static inline void cpu_x86_set_cpl(CPUX86State *s, int cpl)
{
#if HF_CPL_MASK == 3
    s->hflags = (s->hflags & ~HF_CPL_MASK) | cpl;
#else
#error HF_CPL_MASK is hardcoded
#endif
}

/* the following helpers are only usable in user mode simulation as
   they can trigger unexpected exceptions */
void cpu_x86_load_seg(CPUX86State *s, int seg_reg, int selector);
void cpu_x86_fsave(CPUX86State *s, uint8_t *ptr, int data32);
void cpu_x86_frstor(CPUX86State *s, uint8_t *ptr, int data32);

/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
struct siginfo;
int cpu_x86_signal_handler(int host_signum, struct siginfo *info, 
                           void *puc);

/* MMU defines */
void cpu_x86_init_mmu(CPUX86State *env);
extern int phys_ram_size;
extern int phys_ram_fd;
extern uint8_t *phys_ram_base;
extern int a20_enabled;

void cpu_x86_set_a20(CPUX86State *env, int a20_state);

/* used to debug */
#define X86_DUMP_FPU  0x0001 /* dump FPU state too */
#define X86_DUMP_CCOP 0x0002 /* dump qemu flag cache */
void cpu_x86_dump_state(CPUX86State *env, FILE *f, int flags);

#define TARGET_PAGE_BITS 12
#include "cpu-all.h"

#endif /* CPU_I386_H */
