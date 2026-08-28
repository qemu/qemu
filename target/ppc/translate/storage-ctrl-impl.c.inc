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

static bool trans_ISYNC(DisasContext *ctx, arg_ISYNC *a)
{
    REQUIRE_INSNS_FLAGS(ctx, MEM);

    /*
     * We need to check for a pending TLB flush. This can only happen in
     * kernel mode however so check MSR_PR
     */
    if (!ctx->pr) {
        gen_check_tlb_flush(ctx, false);
    }
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    ctx->base.is_jmp = DISAS_EXIT_UPDATE;

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

/*
 * Decodetree populates a->wc and a->pl based on ISA v3.1, however they may
 * or may not be used based on the ISA:
 *   For ISA < 2.06 - both wc and pl are ignored
 *   For ISA == 2.06, 2.07, 3.0 - wc is used and pl is ignored
 *   For ISA 3.1 - both wc and pl are used
 */
static bool do_wait(DisasContext *ctx, arg_X_wait *a)
{
    uint32_t wc;

    if (ctx->insns_flags & PPC_WAIT) {
        /* v2.03-v2.07 define an older incompatible 'wait' encoding. */

        if (ctx->insns_flags2 & PPC2_PM_ISA206) {
            /* v2.06 introduced the WC field. WC > 0 may be treated as no-op. */
            wc = a->wc;
        } else {
            wc = 0;
        }

    } else if (ctx->insns_flags2 & PPC2_ISA300) {
        /* v3.0 defines a new 'wait' encoding. */
        wc = a->wc;
        if (ctx->insns_flags2 & PPC2_ISA310) {
            uint32_t pl = a->pl;

            /* WC 1,2 may be treated as no-op. WC 3 is reserved. */
            if (wc == 3) {
                gen_invalid(ctx);
                return true;
            }

            /* PL 1-3 are reserved. If WC=2 then the insn is treated as noop. */
            if (pl > 0 && wc != 2) {
                gen_invalid(ctx);
                return true;
            }

        } else { /* ISA300 */
            /* WC 1-3 are reserved */
            if (wc > 0) {
                gen_invalid(ctx);
                return true;
            }
        }

    } else {
        warn_report("wait instruction decoded with wrong ISA flags.");
        gen_invalid(ctx);
        return true;
    }

    /*
     * wait without WC field or with WC=0 waits for an exception / interrupt
     * to occur.
     */
    if (wc == 0) {
        TCGv_i32 t0 = tcg_constant_i32(1);
        tcg_gen_st_i32(t0, tcg_env,
                       -offsetof(PowerPCCPU, env) + offsetof(CPUState, halted));
        /* Stop translation, as the CPU is supposed to sleep from now */
        gen_exception_nip(ctx, EXCP_HLT, ctx->base.pc_next);
    }

    /*
     * Other wait types must not just wait until an exception occurs because
     * ignoring their other wake-up conditions could cause a hang.
     *
     * For v2.06 and 2.07, wc=1,2,3 are architected but may be implemented as
     * no-ops.
     *
     * wc=1 and wc=3 explicitly allow the instruction to be treated as a no-op.
     *
     * wc=2 waits for an implementation-specific condition, such could be
     * always true, so it can be implemented as a no-op.
     *
     * For v3.1, wc=1,2 are architected but may be implemented as no-ops.
     *
     * wc=1 (waitrsv) waits for an exception or a reservation to be lost.
     * Reservation-loss may have implementation-specific conditions, so it
     * can be implemented as a no-op.
     *
     * wc=2 waits for an exception or an amount of time to pass. This
     * amount is implementation-specific so it can be implemented as a
     * no-op.
     *
     * ISA v3.1 allows for execution to resume "in the rare case of
     * an implementation-dependent event", so in any case software must
     * not depend on the architected resumption condition to become
     * true, so no-op implementations should be architecturally correct
     * (if suboptimal).
     */
    return true;
}

TRANS_FLAGS(WAIT, WAIT_ISA_2_X, do_wait)
TRANS_FLAGS2(ISA300, WAIT, do_wait)
