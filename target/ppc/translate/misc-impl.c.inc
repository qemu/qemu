/*
 * Power ISA decode for misc instructions
 *
 * Copyright (c) 2024, IBM Corporation.
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
 * Memory Barrier Instructions
 */

static bool trans_SYNC(DisasContext *ctx, arg_X_sync *a)
{
    TCGBar bar = TCG_MO_ALL;
    uint32_t l = a->l;
    uint32_t sc = a->sc;

    /*
     * BookE uses the msync mnemonic. This means hwsync, except in the
     * 440, where it an execution serialisation point that requires all
     * previous storage accesses to have been performed to memory (which
     * doesn't matter for TCG).
     */
    if (!(ctx->insns_flags & PPC_MEM_SYNC)) {
        if (ctx->insns_flags & PPC_BOOKE) {
            tcg_gen_mb(bar | TCG_BAR_SC);
            return true;
        }

        return false;
    }

    /*
     * In ISA v3.1, the L field grew one bit. Mask that out to ignore it in
     * older processors. It also added the SC field, zero this to ignore
     * it too.
     */
    if (!(ctx->insns_flags2 & PPC2_ISA310)) {
        l &= 0x3;
        sc = 0;
    }

    if (sc) {
        /* Store syncs [stsync, stcisync, stncisync]. These ignore L. */
        bar = TCG_MO_ST_ST;
    } else {
        if (((l == 1) && (ctx->insns_flags2 & PPC2_MEM_LWSYNC)) || (l == 5)) {
            /* lwsync, or plwsync on POWER10 and later */
            bar = TCG_MO_LD_LD | TCG_MO_LD_ST | TCG_MO_ST_ST;
        }

        /*
         * We may need to check for a pending TLB flush.
         *
         * We do this on ptesync (l == 2) on ppc64 and any sync on ppc32.
         *
         * Additionally, this can only happen in kernel mode however so
         * check MSR_PR as well.
         */
        if (((l == 2) || !(ctx->insns_flags & PPC_64B)) && !ctx->pr) {
            gen_check_tlb_flush(ctx, true);
        }
    }

    tcg_gen_mb(bar | TCG_BAR_SC);

    return true;
}

static bool trans_EIEIO(DisasContext *ctx, arg_EIEIO *a)
{
    TCGBar bar = TCG_MO_ALL;

    /*
     * BookE uses the mbar instruction instead of eieio, which is basically
     * full hwsync memory barrier, but is not execution synchronising. For
     * the purpose of TCG the distinction is not relevant.
     */
    if (!(ctx->insns_flags & PPC_MEM_EIEIO)) {
        if ((ctx->insns_flags & PPC_BOOKE) ||
            (ctx->insns_flags2 & PPC2_BOOKE206)) {
            tcg_gen_mb(bar | TCG_BAR_SC);
            return true;
        }
        return false;
    }

    /*
     * eieio has complex semanitcs. It provides memory ordering between
     * operations in the set:
     * - loads from CI memory.
     * - stores to CI memory.
     * - stores to WT memory.
     *
     * It separately also orders memory for operations in the set:
     * - stores to cacheble memory.
     *
     * It also serializes instructions:
     * - dcbt and dcbst.
     *
     * It separately serializes:
     * - tlbie and tlbsync.
     *
     * And separately serializes:
     * - slbieg, slbiag, and slbsync.
     *
     * The end result is that CI memory ordering requires TCG_MO_ALL
     * and it is not possible to special-case more relaxed ordering for
     * cacheable accesses. TCG_BAR_SC is required to provide this
     * serialization.
     */

    /*
     * POWER9 has a eieio instruction variant using bit 6 as a hint to
     * tell the CPU it is a store-forwarding barrier.
     */
    if (ctx->opcode & 0x2000000) {
        /*
         * ISA says that "Reserved fields in instructions are ignored
         * by the processor". So ignore the bit 6 on non-POWER9 CPU but
         * as this is not an instruction software should be using,
         * complain to the user.
         */
        if (!(ctx->insns_flags2 & PPC2_ISA300)) {
            qemu_log_mask(LOG_GUEST_ERROR, "invalid eieio using bit 6 at @"
                          TARGET_FMT_lx "\n", ctx->cia);
        } else {
            bar = TCG_MO_ST_LD;
        }
    }

    tcg_gen_mb(bar | TCG_BAR_SC);

    return true;
}

static bool trans_ATTN(DisasContext *ctx, arg_ATTN *a)
{
#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
    gen_helper_attn(tcg_env);
    return true;
#else
    return false;
#endif
}

static bool trans_MCRF(DisasContext *ctx, arg_MCRF *a)
{
    tcg_gen_mov_i32(cpu_crf[a->bf], cpu_crf[a->bfa]);
    return true;
}

static bool do_mfmsr(DisasContext *ctx, arg_X_t *a)
{
    REQUIRE_SV(ctx);
    tcg_gen_mov_tl(cpu_gpr[a->rt], cpu_msr);
    return true;
}
TRANS_FLAGS(MISC, MFMSR, do_mfmsr)

static bool do_mtmsr(DisasContext *ctx, arg_X_rs_l *a, bool is_mtmsrd)
{
    REQUIRE_SV(ctx);
#if !defined(CONFIG_USER_ONLY)
    TCGv t0, t1;
    target_ulong mask;

    if (is_mtmsrd) {
        if (unlikely(!is_book3s_arch2x(ctx))) {
            gen_invalid(ctx);
            return true;
        }
        mask = ~(target_ulong)0;
    } else {
        mask = UINT32_MAX;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    translator_io_start(&ctx->base);

    if (a->l) {
        /* L=1 form only updates EE and RI */
        mask &= (1ULL << MSR_RI) | (1ULL << MSR_EE);
    } else {
        if (is_mtmsrd) {
            /* mtmsrd does not alter HV, S, ME, or LE */
            mask &= ~((1ULL << MSR_LE) | (1ULL << MSR_ME) |
                      (1ULL << MSR_S)  | (1ULL << MSR_HV));
        } else if (likely(!(ctx->insns_flags2 & PPC2_PPE42))) {
            /* mtmsr does not alter S, ME, or LE */
            mask &= ~((1ULL << MSR_LE) | (1ULL << MSR_ME) | (1ULL << MSR_S));
        }
        /*
         * XXX: we need to update nip before the store if we enter
         *      power saving mode, we will exit the loop directly from
         *      ppc_store_msr
         */
        gen_update_nip(ctx, ctx->base.pc_next);
    }

    tcg_gen_andi_tl(t0, cpu_gpr[a->rs], mask);
    tcg_gen_andi_tl(t1, cpu_msr, ~mask);
    tcg_gen_or_tl(t0, t0, t1);

    gen_helper_store_msr(tcg_env, t0);

    /* Must stop the translation as machine state (may have) changed */
    ctx->base.is_jmp = DISAS_EXIT_UPDATE;
#endif /* !CONFIG_USER_ONLY */
    return true;
}

TRANS_FLAGS(MISC, MTMSR, do_mtmsr, false)
TRANS64(MTMSRD, do_mtmsr, true)

/*
 * System Call and Return from Interrupt Instructions
 */

static bool do_SC(DisasContext *ctx, arg_SC *a, uint32_t excp)
{
    uint32_t lev;

    /*
     * LEV is a 7-bit field, but the top 6 bits are treated as a reserved
     * field (i.e., ignored). ISA v3.1 changes that to 5 bits, but that is
     * for Ultravisor which TCG does not support, so just ignore the top 6.
     */
    lev = a->lev & 0x1;
    gen_exception_err(ctx, excp, lev);
    return true;
}

static bool do_SCV(DisasContext *ctx, arg_SC *a)
{
#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
    /* Set the PC back to the faulting instruction. */
    gen_update_nip(ctx, ctx->cia);
    gen_helper_scv(tcg_env, tcg_constant_i32(a->lev));

    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
#else
    gen_invalid(ctx);
    return true;
#endif
}

/*
 * Common helper for return-from-interrupt instructions
 */
enum {
   RFI_KIND_RFI = 0,
   RFI_KIND_RFID = 1,
   RFI_KIND_HRFID = 2,
   RFI_KIND_RFSCV = 3,
};

static bool do_rfi(DisasContext *ctx, arg_RFID *a, int kind)
{
#if defined(CONFIG_USER_ONLY)
    gen_priv_opc(ctx);
    return true;
#else
    void (*helper)(TCGv_ptr);

    switch (kind) {
    case RFI_KIND_RFI:
        if (is_book3s_arch2x(ctx)) {
            gen_invalid(ctx);
            return true;
        }
        REQUIRE_SV(ctx);
        helper = gen_helper_RFI;
        break;
#if defined(TARGET_PPC64)
    case RFI_KIND_HRFID:
        REQUIRE_HV(ctx);
        helper = gen_helper_HRFID;
        break;
    case RFI_KIND_RFID:
        REQUIRE_SV(ctx);
        helper = gen_helper_RFID;
        break;
    case RFI_KIND_RFSCV:
        REQUIRE_SV(ctx);
        helper = gen_helper_RFSCV;
        break;
#endif
    default:
        gen_invalid(ctx);
        return true;
    }

    translator_io_start(&ctx->base);
    if (kind != RFI_KIND_HRFID) {
        gen_update_branch_history(ctx, ctx->cia, NULL, BHRB_TYPE_NORECORD);
    }
    helper(tcg_env);
    ctx->base.is_jmp = DISAS_EXIT;
    return true;
#endif
}

#if defined(CONFIG_USER_ONLY)
TRANS_FLAGS(FLOW, SC, do_SC, POWERPC_EXCP_SYSCALL_USER)
#else
TRANS_FLAGS(FLOW, SC, do_SC, POWERPC_EXCP_SYSCALL)
#endif

TRANS64_FLAGS2(ISA300, SCV, do_SCV)

TRANS_FLAGS(FLOW, RFI, do_rfi, RFI_KIND_RFI)
TRANS64(RFID, do_rfi, RFI_KIND_RFID)
TRANS64_FLAGS(64H, HRFID, do_rfi, RFI_KIND_HRFID)
TRANS64_FLAGS2(ISA300, RFSCV, do_rfi, RFI_KIND_RFSCV)
