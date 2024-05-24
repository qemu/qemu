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
