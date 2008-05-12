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

/* n must be a constant to be efficient */
static inline target_long lshift(target_long x, int n)
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

/* operations with flags */

/* update flags with T0 and T1 (add/sub case) */
void OPPROTO op_update2_cc(void)
{
    CC_SRC = T1;
    CC_DST = T0;
}

/* update flags with T0 (logic operation case) */
void OPPROTO op_update1_cc(void)
{
    CC_DST = T0;
}

void OPPROTO op_update_neg_cc(void)
{
    CC_SRC = -T0;
    CC_DST = T0;
}

void OPPROTO op_cmpl_T0_T1_cc(void)
{
    CC_SRC = T1;
    CC_DST = T0 - T1;
}

void OPPROTO op_update_inc_cc(void)
{
    CC_SRC = cc_table[CC_OP].compute_c();
    CC_DST = T0;
}

void OPPROTO op_testl_T0_T1_cc(void)
{
    CC_DST = T0 & T1;
}

/* operations without flags */

void OPPROTO op_negl_T0(void)
{
    T0 = -T0;
}

void OPPROTO op_incl_T0(void)
{
    T0++;
}

void OPPROTO op_decl_T0(void)
{
    T0--;
}

void OPPROTO op_notl_T0(void)
{
    T0 = ~T0;
}

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
    helper_mulq_EAX_T0();
}

void OPPROTO op_imulq_EAX_T0(void)
{
    helper_imulq_EAX_T0();
}

void OPPROTO op_imulq_T0_T1(void)
{
    helper_imulq_T0_T1();
}
#endif

/* constant load & misc op */

/* XXX: consistent names */
void OPPROTO op_addl_T1_im(void)
{
    T1 += PARAM1;
}

void OPPROTO op_movl_T1_A0(void)
{
    T1 = A0;
}

void OPPROTO op_addl_A0_AL(void)
{
    A0 = (uint32_t)(A0 + (EAX & 0xff));
}

#ifdef WORDS_BIGENDIAN
typedef union UREG64 {
    struct { uint16_t v3, v2, v1, v0; } w;
    struct { uint32_t v1, v0; } l;
    uint64_t q;
} UREG64;
#else
typedef union UREG64 {
    struct { uint16_t v0, v1, v2, v3; } w;
    struct { uint32_t v0, v1; } l;
    uint64_t q;
} UREG64;
#endif

#define PARAMQ1 \
({\
    UREG64 __p;\
    __p.l.v1 = PARAM1;\
    __p.l.v0 = PARAM2;\
    __p.q;\
})

#ifdef TARGET_X86_64

void OPPROTO op_addq_A0_AL(void)
{
    A0 = (A0 + (EAX & 0xff));
}

#endif

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
    helper_cmpxchg8b();
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
    EAX = (uint32_t)((int16_t)EAX);
}

#ifdef TARGET_X86_64
void OPPROTO op_movslq_T0_T0(void)
{
    T0 = (int32_t)T0;
}

void OPPROTO op_movslq_RAX_EAX(void)
{
    EAX = (int32_t)EAX;
}
#endif

void OPPROTO op_movsbw_AX_AL(void)
{
    EAX = (EAX & ~0xffff) | ((int8_t)EAX & 0xffff);
}

void OPPROTO op_movslq_EDX_EAX(void)
{
    EDX = (uint32_t)((int32_t)EAX >> 31);
}

void OPPROTO op_movswl_DX_AX(void)
{
    EDX = (EDX & ~0xffff) | (((int16_t)EAX >> 15) & 0xffff);
}

#ifdef TARGET_X86_64
void OPPROTO op_movsqo_RDX_RAX(void)
{
    EDX = (int64_t)EAX >> 63;
}
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

/* never use it with R_CS */
void OPPROTO op_movl_seg_T0(void)
{
    helper_load_seg(PARAM1, T0);
}

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
    helper_lsl(T0);
}

void OPPROTO op_lar(void)
{
    helper_lar(T0);
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

/* T0: segment, T1:eip */
void OPPROTO op_ljmp_protected_T0_T1(void)
{
    helper_ljmp_protected_T0_T1(PARAM1);
}

void OPPROTO op_lcall_real_T0_T1(void)
{
    helper_lcall_real_T0_T1(PARAM1, PARAM2);
}

void OPPROTO op_lcall_protected_T0_T1(void)
{
    helper_lcall_protected_T0_T1(PARAM1, PARAM2);
}

void OPPROTO op_iret_real(void)
{
    helper_iret_real(PARAM1);
}

void OPPROTO op_iret_protected(void)
{
    helper_iret_protected(PARAM1, PARAM2);
}

void OPPROTO op_lret_protected(void)
{
    helper_lret_protected(PARAM1, PARAM2);
}

/* CR registers access. */
void OPPROTO op_movl_crN_T0(void)
{
    helper_movl_crN_T0(PARAM1);
}

/* These pseudo-opcodes check for SVM intercepts. */
void OPPROTO op_svm_check_intercept(void)
{
    A0 = PARAM1 & PARAM2;
    svm_check_intercept(PARAMQ1);
}

void OPPROTO op_svm_check_intercept_param(void)
{
    A0 = PARAM1 & PARAM2;
    svm_check_intercept_param(PARAMQ1, T1);
}

void OPPROTO op_svm_vmexit(void)
{
    A0 = PARAM1 & PARAM2;
    vmexit(PARAMQ1, T1);
}

void OPPROTO op_geneflags(void)
{
    CC_SRC = cc_table[CC_OP].compute_all();
}

/* This pseudo-opcode checks for IO intercepts. */
#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_svm_check_intercept_io(void)
{
    A0 = PARAM1 & PARAM2;
    /* PARAMQ1 = TYPE (0 = OUT, 1 = IN; 4 = STRING; 8 = REP)
       T0      = PORT
       T1      = next eip */
    stq_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2), T1);
    /* ASIZE does not appear on real hw */
    svm_check_intercept_param(SVM_EXIT_IOIO,
                              (PARAMQ1 & ~SVM_IOIO_ASIZE_MASK) |
                              ((T0 & 0xffff) << 16));
}
#endif

#if !defined(CONFIG_USER_ONLY)
void OPPROTO op_movtl_T0_cr8(void)
{
    T0 = cpu_get_apic_tpr(env);
}
#endif

/* DR registers access */
void OPPROTO op_movl_drN_T0(void)
{
    helper_movl_drN_T0(PARAM1);
}

void OPPROTO op_lmsw_T0(void)
{
    /* only 4 lower bits of CR0 are modified. PE cannot be set to zero
       if already set to one. */
    T0 = (env->cr[0] & ~0xe) | (T0 & 0xf);
    helper_movl_crN_T0(0);
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

void OPPROTO op_clts(void)
{
    env->cr[0] &= ~CR0_TS_MASK;
    env->hflags &= ~HF_TS_MASK;
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

static int compute_all_eflags(void)
{
    return CC_SRC;
}

static int compute_c_eflags(void)
{
    return CC_SRC & CC_C;
}

CCTable cc_table[CC_OP_NB] = {
    [CC_OP_DYNAMIC] = { /* should never happen */ },

    [CC_OP_EFLAGS] = { compute_all_eflags, compute_c_eflags },

    [CC_OP_MULB] = { compute_all_mulb, compute_c_mull },
    [CC_OP_MULW] = { compute_all_mulw, compute_c_mull },
    [CC_OP_MULL] = { compute_all_mull, compute_c_mull },

    [CC_OP_ADDB] = { compute_all_addb, compute_c_addb },
    [CC_OP_ADDW] = { compute_all_addw, compute_c_addw  },
    [CC_OP_ADDL] = { compute_all_addl, compute_c_addl  },

    [CC_OP_ADCB] = { compute_all_adcb, compute_c_adcb },
    [CC_OP_ADCW] = { compute_all_adcw, compute_c_adcw  },
    [CC_OP_ADCL] = { compute_all_adcl, compute_c_adcl  },

    [CC_OP_SUBB] = { compute_all_subb, compute_c_subb  },
    [CC_OP_SUBW] = { compute_all_subw, compute_c_subw  },
    [CC_OP_SUBL] = { compute_all_subl, compute_c_subl  },

    [CC_OP_SBBB] = { compute_all_sbbb, compute_c_sbbb  },
    [CC_OP_SBBW] = { compute_all_sbbw, compute_c_sbbw  },
    [CC_OP_SBBL] = { compute_all_sbbl, compute_c_sbbl  },

    [CC_OP_LOGICB] = { compute_all_logicb, compute_c_logicb },
    [CC_OP_LOGICW] = { compute_all_logicw, compute_c_logicw },
    [CC_OP_LOGICL] = { compute_all_logicl, compute_c_logicl },

    [CC_OP_INCB] = { compute_all_incb, compute_c_incl },
    [CC_OP_INCW] = { compute_all_incw, compute_c_incl },
    [CC_OP_INCL] = { compute_all_incl, compute_c_incl },

    [CC_OP_DECB] = { compute_all_decb, compute_c_incl },
    [CC_OP_DECW] = { compute_all_decw, compute_c_incl },
    [CC_OP_DECL] = { compute_all_decl, compute_c_incl },

    [CC_OP_SHLB] = { compute_all_shlb, compute_c_shlb },
    [CC_OP_SHLW] = { compute_all_shlw, compute_c_shlw },
    [CC_OP_SHLL] = { compute_all_shll, compute_c_shll },

    [CC_OP_SARB] = { compute_all_sarb, compute_c_sarl },
    [CC_OP_SARW] = { compute_all_sarw, compute_c_sarl },
    [CC_OP_SARL] = { compute_all_sarl, compute_c_sarl },

#ifdef TARGET_X86_64
    [CC_OP_MULQ] = { compute_all_mulq, compute_c_mull },

    [CC_OP_ADDQ] = { compute_all_addq, compute_c_addq  },

    [CC_OP_ADCQ] = { compute_all_adcq, compute_c_adcq  },

    [CC_OP_SUBQ] = { compute_all_subq, compute_c_subq  },

    [CC_OP_SBBQ] = { compute_all_sbbq, compute_c_sbbq  },

    [CC_OP_LOGICQ] = { compute_all_logicq, compute_c_logicq },

    [CC_OP_INCQ] = { compute_all_incq, compute_c_incl },

    [CC_OP_DECQ] = { compute_all_decq, compute_c_incl },

    [CC_OP_SHLQ] = { compute_all_shlq, compute_c_shlq },

    [CC_OP_SARQ] = { compute_all_sarq, compute_c_sarl },
#endif
};

void OPPROTO op_fcomi_dummy(void)
{
    T0 = 0;
}

/* threading support */
void OPPROTO op_lock(void)
{
    cpu_lock();
}

void OPPROTO op_unlock(void)
{
    cpu_unlock();
}

/* SSE support */
void OPPROTO op_com_dummy(void)
{
    T0 = 0;
}
