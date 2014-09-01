/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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
#include <stdlib.h>
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"

#define SSOV(env, ret, arg, len) do {               \
    int64_t max_pos = INT##len ##_MAX;              \
    int64_t max_neg = INT##len ##_MIN;              \
    if (arg > max_pos) {                            \
        env->PSW_USB_V = (1 << 31);                 \
        env->PSW_USB_SV = (1 << 31);                \
        ret = (target_ulong)max_pos;                \
    } else {                                        \
        if (arg < max_neg) {                        \
            env->PSW_USB_V = (1 << 31);             \
            env->PSW_USB_SV = (1 << 31);            \
            ret = (target_ulong)max_neg;            \
        } else {                                    \
            env->PSW_USB_V = 0;                     \
            ret = (target_ulong)arg;                \
        }                                           \
    }                                               \
    env->PSW_USB_AV = arg ^ arg * 2u;               \
    env->PSW_USB_SAV |= env->PSW_USB_AV;            \
} while (0)

target_ulong helper_add_ssov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    target_ulong ret;
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t result = t1 + t2;
    SSOV(env, ret, result, 32);
    return ret;
}

target_ulong helper_sub_ssov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    target_ulong ret;
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t result = t1 - t2;
    SSOV(env, ret, result, 32);
    return ret;
}

/* context save area (CSA) related helpers */

static int cdc_increment(target_ulong *psw)
{
    if ((*psw & MASK_PSW_CDC) == 0x7f) {
        return 0;
    }

    (*psw)++;
    /* check for overflow */
    int lo = clo32((*psw & MASK_PSW_CDC) << (32 - 7));
    int mask = (1u << (7 - lo)) - 1;
    int count = *psw & mask;
    if (count == 0) {
        (*psw)--;
        return 1;
    }
    return 0;
}

static int cdc_decrement(target_ulong *psw)
{
    if ((*psw & MASK_PSW_CDC) == 0x7f) {
        return 0;
    }
    /* check for underflow */
    int lo = clo32((*psw & MASK_PSW_CDC) << (32 - 7));
    int mask = (1u << (7 - lo)) - 1;
    int count = *psw & mask;
    if (count == 0) {
        return 1;
    }
    (*psw)--;
    return 0;
}

static void save_context_upper(CPUTriCoreState *env, int ea,
                               target_ulong *new_FCX)
{
    *new_FCX = cpu_ldl_data(env, ea);
    cpu_stl_data(env, ea, env->PCXI);
    cpu_stl_data(env, ea+4, env->PSW);
    cpu_stl_data(env, ea+8, env->gpr_a[10]);
    cpu_stl_data(env, ea+12, env->gpr_a[11]);
    cpu_stl_data(env, ea+16, env->gpr_d[8]);
    cpu_stl_data(env, ea+20, env->gpr_d[9]);
    cpu_stl_data(env, ea+24, env->gpr_d[10]);
    cpu_stl_data(env, ea+28, env->gpr_d[11]);
    cpu_stl_data(env, ea+32, env->gpr_a[12]);
    cpu_stl_data(env, ea+36, env->gpr_a[13]);
    cpu_stl_data(env, ea+40, env->gpr_a[14]);
    cpu_stl_data(env, ea+44, env->gpr_a[15]);
    cpu_stl_data(env, ea+48, env->gpr_d[12]);
    cpu_stl_data(env, ea+52, env->gpr_d[13]);
    cpu_stl_data(env, ea+56, env->gpr_d[14]);
    cpu_stl_data(env, ea+60, env->gpr_d[15]);

}

static void save_context_lower(CPUTriCoreState *env, int ea,
                               target_ulong *new_FCX)
{
    *new_FCX = cpu_ldl_data(env, ea);
    cpu_stl_data(env, ea, env->PCXI);
    cpu_stl_data(env, ea+4, env->PSW);
    cpu_stl_data(env, ea+8, env->gpr_a[2]);
    cpu_stl_data(env, ea+12, env->gpr_a[3]);
    cpu_stl_data(env, ea+16, env->gpr_d[0]);
    cpu_stl_data(env, ea+20, env->gpr_d[1]);
    cpu_stl_data(env, ea+24, env->gpr_d[2]);
    cpu_stl_data(env, ea+28, env->gpr_d[3]);
    cpu_stl_data(env, ea+32, env->gpr_a[4]);
    cpu_stl_data(env, ea+36, env->gpr_a[5]);
    cpu_stl_data(env, ea+40, env->gpr_a[6]);
    cpu_stl_data(env, ea+44, env->gpr_a[7]);
    cpu_stl_data(env, ea+48, env->gpr_d[4]);
    cpu_stl_data(env, ea+52, env->gpr_d[5]);
    cpu_stl_data(env, ea+56, env->gpr_d[6]);
    cpu_stl_data(env, ea+60, env->gpr_d[7]);
}

static void restore_context_upper(CPUTriCoreState *env, int ea,
                                  target_ulong *new_PCXI, target_ulong *new_PSW)
{
    *new_PCXI = cpu_ldl_data(env, ea);
    *new_PSW = cpu_ldl_data(env, ea+4);
    env->gpr_a[10] = cpu_ldl_data(env, ea+8);
    env->gpr_a[11] = cpu_ldl_data(env, ea+12);
    env->gpr_d[8]  = cpu_ldl_data(env, ea+16);
    env->gpr_d[9]  = cpu_ldl_data(env, ea+20);
    env->gpr_d[10] = cpu_ldl_data(env, ea+24);
    env->gpr_d[11] = cpu_ldl_data(env, ea+28);
    env->gpr_a[12] = cpu_ldl_data(env, ea+32);
    env->gpr_a[13] = cpu_ldl_data(env, ea+36);
    env->gpr_a[14] = cpu_ldl_data(env, ea+40);
    env->gpr_a[15] = cpu_ldl_data(env, ea+44);
    env->gpr_d[12] = cpu_ldl_data(env, ea+48);
    env->gpr_d[13] = cpu_ldl_data(env, ea+52);
    env->gpr_d[14] = cpu_ldl_data(env, ea+56);
    env->gpr_d[15] = cpu_ldl_data(env, ea+60);
    cpu_stl_data(env, ea, env->FCX);
}

void helper_call(CPUTriCoreState *env, uint32_t next_pc)
{
    target_ulong tmp_FCX;
    target_ulong ea;
    target_ulong new_FCX;
    target_ulong psw;

    psw = psw_read(env);
    /* if (FCX == 0) trap(FCU); */
    if (env->FCX == 0) {
        /* FCU trap */
    }
    /* if (PSW.CDE) then if (cdc_increment()) then trap(CDO); */
    if (psw & MASK_PSW_CDE) {
        if (cdc_increment(&psw)) {
            /* CDO trap */
        }
    }
    /* PSW.CDE = 1;*/
    psw |= MASK_PSW_CDE;
    /* tmp_FCX = FCX; */
    tmp_FCX = env->FCX;
    /* EA = {FCX.FCXS, 6'b0, FCX.FCXO, 6'b0}; */
    ea = ((env->FCX & MASK_FCX_FCXS) << 12) +
         ((env->FCX & MASK_FCX_FCXO) << 6);
    /* new_FCX = M(EA, word);
       M(EA, 16 * word) = {PCXI, PSW, A[10], A[11], D[8], D[9], D[10], D[11],
                          A[12], A[13], A[14], A[15], D[12], D[13], D[14],
                          D[15]}; */
    save_context_upper(env, ea, &new_FCX);

    /* PCXI.PCPN = ICR.CCPN; */
    env->PCXI = (env->PCXI & 0xffffff) +
                ((env->ICR & MASK_ICR_CCPN) << 24);
    /* PCXI.PIE = ICR.IE; */
    env->PCXI = ((env->PCXI & ~MASK_PCXI_PIE) +
                ((env->ICR & MASK_ICR_IE) << 15));
    /* PCXI.UL = 1; */
    env->PCXI |= MASK_PCXI_UL;

    /* PCXI[19: 0] = FCX[19: 0]; */
    env->PCXI = (env->PCXI & 0xfff00000) + (env->FCX & 0xfffff);
    /* FCX[19: 0] = new_FCX[19: 0]; */
    env->FCX = (env->FCX & 0xfff00000) + (new_FCX & 0xfffff);
    /* A[11] = next_pc[31: 0]; */
    env->gpr_a[11] = next_pc;

    /* if (tmp_FCX == LCX) trap(FCD);*/
    if (tmp_FCX == env->LCX) {
        /* FCD trap */
    }
    psw_write(env, psw);
}

void helper_ret(CPUTriCoreState *env)
{
    target_ulong ea;
    target_ulong new_PCXI;
    target_ulong new_PSW, psw;

    psw = psw_read(env);
     /* if (PSW.CDE) then if (cdc_decrement()) then trap(CDU);*/
    if (env->PSW & MASK_PSW_CDE) {
        if (cdc_decrement(&(env->PSW))) {
            /* CDU trap */
        }
    }
    /*   if (PCXI[19: 0] == 0) then trap(CSU); */
    if ((env->PCXI & 0xfffff) == 0) {
        /* CSU trap */
    }
    /* if (PCXI.UL == 0) then trap(CTYP); */
    if ((env->PCXI & MASK_PCXI_UL) == 0) {
        /* CTYP trap */
    }
    /* PC = {A11 [31: 1], 1’b0}; */
    env->PC = env->gpr_a[11] & 0xfffffffe;

    /* EA = {PCXI.PCXS, 6'b0, PCXI.PCXO, 6'b0}; */
    ea = ((env->PCXI & MASK_PCXI_PCXS) << 12) +
         ((env->PCXI & MASK_PCXI_PCXO) << 6);
    /* {new_PCXI, new_PSW, A[10], A[11], D[8], D[9], D[10], D[11], A[12],
        A[13], A[14], A[15], D[12], D[13], D[14], D[15]} = M(EA, 16 * word);
        M(EA, word) = FCX; */
    restore_context_upper(env, ea, &new_PCXI, &new_PSW);
    /* FCX[19: 0] = PCXI[19: 0]; */
    env->FCX = (env->FCX & 0xfff00000) + (env->PCXI & 0x000fffff);
    /* PCXI = new_PCXI; */
    env->PCXI = new_PCXI;

    if (tricore_feature(env, TRICORE_FEATURE_13)) {
        /* PSW = new_PSW */
        psw_write(env, new_PSW);
    } else {
        /* PSW = {new_PSW[31:26], PSW[25:24], new_PSW[23:0]}; */
        psw_write(env, (new_PSW & ~(0x3000000)) + (psw & (0x3000000)));
    }
}

void helper_bisr(CPUTriCoreState *env, uint32_t const9)
{
    target_ulong tmp_FCX;
    target_ulong ea;
    target_ulong new_FCX;

    if (env->FCX == 0) {
        /* FCU trap */
    }

    tmp_FCX = env->FCX;
    ea = ((env->FCX & 0xf0000) << 12) + ((env->FCX & 0xffff) << 6);

    save_context_lower(env, ea, &new_FCX);

    /* PCXI.PCPN = ICR.CCPN */
    env->PCXI = (env->PCXI & 0xffffff) +
                 ((env->ICR & MASK_ICR_CCPN) << 24);
    /* PCXI.PIE  = ICR.IE */
    env->PCXI = ((env->PCXI & ~MASK_PCXI_PIE) +
                 ((env->ICR & MASK_ICR_IE) << 15));
    /* PCXI.UL = 0 */
    env->PCXI &= ~(MASK_PCXI_UL);
    /* PCXI[19: 0] = FCX[19: 0] */
    env->PCXI = (env->PCXI & 0xfff00000) + (env->FCX & 0xfffff);
    /* FXC[19: 0] = new_FCX[19: 0] */
    env->FCX = (env->FCX & 0xfff00000) + (new_FCX & 0xfffff);
    /* ICR.IE = 1 */
    env->ICR |= MASK_ICR_IE;

    env->ICR |= const9; /* ICR.CCPN = const9[7: 0];*/

    if (tmp_FCX == env->LCX) {
        /* FCD trap */
    }
}

static inline void QEMU_NORETURN do_raise_exception_err(CPUTriCoreState *env,
                                                        uint32_t exception,
                                                        int error_code,
                                                        uintptr_t pc)
{
    CPUState *cs = CPU(tricore_env_get_cpu(env));
    cs->exception_index = exception;
    env->error_code = error_code;

    if (pc) {
        /* now we have a real cpu fault */
        cpu_restore_state(cs, pc);
    }

    cpu_loop_exit(cs);
}

static inline void QEMU_NORETURN do_raise_exception(CPUTriCoreState *env,
                                                    uint32_t exception,
                                                    uintptr_t pc)
{
    do_raise_exception_err(env, exception, 0, pc);
}

void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;
    ret = cpu_tricore_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (ret) {
        TriCoreCPU *cpu = TRICORE_CPU(cs);
        CPUTriCoreState *env = &cpu->env;
        do_raise_exception_err(env, cs->exception_index,
                               env->error_code, retaddr);
    }
}
