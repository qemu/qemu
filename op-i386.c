typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

#define NULL 0

#ifdef __i386__
register int T0 asm("esi");
register int T1 asm("ebx");
register int A0 asm("edi");
register struct CPU86State *env asm("ebp");
#define FORCE_RET() asm volatile ("ret");
#endif
#ifdef __powerpc__
register int T0 asm("r24");
register int T1 asm("r25");
register int A0 asm("r26");
register struct CPU86State *env asm("r27");
#define FORCE_RET() asm volatile ("blr");
#endif
#ifdef __arm__
register int T0 asm("r4");
register int T1 asm("r5");
register int A0 asm("r6");
register struct CPU86State *env asm("r7");
#define FORCE_RET() asm volatile ("mov pc, lr");
#endif
#ifdef __mips__
register int T0 asm("s0");
register int T1 asm("s1");
register int A0 asm("s2");
register struct CPU86State *env asm("s3");
#define FORCE_RET() asm volatile ("jr $31");
#endif
#ifdef __sparc__
register int T0 asm("l0");
register int T1 asm("l1");
register int A0 asm("l2");
register struct CPU86State *env asm("l3");
#define FORCE_RET() asm volatile ("retl ; nop");
#endif

#ifndef OPPROTO
#define OPPROTO
#endif

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)

#define EAX (env->regs[R_EAX])
#define ECX (env->regs[R_ECX])
#define EDX (env->regs[R_EDX])
#define EBX (env->regs[R_EBX])
#define ESP (env->regs[R_ESP])
#define EBP (env->regs[R_EBP])
#define ESI (env->regs[R_ESI])
#define EDI (env->regs[R_EDI])
#define PC  (env->pc)
#define DF  (env->df)

#define CC_SRC (env->cc_src)
#define CC_DST (env->cc_dst)
#define CC_OP  (env->cc_op)

/* float macros */
#define FT0    (env->ft0)
#define ST0    (env->fpregs[env->fpstt])
#define ST(n)  (env->fpregs[(env->fpstt + (n)) & 7])
#define ST1    ST(1)

extern int __op_param1, __op_param2, __op_param3;
#define PARAM1 ((long)(&__op_param1))
#define PARAM2 ((long)(&__op_param2))
#define PARAM3 ((long)(&__op_param3))

#include "cpu-i386.h"

typedef struct CCTable {
    int (*compute_all)(void); /* return all the flags */
    int (*compute_c)(void);  /* return the C flag */
} CCTable;

extern CCTable cc_table[];

uint8_t parity_table[256] = {
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
};

/* modulo 17 table */
const uint8_t rclw_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 
    8, 9,10,11,12,13,14,15,
   16, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 9,10,11,12,13,14,
};

/* modulo 9 table */
const uint8_t rclb_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 
    8, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 0, 1, 2, 3, 4, 5, 
    6, 7, 8, 0, 1, 2, 3, 4,
};

#ifdef USE_X86LDOUBLE
/* an array of Intel 80-bit FP constants, to be loaded via integer ops */
typedef unsigned short f15ld[5];
const f15ld f15rk[] =
{
/*0*/	{0x0000,0x0000,0x0000,0x0000,0x0000},
/*1*/	{0x0000,0x0000,0x0000,0x8000,0x3fff},
/*pi*/	{0xc235,0x2168,0xdaa2,0xc90f,0x4000},
/*lg2*/	{0xf799,0xfbcf,0x9a84,0x9a20,0x3ffd},
/*ln2*/	{0x79ac,0xd1cf,0x17f7,0xb172,0x3ffe},
/*l2e*/	{0xf0bc,0x5c17,0x3b29,0xb8aa,0x3fff},
/*l2t*/	{0x8afe,0xcd1b,0x784b,0xd49a,0x4000}
};
#else
/* the same, 64-bit version */
typedef unsigned short f15ld[4];
const f15ld f15rk[] =
{
#ifndef WORDS_BIGENDIAN
/*0*/	{0x0000,0x0000,0x0000,0x0000},
/*1*/	{0x0000,0x0000,0x0000,0x3ff0},
/*pi*/	{0x2d18,0x5444,0x21fb,0x4009},
/*lg2*/	{0x79ff,0x509f,0x4413,0x3fd3},
/*ln2*/	{0x39ef,0xfefa,0x2e42,0x3fe6},
/*l2e*/	{0x82fe,0x652b,0x1547,0x3ff7},
/*l2t*/	{0xa371,0x0979,0x934f,0x400a}
#else
/*0*/   {0x0000,0x0000,0x0000,0x0000},
/*1*/   {0x3ff0,0x0000,0x0000,0x0000},
/*pi*/  {0x4009,0x21fb,0x5444,0x2d18},
/*lg2*/	{0x3fd3,0x4413,0x509f,0x79ff},
/*ln2*/	{0x3fe6,0x2e42,0xfefa,0x39ef},
/*l2e*/	{0x3ff7,0x1547,0x652b,0x82fe},
/*l2t*/	{0x400a,0x934f,0x0979,0xa371}
#endif
};
#endif
    
/* n must be a constant to be efficient */
static inline int lshift(int x, int n)
{
    if (n >= 0)
        return x << n;
    else
        return x >> (-n);
}

/* we define the various pieces of code used by the JIT */

#define REG EAX
#define REGNAME _EAX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG ECX
#define REGNAME _ECX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EDX
#define REGNAME _EDX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EBX
#define REGNAME _EBX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG ESP
#define REGNAME _ESP
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EBP
#define REGNAME _EBP
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG ESI
#define REGNAME _ESI
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EDI
#define REGNAME _EDI
#include "opreg_template.h"
#undef REG
#undef REGNAME

/* operations */

void OPPROTO op_addl_T0_T1_cc(void)
{
    CC_SRC = T0;
    T0 += T1;
    CC_DST = T0;
}

void OPPROTO op_orl_T0_T1_cc(void)
{
    T0 |= T1;
    CC_DST = T0;
}

void OPPROTO op_adcl_T0_T1_cc(void)
{
    CC_SRC = T0;
    T0 = T0 + T1 + cc_table[CC_OP].compute_c();
    CC_DST = T0;
}

void OPPROTO op_sbbl_T0_T1_cc(void)
{
    CC_SRC = T0;
    T0 = T0 - T1 - cc_table[CC_OP].compute_c();
    CC_DST = T0;
}

void OPPROTO op_andl_T0_T1_cc(void)
{
    T0 &= T1;
    CC_DST = T0;
}

void OPPROTO op_subl_T0_T1_cc(void)
{
    CC_SRC = T0;
    T0 -= T1;
    CC_DST = T0;
}

void OPPROTO op_xorl_T0_T1_cc(void)
{
    T0 ^= T1;
    CC_DST = T0;
}

void OPPROTO op_cmpl_T0_T1_cc(void)
{
    CC_SRC = T0;
    CC_DST = T0 - T1;
}

void OPPROTO op_notl_T0(void)
{
    T0 = ~T0;
}

void OPPROTO op_negl_T0_cc(void)
{
    CC_SRC = 0;
    T0 = -T0;
    CC_DST = T0;
}

void OPPROTO op_incl_T0_cc(void)
{
    T0++;
    CC_DST = T0;
}

void OPPROTO op_decl_T0_cc(void)
{
    T0--;
    CC_DST = T0;
}

void OPPROTO op_testl_T0_T1_cc(void)
{
    CC_SRC = T0;
    CC_DST = T0 & T1;
}

/* multiply/divide */
void OPPROTO op_mulb_AL_T0(void)
{
    unsigned int res;
    res = (uint8_t)EAX * (uint8_t)T0;
    EAX = (EAX & 0xffff0000) | res;
    CC_SRC = (res & 0xff00);
}

void OPPROTO op_imulb_AL_T0(void)
{
    int res;
    res = (int8_t)EAX * (int8_t)T0;
    EAX = (EAX & 0xffff0000) | (res & 0xffff);
    CC_SRC = (res != (int8_t)res);
}

void OPPROTO op_mulw_AX_T0(void)
{
    unsigned int res;
    res = (uint16_t)EAX * (uint16_t)T0;
    EAX = (EAX & 0xffff0000) | (res & 0xffff);
    EDX = (EDX & 0xffff0000) | ((res >> 16) & 0xffff);
    CC_SRC = res >> 16;
}

void OPPROTO op_imulw_AX_T0(void)
{
    int res;
    res = (int16_t)EAX * (int16_t)T0;
    EAX = (EAX & 0xffff0000) | (res & 0xffff);
    EDX = (EDX & 0xffff0000) | ((res >> 16) & 0xffff);
    CC_SRC = (res != (int16_t)res);
}

void OPPROTO op_mull_EAX_T0(void)
{
    uint64_t res;
    res = (uint64_t)((uint32_t)EAX) * (uint64_t)((uint32_t)T0);
    EAX = res;
    EDX = res >> 32;
    CC_SRC = res >> 32;
}

void OPPROTO op_imull_EAX_T0(void)
{
    int64_t res;
    res = (int64_t)((int32_t)EAX) * (int64_t)((int32_t)T0);
    EAX = res;
    EDX = res >> 32;
    CC_SRC = (res != (int32_t)res);
}

void OPPROTO op_imulw_T0_T1(void)
{
    int res;
    res = (int16_t)T0 * (int16_t)T1;
    T0 = res;
    CC_SRC = (res != (int16_t)res);
}

void OPPROTO op_imull_T0_T1(void)
{
    int64_t res;
    res = (int64_t)((int32_t)EAX) * (int64_t)((int32_t)T1);
    T0 = res;
    CC_SRC = (res != (int32_t)res);
}

/* division, flags are undefined */
/* XXX: add exceptions for overflow & div by zero */
void OPPROTO op_divb_AL_T0(void)
{
    unsigned int num, den, q, r;

    num = (EAX & 0xffff);
    den = (T0 & 0xff);
    q = (num / den) & 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & 0xffff0000) | (r << 8) | q;
}

void OPPROTO op_idivb_AL_T0(void)
{
    int num, den, q, r;

    num = (int16_t)EAX;
    den = (int8_t)T0;
    q = (num / den) & 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & 0xffff0000) | (r << 8) | q;
}

void OPPROTO op_divw_AX_T0(void)
{
    unsigned int num, den, q, r;

    num = (EAX & 0xffff) | ((EDX & 0xffff) << 16);
    den = (T0 & 0xffff);
    q = (num / den) & 0xffff;
    r = (num % den) & 0xffff;
    EAX = (EAX & 0xffff0000) | q;
    EDX = (EDX & 0xffff0000) | r;
}

void OPPROTO op_idivw_AX_T0(void)
{
    int num, den, q, r;

    num = (EAX & 0xffff) | ((EDX & 0xffff) << 16);
    den = (int16_t)T0;
    q = (num / den) & 0xffff;
    r = (num % den) & 0xffff;
    EAX = (EAX & 0xffff0000) | q;
    EDX = (EDX & 0xffff0000) | r;
}

void OPPROTO op_divl_EAX_T0(void)
{
    unsigned int den, q, r;
    uint64_t num;
    
    num = EAX | ((uint64_t)EDX << 32);
    den = T0;
    q = (num / den);
    r = (num % den);
    EAX = q;
    EDX = r;
}

void OPPROTO op_idivl_EAX_T0(void)
{
    int den, q, r;
    int16_t num;
    
    num = EAX | ((uint64_t)EDX << 32);
    den = (int16_t)T0;
    q = (num / den);
    r = (num % den);
    EAX = q;
    EDX = r;
}

/* constant load */

void OPPROTO op1_movl_T0_im(void)
{
    T0 = PARAM1;
}

void OPPROTO op1_movl_T1_im(void)
{
    T1 = PARAM1;
}

void OPPROTO op1_movl_A0_im(void)
{
    A0 = PARAM1;
}

/* memory access */

void OPPROTO op_ldub_T0_A0(void)
{
    T0 = ldub((uint8_t *)A0);
}

void OPPROTO op_ldsb_T0_A0(void)
{
    T0 = ldsb((int8_t *)A0);
}

void OPPROTO op_lduw_T0_A0(void)
{
    T0 = lduw((uint8_t *)A0);
}

void OPPROTO op_ldsw_T0_A0(void)
{
    T0 = ldsw((int8_t *)A0);
}

void OPPROTO op_ldl_T0_A0(void)
{
    T0 = ldl((uint8_t *)A0);
}

void OPPROTO op_ldub_T1_A0(void)
{
    T1 = ldub((uint8_t *)A0);
}

void OPPROTO op_ldsb_T1_A0(void)
{
    T1 = ldsb((int8_t *)A0);
}

void OPPROTO op_lduw_T1_A0(void)
{
    T1 = lduw((uint8_t *)A0);
}

void OPPROTO op_ldsw_T1_A0(void)
{
    T1 = ldsw((int8_t *)A0);
}

void OPPROTO op_ldl_T1_A0(void)
{
    T1 = ldl((uint8_t *)A0);
}

void OPPROTO op_stb_T0_A0(void)
{
    stb((uint8_t *)A0, T0);
}

void OPPROTO op_stw_T0_A0(void)
{
    stw((uint8_t *)A0, T0);
}

void OPPROTO op_stl_T0_A0(void)
{
    stl((uint8_t *)A0, T0);
}

/* jumps */

/* indirect jump */
void OPPROTO op_jmp_T0(void)
{
    PC = T0;
}

void OPPROTO op_jmp_im(void)
{
    PC = PARAM1;
}

/* string ops */

#define ldul ldl

#define SHIFT 0
#include "ops_template.h"
#undef SHIFT

#define SHIFT 1
#include "ops_template.h"
#undef SHIFT

#define SHIFT 2
#include "ops_template.h"
#undef SHIFT

/* sign extend */

void OPPROTO op_movsbl_T0_T0(void)
{
    T0 = (int8_t)T0;
}

void OPPROTO op_movzbl_T0_T0(void)
{
    T0 = (uint8_t)T0;
}

void OPPROTO op_movswl_T0_T0(void)
{
    T0 = (int16_t)T0;
}

void OPPROTO op_movzwl_T0_T0(void)
{
    T0 = (uint16_t)T0;
}

void OPPROTO op_movswl_EAX_AX(void)
{
    EAX = (int16_t)EAX;
}

void OPPROTO op_movsbw_AX_AL(void)
{
    EAX = (EAX & 0xffff0000) | ((int8_t)EAX & 0xffff);
}

void OPPROTO op_movslq_EDX_EAX(void)
{
    EDX = (int32_t)EAX >> 31;
}

void OPPROTO op_movswl_DX_AX(void)
{
    EDX = (EDX & 0xffff0000) | (((int16_t)EAX >> 15) & 0xffff);
}

/* push/pop */
/* XXX: add 16 bit operand/16 bit seg variants */

void op_pushl_T0(void)
{
    uint32_t offset;
    offset = ESP - 4;
    stl((void *)offset, T0);
    /* modify ESP after to handle exceptions correctly */
    ESP = offset;
}

void op_pushl_T1(void)
{
    uint32_t offset;
    offset = ESP - 4;
    stl((void *)offset, T1);
    /* modify ESP after to handle exceptions correctly */
    ESP = offset;
}

void op_popl_T0(void)
{
    T0 = ldl((void *)ESP);
    ESP += 4;
}

void op_addl_ESP_im(void)
{
    ESP += PARAM1;
}

/* flags handling */

/* slow jumps cases (compute x86 flags) */
void OPPROTO op_jo_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_O)
        PC += PARAM1;
    else
        PC += PARAM2;
}

void OPPROTO op_jb_cc(void)
{
    if (cc_table[CC_OP].compute_c())
        PC += PARAM1;
    else
        PC += PARAM2;
}

void OPPROTO op_jz_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_Z)
        PC += PARAM1;
    else
        PC += PARAM2;
}

void OPPROTO op_jbe_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & (CC_Z | CC_C))
        PC += PARAM1;
    else
        PC += PARAM2;
}

void OPPROTO op_js_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_S)
        PC += PARAM1;
    else
        PC += PARAM2;
}

void OPPROTO op_jp_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_P)
        PC += PARAM1;
    else
        PC += PARAM2;
}

void OPPROTO op_jl_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if ((eflags ^ (eflags >> 4)) & 0x80)
        PC += PARAM1;
    else
        PC += PARAM2;
}

void OPPROTO op_jle_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (((eflags ^ (eflags >> 4)) & 0x80) || (eflags & CC_Z))
        PC += PARAM1;
    else
        PC += PARAM2;
}

/* slow set cases (compute x86 flags) */
void OPPROTO op_seto_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 11) & 1;
}

void OPPROTO op_setb_T0_cc(void)
{
    T0 = cc_table[CC_OP].compute_c();
}

void OPPROTO op_setz_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 6) & 1;
}

void OPPROTO op_setbe_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags & (CC_Z | CC_C)) != 0;
}

void OPPROTO op_sets_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 7) & 1;
}

void OPPROTO op_setp_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 2) & 1;
}

void OPPROTO op_setl_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = ((eflags ^ (eflags >> 4)) >> 7) & 1;
}

void OPPROTO op_setle_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (((eflags ^ (eflags >> 4)) & 0x80) || (eflags & CC_Z)) != 0;
}

void OPPROTO op_xor_T0_1(void)
{
    T0 ^= 1;
}

void OPPROTO op_set_cc_op(void)
{
    CC_OP = PARAM1;
}

void OPPROTO op_movl_eflags_T0(void)
{
    CC_SRC = T0;
    DF = 1 - (2 * ((T0 >> 10) & 1));
}

/* XXX: compute only O flag */
void OPPROTO op_movb_eflags_T0(void)
{
    int of;
    of = cc_table[CC_OP].compute_all() & CC_O;
    CC_SRC = T0 | of;
}

void OPPROTO op_movl_T0_eflags(void)
{
    T0 = cc_table[CC_OP].compute_all();
    T0 |= (DF & DIRECTION_FLAG);
}

void OPPROTO op_cld(void)
{
    DF = 1;
}

void OPPROTO op_std(void)
{
    DF = -1;
}

void OPPROTO op_clc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags &= ~CC_C;
    CC_SRC = eflags;
}

void OPPROTO op_stc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= CC_C;
    CC_SRC = eflags;
}

void OPPROTO op_cmc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags ^= CC_C;
    CC_SRC = eflags;
}

static int compute_all_eflags(void)
{
    return CC_SRC;
}

static int compute_c_eflags(void)
{
    return CC_SRC & CC_C;
}

static int compute_c_mul(void)
{
    int cf;
    cf = (CC_SRC != 0);
    return cf;
}

static int compute_all_mul(void)
{
    int cf, pf, af, zf, sf, of;
    cf = (CC_SRC != 0);
    pf = 0; /* undefined */
    af = 0; /* undefined */
    zf = 0; /* undefined */
    sf = 0; /* undefined */
    of = cf << 11;
    return cf | pf | af | zf | sf | of;
}
    
CCTable cc_table[CC_OP_NB] = {
    [CC_OP_DYNAMIC] = { /* should never happen */ },

    [CC_OP_EFLAGS] = { compute_all_eflags, compute_c_eflags },

    [CC_OP_MUL] = { compute_all_mul, compute_c_mul },

    [CC_OP_ADDB] = { compute_all_addb, compute_c_addb },
    [CC_OP_ADDW] = { compute_all_addw, compute_c_addw  },
    [CC_OP_ADDL] = { compute_all_addl, compute_c_addl  },

    [CC_OP_SUBB] = { compute_all_subb, compute_c_subb  },
    [CC_OP_SUBW] = { compute_all_subw, compute_c_subw  },
    [CC_OP_SUBL] = { compute_all_subl, compute_c_subl  },
    
    [CC_OP_LOGICB] = { compute_all_logicb, compute_c_logicb },
    [CC_OP_LOGICW] = { compute_all_logicw, compute_c_logicw },
    [CC_OP_LOGICL] = { compute_all_logicl, compute_c_logicl },
    
    [CC_OP_INCB] = { compute_all_incb, compute_c_incb },
    [CC_OP_INCW] = { compute_all_incw, compute_c_incw },
    [CC_OP_INCL] = { compute_all_incl, compute_c_incl },
    
    [CC_OP_DECB] = { compute_all_decb, compute_c_incb },
    [CC_OP_DECW] = { compute_all_decw, compute_c_incw },
    [CC_OP_DECL] = { compute_all_decl, compute_c_incl },
    
    [CC_OP_SHLB] = { compute_all_shlb, compute_c_shlb },
    [CC_OP_SHLW] = { compute_all_shlw, compute_c_shlw },
    [CC_OP_SHLL] = { compute_all_shll, compute_c_shll },
};

/* floating point support */

#ifdef USE_X86LDOUBLE
/* use long double functions */
#define lrint lrintl
#define llrint llrintl
#define fabs fabsl
#define sin sinl
#define cos cosl
#define sqrt sqrtl
#define pow powl
#define log logl
#define tan tanl
#define atan2 atan2l
#define floor floorl
#define ceil ceill
#define rint rintl
#endif

extern int lrint(CPU86_LDouble x);
extern int64_t llrint(CPU86_LDouble x);
extern CPU86_LDouble fabs(CPU86_LDouble x);
extern CPU86_LDouble sin(CPU86_LDouble x);
extern CPU86_LDouble cos(CPU86_LDouble x);
extern CPU86_LDouble sqrt(CPU86_LDouble x);
extern CPU86_LDouble pow(CPU86_LDouble, CPU86_LDouble);
extern CPU86_LDouble log(CPU86_LDouble x);
extern CPU86_LDouble tan(CPU86_LDouble x);
extern CPU86_LDouble atan2(CPU86_LDouble, CPU86_LDouble);
extern CPU86_LDouble floor(CPU86_LDouble x);
extern CPU86_LDouble ceil(CPU86_LDouble x);
extern CPU86_LDouble rint(CPU86_LDouble x);

#define RC_MASK         0xc00
#define RC_NEAR		0x000
#define RC_DOWN		0x400
#define RC_UP		0x800
#define RC_CHOP		0xc00

#define MAXTAN 9223372036854775808.0

#ifdef USE_X86LDOUBLE

/* only for x86 */
typedef union {
    long double d;
    struct {
        unsigned long long lower;
        unsigned short upper;
    } l;
} CPU86_LDoubleU;

/* the following deal with x86 long double-precision numbers */
#define MAXEXPD 0x7fff
#define EXPBIAS 16383
#define EXPD(fp)	(fp.l.upper & 0x7fff)
#define SIGND(fp)	((fp.l.upper) & 0x8000)
#define MANTD(fp)       (fp.l.lower)
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7fff)) | EXPBIAS

#else

typedef {
    double d;
#ifndef WORDS_BIGENDIAN
    struct {
        unsigned long lower;
        long upper;
    } l;
#else
    struct {
        long upper;
        unsigned long lower;
    } l;
#endif
    long long ll;
} CPU86_LDoubleU;

/* the following deal with IEEE double-precision numbers */
#define MAXEXPD 0x7ff
#define EXPBIAS 1023
#define EXPD(fp)	(((fp.l.upper) >> 20) & 0x7FF)
#define SIGND(fp)	((fp.l.upper) & 0x80000000)
#define MANTD(fp)	(fp.ll & ((1LL << 52) - 1))
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7ff << 20)) | (EXPBIAS << 20)
#endif

/* fp load FT0 */

void OPPROTO op_flds_FT0_A0(void)
{
    FT0 = ldfl((void *)A0);
}

void OPPROTO op_fldl_FT0_A0(void)
{
    FT0 = ldfq((void *)A0);
}

void OPPROTO op_fild_FT0_A0(void)
{
    FT0 = (CPU86_LDouble)ldsw((void *)A0);
}

void OPPROTO op_fildl_FT0_A0(void)
{
    FT0 = (CPU86_LDouble)((int32_t)ldl((void *)A0));
}

void OPPROTO op_fildll_FT0_A0(void)
{
    FT0 = (CPU86_LDouble)((int64_t)ldq((void *)A0));
}

/* fp load ST0 */

void OPPROTO op_flds_ST0_A0(void)
{
    ST0 = ldfl((void *)A0);
}

void OPPROTO op_fldl_ST0_A0(void)
{
    ST0 = ldfq((void *)A0);
}

void OPPROTO op_fild_ST0_A0(void)
{
    ST0 = (CPU86_LDouble)ldsw((void *)A0);
}

void OPPROTO op_fildl_ST0_A0(void)
{
    ST0 = (CPU86_LDouble)((int32_t)ldl((void *)A0));
}

void OPPROTO op_fildll_ST0_A0(void)
{
    ST0 = (CPU86_LDouble)((int64_t)ldq((void *)A0));
}

/* fp store */

void OPPROTO op_fsts_ST0_A0(void)
{
    stfl((void *)A0, (float)ST0);
}

void OPPROTO op_fstl_ST0_A0(void)
{
    ST0 = ldfq((void *)A0);
}

void OPPROTO op_fist_ST0_A0(void)
{
    int val;
    val = lrint(ST0);
    stw((void *)A0, val);
}

void OPPROTO op_fistl_ST0_A0(void)
{
    int val;
    val = lrint(ST0);
    stl((void *)A0, val);
}

void OPPROTO op_fistll_ST0_A0(void)
{
    int64_t val;
    val = llrint(ST0);
    stq((void *)A0, val);
}

/* FPU move */

static inline void fpush(void)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fptags[env->fpstt] = 0; /* validate stack entry */
}

static inline void fpop(void)
{
    env->fptags[env->fpstt] = 1; /* invvalidate stack entry */
    env->fpstt = (env->fpstt + 1) & 7;
}

void OPPROTO op_fpush(void)
{
    fpush();
}

void OPPROTO op_fpop(void)
{
    fpop();
}

void OPPROTO op_fdecstp(void)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fpus &= (~0x4700);
}

void OPPROTO op_fincstp(void)
{
    env->fpstt = (env->fpstt + 1) & 7;
    env->fpus &= (~0x4700);
}

void OPPROTO op_fmov_ST0_FT0(void)
{
    ST0 = FT0;
}

void OPPROTO op_fmov_FT0_STN(void)
{
    FT0 = ST(PARAM1);
}

void OPPROTO op_fmov_ST0_STN(void)
{
    ST0 = ST(PARAM1);
}

void OPPROTO op_fmov_STN_ST0(void)
{
    ST(PARAM1) = ST0;
}

void OPPROTO op_fxchg_ST0_STN(void)
{
    CPU86_LDouble tmp;
    tmp = ST(PARAM1);
    ST(PARAM1) = ST0;
    ST0 = tmp;
}

/* FPU operations */

/* XXX: handle nans */
void OPPROTO op_fcom_ST0_FT0(void)
{
    env->fpus &= (~0x4500);	/* (C3,C2,C0) <-- 000 */
    if (ST0 < FT0)
        env->fpus |= 0x100;	/* (C3,C2,C0) <-- 001 */
    else if (ST0 == FT0)
        env->fpus |= 0x4000; /* (C3,C2,C0) <-- 100 */
    FORCE_RET();
}

void OPPROTO op_fadd_ST0_FT0(void)
{
    ST0 += FT0;
}

void OPPROTO op_fmul_ST0_FT0(void)
{
    ST0 *= FT0;
}

void OPPROTO op_fsub_ST0_FT0(void)
{
    ST0 -= FT0;
}

void OPPROTO op_fsubr_ST0_FT0(void)
{
    ST0 = FT0 - ST0;
}

void OPPROTO op_fdiv_ST0_FT0(void)
{
    ST0 /= FT0;
}

void OPPROTO op_fdivr_ST0_FT0(void)
{
    ST0 = FT0 / ST0;
}

/* fp operations between STN and ST0 */

void OPPROTO op_fadd_STN_ST0(void)
{
    ST(PARAM1) += ST0;
}

void OPPROTO op_fmul_STN_ST0(void)
{
    ST(PARAM1) *= ST0;
}

void OPPROTO op_fsub_STN_ST0(void)
{
    ST(PARAM1) -= ST0;
}

void OPPROTO op_fsubr_STN_ST0(void)
{
    CPU86_LDouble *p;
    p = &ST(PARAM1);
    *p = ST0 - *p;
}

void OPPROTO op_fdiv_STN_ST0(void)
{
    ST(PARAM1) /= ST0;
}

void OPPROTO op_fdivr_STN_ST0(void)
{
    CPU86_LDouble *p;
    p = &ST(PARAM1);
    *p = ST0 / *p;
}

/* misc FPU operations */
void OPPROTO op_fchs_ST0(void)
{
    ST0 = -ST0;
}

void OPPROTO op_fabs_ST0(void)
{
    ST0 = fabs(ST0);
}

void OPPROTO op_fxam_ST0(void)
{
    CPU86_LDoubleU temp;
    int expdif;

    temp.d = ST0;

    env->fpus &= (~0x4700);  /* (C3,C2,C1,C0) <-- 0000 */
    if (SIGND(temp))
        env->fpus |= 0x200; /* C1 <-- 1 */

    expdif = EXPD(temp);
    if (expdif == MAXEXPD) {
        if (MANTD(temp) == 0)
            env->fpus |=  0x500 /*Infinity*/;
        else
            env->fpus |=  0x100 /*NaN*/;
    } else if (expdif == 0) {
        if (MANTD(temp) == 0)
            env->fpus |=  0x4000 /*Zero*/;
        else
            env->fpus |= 0x4400 /*Denormal*/;
    } else {
        env->fpus |= 0x400;
    }
    FORCE_RET();
}

void OPPROTO op_fld1_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[1];
}

void OPPROTO op_fld2t_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[6];
}

void OPPROTO op_fld2e_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[5];
}

void OPPROTO op_fldpi_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[2];
}

void OPPROTO op_fldlg2_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[3];
}

void OPPROTO op_fldln2_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[4];
}

void OPPROTO op_fldz_ST0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[0];
}

void OPPROTO op_fldz_FT0(void)
{
    ST0 = *(CPU86_LDouble *)&f15rk[0];
}

void helper_f2xm1(void)
{
    ST0 = pow(2.0,ST0) - 1.0;
}

void helper_fyl2x(void)
{
    CPU86_LDouble fptemp;
    
    fptemp = ST0;
    if (fptemp>0.0){
        fptemp = log(fptemp)/log(2.0);	 /* log2(ST) */
        ST1 *= fptemp;
        fpop();
    } else { 
        env->fpus &= (~0x4700);
        env->fpus |= 0x400;
    }
}

void helper_fptan(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = tan(fptemp);
        fpush();
        ST0 = 1.0;
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg| < 2**52 only */
    }
}

void helper_fpatan(void)
{
    CPU86_LDouble fptemp, fpsrcop;

    fpsrcop = ST1;
    fptemp = ST0;
    ST1 = atan2(fpsrcop,fptemp);
    fpop();
}

void helper_fxtract(void)
{
    CPU86_LDoubleU temp;
    unsigned int expdif;

    temp.d = ST0;
    expdif = EXPD(temp) - EXPBIAS;
    /*DP exponent bias*/
    ST0 = expdif;
    fpush();
    BIASEXPONENT(temp);
    ST0 = temp.d;
}

void helper_fprem1(void)
{
    CPU86_LDouble dblq, fpsrcop, fptemp;
    CPU86_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    int q;

    fpsrcop = ST0;
    fptemp = ST1;
    fpsrcop1.d = fpsrcop;
    fptemp1.d = fptemp;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);
    if (expdif < 53) {
        dblq = fpsrcop / fptemp;
        dblq = (dblq < 0.0)? ceil(dblq): floor(dblq);
        ST0 = fpsrcop - fptemp*dblq;
        q = (int)dblq; /* cutting off top bits is assumed here */
        env->fpus &= (~0x4700); /* (C3,C2,C1,C0) <-- 0000 */
				/* (C0,C1,C3) <-- (q2,q1,q0) */
        env->fpus |= (q&0x4) << 6; /* (C0) <-- q2 */
        env->fpus |= (q&0x2) << 8; /* (C1) <-- q1 */
        env->fpus |= (q&0x1) << 14; /* (C3) <-- q0 */
    } else {
        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, expdif-50);
        fpsrcop = (ST0 / ST1) / fptemp;
        /* fpsrcop = integer obtained by rounding to the nearest */
        fpsrcop = (fpsrcop-floor(fpsrcop) < ceil(fpsrcop)-fpsrcop)?
            floor(fpsrcop): ceil(fpsrcop);
        ST0 -= (ST1 * fpsrcop * fptemp);
    }
}

void helper_fprem(void)
{
    CPU86_LDouble dblq, fpsrcop, fptemp;
    CPU86_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    int q;
    
    fpsrcop = ST0;
    fptemp = ST1;
    fpsrcop1.d = fpsrcop;
    fptemp1.d = fptemp;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);
    if ( expdif < 53 ) {
        dblq = fpsrcop / fptemp;
        dblq = (dblq < 0.0)? ceil(dblq): floor(dblq);
        ST0 = fpsrcop - fptemp*dblq;
        q = (int)dblq; /* cutting off top bits is assumed here */
        env->fpus &= (~0x4700); /* (C3,C2,C1,C0) <-- 0000 */
				/* (C0,C1,C3) <-- (q2,q1,q0) */
        env->fpus |= (q&0x4) << 6; /* (C0) <-- q2 */
        env->fpus |= (q&0x2) << 8; /* (C1) <-- q1 */
        env->fpus |= (q&0x1) << 14; /* (C3) <-- q0 */
    } else {
        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, expdif-50);
        fpsrcop = (ST0 / ST1) / fptemp;
        /* fpsrcop = integer obtained by chopping */
        fpsrcop = (fpsrcop < 0.0)?
            -(floor(fabs(fpsrcop))): floor(fpsrcop);
        ST0 -= (ST1 * fpsrcop * fptemp);
    }
}

void helper_fyl2xp1(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if ((fptemp+1.0)>0.0) {
        fptemp = log(fptemp+1.0) / log(2.0); /* log2(ST+1.0) */
        ST1 *= fptemp;
        fpop();
    } else { 
        env->fpus &= (~0x4700);
        env->fpus |= 0x400;
    }
}

void helper_fsqrt(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if (fptemp<0.0) { 
        env->fpus &= (~0x4700);  /* (C3,C2,C1,C0) <-- 0000 */
        env->fpus |= 0x400;
    }
    ST0 = sqrt(fptemp);
}

void helper_fsincos(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if ((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = sin(fptemp);
        fpush();
        ST0 = cos(fptemp);
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg| < 2**63 only */
    }
}

void helper_frndint(void)
{
    ST0 = rint(ST0);
}

void helper_fscale(void)
{
    CPU86_LDouble fpsrcop, fptemp;

    fpsrcop = 2.0;
    fptemp = pow(fpsrcop,ST1);
    ST0 *= fptemp;
}

void helper_fsin(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if ((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = sin(fptemp);
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg| < 2**53 only */
    }
}

void helper_fcos(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = cos(fptemp);
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg5 < 2**63 only */
    }
}

/* associated heplers to reduce generated code length and to simplify
   relocation (FP constants are usually stored in .rodata section) */

void OPPROTO op_f2xm1(void)
{
    helper_f2xm1();
}

void OPPROTO op_fyl2x(void)
{
    helper_fyl2x();
}

void OPPROTO op_fptan(void)
{
    helper_fptan();
}

void OPPROTO op_fpatan(void)
{
    helper_fpatan();
}

void OPPROTO op_fxtract(void)
{
    helper_fxtract();
}

void OPPROTO op_fprem1(void)
{
    helper_fprem1();
}


void OPPROTO op_fprem(void)
{
    helper_fprem();
}

void OPPROTO op_fyl2xp1(void)
{
    helper_fyl2xp1();
}

void OPPROTO op_fsqrt(void)
{
    helper_fsqrt();
}

void OPPROTO op_fsincos(void)
{
    helper_fsincos();
}

void OPPROTO op_frndint(void)
{
    helper_frndint();
}

void OPPROTO op_fscale(void)
{
    helper_fscale();
}

void OPPROTO op_fsin(void)
{
    helper_fsin();
}

void OPPROTO op_fcos(void)
{
    helper_fcos();
}

