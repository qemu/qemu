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

static bool trans_wrs_sto(DisasContext *ctx, arg_wrs_sto *a)
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

static bool trans_wrs_nto(DisasContext *ctx, arg_wrs_nto *a)
{
    if (!ctx->cfg_ptr->ext_zawrs) {
        return false;
    }

    /*
     * Depending on the mode of execution, mstatus.TW and hstatus.VTW, wrs.nto
     * should raise an exception when the implementation-specific bounded time
     * limit has expired. Our time limit is zero, so we either return
     * immediately, as does our implementation of wrs.sto, or raise an
     * exception, as handled by the wrs.nto helper.
     */
#ifndef CONFIG_USER_ONLY
    gen_helper_wrs_nto(tcg_env);
#endif

    /* We only get here when helper_wrs_nto() doesn't raise an exception. */
    return trans_wrs_sto(ctx, NULL);
}
