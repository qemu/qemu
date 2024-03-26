/*
 * Power ISA decode for Storage Control instructions
 *
 * Copyright (c) 2022 Instituto de Pesquisas Eldorado (eldorado.org.br)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Store Control Instructions
 */

#include "mmu-book3s-v3.h"

static bool trans_SLBIE(DisasContext *ctx, arg_SLBIE *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS(ctx, SLBI);
    REQUIRE_SV(ctx);

#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_helper_SLBIE(tcg_env, cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_SLBIEG(DisasContext *ctx, arg_SLBIEG *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_SV(ctx);

#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_helper_SLBIEG(tcg_env, cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_SLBIA(DisasContext *ctx, arg_SLBIA *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS(ctx, SLBI);
    REQUIRE_SV(ctx);

#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_helper_SLBIA(tcg_env, tcg_constant_i32(a->ih));
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_SLBIAG(DisasContext *ctx, arg_SLBIAG *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_SV(ctx);

#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_helper_SLBIAG(tcg_env, cpu_gpr[a->rs], tcg_constant_i32(a->l));
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_SLBMTE(DisasContext *ctx, arg_SLBMTE *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS(ctx, SEGMENT_64B);
    REQUIRE_SV(ctx);

#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_helper_SLBMTE(tcg_env, cpu_gpr[a->rb], cpu_gpr[a->rt]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_SLBMFEV(DisasContext *ctx, arg_SLBMFEV *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS(ctx, SEGMENT_64B);
    REQUIRE_SV(ctx);

#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_helper_SLBMFEV(cpu_gpr[a->rt], tcg_env, cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_SLBMFEE(DisasContext *ctx, arg_SLBMFEE *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS(ctx, SEGMENT_64B);
    REQUIRE_SV(ctx);

#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_helper_SLBMFEE(cpu_gpr[a->rt], tcg_env, cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_SLBFEE(DisasContext *ctx, arg_SLBFEE *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS(ctx, SEGMENT_64B);

#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else

#if defined(TARGET_PPC64)
    TCGLabel *l1, *l2;

    if (unlikely(ctx->pr)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return true;
    }
    gen_helper_SLBFEE(cpu_gpr[a->rt], tcg_env,
                             cpu_gpr[a->rb]);
    l1 = gen_new_label();
    l2 = gen_new_label();
    tcg_gen_trunc_tl_i32(cpu_crf[0], cpu_so);
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_gpr[a->rt], -1, l1);
    tcg_gen_ori_i32(cpu_crf[0], cpu_crf[0], CRF_EQ);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_tl(cpu_gpr[a->rt], 0);
    gen_set_label(l2);
#else
    qemu_build_not_reached();
#endif
#endif
    return true;
}

static bool trans_SLBSYNC(DisasContext *ctx, arg_SLBSYNC *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_SV(ctx);

#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_check_tlb_flush(ctx, true);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool do_tlbie(DisasContext *ctx, arg_X_tlbie *a, bool local)
{
#if defined(CONFIG_USER_ONLY)
    gen_priv_exception(ctx, POWERPC_EXCP_PRIV_OPC);
    return true;
#else
    TCGv_i32 t1;
    int rb;

    rb = a->rb;

    if ((ctx->insns_flags2 & PPC2_ISA300) == 0) {
        /*
         * Before Power ISA 3.0, the corresponding bits of RIC, PRS, and R
         * (and RS for tlbiel) were reserved fields and should be ignored.
         */
        a->ric = 0;
        a->prs = false;
        a->r = false;
        if (local) {
            a->rs = 0;
        }
    }

    if (ctx->pr) {
        /* tlbie[l] is privileged... */
        gen_priv_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return true;
    } else if (!ctx->hv) {
        if ((!a->prs && ctx->hr) || (!local && !ctx->gtse)) {
            /*
             * ... except when PRS=0 and HR=1, or when GTSE=0 for tlbie,
             * making it hypervisor privileged.
             */
            gen_priv_exception(ctx, POWERPC_EXCP_PRIV_OPC);
            return true;
        }
    }

    if (!local && NARROW_MODE(ctx)) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_ext32u_tl(t0, cpu_gpr[rb]);
        gen_helper_tlbie(tcg_env, t0);

#if defined(TARGET_PPC64)
    /*
     * ISA 3.1B says that MSR SF must be 1 when this instruction is executed;
     * otherwise the results are undefined.
     */
    } else if (a->r) {
        gen_helper_tlbie_isa300(tcg_env, cpu_gpr[rb], cpu_gpr[a->rs],
                tcg_constant_i32(a->ric << TLBIE_F_RIC_SHIFT |
                                 a->prs << TLBIE_F_PRS_SHIFT |
                                 a->r << TLBIE_F_R_SHIFT |
                                 local << TLBIE_F_LOCAL_SHIFT));
        if (!local) {
            /*
             * Global TLB flush uses async-work which must run before the
             * next instruction, so this must be the last in the TB.
             */
            ctx->base.is_jmp = DISAS_EXIT_UPDATE;
        }
        return true;
#endif

    } else {
        gen_helper_tlbie(tcg_env, cpu_gpr[rb]);
    }

    if (local) {
        return true;
    }

    t1 = tcg_temp_new_i32();
    tcg_gen_ld_i32(t1, tcg_env, offsetof(CPUPPCState, tlb_need_flush));
    tcg_gen_ori_i32(t1, t1, TLB_NEED_GLOBAL_FLUSH);
    tcg_gen_st_i32(t1, tcg_env, offsetof(CPUPPCState, tlb_need_flush));

    return true;
#endif
}

TRANS_FLAGS(MEM_TLBIE, TLBIE, do_tlbie, false)
TRANS_FLAGS(MEM_TLBIE, TLBIEL, do_tlbie, true)
