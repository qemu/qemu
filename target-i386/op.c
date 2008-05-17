/*
 *  i386 micro operations
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

#define ASM_SOFTMMU
#include "exec.h"

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

#ifdef TARGET_X86_64

#define REG (env->regs[8])
#define REGNAME _R8
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[9])
#define REGNAME _R9
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[10])
#define REGNAME _R10
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[11])
#define REGNAME _R11
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[12])
#define REGNAME _R12
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[13])
#define REGNAME _R13
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[14])
#define REGNAME _R14
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[15])
#define REGNAME _R15
#include "opreg_template.h"
#undef REG
#undef REGNAME

#endif

/* multiply/divide */

/* XXX: add eflags optimizations */
/* XXX: add non P4 style flags */

void OPPROTO op_mulb_AL_T0(void)
{
    unsigned int res;
    res = (uint8_t)EAX * (uint8_t)T0;
    EAX = (EAX & ~0xffff) | res;
    CC_DST = res;
    CC_SRC = (res & 0xff00);
}

void OPPROTO op_imulb_AL_T0(void)
{
    int res;
    res = (int8_t)EAX * (int8_t)T0;
    EAX = (EAX & ~0xffff) | (res & 0xffff);
    CC_DST = res;
    CC_SRC = (res != (int8_t)res);
}

void OPPROTO op_mulw_AX_T0(void)
{
    unsigned int res;
    res = (uint16_t)EAX * (uint16_t)T0;
    EAX = (EAX & ~0xffff) | (res & 0xffff);
    EDX = (EDX & ~0xffff) | ((res >> 16) & 0xffff);
    CC_DST = res;
    CC_SRC = res >> 16;
}

void OPPROTO op_imulw_AX_T0(void)
{
    int res;
    res = (int16_t)EAX * (int16_t)T0;
    EAX = (EAX & ~0xffff) | (res & 0xffff);
    EDX = (EDX & ~0xffff) | ((res >> 16) & 0xffff);
    CC_DST = res;
    CC_SRC = (res != (int16_t)res);
}

void OPPROTO op_mull_EAX_T0(void)
{
    uint64_t res;
    res = (uint64_t)((uint32_t)EAX) * (uint64_t)((uint32_t)T0);
    EAX = (uint32_t)res;
    EDX = (uint32_t)(res >> 32);
    CC_DST = (uint32_t)res;
    CC_SRC = (uint32_t)(res >> 32);
}

void OPPROTO op_imull_EAX_T0(void)
{
    int64_t res;
    res = (int64_t)((int32_t)EAX) * (int64_t)((int32_t)T0);
    EAX = (uint32_t)(res);
    EDX = (uint32_t)(res >> 32);
    CC_DST = res;
    CC_SRC = (res != (int32_t)res);
}

void OPPROTO op_imulw_T0_T1(void)
{
    int res;
    res = (int16_t)T0 * (int16_t)T1;
    T0 = res;
    CC_DST = res;
    CC_SRC = (res != (int16_t)res);
}

void OPPROTO op_imull_T0_T1(void)
{
    int64_t res;
    res = (int64_t)((int32_t)T0) * (int64_t)((int32_t)T1);
    T0 = res;
    CC_DST = res;
    CC_SRC = (res != (int32_t)res);
}

#ifdef TARGET_X86_64
void OPPROTO op_mulq_EAX_T0(void)
{
    helper_mulq_EAX_T0(T0);
}

void OPPROTO op_imulq_EAX_T0(void)
{
    helper_imulq_EAX_T0(T0);
}

void OPPROTO op_imulq_T0_T1(void)
{
    T0 = helper_imulq_T0_T1(T0, T1);
}
#endif

/* constant load & misc op */

/* XXX: consistent names */
void OPPROTO op_into(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    if (eflags & CC_O) {
        raise_interrupt(EXCP04_INTO, 1, 0, PARAM1);
    }
    FORCE_RET();
}

void OPPROTO op_cmpxchg8b(void)
{
    helper_cmpxchg8b(A0);
}

/* multiple size ops */

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

#ifdef TARGET_X86_64

#define SHIFT 3
#include "ops_template.h"
#undef SHIFT

#endif

/* string ops helpers */

void OPPROTO op_addl_ESI_T0(void)
{
    ESI = (uint32_t)(ESI + T0);
}

void OPPROTO op_addw_ESI_T0(void)
{
    ESI = (ESI & ~0xffff) | ((ESI + T0) & 0xffff);
}

void OPPROTO op_addl_EDI_T0(void)
{
    EDI = (uint32_t)(EDI + T0);
}

void OPPROTO op_addw_EDI_T0(void)
{
    EDI = (EDI & ~0xffff) | ((EDI + T0) & 0xffff);
}

void OPPROTO op_decl_ECX(void)
{
    ECX = (uint32_t)(ECX - 1);
}

void OPPROTO op_decw_ECX(void)
{
    ECX = (ECX & ~0xffff) | ((ECX - 1) & 0xffff);
}

#ifdef TARGET_X86_64
void OPPROTO op_addq_ESI_T0(void)
{
    ESI = (ESI + T0);
}

void OPPROTO op_addq_EDI_T0(void)
{
    EDI = (EDI + T0);
}

void OPPROTO op_decq_ECX(void)
{
    ECX--;
}
#endif

/* bcd */

void OPPROTO op_aam(void)
{
    helper_aam(PARAM1);
}

void OPPROTO op_aad(void)
{
    helper_aad(PARAM1);
}

void OPPROTO op_aaa(void)
{
    helper_aaa();
}

void OPPROTO op_aas(void)
{
    helper_aas();
}

void OPPROTO op_daa(void)
{
    helper_daa();
}

void OPPROTO op_das(void)
{
    helper_das();
}

/* segment handling */

/* faster VM86 version */
void OPPROTO op_movl_seg_T0_vm(void)
{
    int selector;
    SegmentCache *sc;

    selector = T0 & 0xffff;
    /* env->segs[] access */
    sc = (SegmentCache *)((char *)env + PARAM1);
    sc->selector = selector;
    sc->base = (selector << 4);
}

void OPPROTO op_movl_T0_seg(void)
{
    T0 = env->segs[PARAM1].selector;
}

void OPPROTO op_lsl(void)
{
    uint32_t val;
    val = helper_lsl(T0);
    if (CC_SRC & CC_Z)
        T1 = val;
    FORCE_RET();
}

void OPPROTO op_lar(void)
{
    uint32_t val;
    val = helper_lar(T0);
    if (CC_SRC & CC_Z)
        T1 = val;
    FORCE_RET();
}

void OPPROTO op_verr(void)
{
    helper_verr(T0);
}

void OPPROTO op_verw(void)
{
    helper_verw(T0);
}

void OPPROTO op_arpl(void)
{
    if ((T0 & 3) < (T1 & 3)) {
        /* XXX: emulate bug or 0xff3f0000 oring as in bochs ? */
        T0 = (T0 & ~3) | (T1 & 3);
        T1 = CC_Z;
   } else {
        T1 = 0;
    }
    FORCE_RET();
}

void OPPROTO op_arpl_update(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    CC_SRC = (eflags & ~CC_Z) | T1;
}

void OPPROTO op_movl_T0_env(void)
{
    T0 = *(uint32_t *)((char *)env + PARAM1);
}

void OPPROTO op_movl_env_T0(void)
{
    *(uint32_t *)((char *)env + PARAM1) = T0;
}

void OPPROTO op_movl_env_T1(void)
{
    *(uint32_t *)((char *)env + PARAM1) = T1;
}

void OPPROTO op_movtl_T0_env(void)
{
    T0 = *(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_movtl_env_T0(void)
{
    *(target_ulong *)((char *)env + PARAM1) = T0;
}

void OPPROTO op_movtl_T1_env(void)
{
    T1 = *(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_movtl_env_T1(void)
{
    *(target_ulong *)((char *)env + PARAM1) = T1;
}

/* flags handling */

void OPPROTO op_jmp_label(void)
{
    GOTO_LABEL_PARAM(1);
}

void OPPROTO op_jnz_T0_label(void)
{
    if (T0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_jz_T0_label(void)
{
    if (!T0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
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

void OPPROTO op_mov_T0_cc(void)
{
    T0 = cc_table[CC_OP].compute_all();
}

/* XXX: clear VIF/VIP in all ops ? */

void OPPROTO op_movl_eflags_T0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK));
}

void OPPROTO op_movw_eflags_T0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK) & 0xffff);
}

void OPPROTO op_movl_eflags_T0_io(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK));
}

void OPPROTO op_movw_eflags_T0_io(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK) & 0xffff);
}

void OPPROTO op_movl_eflags_T0_cpl0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK | IOPL_MASK));
}

void OPPROTO op_movw_eflags_T0_cpl0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK | IOPL_MASK) & 0xffff);
}

#if 0
/* vm86plus version */
void OPPROTO op_movw_eflags_T0_vm(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~(FL_UPDATE_MASK16 | VIF_MASK)) |
        (eflags & FL_UPDATE_MASK16);
    if (eflags & IF_MASK) {
        env->eflags |= VIF_MASK;
        if (env->eflags & VIP_MASK) {
            EIP = PARAM1;
            raise_exception(EXCP0D_GPF);
        }
    }
    FORCE_RET();
}

void OPPROTO op_movl_eflags_T0_vm(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~(FL_UPDATE_MASK32 | VIF_MASK)) |
        (eflags & FL_UPDATE_MASK32);
    if (eflags & IF_MASK) {
        env->eflags |= VIF_MASK;
        if (env->eflags & VIP_MASK) {
            EIP = PARAM1;
            raise_exception(EXCP0D_GPF);
        }
    }
    FORCE_RET();
}
#endif

/* XXX: compute only O flag */
void OPPROTO op_movb_eflags_T0(void)
{
    int of;
    of = cc_table[CC_OP].compute_all() & CC_O;
    CC_SRC = (T0 & (CC_S | CC_Z | CC_A | CC_P | CC_C)) | of;
}

void OPPROTO op_movl_T0_eflags(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= (DF & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK);
    T0 = eflags;
}

/* vm86plus version */
#if 0
void OPPROTO op_movl_T0_eflags_vm(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= (DF & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK | IF_MASK);
    if (env->eflags & VIF_MASK)
        eflags |= IF_MASK;
    T0 = eflags;
}
#endif

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

void OPPROTO op_salc(void)
{
    int cf;
    cf = cc_table[CC_OP].compute_c();
    EAX = (EAX & ~0xff) | ((-cf) & 0xff);
}

void OPPROTO op_fcomi_dummy(void)
{
    T0 = 0;
}

/* SSE support */
void OPPROTO op_com_dummy(void)
{
    T0 = 0;
}
