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

#include "config.h"
#include <setjmp.h>

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

/* eflags masks */
#define CC_C   	0x0001
#define CC_P 	0x0004
#define CC_A	0x0010
#define CC_Z	0x0040
#define CC_S    0x0080
#define CC_O    0x0800

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

#define EXCP00_DIVZ	1
#define EXCP01_SSTP	2
#define EXCP02_NMI	3
#define EXCP03_INT3	4
#define EXCP04_INTO	5
#define EXCP05_BOUND	6
#define EXCP06_ILLOP	7
#define EXCP07_PREX	8
#define EXCP08_DBLE	9
#define EXCP09_XERR	10
#define EXCP0A_TSS	11
#define EXCP0B_NOSEG	12
#define EXCP0C_STACK	13
#define EXCP0D_GPF	14
#define EXCP0E_PAGE	15
#define EXCP10_COPR	17
#define EXCP11_ALGN	18
#define EXCP12_MCHK	19

#define EXCP_INTERRUPT 	256 /* async interruption */

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
    uint8_t *base;
    unsigned long limit;
    uint8_t seg_32bit;
} SegmentCache;

typedef struct SegmentDescriptorTable {
    uint8_t *base;
    unsigned long limit;
    /* this is the returned base when reading the register, just to
    avoid that the emulated program modifies it */
    unsigned long emu_base;
} SegmentDescriptorTable;

typedef struct CPUX86State {
    /* standard registers */
    uint32_t regs[8];
    uint32_t eip;
    uint32_t eflags; /* eflags register. During CPU emulation, CC
                        flags and DF are set to zero because they are
                        store elsewhere */

    /* emulator internal eflags handling */
    uint32_t cc_src;
    uint32_t cc_dst;
    uint32_t cc_op;
    int32_t df; /* D flag : 1 if D = 0, -1 if D = 1 */

    /* FPU state */
    unsigned int fpstt; /* top of stack index */
    unsigned int fpus;
    unsigned int fpuc;
    uint8_t fptags[8];   /* 0 = valid, 1 = empty */
    CPU86_LDouble fpregs[8];    

    /* emulator internal variables */
    CPU86_LDouble ft0;
    
    /* segments */
    uint32_t segs[6]; /* selector values */
    SegmentCache seg_cache[6]; /* info taken from LDT/GDT */
    SegmentDescriptorTable gdt;
    SegmentDescriptorTable ldt;
    SegmentDescriptorTable idt;
    
    /* exception/interrupt handling */
    jmp_buf jmp_env;
    int exception_index;
    int interrupt_request;

    /* user data */
    void *opaque;
} CPUX86State;

/* all CPU memory access use these macros */
static inline int ldub(void *ptr)
{
    return *(uint8_t *)ptr;
}

static inline int ldsb(void *ptr)
{
    return *(int8_t *)ptr;
}

static inline void stb(void *ptr, int v)
{
    *(uint8_t *)ptr = v;
}

#ifdef WORDS_BIGENDIAN

/* conservative code for little endian unaligned accesses */
static inline int lduw(void *ptr)
{
#ifdef __powerpc__
    int val;
    __asm__ __volatile__ ("lhbrx %0,0,%1" : "=r" (val) : "r" (ptr));
    return val;
#else
    uint8_t *p = ptr;
    return p[0] | (p[1] << 8);
#endif
}

static inline int ldsw(void *ptr)
{
#ifdef __powerpc__
    int val;
    __asm__ __volatile__ ("lhbrx %0,0,%1" : "=r" (val) : "r" (ptr));
    return (int16_t)val;
#else
    uint8_t *p = ptr;
    return (int16_t)(p[0] | (p[1] << 8));
#endif
}

static inline int ldl(void *ptr)
{
#ifdef __powerpc__
    int val;
    __asm__ __volatile__ ("lwbrx %0,0,%1" : "=r" (val) : "r" (ptr));
    return val;
#else
    uint8_t *p = ptr;
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
#endif
}

static inline uint64_t ldq(void *ptr)
{
    uint8_t *p = ptr;
    uint32_t v1, v2;
    v1 = ldl(p);
    v2 = ldl(p + 4);
    return v1 | ((uint64_t)v2 << 32);
}

static inline void stw(void *ptr, int v)
{
#ifdef __powerpc__
    __asm__ __volatile__ ("sthbrx %1,0,%2" : "=m" (*(uint16_t *)ptr) : "r" (v), "r" (ptr));
#else
    uint8_t *p = ptr;
    p[0] = v;
    p[1] = v >> 8;
#endif
}

static inline void stl(void *ptr, int v)
{
#ifdef __powerpc__
    __asm__ __volatile__ ("stwbrx %1,0,%2" : "=m" (*(uint32_t *)ptr) : "r" (v), "r" (ptr));
#else
    uint8_t *p = ptr;
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
#endif
}

static inline void stq(void *ptr, uint64_t v)
{
    uint8_t *p = ptr;
    stl(p, (uint32_t)v);
    stl(p + 4, v >> 32);
}

/* float access */

static inline float ldfl(void *ptr)
{
    union {
        float f;
        uint32_t i;
    } u;
    u.i = ldl(ptr);
    return u.f;
}

static inline double ldfq(void *ptr)
{
    union {
        double d;
        uint64_t i;
    } u;
    u.i = ldq(ptr);
    return u.d;
}

static inline void stfl(void *ptr, float v)
{
    union {
        float f;
        uint32_t i;
    } u;
    u.f = v;
    stl(ptr, u.i);
}

static inline void stfq(void *ptr, double v)
{
    union {
        double d;
        uint64_t i;
    } u;
    u.d = v;
    stq(ptr, u.i);
}

#else

static inline int lduw(void *ptr)
{
    return *(uint16_t *)ptr;
}

static inline int ldsw(void *ptr)
{
    return *(int16_t *)ptr;
}

static inline int ldl(void *ptr)
{
    return *(uint32_t *)ptr;
}

static inline uint64_t ldq(void *ptr)
{
    return *(uint64_t *)ptr;
}

static inline void stw(void *ptr, int v)
{
    *(uint16_t *)ptr = v;
}

static inline void stl(void *ptr, int v)
{
    *(uint32_t *)ptr = v;
}

static inline void stq(void *ptr, uint64_t v)
{
    *(uint64_t *)ptr = v;
}

/* float access */

static inline float ldfl(void *ptr)
{
    return *(float *)ptr;
}

static inline double ldfq(void *ptr)
{
    return *(double *)ptr;
}

static inline void stfl(void *ptr, float v)
{
    *(float *)ptr = v;
}

static inline void stfq(void *ptr, double v)
{
    *(double *)ptr = v;
}
#endif

#ifndef IN_OP_I386
void cpu_x86_outb(int addr, int val);
void cpu_x86_outw(int addr, int val);
void cpu_x86_outl(int addr, int val);
int cpu_x86_inb(int addr);
int cpu_x86_inw(int addr);
int cpu_x86_inl(int addr);
#endif

CPUX86State *cpu_x86_init(void);
int cpu_x86_exec(CPUX86State *s);
void cpu_x86_interrupt(CPUX86State *s);
void cpu_x86_close(CPUX86State *s);

/* needed to load some predefinied segment registers */
void cpu_x86_load_seg(CPUX86State *s, int seg_reg, int selector);

/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
struct siginfo;
int cpu_x86_signal_handler(int host_signum, struct siginfo *info, 
                           void *puc);

/* internal functions */

#define GEN_FLAG_CODE32_SHIFT 0
#define GEN_FLAG_ADDSEG_SHIFT 1
#define GEN_FLAG_SS32_SHIFT   2
#define GEN_FLAG_VM_SHIFT     3
#define GEN_FLAG_ST_SHIFT     4

int cpu_x86_gen_code(uint8_t *gen_code_buf, int max_code_size, 
                     int *gen_code_size_ptr,
                     uint8_t *pc_start,  uint8_t *cs_base, int flags);
void cpu_x86_tblocks_init(void);

#endif /* CPU_I386_H */
