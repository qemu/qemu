typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

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
    int (*compute_c)(void);  /* return the C flag */
    int (*compute_z)(void);  /* return the Z flag */
    int (*compute_s)(void);  /* return the S flag */
    int (*compute_o)(void);  /* return the O flag */
    int (*compute_all)(void); /* return all the flags */
} CCTable;

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

static int compute_eflags_all(void)
{
    return CC_SRC;
}

static int compute_eflags_addb(void)
{
    int cf, pf, af, zf, sf, of;
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_DST - CC_SRC;
    cf = (uint8_t)CC_DST < (uint8_t)src1;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((uint8_t)CC_DST != 0) << 6;
    sf = CC_DST & 0x80;
    of = ((src1 ^ src2 ^ -1) & (src1 ^ CC_DST) & 0x80) << 4;
    return cf | pf | af | zf | sf | of;
}

static int compute_eflags_subb(void)
{
    int cf, pf, af, zf, sf, of;
    int src1, src2;
    src1 = CC_SRC;
    src2 = CC_SRC - CC_DST;
    cf = (uint8_t)src1 < (uint8_t)src2;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((uint8_t)CC_DST != 0) << 6;
    sf = CC_DST & 0x80;
    of = ((src1 ^ src2 ^ -1) & (src1 ^ CC_DST) & 0x80) << 4;
    return cf | pf | af | zf | sf | of;
}

static int compute_eflags_logicb(void)
{
    cf = 0;
    pf = parity_table[(uint8_t)CC_DST];
    af = 0;
    zf = ((uint8_t)CC_DST != 0) << 6;
    sf = CC_DST & 0x80;
    of = 0;
    return cf | pf | af | zf | sf | of;
}

static int compute_eflags_incb(void)
{
    int cf, pf, af, zf, sf, of;
    int src2;
    src1 = CC_DST - 1;
    src2 = 1;
    cf = CC_SRC;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((uint8_t)CC_DST != 0) << 6;
    sf = CC_DST & 0x80;
    of = ((src1 ^ src2 ^ -1) & (src1 ^ CC_DST) & 0x80) << 4;
    return cf | pf | af | zf | sf | of;
}

static int compute_eflags_decb(void)
{
    int cf, pf, af, zf, sf, of;
    int src1, src2;
    src1 = CC_DST + 1;
    src2 = 1;
    cf = (uint8_t)src1 < (uint8_t)src2;
    pf = parity_table[(uint8_t)CC_DST];
    af = (CC_DST ^ src1 ^ src2) & 0x10;
    zf = ((uint8_t)CC_DST != 0) << 6;
    sf = CC_DST & 0x80;
    of = ((src1 ^ src2 ^ -1) & (src1 ^ CC_DST) & 0x80) << 4;
    return cf | pf | af | zf | sf | of;
}

static int compute_eflags_shlb(void)
{
    cf = CC_SRC;
    pf = parity_table[(uint8_t)CC_DST];
    af = 0; /* undefined */
    zf = ((uint8_t)CC_DST != 0) << 6;
    sf = CC_DST & 0x80;
    of = 0; /* undefined */
    return cf | pf | af | zf | sf | of;
}

static int compute_eflags_shrb(void)
{
    cf = CC_SRC & 1;
    pf = parity_table[(uint8_t)CC_DST];
    af = 0; /* undefined */
    zf = ((uint8_t)CC_DST != 0) << 6;
    sf = CC_DST & 0x80;
    of = sf << 4;
    return cf | pf | af | zf | sf | of;
}

static int compute_eflags_mul(void)
{
    cf = (CC_SRC != 0);
    pf = 0; /* undefined */
    af = 0; /* undefined */
    zf = 0; /* undefined */
    sf = 0; /* undefined */
    of = cf << 11;
    return cf | pf | af | zf | sf | of;
}
    
CTable cc_table[CC_OP_NB] = {
    [CC_OP_DYNAMIC] = { NULL, NULL, NULL },
    [CC_OP_EFLAGS] = { NULL, NULL, NULL },
    
};

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

/* shifts */

void OPPROTO op_roll_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count) {
        CC_SRC = T0;
        T0 = (T0 << count) | (T0 >> (32 - count));
        CC_DST = T0;
        CC_OP = CC_OP_ROLL;
    }
}

void OPPROTO op_rolw_T0_T1_cc(void)
{
    int count;
    count = T1 & 0xf;
    if (count) {
        T0 = T0 & 0xffff;
        CC_SRC = T0;
        T0 = (T0 << count) | (T0 >> (16 - count));
        CC_DST = T0;
        CC_OP = CC_OP_ROLW;
    }
}

void OPPROTO op_rolb_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x7;
    if (count) {
        T0 = T0 & 0xff;
        CC_SRC = T0;
        T0 = (T0 << count) | (T0 >> (8 - count));
        CC_DST = T0;
        CC_OP = CC_OP_ROLB;
    }
}

void OPPROTO op_rorl_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count) {
        CC_SRC = T0;
        T0 = (T0 >> count) | (T0 << (32 - count));
        CC_DST = T0;
        CC_OP = CC_OP_RORB;
    }
}

void OPPROTO op_rorw_T0_T1_cc(void)
{
    int count;
    count = T1 & 0xf;
    if (count) {
        CC_SRC = T0;
        T0 = (T0 >> count) | (T0 << (16 - count));
        CC_DST = T0;
        CC_OP = CC_OP_RORW;
    }
}

void OPPROTO op_rorb_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x7;
    if (count) {
        CC_SRC = T0;
        T0 = (T0 >> count) | (T0 << (8 - count));
        CC_DST = T0;
        CC_OP = CC_OP_RORL;
    }
}

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

void helper_rcll_T0_T1_cc(void)
{
    int count, res;

    count = T1 & 0x1f;
    if (count) {
        CC_SRC = T0;
        res = (T0 << count) | (cc_table[CC_OP].compute_c() << (count - 1));
        if (count > 1)
            res |= T0 >> (33 - count);
        T0 = res;
        CC_DST = T0 ^ CC_SRC;    /* O is in bit 31 */
        CC_SRC >>= (32 - count); /* CC is in bit 0 */
        CC_OP = CC_OP_RCLL;
    }
}

void OPPROTO op_rcll_T0_T1_cc(void)
{
    helper_rcll_T0_T1_cc();
}

void OPPROTO op_rclw_T0_T1_cc(void)
{
    int count;
    count = rclw_table[T1 & 0x1f];
    if (count) {
        T0 = T0 & 0xffff;
        CC_SRC = T0;
        T0 = (T0 << count) | (cc_table[CC_OP].compute_c() << (count - 1)) |
            (T0 >> (17 - count));
        CC_DST = T0 ^ CC_SRC;
        CC_SRC >>= (16 - count);
        CC_OP = CC_OP_RCLW;
    }
}

void OPPROTO op_rclb_T0_T1_cc(void)
{
    int count;
    count = rclb_table[T1 & 0x1f];
    if (count) {
        T0 = T0 & 0xff;
        CC_SRC = T0;
        T0 = (T0 << count) | (cc_table[CC_OP].compute_c() << (count - 1)) |
            (T0 >> (9 - count));
        CC_DST = T0 ^ CC_SRC;
        CC_SRC >>= (8 - count);
        CC_OP = CC_OP_RCLB;
    }
}

void OPPROTO op_rcrl_T0_T1_cc(void)
{
    int count, res;
    count = T1 & 0x1f;
    if (count) {
        CC_SRC = T0;
        res = (T0 >> count) | (cc_table[CC_OP].compute_c() << (32 - count));
        if (count > 1)
            res |= T0 << (33 - count);
        T0 = res;
        CC_DST = T0 ^ CC_SRC;
        CC_SRC >>= (count - 1);
        CC_OP = CC_OP_RCLL;
    }
}

void OPPROTO op_rcrw_T0_T1_cc(void)
{
    int count;
    count = rclw_table[T1 & 0x1f];
    if (count) {
        T0 = T0 & 0xffff;
        CC_SRC = T0;
        T0 = (T0 >> count) | (cc_table[CC_OP].compute_c() << (16 - count)) |
            (T0 << (17 - count));
        CC_DST = T0 ^ CC_SRC;
        CC_SRC >>= (count - 1);
        CC_OP = CC_OP_RCLW;
    }
}

void OPPROTO op_rcrb_T0_T1_cc(void)
{
    int count;
    count = rclb_table[T1 & 0x1f];
    if (count) {
        T0 = T0 & 0xff;
        CC_SRC = T0;
        T0 = (T0 >> count) | (cc_table[CC_OP].compute_c() << (8 - count)) |
            (T0 << (9 - count));
        CC_DST = T0 ^ CC_SRC;
        CC_SRC >>= (count - 1);
        CC_OP = CC_OP_RCLB;
    }
}

void OPPROTO op_shll_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count == 1) {
        CC_SRC = T0;
        T0 = T0 << 1;
        CC_DST = T0;
        CC_OP = CC_OP_ADDL;
    } else if (count) {
        CC_SRC = T0 >> (32 - count);
        T0 = T0 << count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLL;
    }
}

void OPPROTO op_shlw_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count == 1) {
        CC_SRC = T0;
        T0 = T0 << 1;
        CC_DST = T0;
        CC_OP = CC_OP_ADDW;
    } else if (count) {
        CC_SRC = T0 >> (16 - count);
        T0 = T0 << count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLW;
    }
}

void OPPROTO op_shlb_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count == 1) {
        CC_SRC = T0;
        T0 = T0 << 1;
        CC_DST = T0;
        CC_OP = CC_OP_ADDB;
    } else if (count) {
        CC_SRC = T0 >> (8 - count);
        T0 = T0 << count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLB;
    }
}

void OPPROTO op_shrl_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count == 1) {
        CC_SRC = T0;
        T0 = T0 >> 1;
        CC_DST = T0;
        CC_OP = CC_OP_SHRL;
    } else if (count) {
        CC_SRC = T0 >> (count - 1);
        T0 = T0 >> count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLL;
    }
}

void OPPROTO op_shrw_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count == 1) {
        T0 = T0 & 0xffff;
        CC_SRC = T0;
        T0 = T0 >> 1;
        CC_DST = T0;
        CC_OP = CC_OP_SHRW;
    } else if (count) {
        T0 = T0 & 0xffff;
        CC_SRC = T0 >> (count - 1);
        T0 = T0 >> count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLW;
    }
}

void OPPROTO op_shrb_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count == 1) {
        T0 = T0 & 0xff;
        CC_SRC = T0;
        T0 = T0 >> 1;
        CC_DST = T0;
        CC_OP = CC_OP_SHRB;
    } else if (count) {
        T0 = T0 & 0xff;
        CC_SRC = T0 >> (count - 1);
        T0 = T0 >> count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLB;
    }
}

void OPPROTO op_sarl_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count) {
        CC_SRC = (int32_t)T0 >> (count - 1);
        T0 = (int32_t)T0 >> count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLL;
    }
}

void OPPROTO op_sarw_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count) {
        CC_SRC = (int16_t)T0 >> (count - 1);
        T0 = (int16_t)T0 >> count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLW;
    }
}

void OPPROTO op_sarb_T0_T1_cc(void)
{
    int count;
    count = T1 & 0x1f;
    if (count) {
        CC_SRC = (int8_t)T0 >> (count - 1);
        T0 = (int8_t)T0 >> count;
        CC_DST = T0;
        CC_OP = CC_OP_SHLB;
    }
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

/* flags */

void OPPROTO op_set_cc_op(void)
{
    CC_OP = PARAM1;
}

void OPPROTO op_movl_eflags_T0(void)
{
    CC_SRC = T0;
    DF = (T0 & DIRECTION_FLAG) ? -1 : 1;
}

void OPPROTO op_movb_eflags_T0(void)
{
    int cc_o;
    cc_o = cc_table[CC_OP].compute_o();
    CC_SRC = T0 | (cc_o << 11);
}

void OPPROTO op_movl_T0_eflags(void)
{
    cc_table[CC_OP].compute_eflags();
}

void OPPROTO op_cld(void)
{
    DF = 1;
}

void OPPROTO op_std(void)
{
    DF = -1;
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

void OPPROTO op_jne_b(void)
{
    if ((uint8_t)CC_DST != 0)
        PC += PARAM1;
    else
        PC += PARAM2;
    FORCE_RET();
}

void OPPROTO op_jne_w(void)
{
    if ((uint16_t)CC_DST != 0)
        PC += PARAM1;
    else
        PC += PARAM2;
    FORCE_RET();
}

void OPPROTO op_jne_l(void)
{
    if (CC_DST != 0)
        PC += PARAM1;
    else
        PC += PARAM2;
    FORCE_RET(); /* generate a return so that gcc does not generate an
                    early function return */
}

/* string ops */

#define ldul ldl

#define SUFFIX b
#define SHIFT 0
#include "opstring_template.h"
#undef SUFFIX
#undef SHIFT

#define SUFFIX w
#define SHIFT 1
#include "opstring_template.h"
#undef SUFFIX
#undef SHIFT

#define SUFFIX l
#define SHIFT 2
#include "opstring_template.h"
#undef SUFFIX
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
