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
#define CC_OP (env->cc_op)

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
