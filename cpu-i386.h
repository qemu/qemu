/* NOTE: this header is included in op-i386.c where global register
   variable are used. Care must be used when including glibc headers.
 */
#ifndef CPU_I386_H
#define CPU_I386_H

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

#define CC_C   	0x0001
#define CC_P 	0x0004
#define CC_A	0x0010
#define CC_Z	0x0040
#define CC_S    0x0080
#define CC_O    0x0800

#define TRAP_FLAG		0x0100
#define INTERRUPT_FLAG		0x0200
#define DIRECTION_FLAG		0x0400
#define IOPL_FLAG_MASK		0x3000
#define NESTED_FLAG		0x4000
#define BYTE_FL			0x8000	/* Intel reserved! */
#define RF_FLAG			0x10000
#define VM_FLAG			0x20000
/* AC				0x40000 */

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

#define EXCP_SIGNAL	256 /* async signal */

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

typedef struct CPUX86State {
    /* standard registers */
    uint32_t regs[8];
    uint32_t pc; /* cs_case + eip value */
    uint32_t eflags;

    /* emulator internal eflags handling */
    uint32_t cc_src;
    uint32_t cc_dst;
    uint32_t cc_op;
    int32_t df; /* D flag : 1 if D = 0, -1 if D = 1 */

    /* segments */
    uint8_t *segs_base[6];

    /* FPU state */
    unsigned int fpstt; /* top of stack index */
    unsigned int fpus;
    unsigned int fpuc;
    uint8_t fptags[8];   /* 0 = valid, 1 = empty */
    CPU86_LDouble fpregs[8];    

    /* segments */
    uint32_t segs[6];

    /* emulator internal variables */
    CPU86_LDouble ft0;
    
    /* exception handling */
    jmp_buf jmp_env;
    int exception_index;
} CPUX86State;

static inline int ldub(void *ptr)
{
    return *(uint8_t *)ptr;
}

static inline int ldsb(void *ptr)
{
    return *(int8_t *)ptr;
}

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

static inline void stb(void *ptr, int v)
{
    *(uint8_t *)ptr = v;
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
void cpu_x86_close(CPUX86State *s);

/* internal functions */
int cpu_x86_gen_code(uint8_t *gen_code_buf, int max_code_size, 
                     int *gen_code_size_ptr, uint8_t *pc_start);
void cpu_x86_tblocks_init(void);

#endif /* CPU_I386_H */
