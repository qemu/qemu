/*
 * RISC-V translation routines for the RISC-V Zawrs Extension.
 *
 * Copyright (c) 2022 Christoph Muellner, christoph.muellner@vrull.io
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

static bool trans_wrs(DisasContext *ctx)
{
    if (!ctx->cfg_ptr->ext_zawrs) {
        return false;
    }

    /*
     * The specification says:
     * While stalled, an implementation is permitted to occasionally
     * terminate the stall and complete execution for any reason.
     *
     * So let's just exit TB and return to the main loop.
     */

    /* Clear the load reservation  (if any).  */
    tcg_gen_movi_tl(load_res, -1);

    gen_update_pc(ctx, ctx->cur_insn_len);
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

#define GEN_TRANS_WRS(insn)                                     \
static bool trans_ ## insn(DisasContext *ctx, arg_ ## insn *a)  \
{                                                               \
    (void)a;                                                    \
    return trans_wrs(ctx);                                      \
}

GEN_TRANS_WRS(wrs_nto)
GEN_TRANS_WRS(wrs_sto)
