#ifndef CPU_I386_H
#define CPU_I386_H

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

enum {
    CC_OP_DYNAMIC, /* must use dynamic code to get cc_op */
    CC_OP_EFLAGS,  /* all cc are explicitely computed, CC_SRC = flags */
    CC_OP_MUL, /* modify all flags, C, O = (CC_SRC != 0) */

    CC_OP_ADDB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_ADDW,
    CC_OP_ADDL,

    CC_OP_SUBB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_SUBW,
    CC_OP_SUBL,

    CC_OP_LOGICB, /* modify all flags, CC_DST = res */
    CC_OP_LOGICW,
    CC_OP_LOGICL,

    CC_OP_INCB, /* modify all flags except, CC_DST = res */
    CC_OP_INCW,
    CC_OP_INCL,

    CC_OP_DECB, /* modify all flags except, CC_DST = res */
    CC_OP_DECW,
    CC_OP_DECL,

    CC_OP_SHLB, /* modify all flags, CC_DST = res, CC_SRC.lsb = C */
    CC_OP_SHLW,
    CC_OP_SHLL,

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

typedef struct CPU86State {
    /* standard registers */
    uint32_t regs[8];
    uint32_t pc; /* cs_case + eip value */

    /* eflags handling */
    uint32_t eflags;
    uint32_t cc_src;
    uint32_t cc_dst;
    uint32_t cc_op;
    int32_t df; /* D flag : 1 if D = 0, -1 if D = 1 */
    
    /* segments */
    uint8_t *segs_base[6];
    uint32_t segs[6];

    /* FPU state */
    CPU86_LDouble fpregs[8];    
    uint8_t fptags[8];   /* 0 = valid, 1 = empty */
    unsigned int fpstt; /* top of stack index */
    unsigned int fpus;
    unsigned int fpuc;

    /* emulator internal variables */
    uint32_t t0; /* temporary t0 storage */
    uint32_t t1; /* temporary t1 storage */
    uint32_t a0; /* temporary a0 storage (address) */
    CPU86_LDouble ft0;
} CPU86State;

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

static inline void stq(void *ptr, int v)
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
void port_outb(int addr, int val);
void port_outw(int addr, int val);
void port_outl(int addr, int val);
int port_inb(int addr);
int port_inw(int addr);
int port_inl(int addr);
#endif

#endif /* CPU_I386_H */
