/*
 * RISC-V translation routines for the RVXI Base Integer Instruction Set.
 *
 * Copyright (c) 2020 Western Digital
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

static bool trans_hfence_gvma(DisasContext *ctx, arg_sfence_vma *a)
{
#ifndef CONFIG_USER_ONLY
    if (ctx->priv_ver >= PRIV_VERSION_1_10_0 &&
        has_ext(ctx, RVH)) {
        /* Hpervisor extensions exist */
        /*
         * if (env->priv == PRV_M ||
         *   (env->priv == PRV_S &&
         *    !riscv_cpu_virt_enabled(env) &&
         *    get_field(ctx->mstatus_fs, MSTATUS_TVM))) {
         */
            gen_helper_tlb_flush(cpu_env);
            return true;
        /* } */
    }
#endif
    return false;
}

static bool trans_hfence_vvma(DisasContext *ctx, arg_sfence_vma *a)
{
#ifndef CONFIG_USER_ONLY
    if (ctx->priv_ver >= PRIV_VERSION_1_10_0 &&
        has_ext(ctx, RVH)) {
        /* Hpervisor extensions exist */
        /*
         * if (env->priv == PRV_M ||
         *   (env->priv == PRV_S &&
         *    !riscv_cpu_virt_enabled(env) &&
         *    get_field(ctx->mstatus_fs, MSTATUS_TVM))) {
         */
            gen_helper_tlb_flush(cpu_env);
            return true;
        /* } */
    }
#endif
    return false;
}
